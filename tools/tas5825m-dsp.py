#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Live DRC, AGL and EQ for a TAS5825M running the PPC3 "drc3agl" flow.

The driver exposes the DSP's coefficients as raw signed 32-bit ALSA controls
("PPC3 DRC3 Low Coefficients", "PPC3 PEQ Left 01 Coefficients", ...). This
tool turns musical settings (dB, ms, ratio, Hz, Q) into those words and back,
using the same formulas as TI PurePath Console 3, and writes them with amixer.
All the floating point stays here in userspace; the kernel only stores words.

    tas5825m-dsp.py status
    tas5825m-dsp.py drc low --threshold2 -12 --ratio2 4 --attack 5 --release 200
    tas5825m-dsp.py agl --on --threshold -3
    tas5825m-dsp.py eq left 1 peaking --freq 60 --gain 3 --q 1.4
    tas5825m-dsp.py eq right 2 high-shelf --freq 8000 --gain -2 --q 0.7
    tas5825m-dsp.py crossover --low-mid 200 --mid-high 4000

Add --dry-run to see the words without writing. Settings are read back from
the DSP first, so options you leave out keep their current value. The DSP's
coefficients are only readable once the codec has started (play something
first), unless they were restored or set since boot.

Only use this with the drc3agl PPC3 flow the controls were defined for.
"""
import argparse
import math
import re
import subprocess
import sys

DSP_RATE = 96000.0           # the PF3 flow runs at twice the 48 kHz input
DEN = 6.0206 * 2 ** 8        # PPC3's dB scale for DRC thresholds and offsets
AGL_HEADROOM_DB = 20.0 * math.log10(2 ** 4)   # 4 overhead bits in the AGL flow

BANDS = ("low", "mid", "high")
DRC_FIELDS = ("energy", "attack", "release", "ratio0", "ratio1", "ratio2",
              "threshold1", "threshold2", "offset1", "offset2")
NAMES = {
    "drc_low": "PPC3 DRC3 Low Coefficients",
    "drc_mid": "PPC3 DRC3 Mid Coefficients",
    "drc_high": "PPC3 DRC3 High Coefficients",
    "drc_mixer": "PPC3 DRC3 Mixer Coefficients",
    "agl_enable": "PPC3 AGL Enable Coefficient",
    "agl_threshold": "PPC3 AGL Threshold Coefficient",
    "agl_soft_alpha": "PPC3 AGL Soft Alpha Coefficient",
    "agl_attack": "PPC3 AGL Attack Coefficient",
    "agl_soft_omega": "PPC3 AGL Soft Omega Coefficient",
    "agl_release": "PPC3 AGL Release Coefficient",
    "xover_low_lp": "PPC3 DRC Xover Low LP Coefficients",
    "xover_mid_hp": "PPC3 DRC Xover Mid HP Coefficients",
    "xover_mid_lp": "PPC3 DRC Xover Mid LP Coefficients",
    "xover_high_hp": "PPC3 DRC Xover High HP Coefficients",
    "gang_eq": "PPC3 Gang EQ",
}


def peq_name(channel, band):
    return f"PPC3 PEQ {channel.capitalize()} {band:02d} Coefficients"


# --- fixed point ------------------------------------------------------------

def _round_away(x):
    """Round half away from zero, as PPC3 (JavaScript) and esphome-tas58xx do."""
    return int(math.floor(abs(x) + 0.5)) * (1 if x >= 0 else -1)


def q31(value):
    """Signed Q1.31, saturating at +1 - 2^-31 like PPC3's numToCoef32."""
    if not math.isfinite(value):
        raise ValueError("non-finite coefficient")
    raw = _round_away(value * 2 ** 31)
    return max(-(2 ** 31), min(2 ** 31 - 1, raw))


def from_q31(raw):
    return raw / 2 ** 31


def q527(value):
    """Signed Q5.27, the format of every PF3 biquad word (1.0 = 0x08000000)."""
    if not math.isfinite(value) or not -16.0 <= value < 16.0:
        raise ValueError("biquad coefficient outside the Q5.27 range -16..16")
    return _round_away(value * 2 ** 27)


