#!/usr/bin/env python3
"""tools/tas5825m-dsp.py: its maths against reference words, and its commands
end to end against a stand-in for amixer."""
from pathlib import Path
import importlib.util
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "tas5825m-dsp.py"
GOLDEN = json.loads((ROOT / "tests" / "fixtures" / "dsp_golden.json").read_text())

spec = importlib.util.spec_from_file_location("dsp", TOOL)
dsp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dsp)


def c_float(x):
    return struct.unpack("f", struct.pack("f", x))[0]


class Maths(unittest.TestCase):
    def test_peaking_and_shelves_match_esphome_tas58xx(self):
        for kind, freq, gain, q, words in GOLDEN["esphome_biquads"]:
            with self.subTest(kind=kind, freq=freq, gain=gain, q=q):
                self.assertEqual(dsp.biquad(kind, freq, gain, c_float(q)), words)

    def test_biquads_match_the_rondo_helper(self):
        for kind, freq, gain, q, words in GOLDEN["rondo_biquads"]:
            with self.subTest(kind=kind, freq=freq, gain=gain):
                self.assertEqual(dsp.biquad(kind, freq, gain, q), words)

    def test_drc_matches_the_rondo_helper(self):
        for settings, words in GOLDEN["rondo_drc"]:
            with self.subTest(settings=settings):
                self.assertEqual([dsp.drc_encode(f, settings[f]) for f in dsp.DRC_FIELDS], words)

    def test_agl_and_mixer_match_the_rondo_helper(self):
        start = {k: 0 for k in dsp.AGL_KEYS}
        start["agl_threshold"] = dsp.q31(0.5)
        for settings, words in GOLDEN["rondo_agl"]:
            with self.subTest(settings=settings):
                self.assertEqual(dsp.agl_update(start, settings), words)
        for gain, word in GOLDEN["rondo_mixer"]:
            self.assertEqual(dsp.mixer_encode(gain), word)

    def test_words_from_a_real_ppc3_export(self):
        # PPC3 wrote these for thresholds -50 / -20 dB, ratio 1, mixer 1 and
        # "instant" time constants (alpha 1.0) in a TAS5825M drc3agl export.
        self.assertEqual(dsp.drc_encode("threshold1", -50.0), -103105503)
        self.assertEqual(dsp.drc_encode("threshold2", -20.0), -61305975)
        self.assertEqual(dsp.drc_encode("ratio0", 1.0), 0)
        self.assertEqual(dsp.mixer_encode(1.0), 8388608)
        s = dsp.drc_decode([2147483647] * 3 + [0, 0, 0, -103105503, -61305975, 0, 0])
        self.assertEqual((s["energy"], s["attack"], s["release"]), (0.0, 0.0, 0.0))
        self.assertAlmostEqual(s["threshold1"], -50.0, places=4)
        self.assertAlmostEqual(s["threshold2"], -20.0, places=4)
        self.assertEqual((s["ratio0"], s["ratio1"], s["ratio2"]), (1.0, 1.0, 1.0))

    def test_drc_decode_inverts_encode(self):
        for settings, words in GOLDEN["rondo_drc"]:
            decoded = dsp.drc_decode(words)
            for f in dsp.DRC_FIELDS:
                with self.subTest(field=f, value=settings[f]):
                    self.assertAlmostEqual(decoded[f], settings[f], delta=1e-4 * max(1.0, abs(settings[f])))
                    self.assertEqual(dsp.drc_encode(f, decoded[f]), words[dsp.DRC_FIELDS.index(f)])

    def test_agl_decode_inverts_encode(self):
        for settings, words in GOLDEN["rondo_agl"]:
            decoded = dsp.agl_decode(words)
            self.assertEqual(decoded["enabled"], settings["enabled"])
            for key in ("threshold", "softening", "attack", "release"):
                self.assertAlmostEqual(decoded[key], settings[key], delta=1e-4 * max(1.0, abs(settings[key])))

    def test_drc_update_keeps_untouched_words_bit_for_bit(self):
        # Words straight from a PPC3 export, which no formula here reproduces exactly.
        words = [2147483647, 2147483647, 2147483647, 0, 0, 0, -103105503, -61305975, 0, 0]
        new = dsp.drc_update(words, {"threshold2": -12.0, "ratio2": 4.0})
        changed = [i for i, (a, b) in enumerate(zip(words, new)) if a != b]
        self.assertEqual(changed, [5, 7])
        self.assertAlmostEqual(dsp.drc_decode(new)["ratio2"], 4.0, places=4)
        with self.assertRaises(ValueError):
            dsp.drc_update(words, {"threshold1": -10.0})   # above threshold2 (-20)

    def test_agl_attack_follows_a_release_change(self):
        start = dsp.agl_update({k: 0 for k in dsp.AGL_KEYS}, {"enabled": True, "threshold": -2.0,
                               "softening": 0.55, "attack": 0.2, "release": 0.001})
        new = dsp.agl_update(start, {"release": 0.01})
        self.assertNotEqual(new["agl_attack"], start["agl_attack"])
        self.assertAlmostEqual(dsp.agl_decode(new)["attack"], 0.2, places=5)
        self.assertEqual(new["agl_threshold"], start["agl_threshold"])

    def test_filters_have_the_expected_response(self):
        cases = [("peaking", 1000, 6.0, 1.0, 1000, 6.0), ("peaking", 80, -9.0, 2.0, 80, -9.0),
                 ("low-shelf", 100, 6.0, 0.707, 20, 6.0), ("low-shelf", 100, 6.0, 0.707, 10000, 0.0),
                 ("high-shelf", 8000, -4.0, 0.707, 30000, -4.0), ("high-shelf", 8000, -4.0, 0.707, 100, 0.0),
                 ("low-pass", 2000, 0.0, 0.707, 2000, -3.01), ("high-pass", 2000, 0.0, 0.707, 2000, -3.01),
                 ("low-pass-lr2", 2000, 0.0, 0.707, 2000, -6.02), ("high-pass-lr2", 2000, 0.0, 0.707, 2000, -6.02),
                 ("gain", 1000, -3.0, 0.707, 5000, -3.0), ("bypass", 1000, 0.0, 0.707, 5000, 0.0)]
        for kind, f, g, q, at, expect in cases:
            with self.subTest(kind=kind, f=f, at=at):
                self.assertAlmostEqual(dsp.response_db(dsp.biquad(kind, f, g, q), at), expect, delta=0.15)

    def test_every_filter_on_a_grid_is_stable_and_finite(self):
        for kind in dsp.FILTERS:
            for f in (10, 31.5, 100, 1000, 10000, 30000, 47000):
                for g in (-110, -20, 0, 20):
                    for q in (0.1, 0.707, 10):
                        try:
                            words = dsp.biquad(kind, f, g, q)
                        except ValueError as e:
                            # Refused, never written: a gain too big for the format, or
                            # a -110 dB cut so deep its quantised poles reach the circle.
                            self.assertTrue("Q5.27" in str(e) or ("unstable" in str(e) and g == -110), str(e))
                            continue
                        dsp.check_stable(words)

    def test_unstable_and_out_of_range_biquads_are_refused(self):
        one = 2 ** 27
        for a1, a2 in ((0, one), (0, -one), (2 * one, -one // 2), (-one, 0), (one, 0)):
            with self.subTest(a1=a1, a2=a2), self.assertRaises(ValueError):
                dsp.check_stable([one, 0, 0, a1, a2])
        dsp.check_stable([one, 0, 0, int(1.9 * one), int(-0.95 * one)])
        for bad in ({"freq": 0.5}, {"freq": 48000}, {"gain": 21}, {"q": 0.05}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                dsp.biquad("peaking", **{"freq": 1000, "gain": 0, "q": 1, **bad})

    def test_q31_saturates_like_ppc3(self):
        self.assertEqual(dsp.q31(1.0), 2 ** 31 - 1)
        self.assertEqual(dsp.q31(-1.0), -(2 ** 31))
        self.assertEqual(dsp.q31(0.5), 2 ** 30)
        self.assertEqual(dsp.q527(1.0), 0x08000000)


FAKE_AMIXER = r'''#!/usr/bin/env python3
import json, os, sys
state_path = os.environ["FAKE_AMIXER_STATE"]
state = json.load(open(state_path))
args = [a for a in sys.argv[1:] if a not in ("-q", "--")]
if "-c" in args:
    i = args.index("-c"); card = args[i + 1]; del args[i:i + 2]
    if card != "0":
        sys.exit(1)
cmd = args[0]
if cmd == "controls":
    for n, name in enumerate(state["values"], 1):
        print(f"numid={n},iface=MIXER,name='{name}'")
    sys.exit(0)
name = args[1][len("name="):]
if name not in state["values"]:
    print(f"amixer: Cannot find the given element from control sysdefault:0", file=sys.stderr)
    sys.exit(1)
if cmd == "cget":
    if state.get("unclocked"):
        print("amixer: Control sysdefault:0 element read error: Resource temporarily unavailable", file=sys.stderr)
        sys.exit(1)
    v = state["values"][name]
    print(f"numid=1,iface=MIXER,name='{name}'")
    print(f"  ; type=INTEGER,access=rw------,values={len(v)},min=-2147483648,max=2147483647,step=0")
    print("  : values=" + ",".join(str(x) for x in v))
elif cmd == "cset":
    new = [int(x) for x in args[2].split(",")]
    assert len(new) == len(state["values"][name]), "wrong number of values"
    state["values"][name] = new
    state.setdefault("writes", []).append(name)
    json.dump(state, open(state_path, "w"))
'''


def initial_values():
    agl = dsp.agl_update({k: 0 for k in dsp.AGL_KEYS},
                         {"enabled": False, "threshold": 0.0, "softening": 0.55,
                          "attack": 0.2, "release": 0.001})
    v = {dsp.NAMES[k]: [word] for k, word in agl.items()}
    for band in dsp.BANDS:
        v[dsp.NAMES[f"drc_{band}"]] = list(GOLDEN["rondo_drc"][0][1])
    v[dsp.NAMES["drc_mixer"]] = [8388608, -8388608, 8388608]
    for key in ("xover_low_lp", "xover_mid_hp", "xover_mid_lp", "xover_high_hp"):
        v[dsp.NAMES[key]] = list(dsp.BYPASS)
    for ch in ("left", "right"):
        for band in range(1, 16):
            v[dsp.peq_name(ch, band)] = list(dsp.BYPASS)
    v[dsp.NAMES["gang_eq"]] = [0]
    v["Digital Volume"] = [96]
    return v


class Commands(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="tas58xx-dsp-")
        bindir = Path(self.tmp.name)
        amixer = bindir / "amixer"
        amixer.write_text(FAKE_AMIXER)
        amixer.chmod(0o755)
        self.state = bindir / "state.json"
        self.state.write_text(json.dumps({"values": initial_values()}))
        self.env = dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}",
                        FAKE_AMIXER_STATE=str(self.state))

    def tearDown(self):
        self.tmp.cleanup()

    def tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *args], env=self.env,
                              capture_output=True, text=True, timeout=60)

    def values(self):
        return json.loads(self.state.read_text())

    def test_status_reads_everything_in_plain_units(self):
        r = self.tool("status")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("low   thresholds -60.0 / 0.0 dB, ratios 1 / 1 / 100", r.stdout)
        self.assertIn("off, threshold 0.0 dB", r.stdout)
        self.assertIn("left  all bypassed", r.stdout)

    def test_drc_changes_only_the_named_settings(self):
        before = self.values()["values"]
        r = self.tool("drc", "mid", "--threshold2", "-12", "--ratio2", "4", "--mixer", "0.5")
        self.assertEqual(r.returncode, 0, r.stderr)
        after = self.values()
        name = dsp.NAMES["drc_mid"]
        diff = [i for i, (a, b) in enumerate(zip(before[name], after["values"][name])) if a != b]
        self.assertEqual(diff, [5, 7])
        self.assertEqual(after["values"][dsp.NAMES["drc_mixer"]], [8388608, dsp.mixer_encode(0.5), 8388608])
        self.assertEqual(after["values"][dsp.NAMES["drc_low"]], before[dsp.NAMES["drc_low"]])
        self.assertIn("thresholds -60.0 / -12.0 dB, ratios 1 / 1 / 4", self.tool("status").stdout)

    def test_agl_writes_the_enable_last(self):
        r = self.tool("agl", "--on", "--threshold", "-3")
        self.assertEqual(r.returncode, 0, r.stderr)
        writes = self.values()["writes"]
        self.assertEqual(writes, [dsp.NAMES["agl_threshold"], dsp.NAMES["agl_enable"]])
        self.assertIn("on, threshold -3.0 dB", self.tool("status").stdout)

    def test_eq_and_crossover(self):
        r = self.tool("eq", "right", "3", "low-shelf", "--freq", "120", "--gain", "4", "--q", "0.7")
        self.assertEqual(r.returncode, 0, r.stderr)
        v = self.values()["values"]
        self.assertEqual(v[dsp.peq_name("right", 3)], dsp.biquad("low-shelf", 120, 4, 0.7))
        self.assertEqual(v[dsp.peq_name("left", 3)], dsp.BYPASS)
        self.assertIn("right 3: +3.9/+0.0/+0.0", self.tool("status").stdout)
        r = self.tool("crossover", "--low-mid", "250", "--mid-high", "3000")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.values()["values"][dsp.NAMES["xover_mid_lp"]], dsp.biquad("low-pass-lr2", 3000))
        self.assertIn("DRC band crossover: active", self.tool("status").stdout)
        self.assertEqual(self.tool("crossover", "--off").returncode, 0)
        self.assertIn("DRC band crossover: bypassed", self.tool("status").stdout)

    def test_dry_run_writes_nothing(self):
        r = self.tool("--dry-run", "eq", "left", "1", "peaking", "--freq", "60", "--gain", "3")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("would set 'PPC3 PEQ Left 01 Coefficients'", r.stdout)
        self.assertNotIn("writes", self.values())

    def test_bad_settings_write_nothing(self):
        for args in (("drc", "low", "--threshold1", "10"),       # above threshold2 (0 dB)
                     ("drc", "low", "--ratio1", "0"),
                     ("agl", "--threshold", "3"),
                     ("eq", "left", "16", "peaking"),
                     ("eq", "left", "1", "peaking", "--gain", "30"),
                     ("crossover", "--low-mid", "5000", "--mid-high", "300"),
                     ("drc", "low")):
            with self.subTest(args=args):
                r = self.tool(*args)
                self.assertNotEqual(r.returncode, 0)
                self.assertNotIn("writes", self.values())

    def test_unclocked_dsp_says_to_start_playback(self):
        s = self.values()
        s["unclocked"] = True
        self.state.write_text(json.dumps(s))
        r = self.tool("status")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("play something", r.stderr)

    def test_no_card_with_the_controls(self):
        s = self.values()
        s["values"] = {"Digital Volume": [96]}
        self.state.write_text(json.dumps(s))
        r = self.tool("status")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("no sound card with the PPC3 drc3agl controls", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
