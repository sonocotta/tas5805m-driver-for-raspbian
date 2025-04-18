# TAS5805M DAC Linux kernel module 

This is a kernel module code to run a TAS5805M DAC on Raspberry Pi (Zero, Zero W, Zero 2W), Debian-based distributions (Raspbian, Volumio, DietPI, etc)

The repository contains the device tree and source for the `snd-soc-tasdevice` and `tas5805m` kernel modules. It also contains step-by-step instructions on how to use it.

## Introduction

I've created a [set of development boards](https://github.com/sonocotta/raspberry-media-center/) using TAS5805M DAC, and I think this DAC is truly unique in terms of its features for the price. Not only does it provide pure audio with incredibly high efficiency, but being a digital DAC with an I2C control interface, it allows an incredible level of control over the DAC functions. 

![image](https://github.com/user-attachments/assets/61add991-8545-4de7-861d-77367b8f06d8)

When I started to work on this DAC using the ESP32 platform, information available for it was scarce, except for the datasheet nothing could be found. You say, what else would you need? It turns out the datasheet has no information about the DSP function of the device, which is 95% of its complexity (and coolness, I might say). Then there was a journey of trial and error and eventual success on the ESP32 platform, with a consecutive move to the Raspberry.

The Linux kernel has [tas5805m](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/tas5805m.c) module code already. The first issue with it is that it is not included by default in any kernel, and although it is possible to rebuild and reinstall the kernel on the Raspberry Pi, it is definitely not a trivial task to do. But the real issue is that it is a very basic, if not minimal, implementation with nothing but a startup sequence and volume pot implementation. What do we miss out?

- Mixer controls - you have precise control over how left and right channel signals are routed into output drivers. Without it, you can't even get pure mono
- Analog Gain - something you need to consider for lower distortions
- Modulation scheme and switching frequency - very important to reach high efficiency for your power conditions
- Bridge mode - if you aim to push out max power into a single speaker
- 15-channel EQ controls (!!!) with precise transfer characteristics control
- DRC/AGL for fine-tuning for specific enclosure/room compensation
- FIR filter (no idea what it is for)
- Soft-clipping - for lower distortions when you push it beyond reasonable.

Nuff said, it is a great device and deserves community attention and adoption.

## Word on dual DAC configuration

The TAS5805M supports chaining devices in either pass-through or DSP-chaining mode. Specifically, it exposes the SDOUT pin that would be an input pin for downstream DAC and allows configuration of that SDPIN to spit out either the original data stream or post-DSP data. 

I started to update the original implementation to support dual DAC operation, but got stuck configuring the original device tree implementation into a single-audio-device -> multiple DACs config. I reached out to TI support and they provided a whole new driver code that seems to support device chaining.

After looking inside the code I found it to be a bit clumsy, and many features missing compared to the original implementation. But in other aspects TI ode was much better integrated into linux kernel ASoC, and most importantly, after some changes it worked with dual DAC implemetation.

At this point, I will provide this implementation as an alternative to the original driver until I figure out how to make it work in chaining config

### What doesn't look right in the [this branch] TI driver implementation

- The device tree defines only the master I2C device; all slave devices are identified on the fly.
- DSP configuration is provided via a binary format that is extremely inconvenient to use
- DSP configuration needs to be generated for a specific setup and cannot be easily changed or adjusted
- None of the ALSA controls are implemented properly; to be more specific, they only work for the **latest DAC** in the chain 
- Binary configuration has a static naming, so changing the config is a multi-step file-copying exercise

## Installation

We're about to build kernel modules, so we need to install a few dependencies first (all commands going forward are running on the target host, ie, Raspberry Pi) 

```
$ sudo apt update && sudo apt install git raspberrypi-kernel-headers build-essential -y
```

Let's get the code from GitHub 

```
$ git clone https://github.com/sonocotta/tas5805m-for-raspbian-paspberry-pi-zero && cd tas5805m-for-raspbian-paspberry-pi-zero

# if you're building for a dual DAC device (2X)
git checkout -b features/louder-raspberry-2x-support

```

## Device tree 

We need to add user overlays to enable I2S  (disabled by default) and enable a sound card on that port

```
$ sudo ./compile-overlay.sh
```

This will compile the overlay file, put the compiled file under `/boot/overlays` 

Next, add to `/boot/firmware/config.txt`

```
# Enable DAC (Louder Raspberry Media Center or Louder Raspberry 1X Hat, original driver)
dtoverlay=tas5805m,i2creg=0x2d

# Raspberry  DAC (Louder Raspberry Media Center or Louder Raspberry 1X Hat, TI driver)
dtoverlay=tas5805-1x

# Enable DAC (Louder Raspberry, original driver)
dtoverlay=tas5805m-2x

```

`0x2d` is an I2C address of the device; it can be different on boards, but you should find in the documentation what it is exactly. Another way to validate it is to check the device directly

```
# install i2c tools
sudo apt install i2c-tools

# list devices
i2cdetect -y 1

```

spits out smth like this:
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- 2d 2e --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- —
```

While you're in the `config.txt` file, you may also comment out built-in audio and HDMI if you're running headless

```
#dtparam=audio=on
#dtoverlay=vc4-kms-v3d
```

You need to reboot for changes to take effect, but this will not work just yet. We are referencing the `tas5805m` or `snd-soc-tasdevice` kernel module there, and this one is not there yet. Therefore, we will build it on the same host using the current kernel sources that we just pulled 

## Kernel module - basic setup

Now you are ready to build. The first command produces `tas5805m.ko` (or `snd-soc-tasdevice` on TI driver) file among others. Second, will copy it to the appropriate kernel modules folder.

```
$ make all
$ sudo make install
```

In case of the TI driver last step is to provide the DSP configuration to the driver
```
# Single subwoofer (PBTL) device
sudo cp json/cfg-0.1/tas5805-0.1-novolume.bin /lib/firmware/tas5805-1amp-reg.bin

# Single stereo (BTL) device
sudo cp json/cfg-2.0/tas5805-2.0-novolume.bin /lib/firmware/tas5805-1amp-reg.bin

# DUAL 2.1 (2xBTL + PBTL) device
sudo cp json/cfg-2.1/tas5805-2.1-novolume.bin /lib/firmware/tas5805-2amp-reg.bin

```

Now we are ready to reboot and check if we have a sound card listed

```
$ aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: LouderRaspberry [Louder-Raspberry], device 0: bcm2835-i2s-tas5805m-amplifier tas5805m-amplifier-0 [bcm2835-i2s-tas5805m-amplifier tas5805m-amplifier-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

Check if the new module is correctly loaded

```
$ lsmod | grep tas5805m
tas5805m                6269  1
regmap_i2c              5027  1 tas5805m
snd_soc_core          240140  4 snd_soc_simple_card_utils,snd_soc_bcm2835_i2s,tas5805m,snd_soc_simple_card
```

Now, quick test if the audio is functional

```
speaker-test -t sine -f 500 -c 2
```

Finally, I can hear a beeping sound on both speakers. Hooray!

ALSA should give you baseline controls as well

```
alsamixer
```
![image](https://github.com/user-attachments/assets/58c44fbe-2532-4ca3-b89e-1d676f43647b)

In case of Ti driver set of controls looks a bit different, with an important addition: profile Id

TODO: add image

### [TI driver] Profile Id

TI driver implements multiple DSP profiles in the same binary firmware, that can be changed on the fly. The only  limitation is you need to stop playback for ~10 seconds for the DAC to go into sleep mode, and the new profile will be applied on playback resume.

I've implemented 11 profiles for all configurations: 0.1 (subwoofer profiles) / 2.0 (stereo) / 2.1 configs

- 0: 60Hz cutoff low-pass / 60Hz cutoff high-pass / 60Hz crossover  
- 1: 70Hz cutoff low-pass / 70Hz cutoff high-pass / 70Hz crossover
- 2: 80Hz cutoff low-pass / 80Hz cutoff high-pass / 80Hz crossover
...
- 9: 150Hz cutoff low-pass / 150Hz cutoff high-pass / 150Hz crossover
- 10: Flat response /  Flat response / No crossover

After you set up your driver, select the desired profile, and it will be preserved across reboots

### Digital Volume and Analog Gain

> A combination of digital gain and analog gain is used to provide the overall gain of the speaker amplifier. The total amplifier gain consists of the digital gain and the analog gain from the input of the analog modulator to the output of the speaker amplifier power stage.

> The first gain stage of the speaker amplifier is present in the digital audio path. Digital
gain consists of the volume control, input Mixer, or output Crossbar. The digital gain is set to 0dB by default.
Change analog gain via register 0x54, AGAIN[4:0], which supports a 32-step analog gain setting (0.5dB per step).
These analog gain settings ensure that the output signal is not clipped at different PVDD levels. 0dBFS output
corresponds to 29.5-V peak output voltage. 

<details>
<summary>Analog gain</summary>

| Binary | dB Value | Output Voltage (V) | Alsa display value |
|--------|----------|--------------------|--------------------|
| 0      | 0        | 29.5               | 100                |
| 1      | -0.5     | 27.92              | 97                 |
| 10     | -1       | 26.42              | 94                 |
| 11     | -1.5     | 25                 | 90                 |
| 100    | -2       | 23.65              | 87                 |
| 101    | -2.5     | 22.38              | 84                 |
| 110    | -3       | 21.17              | 81                 |
| 111    | -3.5     | 20.02              | 77                 |
| 1000   | -4       | 18.94              | 74                 |
| 1001   | -4.5     | 17.91              | 71                 |
| 1010   | -5       | 16.94              | 68                 |
| 1011   | -5.5     | 16.02              | 65                 |
| 1100   | -6       | 15.15              | 61                 |
| 1101   | -6.5     | 14.33              | 58                 |
| 1110   | -7       | 13.55              | 55                 |
| 1111   | -7.5     | 12.82              | 52                 |
| 10000  | -8       | 12.13              | 48                 |
| 10001  | -8.5     | 11.48              | 45                 |
| 10010  | -9       | 10.87              | 42                 |
| 10011  | -9.5     | 10.29              | 39                 |
| 10100  | -10      | 9.75               | 35                 |
| 10101  | -10.5    | 9.24               | 32                 |
| 10110  | -11      | 8.76               | 29                 |
| 10111  | -11.5    | 8.3                | 26                 |
| 11000  | -12      | 7.87               | 23                 |
| 11001  | -12.5    | 7.46               | 19                 |
| 11010  | -13      | 7.08               | 16                 |
| 11011  | -13.5    | 6.71               | 13                 |
| 11100  | -14      | 6.37               | 10                 |
| 11101  | -14.5    | 6.04               | 6                  |
| 11110  | -15      | 5.73               | 3                  |
| 11111  | -15.5    | 4.95               | 0                  |

</details>

Having the analog gain set at the appropriate level, the digital volume should be used to set the desired audio volume. Keep in mind, it is **perfectly safe to set the analog gain at a lower level**, further avoiding clipping (and effectively limiting output power) and reducing digital distortions caused by low digital gain. 

### Driver Modulation scheme

Both the modulation scheme and the switching frequency have an impact on power consumption and losses. 

#### BD Modulation

> This is a modulation scheme that allows operation without the classic LC reconstruction filter when the amp is
driving an inductive load with short speaker wires. Each output is switching from 0 volts to the supply voltage.
The OUTPx and OUTNx are in phase with each other, with no input, so that there is little or no current in the
speaker. The duty cycle of OUTPx is greater than 50% and OUTNx is less than 50% for positive output voltages.
The duty cycle of OUTPx is less than 50%, and OUTNx is greater than 50% for negative output voltages. The
voltage across the load sits at 0 V throughout most of the switching period, reducing the switching current, which
reduces any I2R losses in the load.

#### 1SPW Modulation

> The 1SPW mode alters the normal modulation scheme in order to achieve higher efficiency with a slight penalty
in THD degradation and more attention required in the output filter selection. In Low Idle Current mode, the
outputs operate at ~14% modulation during idle conditions. When an audio signal is applied, one output will
decrease and one will increase. The decreasing output signal will quickly rail to GND at which point all the audio
modulation takes place through the rising output. The result is that only one output is switching during a majority
of the audio cycle. Efficiency is improved in this mode due to the reduction of switching losses.

#### Hybrid Modulation

> Hybrid Modulation is designed to minimize power loss without compromising the THD+N performance, and is
optimized for battery-powered applications. With Hybrid modulation enabled, the device detects the input signal level
and adjust PWM duty cycle dynamically based on PVDD. Hybrid modulation achieves ultra-low idle current and
maintains the same audio performance level as the BD Modulation. In order to minimize the power dissipation,
low switching frequency (For example, Fsw = 384 kHz) with a proper LC filter (15 µH + 0.68 µF or 22 µH + 0.68
µF) is recommended

Not yet implemented:

> 1) With Hybrid Modulation, users need to input the system's PVDD value via the device development App.
> 2) With Hybrid Modulation, Change device state from Deep Sleep Mode to Play Mode, the specific sequence is required:
> 1. Set device's PWM Modulation to BD or 1SPW mode via Register (Book0/Page0/Register0x02h, Bit [1:0]).
> 2. Set device to Hi-Z state via Register (Book0/Page0/Register0x03h, Bit [1:0]).
> 3. Delay 2ms.
> 4. Set device's PWM Modulation to Hybrid mode via Register (Book0/Page0/Register0x02h, Bit[1:0]).
> 5. Delay 15ms.
> 6. Set device to Play state via Register (Book0/Page0/Register0x03h, Bit [1:0])

### Driver Switching frequency

TAS5805M supports different switching frequencies, which mostly affect the balance between output filter losses and EMI noise. Below is the recommendation from TI

![image](https://github.com/user-attachments/assets/72d7c8cf-1e47-4b92-b191-c7f4a6728bd0)

- Ferrite bead filter is appropriate for lower PVCC (< 12V)
- Ferrite bead filter is recommended for use with  Fsw = 384 kHz with Spread spectrum enabled, BD Modulation
- With an Inductor as the output filter, DAC can achieve ultra-low idle current (with Hybrid Modulation or 1SPW Modulation) and keep a large EMI margin. The switching frequency of TAS5805M can be adjusted from 384 kHz to 768 kHz. Higher switching frequency means a smaller Inductor value needed
  - With 768 kHz switching frequency. Designers can select 10uH + 0.68 µF or 4.7 µH +0.68 µF as the output filter, which will help customers to save the Inductor size with the same rated current during the inductor selection. With 4.7uH + 0.68uF, make sure PVDD ≤ 12V to avoid the large ripple current to trigger the OC threshold (5A)
  - With 384 kHz switching frequency. Designers can select 22 µH + 0.68 µF or 15 µH + 0.68 µF or 10 µH + 0.68 µF as the output filter, this will help customers to save power dissipation for some battery power supply applications. With 10 µH + 0.68 µF, make sure PVDD ≤ 12 V to avoid the large ripple current from triggering the OC threshold (5 A).

### Bridge mode

TAS5805M has a bridge mode of operation, which causes both output drivers to synchronize and push out the same audio with double the power.  In that case single speaker is expected to be connected across channels, so remember to reconnect speakers if you're changing to bridge mode. 

|  | BTL | PBTL |
|---|---|---|
| Descriotion | Bridge Tied Load, Stereo | Parallel Bridge Tied Load, Mono |
| Rated Power | 2×23W (8-Ω, 21 V, THD+N=1%) | 45W (4-Ω, 21 V, THD+N=1%) |
| Schematics | ![image](https://github.com/sonocotta/esp32-audio-dock/assets/5459747/e7ada8c0-c906-4c08-ae99-be9dfe907574) | ![image](https://github.com/sonocotta/esp32-audio-dock/assets/5459747/55f5315a-03eb-47c8-9aea-51e3eb3757fe)
| Speaker Connection | ![image](https://github.com/user-attachments/assets/8e5e9c38-2696-419b-9c5b-d278c655b0db) | ![image](https://github.com/user-attachments/assets/8aba6273-84c4-45a8-9808-93317d794a44)


## Basic mode disclaimer

The basic method sets all DSP parameters into a disabled state, so at this point, you're using DAC in its most basic function. Currently, the only way to play with TAS5805M DSP is to buy an evaluation board from TI ($250+) and request TI PurePath software to interact with it. Not only is it incredibly impractical, but you don't get to change settings on the fly as soon as you disconnect your PC from the evaluation board, since you can only take a snapshot of your settings and stick to them forever. Bugger!

  ![image](https://github.com/user-attachments/assets/a324c8b1-c95f-48c9-99c4-a934ab2a7527)
  ![image](https://github.com/user-attachments/assets/77234d68-d83b-4766-aa4e-1ab4c6fae852)
  ![image](https://github.com/user-attachments/assets/f36ff5cf-da54-4bc7-9dc7-cb7e6df62cbd)
  ![image](https://github.com/user-attachments/assets/c6fa2f51-e50d-4753-aae9-f064f7f60d7d)

The work I'm trying to perform is to:

- Allow settings snapshots to be applied on the board's startup without an evaluation kit, so at least you can transfer your carefully crafted settings into a working Raspberry Pi setup
- Allow some settings to be changed on the fly as if you had an evaluation kit all the time.

## Kernel module - pre-defined setup (original driver only)

As said above, you can create a custom DSP config in the TI PurePath application, which I did and included in the [startup](/startup) folder. Some of them are subwoofer configs, and some specific EQ settings fit my taste. I never intended to cover every possible scenario, but rather provide a few examples starting from those I use most often. I encourage everyone to create their own configs in PurePath and include them in the startup folder as well.

First, let's enable the  config we selected in the `tas5805m.c` file. Uncomment two lines like in the example below

```
#pragma message("tas5805m_2.0+eq(+12db_30Hz)(-3Db_500Hz)(+3Db_8kHz)(+3Db_15kHz) config is used")
#include "startup/custom/tas5805m_2.0+eq(+12db_30Hz)(-3Db_500Hz)(+3Db_8kHz)(+3Db_15kHz).h"
```

Enable custom DSP config in the `Makefile` by uncommenting this line

```
CFLAGS_tas5805m.o += -DTAS5805M_DSP_CUSTOM
```

Rebuild and reinstall the driver, and reboot the device

```
make all && sudo make install && sudo reboot
```

The audio changes will be applied after reboot, also, you should find basic settings in the ALSA

![image](https://github.com/user-attachments/assets/741ba389-fc29-48c2-9b65-4a1d1932a5b1)

## Kernel module - change settings on the fly (original driver only)

I'm currently working on the ALSA controls that directly change DSP settings on the device. This allows both on-demand in-place changes and code-driven changes from the UI or automation tools.

Build the kernel with disabled DSP config in the `Makefile` flag

```
# CUSTOM DSP config
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_CUSTOM
```

As usual, build, install, reboot
```
make all && sudo make install && sudo reboot
```

After reboot, you should be able to see the following settings in the ALSA. Let's go through them one by one

![image](https://github.com/user-attachments/assets/217430fa-6c53-4b25-8143-d74a6e383de3)
 
### EQ controls (original driver only)

TAS5805M DAC has a powerful 15-channel EQ that allows defining each channel's transfer function using BQ coefficients. In a practical sense, it allows us to draw pretty much any curve in a frequency response. I decided to split the audio range into 15 sections, defining for each -15Db..+15Db adjustment range and appropriate bandwidth to cause mild overlap. This allows both to keep the curve flat enough to not cause distortions even in extreme settings but also allows a wide range of transfer characteristics. This EQ setup is a common approach for full-range speakers; the subwoofer-specific setup is underway.

| Band | Center Frequency (Hz) | Frequency Range (Hz) | Q-Factor (Approx.) |
|------|-----------------------|----------------------|--------------------|
| 1    | 20                    | 10–30                | 2                  |
| 2    | 31.5                  | 20–45                | 2                  |
| 3    | 50                    | 35–70                | 1.5                |
| 4    | 80                    | 55–110               | 1.5                |
| 5    | 125                   | 85–175               | 1                  |
| 6    | 200                   | 140–280              | 1                  |
| 7    | 315                   | 220–440              | 0.9                |
| 8    | 500                   | 350–700              | 0.9                |
| 9    | 800                   | 560–1120             | 0.8                |
| 10   | 1250                  | 875–1750             | 0.8                |
| 11   | 2000                  | 1400–2800            | 0.7                |
| 12   | 3150                  | 2200–4400            | 0.7                |
| 13   | 5000                  | 3500–7000            | 0.6                |
| 14   | 8000                  | 5600–11200           | 0.6                |
| 15   | 16000                 | 11200–20000          | 0.5                |

Here are a few examples of different configurations that can be created with the above setup. 

  ![image](https://github.com/user-attachments/assets/91f360fa-7e2a-4ca8-8b72-4ed7830bf7f7)
  ![image](https://github.com/user-attachments/assets/15164675-8899-44b7-a551-0585a2a7fd8c)
  ![image](https://github.com/user-attachments/assets/31f17a19-9dbe-4e9e-947e-0cbcccbf218c)
  ![image](https://github.com/user-attachments/assets/c0445bd6-29f6-44d0-9632-14becc1de35e)

### Mixer settings (original driver only)

Mixer settings allow you to mix channel signals and route them to the appropriate channel. The typical setup for the mixer is to send the Left channel audio to the Left driver, and the right channel to the Right 

TODO: add mixer values table

![image](https://github.com/user-attachments/assets/d1a24adf-a417-48a1-b35d-39ee9d199587)

A common alternative is to combine both channels into true Mono (you need to reduce both to -3Db to compensate for signal doubling)

![image](https://github.com/user-attachments/assets/390d1ecb-e3cd-4fff-8951-80fc318ec7d9)

Of course, you can decide to use a single channel or a mix of two, just keep in mind that the sum of the signal may cause clipping if not compensated properly.

## Known issues

The current version of the driver is built in a very specific and atypical way. The DSP initialization is done on the first PLAY/RESUME event. Why is that specifically? TAS5805M requires an I2S clock to be present and stable for at least a few milliseconds before DSP settings will be considered. If controls are applied before the clock is present, they will be simply ignored. 

The method described above worked well until DSP settings became dynamic. Upon reboot `alsa-restore` service will try to apply your last settings, and with the current architecture, they will be simply ignored.

I'm currently working to fix this bug in either

# TODO

- [ ] Spread spectrum switch
- [ ] Soft clipper settings
- [ ] Power testing for switching frequency, modulation, and output filters
- [ ] Specific recommendations for my boards 
- [ ] Fix the bug with the `alsa-restore` service not working for anything but Digital Volume (this can be fixed simply by excluding ALSA-controlled registers from the DSP startup sequence)
- [ ] Allow config loading from simple text format 

## References

- [TAS5805M Datasheet](https://www.ti.com/lit/ds/symlink/tas5805m.pdf?ts=1711108445083) 
- [TAS5805M: Linux driver for TAS58xx family](https://e2e.ti.com/support/audio-group/audio/f/audio-forum/1165952/tas5805m-linux-driver-for-tas58xx-family)
- [Linux/TAS5825M: Linux drivers](https://e2e.ti.com/support/audio-group/audio/f/audio-forum/722027/linux-tas5825m-linux-drivers)
