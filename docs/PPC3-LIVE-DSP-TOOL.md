# Live DRC, AGL and EQ: `tools/tas5825m-dsp.py`

The `PPC3 ...` ALSA controls hold raw DSP words. On their own they answer
"which numbers are in the DSP", not "how hard does the compressor work".
`tools/tas5825m-dsp.py` is the missing half: it converts thresholds in dB,
ratios, attack and release times, filter frequencies, gains and Q into the
TAS5825M's fixed-point words with the same formulas as TI PurePath Console 3,
writes them with `amixer`, reads them back, and decodes the DSP's current
words into the same units.

Everything that needs floating point stays in this userspace tool. The kernel
driver only stores, caches and writes the words.

## Requirements

- A TAS5825M with the driver from this branch, loaded with a PPC3 export of the
  DRC3/AGL Process Flow 3 at `/lib/firmware/tas58xx_dsp_drc3agl.cfg` and
  `ti,dsp-config-name = "drc3agl"` (see [PPC3-PF3-TESTING.md](PPC3-PF3-TESTING.md)).
- Python 3.7 or newer and `amixer` (alsa-utils). No other packages.
- The codec must have started once since boot (play something), because the DSP
  is only readable while it is clocked. The tool says so if it isn't.

## Commands

```
python3 tools/tas5825m-dsp.py status
```
shows every DRC band, the DRC mixer, the AGL, the DRC band crossover and which
PEQ bands are active (with their gain at 50 Hz, 1 kHz and 10 kHz):

```
DRC (three-band compressor)
  low   thresholds -60.0 / 0.0 dB, ratios 1 / 1 / 100, offsets 0.0 / 0.0 dB
        energy 25 ms, attack 25 ms, release 50 ms, mixer 1
  ...
AGL (limiter)
  on, threshold -2.0 dB, softening 0.55 ms, attack 0.2, release 0.001
DRC band crossover: active
PEQ (dB at the listed frequencies: 50 Hz, 1 kHz, 10 kHz)
  left  1: +2.5/+0.0/+0.0
  right all bypassed
```

### DRC (three bands)

```
python3 tools/tas5825m-dsp.py drc low --threshold2 -12 --ratio2 4 --attack 5 --release 200
```

Each band's static curve has two thresholds. Below `threshold1` the band
follows `ratio0`, between the thresholds `ratio1`, above `threshold2`
`ratio2`. A ratio of 1 leaves the level alone, 4 is 4:1 compression, 100 is
limiting, and a ratio below 1 expands. `offset1` and `offset2` move the curve
up or down at each threshold. `energy` is the level detector's averaging time;
`attack` and `release` are the gain smoothing times. `--mixer` sets the band's
gain into the output sum. PPC3's default is 1, -1, 1: with the LR2 band split
the mid band is inverted so the three bands sum flat.

Options you leave out keep their current value: the tool reads the band, changes
only the words for the settings you name and leaves the rest exactly as the
DSP holds them, even words PPC3 wrote that no formula reproduces.

```
python3 tools/tas5825m-dsp.py crossover --low-mid 200 --mid-high 4000
python3 tools/tas5825m-dsp.py crossover --off
```
sets the frequencies that split the signal into the three DRC bands
(Linkwitz-Riley LR2, as PPC3 does), or bypasses the split.

### AGL (limiter)

```
python3 tools/tas5825m-dsp.py agl --on --threshold -3
python3 tools/tas5825m-dsp.py agl --softening 1 --attack 0.2 --release 0.001
python3 tools/tas5825m-dsp.py agl --off
```

The threshold is in dB, as PPC3 shows it (-138..0). Attack and release are
PPC3's 0..1 rate settings; softening is a time constant in ms. The enable
word is written last, so the limiter never runs with half its new settings.

### Parametric EQ (15 bands per channel)

```
python3 tools/tas5825m-dsp.py eq left 1 peaking --freq 60 --gain 3 --q 1.4
python3 tools/tas5825m-dsp.py eq right 2 high-shelf --freq 8000 --gain -2 --q 0.7
python3 tools/tas5825m-dsp.py eq left 15 high-pass --freq 30
python3 tools/tas5825m-dsp.py eq left 1 bypass
```

| Filter | Uses | Formula |
|---|---|---|
| `peaking` | freq, gain, Q | PPC3's "EQ" (biquad.model.js) |
| `low-shelf`, `high-shelf` | freq, gain, Q | PPC3's shelves (RBJ cookbook form) |
| `low-pass`, `high-pass` | freq, gain | Butterworth, Q 1/√2 (PPC3's BW2); two in a row make LR4 |
| `low-pass-lr2`, `high-pass-lr2` | freq, gain | Linkwitz-Riley 2nd order |
| `gain` | gain | plain gain |
| `bypass` | | 1.0, nothing else |

Ranges: 1 Hz to 47.999 kHz (the flow runs at 96 kHz), -110 to +20 dB, Q 0.1
to 10. Every filter is checked for stability before it is written; one whose
poles would sit on or outside the unit circle is refused.

### Trying a change first

`--dry-run` before the command prints the words it would write and writes
nothing:

```
python3 tools/tas5825m-dsp.py --dry-run eq left 1 peaking --freq 60 --gain 3
would set 'PPC3 PEQ Left 01 Coefficients' = 134371076,-267689952,133320940,267689952,-133474288
```

## Keeping settings across reboots

The words live in ALSA controls, so `alsactl store` saves them and
`alsactl restore` brings them back at boot; the driver replays them after the
PPC3 export loads. Note that a saved state then overrides those coefficients
in a changed export: after editing the export in PPC3, set the controls again
or remove the saved state.

## Safety

- Start at low volume. A DRC or EQ change takes effect immediately.
- A DRC band is ten words and the driver writes them one at a time, so a live
  edit passes through a mix of old and new settings for a few milliseconds.
  Biquads are written as one burst, but even that is not atomic in the DSP.
  Expect an occasional click when editing during playback.
- The AGL is the last line of protection for the speakers. Set its threshold
  for your amplifier and speakers before experimenting with the DRC and EQ.
- These formulas are for the drc3agl flow's coefficient layout only.

## Where the numbers come from

The formulas follow TI PurePath Console 3 (coefficientService and
biquad.model.js), as used by the Rondo project's helper, which has driven these
controls on a TAS5825M since October 2026. `tests/test_dsp_tool.py` checks
them word for word against:

- esphome-tas58xx's `equalizer_qfactor_`, `low_shelf_filter_` and
  `high_shelf_filter_` (96 combinations of frequency, gain and Q);
- the Rondo helper's biquads, DRC, AGL and mixer words;
- words PPC3 itself wrote into a drc3agl export (DRC thresholds, ratio and
  mixer).

It also runs every command end to end against a stand-in for `amixer`.