def from_q527(raw):
    return raw / 2 ** 27


# --- time constants -----------------------------------------------------------

def alpha_from_ms(ms):
    if ms <= 0:
        raise ValueError("time constants must be above 0 ms")
    return 1.0 - math.exp(-1000.0 / (DSP_RATE * ms))


def ms_from_alpha(alpha):
    if alpha >= 1.0 - 2.0 ** -31:
        return 0.0                      # PPC3 writes 0x7fffffff (1.0) for "instant"
    if alpha <= 0.0:
        return math.inf
    return -1000.0 / (DSP_RATE * math.log(1.0 - alpha))


# --- DRC ------------------------------------------------------------------------

def _check_ratio(r):
    if not ((0.01 <= r <= 1e6) or (-1e6 <= r <= -1.0 / 3.0)):
        raise ValueError("DRC ratios must be 0.01..1000000 (or -1000000..-0.333)")


def drc_encode(field, value):
    """One DRC band word from one setting."""
    if field in ("energy", "attack", "release"):
        if not 0.002 <= value <= 100000.0:
            raise ValueError(f"DRC {field} must be 0.002..100000 ms")
        return q31(alpha_from_ms(value))
    if field.startswith("ratio"):
        _check_ratio(value)
        return q31((1.0 / value - 1.0) / 2 ** 8)
    if field.startswith("threshold"):
        if not -124.0 < value <= 24.0:
            raise ValueError("DRC thresholds must be above -124 and at most 24 dB")
        return q31((value - 24.0) / DEN)
    if field.startswith("offset"):
        if not -50.0 <= value <= 32.0:
            raise ValueError("DRC offsets must be -50..32 dB")
        return q31(value / DEN)
    raise KeyError(field)


def drc_decode(words):
    """A band's ten words as settings."""
    out = {}
    for field, raw in zip(DRC_FIELDS, words):
        v = from_q31(raw)
        if field in ("energy", "attack", "release"):
            out[field] = ms_from_alpha(v)
        elif field.startswith("ratio"):
            k = v * 2 ** 8
            out[field] = math.inf if k == -1.0 else 1.0 / (k + 1.0)
        elif field.startswith("threshold"):
            out[field] = v * DEN + 24.0
        else:
            out[field] = v * DEN
    return out


def drc_update(words, changes):
    """New band words: changed settings re-encoded, the rest kept bit for bit."""
    current = drc_decode(words)
    current.update(changes)
    if not current["threshold1"] < current["threshold2"]:
        raise ValueError("DRC threshold1 must be below threshold2")
    new = list(words)
    for i, field in enumerate(DRC_FIELDS):
        if field in changes:
            new[i] = drc_encode(field, changes[field])
    return new


def mixer_encode(gain):
    if not -20.0 <= gain <= 20.0:
        raise ValueError("DRC mixer gain must be -20..20")
    return q31(gain / 2 ** 8)


def mixer_decode(raw):
    return from_q31(raw) * 2 ** 8


# --- AGL --------------------------------------------------------------------------

AGL_KEYS = ("agl_enable", "agl_threshold", "agl_soft_alpha", "agl_attack",
            "agl_soft_omega", "agl_release")


def agl_decode(w):
    lin = from_q31(w["agl_threshold"])
    release = from_q31(w["agl_release"]) * DSP_RATE / 1000.0
    attack = (from_q31(w["agl_attack"]) / 2.0 - release * 1000.0 / DSP_RATE) * DSP_RATE / 1000.0
    return {
        "enabled": w["agl_enable"] < 0,
        "threshold": 20.0 * math.log10(lin) + AGL_HEADROOM_DB if lin > 0 else -math.inf,
        "softening": ms_from_alpha(from_q31(w["agl_soft_alpha"])),
        "attack": attack,
        "release": release,
    }


