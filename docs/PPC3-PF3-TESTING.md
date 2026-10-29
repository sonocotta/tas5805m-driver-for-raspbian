# TAS5825M PPC3 / PF3 testing

This branch extends the generic TAS58xx driver conservatively. The normal
TAS5805M/TAS5825M paths remain unchanged unless a text DSP config is loaded.
The Process Flow 3 live coefficient controls are exposed only for a TAS5825M
whose `ti,dsp-config-name` is exactly `drc3agl`.

## What can be tested without a DSP config

On a TAS5825M, `PVDD Voltage mV` is available without a PPC3 profile. Normal
digital volume, analog gain, native EQ/crossover/mixer and fault-monitor paths
continue to behave as before when no DSP config is loaded.

The PPC3 text parser is also generic: it accepts both Linux 7-bit and PPC3
8-bit write addresses and expands PPC3 burst writes into sequential register
writes.

## What requires a DRC3AGL PPC3 export

The `PPC3 ...` live controls describe the coefficient layout of the TAS5825M
DRC3/AGL Process Flow 3 image. To exercise those controls, install a matching
PPC3 text export as:

```
/lib/firmware/tas58xx_dsp_drc3agl.cfg
```

and set the device-tree node to:

```
ti,dsp-config-name = "drc3agl";
```

The repository intentionally does not bundle a TI PurePath Console generated
configuration. A tester can use their own DRC3AGL/PF3 export; it does not need
to be the exact file used during development, but it must use the same PF3
coefficient layout.

The DSP config is applied after the I2S clock is running, on the driver's normal
playback trigger path. Start playback once before reading the live PF3 controls.

## Non-destructive checks

Replace `CARD` with the ALSA card number:

```sh
amixer -c CARD controls | grep -E 'PPC3|PVDD'
amixer -c CARD cget name='PVDD Voltage mV'
amixer -c CARD cget name='PPC3 Gang EQ'
amixer -c CARD cget name='PPC3 Global EQ Bypass'
amixer -c CARD cget name='PPC3 DC Block Bypass'
amixer -c CARD cget name='PPC3 PEQ Left 01 Coefficients'
amixer -c CARD cget name='PPC3 PEQ Right 01 Coefficients'
```

For independent left/right PEQ banks, Gang EQ must be zero. The driver exposes
the profile flag but deliberately does not override the PPC3 file at load time:

```sh
amixer -c CARD cset name='PPC3 Gang EQ' 0
```

A safe transport check for a five-coefficient biquad is to read all five values,
write the exact same five values back, and read again. Do not invent raw
coefficients merely to test the interface.

## Hardware validation already performed

The development hardware verified the PF3 Book 0x8c / Book 0xaa coefficient
mapping, complete B0/B1/B2/A1/A2 writes, independent left/right PEQ with Gang
EQ disabled, the DC-block/Gang-EQ/global-EQ flags, and PVDD telemetry.

The raw level-meter controls use ALSA INTEGER64 so the full unsigned 32-bit DSP
word is representable on both 32-bit and 64-bit kernels.

## Coefficient transport checks

Run `python3 tests/test_ppc3_transport.py` with a C compiler installed. This
compiles the driver's coefficient helpers against a fake regmap and checks byte
order, contiguous writes, page-boundary splits, failed selections and writes,
and control-page restoration. It does not exercise the kernel, the physical
I2C bus or audio playback. The checks also run in the kernel build workflow.

An earlier Rondo build with these driver fixes was built and installed on a
TAS5825M Pi running kernel 6.18.50+rpt-rpi-v8. DSP persistence and speaker
protection checks passed. That build did not include this branch's later
INTEGER64 meter change; the combined branch still needs testing on a Pi.
Listening checks for live EQ and crossover edits were outstanding in the
hardware notes.

The playback and coefficient-access fixes were adapted from the TAS58xx Local
Repair Kit v0.2 reviewed for Rondo.