def agl_update(w, changes):
    """New AGL words; words whose settings did not change are kept bit for bit."""
    s = agl_decode(w)
    s.update(changes)
    new = dict(w)
    if "enabled" in changes:
        new["agl_enable"] = q31(-0.5 if changes["enabled"] else 0.5)
    if "threshold" in changes:
        if not -138.0 <= s["threshold"] <= 0.0:
            raise ValueError("AGL threshold must be -138..0 dB")
        new["agl_threshold"] = q31(10 ** ((s["threshold"] - AGL_HEADROOM_DB) / 20.0))
    if "softening" in changes:
        if not 0.001 <= s["softening"] <= 100000.0:
            raise ValueError("AGL softening must be 0.001..100000 ms")
        alpha = alpha_from_ms(s["softening"])
        new["agl_soft_alpha"] = q31(alpha)
        new["agl_soft_omega"] = q31(1.0 - alpha)
    if "attack" in changes or "release" in changes:
        for key in ("attack", "release"):
            if not 0.0 <= s[key] <= 1.0:
                raise ValueError(f"AGL {key} must be 0..1")
        step = s["release"] * 1000.0 / DSP_RATE
        new["agl_attack"] = q31(2.0 * (s["attack"] * 1000.0 / DSP_RATE + step))
        new["agl_release"] = q31(step)
    return new


# --- biquads --------------------------------------------------------------------
# Each returns (b0, b1, b2, a1, a2) as PF3 stores them:
#   y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] + a1 y[n-1] + a2 y[n-2]
# i.e. a1 and a2 carry the opposite sign to the usual textbook form.

def _peaking(freq, gain, q):
    """TI PPC3's "EQ" filter (biquad.model.js); also esphome-tas58xx's
    equalizer_qfactor_."""
    g = 10.0 ** (gain / 20.0)
    t0 = 2.0 * math.pi * freq / DSP_RATE
    beta = t0 / (2.0 * q) if g >= 1.0 else t0 / (2.0 * g * q)
    a2 = -0.5 * (1.0 - beta) / (1.0 + beta)
    a1 = (0.5 - a2) * math.cos(t0)
    x = (g - 1.0) * (0.25 + 0.5 * a2)
    return (2.0 * (x + 0.5), -2.0 * a1, 2.0 * (-x - a2), 2.0 * a1, 2.0 * a2)


def _shelf(freq, gain, q, high):
    """TI PPC3's low and high shelf (the RBJ cookbook form); the same numbers as
    esphome-tas58xx's low_shelf_filter_ and high_shelf_filter_."""
    a = 10.0 ** (gain / 40.0)
    w0 = 2.0 * math.pi * freq / DSP_RATE
    c = math.cos(w0)
    beta = math.sqrt(a) * math.sin(w0) / q
    if not high:
        a0 = (a + 1) + (a - 1) * c + beta
        b = (a * ((a + 1) - (a - 1) * c + beta), 2 * a * ((a - 1) - (a + 1) * c),
             a * ((a + 1) - (a - 1) * c - beta))
        a1 = 2 * ((a - 1) + (a + 1) * c)
        a2 = beta - ((a + 1) + (a - 1) * c)
    else:
        a0 = (a + 1) - (a - 1) * c + beta
        b = (a * ((a + 1) + (a - 1) * c + beta), -2 * a * ((a - 1) + (a + 1) * c),
             a * ((a + 1) + (a - 1) * c - beta))
        a1 = -2 * ((a - 1) - (a + 1) * c)
        a2 = beta - ((a + 1) - (a - 1) * c)
    return (b[0] / a0, b[1] / a0, b[2] / a0, a1 / a0, a2 / a0)


def _second_order(freq, highpass, damping):
    """Bilinear transform with prewarp of wc^2 / (s^2 + damping*wc*s + wc^2):
    damping 2 is Linkwitz-Riley LR2, sqrt(2) is Butterworth (PPC3's BW2)."""
    wc = 2.0 * math.pi * freq
    k = wc / math.tan(math.pi * freq / DSP_RATE)
    den = k * k + damping * wc * k + wc * wc
    if highpass:
        b = (k * k / den, -2 * k * k / den, k * k / den)
    else:
        b = (wc * wc / den, 2 * wc * wc / den, wc * wc / den)
    return (b[0], b[1], b[2], (2 * k * k - 2 * wc * wc) / den,
            (damping * wc * k - k * k - wc * wc) / den)


FILTERS = ("bypass", "gain", "peaking", "low-shelf", "high-shelf",
           "low-pass", "high-pass", "low-pass-lr2", "high-pass-lr2")


def biquad(kind, freq=1000.0, gain=0.0, q=0.707):
    if not 1.0 <= freq < DSP_RATE / 2:
        raise ValueError("frequency must be 1..47999 Hz")
    if not -110.0 <= gain <= 20.0:
        raise ValueError("gain must be -110..20 dB")
    if not 0.1 <= q <= 10.0:
        raise ValueError("Q must be 0.1..10")
    if kind == "bypass":
        vals = (1.0, 0.0, 0.0, 0.0, 0.0)
    elif kind == "gain":
        vals = (10.0 ** (gain / 20.0), 0.0, 0.0, 0.0, 0.0)
    elif kind == "peaking":
        vals = _peaking(freq, gain, q)
    elif kind in ("low-shelf", "high-shelf"):
        vals = _shelf(freq, gain, q, kind == "high-shelf")
    elif kind in ("low-pass", "high-pass", "low-pass-lr2", "high-pass-lr2"):
        damping = 2.0 if kind.endswith("lr2") else math.sqrt(2.0)
        vals = list(_second_order(freq, kind.startswith("high"), damping))
        g = 10.0 ** (gain / 20.0)
        vals[:3] = [v * g for v in vals[:3]]
    else:
        raise ValueError(f"unknown filter {kind}")
    words = [q527(v) for v in vals]
    check_stable(words)
    return words


def check_stable(words):
    """Refuse a biquad whose poles are not inside the unit circle. With PF3's
    stored signs that is |a2| < 1 and |a1| < 1 - a2, checked on the words."""
    a1, a2 = words[3], words[4]
    one = 2 ** 27
    if not (abs(a2) < one and abs(a1) < one - a2):
        raise ValueError("unstable biquad (poles on or outside the unit circle)")


def response_db(words, freq):
    """The magnitude of a stored biquad at freq, in dB."""
    b0, b1, b2, a1, a2 = (from_q527(w) for w in words)
    z = complex(math.cos(2 * math.pi * freq / DSP_RATE), -math.sin(2 * math.pi * freq / DSP_RATE))
    h = (b0 + b1 * z + b2 * z * z) / (1 - a1 * z - a2 * z * z)
    return 20 * math.log10(max(abs(h), 1e-12))


BYPASS = [q527(1.0), 0, 0, 0, 0]


# --- ALSA -------------------------------------------------------------------------

def run(*args):
    return subprocess.run(args, capture_output=True, text=True, timeout=10)


def find_card():
    for i in range(32):
        p = run("amixer", "-c", str(i), "controls")
        if p.returncode == 0 and f"name='{NAMES['drc_low']}'" in p.stdout:
            return i
    raise SystemExit("no sound card with the PPC3 drc3agl controls found "
                     "(is the driver loaded with the drc3agl PPC3 config?)")


class Card:
    def __init__(self, index, dry_run):
        self.index = index
        self.dry_run = dry_run

    def get(self, name):
        p = run("amixer", "-c", str(self.index), "cget", f"name={name}")
        values = None
        for line in p.stdout.splitlines():
            line = line.strip()
            if line.startswith(": values="):
                values = [int(v) for v in line.split("=", 1)[1].split(",")]
        if p.returncode or values is None:
            raise SystemExit(f"cannot read '{name}': "
                             f"{(p.stderr or p.stdout).strip() or 'no value'}\n"
                             "The DSP is readable once playback has started "
                             "(play something, then try again).")
        return values

    def set(self, name, values):
        line = ",".join(str(v) for v in values)
        if self.dry_run:
            print(f"would set '{name}' = {line}")
            return
        p = run("amixer", "-q", "-c", str(self.index), "cset", f"name={name}", "--", line)
        if p.returncode:
            raise SystemExit(f"cannot write '{name}': {(p.stderr or p.stdout).strip()}")
        back = self.get(name)
        if back != list(values):
            raise SystemExit(f"'{name}' reads back {back}, not {list(values)}")
        print(f"set '{name}'")


# --- commands -------------------------------------------------------------------

def fmt_db(db):
    return f"{round(db, 1) + 0.0:.1f}"     # no "-0.0"


def fmt_ms(ms):
    return "instant" if ms == 0 else ("off" if math.isinf(ms) else f"{ms:.3g} ms")


def fmt_ratio(r):
    return "inf" if math.isinf(r) else f"{r:.3g}"


def cmd_status(card, args):
    mixer = card.get(NAMES["drc_mixer"])
    print("DRC (three-band compressor)")
    for i, band in enumerate(BANDS):
        s = drc_decode(card.get(NAMES[f"drc_{band}"]))
        print(f"  {band:4}  thresholds {fmt_db(s['threshold1'])} / {fmt_db(s['threshold2'])} dB, "
              f"ratios {fmt_ratio(s['ratio0'])} / {fmt_ratio(s['ratio1'])} / {fmt_ratio(s['ratio2'])}, "
              f"offsets {fmt_db(s['offset1'])} / {fmt_db(s['offset2'])} dB")
        print(f"        energy {fmt_ms(s['energy'])}, attack {fmt_ms(s['attack'])}, "
              f"release {fmt_ms(s['release'])}, mixer {mixer_decode(mixer[i]):.3g}")
    a = agl_decode({k: card.get(NAMES[k])[0] for k in AGL_KEYS})
    print("AGL (limiter)")
    print(f"  {'on' if a['enabled'] else 'off'}, threshold {fmt_db(a['threshold'])} dB, "
          f"softening {fmt_ms(a['softening'])}, attack {a['attack']:.3g}, release {a['release']:.3g}")
    xo = [card.get(NAMES[k]) for k in ("xover_low_lp", "xover_mid_hp", "xover_mid_lp", "xover_high_hp")]
    print("DRC band crossover: " + ("bypassed" if all(w == BYPASS for w in xo) else "active"))
    print("PEQ (dB at the listed frequencies: 50 Hz, 1 kHz, 10 kHz)")
    for ch in ("left", "right"):
        active = []
        for band in range(1, 16):
            w = card.get(peq_name(ch, band))
            if w != BYPASS:
                active.append(f"{band}: " + "/".join(f"{response_db(w, f):+.1f}" for f in (50, 1000, 10000)))
        print(f"  {ch:5} " + ("all bypassed" if not active else "; ".join(active)))


def cmd_drc(card, args):
    changes = {f: getattr(args, f) for f in DRC_FIELDS if getattr(args, f) is not None}
    if not changes and args.mixer is None:
        raise SystemExit("nothing to change: give at least one setting (see --help)")
    name = NAMES[f"drc_{args.band}"]
    if changes:
        old = card.get(name)
        card.set(name, drc_update(old, changes))
    if args.mixer is not None:
        mixer = card.get(NAMES["drc_mixer"])
        mixer[BANDS.index(args.band)] = mixer_encode(args.mixer)
        card.set(NAMES["drc_mixer"], mixer)


def cmd_agl(card, args):
    changes = {k: getattr(args, k) for k in ("threshold", "softening", "attack", "release")
               if getattr(args, k) is not None}
    if args.on or args.off:
        changes["enabled"] = bool(args.on)
    if not changes:
        raise SystemExit("nothing to change: give at least one setting (see --help)")
    old = {k: card.get(NAMES[k])[0] for k in AGL_KEYS}
    new = agl_update(old, changes)
    # Settings first, the enable last, so the limiter never runs half-updated.
    for key in sorted(AGL_KEYS, key=lambda k: k == "agl_enable"):
        if new[key] != old[key]:
            card.set(NAMES[key], [new[key]])


def cmd_eq(card, args):
    if not 1 <= args.band <= 15:
        raise SystemExit("band must be 1..15")
    words = biquad(args.filter, args.freq, args.gain, args.q)
    card.set(peq_name(args.channel, args.band), words)
    if not card.dry_run and card.get(NAMES["gang_eq"])[0]:
        print("note: PPC3 Gang EQ is on, so the left bands apply to both channels")


def cmd_crossover(card, args):
    if args.off:
        words = {k: BYPASS for k in ("xover_low_lp", "xover_mid_hp", "xover_mid_lp", "xover_high_hp")}
    else:
        if args.low_mid is None or args.mid_high is None:
            raise SystemExit("give --low-mid and --mid-high, or --off")
        if not 20.0 <= args.low_mid < args.mid_high <= 20000.0:
            raise SystemExit("need 20 <= low-mid < mid-high <= 20000 Hz")
        words = {"xover_low_lp": biquad("low-pass-lr2", args.low_mid),
                 "xover_mid_hp": biquad("high-pass-lr2", args.low_mid),
                 "xover_mid_lp": biquad("low-pass-lr2", args.mid_high),
                 "xover_high_hp": biquad("high-pass-lr2", args.mid_high)}
    for key, w in words.items():
        card.set(NAMES[key], w)


def parser():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__.split("\n\n", 1)[1])
    p.add_argument("--card", type=int, help="ALSA card number (default: find it)")
    p.add_argument("--dry-run", action="store_true", help="print the words, write nothing")
    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="show DRC, AGL, crossover and EQ in plain units")

    d = sub.add_parser("drc", help="change one DRC band",
                       description="Each band compresses between two thresholds: ratio0 "
                       "applies below threshold1, ratio1 between them and ratio2 above "
                       "threshold2 (1 = no change, 4 = 4:1, 100 = limiting). Times are "
                       "the level detector's energy window and the gain's attack and release.")
    d.add_argument("band", choices=BANDS)
    for f, unit in (("energy", "ms"), ("attack", "ms"), ("release", "ms"),
                    ("threshold1", "dB"), ("threshold2", "dB"),
                    ("ratio0", ""), ("ratio1", ""), ("ratio2", ""),
                    ("offset1", "dB"), ("offset2", "dB")):
        d.add_argument("--" + f, type=float, metavar=unit.upper() or "R")
    d.add_argument("--mixer", type=float, metavar="GAIN",
                   help="the band's gain into the output mix (PPC3 units, usually 1 or -1)")

    a = sub.add_parser("agl", help="change the AGL limiter")
    on = a.add_mutually_exclusive_group()
    on.add_argument("--on", action="store_true")
    on.add_argument("--off", action="store_true")
    a.add_argument("--threshold", type=float, metavar="DB", help="-138..0 dB")
    a.add_argument("--softening", type=float, metavar="MS")
    a.add_argument("--attack", type=float, metavar="0..1")
    a.add_argument("--release", type=float, metavar="0..1")

    e = sub.add_parser("eq", help="set one parametric EQ band")
    e.add_argument("channel", choices=("left", "right"))
    e.add_argument("band", type=int, help="1..15")
    e.add_argument("filter", choices=FILTERS)
    e.add_argument("--freq", type=float, default=1000.0, metavar="HZ")
    e.add_argument("--gain", type=float, default=0.0, metavar="DB")
    e.add_argument("--q", type=float, default=0.707)

    x = sub.add_parser("crossover", help="set the DRC's band-split frequencies (LR2)")
    x.add_argument("--low-mid", type=float, metavar="HZ")
    x.add_argument("--mid-high", type=float, metavar="HZ")
    x.add_argument("--off", action="store_true", help="bypass the band split")
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    card = Card(args.card if args.card is not None else find_card(), args.dry_run)
    try:
        {"status": cmd_status, "drc": cmd_drc, "agl": cmd_agl, "eq": cmd_eq,
         "crossover": cmd_crossover}[args.command](card, args)
    except ValueError as e:
        raise SystemExit(f"error: {e}")


if __name__ == "__main__":
    main()
