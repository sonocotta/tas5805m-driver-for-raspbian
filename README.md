# TAS5805M DAC Linux kernel module 

This is a kernel module code to run a TAS5805M DAC on Raspberry Pi (Zero, Zero W, Zero 2W), Debian-based distributions (Raspbian, Volumio, DietPI, etc)

The repository contains the device tree and source for the `tas5805m` kernel module. It also contains step-by-step instructions on how to use it.

## Introduction

I've created a [set of development boards](https://github.com/sonocotta/raspberry-media-center/) using TAS5805M DAC, and I think this DAC is truly unique in terms of its features for the price. Not only does it provide pure audio with incredibly high efficiency, but being a digital DAC with an I2C control interface it allows an incredible level of control over the DAC functions. When I started to work on this DAC using the ESP32 platform, information available for it was scarce, except for the datasheet nothing could be found. You say what else would you need? It turns out the datasheet has no information about the DSP function of the device, which is 95% of its complexity (and coolness I might say). Then there was a journey of trial and error and eventual success on the ESP32 platform, with consecutive move to the Raspberry.

Linux kernel has [tas5805m](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/tas5805m.c) module code already. The first issue with it, it is not included by default in any kernel, and although it is possible to rebuild and reinstall the kernel on the Raspberry, it is definitely not a trivial task to do. But the real issue is, that it is a very basic, if not minimal implementation with nothing but startup sequence and volume pot implementation. what do we miss out?

- Mixer controls - you have precise control over how left and right channel signals are routed into output drivers. Without it, you can't even get pure mono
- Analog Gain - something you need to consider for lower distortions
- Modulation scheme and switching frequency - very important to reach high efficiency for your power conditions
- Bridge mode - if you aim to push out max power into a single speaker
- 15-channel EQ controls (!!!) with precise transfer characteristics control
- DRC/AGL for fine-tuning for specific enclosure/room compensation
- FIR filter (no idea what it is for)
- Soft-clipping - for lower distortions when you push it beyond reasonable.

Nuff said, it is a great device and deserves community attention and adoption.

## Installation

We're about to build kernel modules, so we need to install a few dependencies first (all commands going forward are running on the target host, ie Raspberry Pi) 

```
$ sudo apt update && sudo apt install git raspberrypi-kernel-headers build-essential -y
```

Let's get code from GitHub 

```
$ git clone https://github.com/sonocotta/tas5805m-for-raspbian-paspberry-pi-zero && cd tas5805m-for-raspbian-paspberry-pi-zero
```


## Device tree 

We need to add user overlays to enable I2S  (disabled by default) and enable a sound card on that port

```
$ sudo ./compile-overlay.sh
```

this will compile the overlay file, put the compiled file under `/boot/overlays` 

### Single DAC Configuration

Next, add to `/boot/config.txt`

```
# Enable DAC
dtoverlay=tas5805m,i2creg=0x2d
```

`0x2d` is an I2C address of the device, it can be different on boards, but you should find in the documentation what it is exactly

### Dual DAC Configuration (2.1 Stereo + Subwoofer)

For a 2.1 audio system with stereo speakers and a subwoofer, use the dual overlay:

```
# Enable Dual DAC (2.1 audio)
dtoverlay=tas5805m-dual
```

This configuration:
- Primary DAC (0x2d, GPIO4): Stereo speakers in normal mode with HF crossover (high-pass filter)
- Secondary DAC (0x2e, GPIO5): Subwoofer in bridge/PBTL mode with LF crossover (low-pass filter)
- Both DACs: Hybrid modulation, 768kHz switching frequency
- Mixer modes locked via device tree for safety

### Device Tree Properties

The driver supports several device tree properties to configure hardware behavior. These settings are applied at boot and cannot be changed at runtime (for safety and stability):

| Property | Values | Default | Description |
|----------|--------|---------|-------------|
| `ti,modulation-mode` | 0=BD, 1=1SPW, 2=Hybrid | 2 (Hybrid) | PWM modulation scheme |
| `ti,switching-freq` | 0=768K, 1=384K, 2=480K, 3=576K | 0 (768K) | PWM switching frequency |
| `ti,bridge-mode` | boolean | false | Enable bridge/PBTL mode for mono high-power output |
| `ti,eq-mode` | 0=OFF, 1=15-band, 2=LF Crossover, 3=HF Crossover | 1 (15-band) | Equalizer mode |
| `ti,mixer-mode` | 0=Stereo, 1=Mono, 2=Left, 3=Right | 0 (Stereo) | Channel mixer preset |

When `ti,mixer-mode` is set in the device tree, individual mixer sliders are hidden from ALSA. 

Bridge mode is also set in the device tree and do not get changed on the fly. This prevents accidental changes that could damage speakers (especially important for bridge mode and subwoofers).

Example custom configuration in device tree:

```dts
tas5805m@2d {
    compatible = "ti,tas5805m";
    reg = <0x2d>;
    pvdd-supply = <&vdd_3v3_reg>;
    pdn-gpios = <&gpio 4 0>;
    ti,modulation-mode = <2>;  /* Hybrid */
    ti,switching-freq = <0>;   /* 768kHz */
    ti,bridge-mode;            /* Enable PBTL mode */
    ti,eq-mode = <0>;          /* EQ disabled */
    ti,mixer-mode = <1>;       /* Mono mode */
};
```

### Optional Settings

You may also comment out built-in audio and HDMI if you're running headless

```
#dtparam=audio=on
#dtoverlay=vc4-kms-v3d
```

You need to reboot for changes to take effect, but this will not work just yet. We are referencing the `tas5805m` kernel module there, and this one is not there yet. Therefore we will build it on the same host using the current kernel sources that we just pulled 

## Kernel module - basic setup

Now you are ready to build. The first command produces `tas5805m.ko` file among others. Second will copy it to the appropriate kernel modules folder.

```
$ make all
$ sudo make install
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

Now quick test if the audio is functional

```
speaker-test -t sine -f 500 -c 2
```

Finally, I can hear a beeping sound on both speakers. Hooray!

Alsa should give you baseline controls as well

```
alsamixer
```
![image](https://github.com/user-attachments/assets/58c44fbe-2532-4ca3-b89e-1d676f43647b)

### Digital Volume and Analog Gain

> A combination of digital gain and analog gain is used to provide the overall gain of the speaker amplifier. The total amplifier gain consists of the digital gain and the analog gain from the input of the analog modulator to the output of the speaker amplifier power stage.

> The first gain stage of the speaker amplifier is present in the digital audio path. Digital
gain consists of the volume control, input Mixer, or output Crossbar. The digital gain is set to 0dB by default.
Change analog gain via register 0x54, AGAIN[4:0] which supports 32 steps analog gain setting (0.5dB per step).
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

Having analog gain set at the appropriate level, the digital volume should be used to set the desired audio volume. Keep in mind, it is **perfectly safe to set the analog gain at a lower level**, further avoiding clipping (and effectively limiting output power) and reducing digital distortions caused by low digital gain. 

### Driver Modulation scheme

Both modulation scheme and switching frequency have an impact on power consumption and losses. 

#### BD Modulation

> This is a modulation scheme that allows operation without the classic LC reconstruction filter when the amp is
driving an inductive load with short speaker wires. Each output is switching from 0 volts to the supply voltage.
The OUTPx and OUTNx are in phase with each other with no input so that there is little or no current in the
speaker. The duty cycle of OUTPx is greater than 50% and OUTNx is less than 50% for positive output voltages.
The duty cycle of OUTPx is less than 50% and OUTNx is greater than 50% for negative output voltages. The
voltage across the load sits at 0 V throughout most of the switching period, reducing the switching current, which
reduces any I2R losses in the load.

#### 1SPW Modulation

> The 1SPW mode alters the normal modulation scheme in order to achieve higher efficiency with a slight penalty
in THD degradation and more attention required in the output filter selection. In Low Idle Current mode the
outputs operate at ~14% modulation during idle conditions. When an audio signal is applied one output will
decrease and one will increase. The decreasing output signal will quickly rail to GND at which point all the audio
modulation takes place through the rising output. The result is that only one output is switching during a majority
of the audio cycle. Efficiency is improved in this mode due to the reduction of switching losses.

#### Hybrid Modulation

> Hybrid Modulation is designed to minimize power loss without compromising the THD+N performance, and is
optimized for battery-powered applications. With Hybrid modulation enabled, the device detects the input signal level
and adjust PWM duty cycle dynamically based on PVDD. Hybrid modulation achieves ultra low idle current and
maintains the same audio performance level as the BD Modulation. In order to minimize the power dissipation,
low switching frequency (For example, Fsw = 384 kHz) with proper LC filter (15 µH + 0.68 µF or 22 µH + 0.68
µF) is recommended.

Hybrid modulation is the default and can be changed via device tree (`ti,modulation-mode`). **Note:** Modulation mode is not runtime configurable for stability and is set at boot via device tree.

### Driver Switching frequency

TAS5805M supports different switching frequencies (384kHz, 480kHz, 576kHz, 768kHz), which mostly affect the balance between output filter losses and EMI noise. The switching frequency is configured via device tree (`ti,switching-freq`) and defaults to 768kHz. **Note:** Switching frequency is not runtime configurable for stability and is set at boot via device tree.

Below is the recommendation from TI

![image](https://github.com/user-attachments/assets/72d7c8cf-1e47-4b92-b191-c7f4a6728bd0)

- Ferrite bead filter is appropriate for lower PVCC (< 12V)
- Ferrite bead filter is recommended for use with  Fsw = 384 kHz with Spread spectrum enable, BD Modulation
- With Inductor as the output filter, DAC can achieve ultra low idle current (with Hybrid Modulation or 1SPW Modulation) and keep large EMI margin. As the switching frequency of TAS5805M can be adjustable from 384kHz to 768 kHz. Higher switching frequency means smaller Inductor value needed
  - With 768 kHz switching frequency. Designers can select 10uH + 0.68 µF or 4.7 µH +0.68 µF as the output filter, this will help customer to save the Inductor size with the same rated current during the inductor selection. With 4.7uH + 0.68uF, make sure PVDD ≤ 12V to avoid the large ripple current to trigger the OC threshold (5A)
  - With 384 kHZ switching frequency. Designers can select 22 µH + 0.68 µF or 15 µH + 0.68 µF or 10 µH + 0.68 µF as the output filter, this will help customer to save power dissipation for some battery power supply application. With 10 µH + 0.68 µF, make sure PVDD ≤ 12 V to avoid the large ripple current to trigger the OC threshold (5 A).

### Bridge mode

TAS5805M has a bridge mode of operation (PBTL), that causes both output drivers to synchronize and push out the same audio with double the power. In that case a single speaker is expected to be connected across channels, so remember to reconnect speakers if you're changing to bridge mode.

**Important:** Bridge mode is configured via device tree (`ti,bridge-mode`) and cannot be changed at runtime. This prevents accidental switching that could damage speakers.

|  | BTL | PBTL |
|---|---|---|
| Description | Bridge Tied Load, Stereo | Parallel Bridge Tied Load, Mono |
| Rated Power | 2×23W (8-Ω, 21 V, THD+N=1%) | 45W (4-Ω, 21 V, THD+N=1%) |
| Configuration | Normal mode (default) | `ti,bridge-mode` in device tree |
| Schematics | ![image](https://github.com/sonocotta/esp32-audio-dock/assets/5459747/e7ada8c0-c906-4c08-ae99-be9dfe907574) | ![image](https://github.com/sonocotta/esp32-audio-dock/assets/5459747/55f5315a-03eb-47c8-9aea-51e3eb3757fe)
| Speaker Connection | ![image](https://github.com/user-attachments/assets/8e5e9c38-2696-419b-9c5b-d278c655b0db) | ![image](https://github.com/user-attachments/assets/8aba6273-84c4-45a8-9808-93317d794a44)

## Basic mode disclaimer

**Note:** This section describes the original limitations that motivated this driver development. The driver now provides full dynamic control without requiring an evaluation board.

Originally, the only way to configure TAS5805M DSP was to buy an evaluation board from TI ($250+) and use TI PurePath software. This approach had major limitations:
- No ability to change settings on the fly
- Settings locked after creating a snapshot
- Requires keeping the evaluation board connected

This driver solves these problems by providing:

  ![image](https://github.com/user-attachments/assets/a324c8b1-c95f-48c9-99c4-a934ab2a7527)
  ![image](https://github.com/user-attachments/assets/77234d68-d83b-4766-aa4e-1ab4c6fae852)
  ![image](https://github.com/user-attachments/assets/f36ff5cf-da54-4bc7-9dc7-cb7e6df62cbd)
  ![image](https://github.com/user-attachments/assets/c6fa2f51-e50d-4753-aae9-f064f7f60d7d)

This driver provides:

-  Load PurePath configuration snapshots at boot via firmware files
-  Full dynamic control of all major DSP parameters through ALSA
-  Real-time adjustment without reboots or reconnecting hardware
-  15-band parametric equalizer with live updates
-  Mixer controls for flexible channel routing
-  Modulation and power optimization settings
-  Bridge mode for maximum power output

## Kernel module - loading custom DSP configuration

You can create a custom DSP config in the TI PurePath application and load it at boot time. The driver supports loading DSP configuration from firmware files.

### Creating a DSP configuration binary

1. Use TI PurePath Console software to create your desired configuration
2. Export the configuration as a register dump (sequence of register address/value pairs)
3. Create a binary file with the register sequence (omit the pre-boot initialization sequence that occurs before the 5ms delay)
4. Name the file `tas5805m_dsp_<config_name>.bin`
5. Place the file in `/lib/firmware/`

### Enabling the configuration

Add the configuration name to your device tree overlay or `/boot/config.txt`:

```
dtoverlay=tas5805m,i2creg=0x2d,dsp_config_name=<config_name>
```

For example, if your file is `tas5805m_dsp_myconfig.bin`, use:

```
dtoverlay=tas5805m,i2creg=0x2d,dsp_config_name=myconfig
```

Rebuild and reinstall the driver, then reboot:

```
make all && sudo make install && sudo reboot
```

The DSP configuration will be applied on first audio playback. If no configuration is specified, the driver will start with default settings and all controls available through ALSA.

## Kernel module - dynamic DSP controls

The driver provides comprehensive ALSA controls that allow real-time changes to DSP settings. This enables both manual adjustments through `alsamixer` and automated changes from scripts or applications.

### Using the controls

Build and install the driver:

```
make all && sudo make install && sudo reboot
```

After reboot, you can access all settings through ALSA. Use `alsamixer` for interactive control or `amixer` for scripting. All changes take effect immediately without requiring a reboot.

### Control Availability

The available ALSA controls depend on your device tree configuration:

**Always Available:**
- Digital Volume
- Analog Gain
- Equalizer (on/off toggle)

**Conditionally Available:**
- **15-band EQ sliders**: Only when `ti,eq-mode=<1>` (15-band mode)
- **Crossover Frequency**: Only when `ti,eq-mode=<2>` (LF Crossover) or `ti,eq-mode=<3>` (HF Crossover)
- **Mixer Mode + Individual Sliders**: Only when `ti,mixer-mode` is **NOT** set in device tree

**Example Configurations:**

*Single DAC (default):*
- Digital Volume, Analog Gain, Equalizer
- 15 EQ band sliders (00020 Hz - 16000 Hz)
- Mixer Mode control
- 4 individual mixer sliders (L2L, R2L, L2R, R2R)

*Dual DAC Primary (2.0 stereo):*
- Digital Volume, Analog Gain, Equalizer
- Crossover Frequency slider (OFF, 60-150Hz) for HF crossover
- No mixer controls (locked to Stereo via device tree)

*Dual DAC Secondary (0.1 subwoofer):*
- Digital Volume, Analog Gain, Equalizer
- Crossover Frequency slider (OFF, 60-150Hz) for LF crossover
- No mixer controls (locked to Mono via device tree)

Let's go through the available controls:

![image](https://github.com/user-attachments/assets/217430fa-6c53-4b25-8143-d74a6e383de3)

### EQ controls

TAS5805M DAC has a powerful 15-channel EQ, that allows defining each channel's transfer function using BQ coefficients. In a practical sense, it allows us to draw pretty much any curve in a frequency response. 

The driver supports three EQ modes, configured via device tree (`ti,eq-mode`):

1. **15-band Parametric EQ** (default, `ti,eq-mode=<1>`): Full-range speakers with -15dB to +15dB adjustment per band
2. **LF Crossover Filter** (`ti,eq-mode=<2>`): Low-pass filter for subwoofers with adjustable cutoff frequency (OFF, 60-150Hz)
3. **HF Crossover Filter** (`ti,eq-mode=<3>`): High-pass filter for satellite speakers with adjustable cutoff frequency (OFF, 60-150Hz)

**Note:** The EQ mode is set at boot via device tree and determines which ALSA controls are available. When set to OFF (`ti,eq-mode=<0>`), no EQ controls appear. Both crossover modes use the same "Crossover Frequency" slider but apply different filter types (low-pass vs high-pass).

#### EQ Mode ALSA Controls

The driver dynamically registers different ALSA controls based on the EQ mode configured in the device tree:

**15-Band Parametric EQ Mode** (`ti,eq-mode=<1>`):
- Equalizer: On/Off toggle
- 15 frequency band sliders (00020 Hz through 16000 Hz)
- Each band adjustable from -15dB to +15dB

<img width="1568" height="433" alt="image" src="https://github.com/user-attachments/assets/d85b934c-6b2d-4548-92cc-8908394258b2" />

**LF Crossover Mode** (`ti,eq-mode=<2>`) - For subwoofers:
- Equalizer: On/Off toggle  
- Crossover Frequency: Enumerated control (OFF, 60Hz, 70Hz, 80Hz, 90Hz, 100Hz, 110Hz, 120Hz, 130Hz, 140Hz, 150Hz)
- Applies low-pass filter with 4th order Linkwitz-Riley response

<img width="459" height="433" alt="image" src="https://github.com/user-attachments/assets/12084150-85d5-4ec2-a355-b41cdc9964cc" />


**HF Crossover Mode** (`ti,eq-mode=<3>`) - For satellite speakers:
- Equalizer: On/Off toggle
- Crossover Frequency: Enumerated control (OFF, 60Hz, 70Hz, 80Hz, 90Hz, 100Hz, 110Hz, 120Hz, 130Hz, 140Hz, 150Hz)
- Applies high-pass filter with 4th order Linkwitz-Riley response

<img width="464" height="434" alt="image" src="https://github.com/user-attachments/assets/0f84c584-6d27-4694-8227-9ad7c22462b9" />


**EQ Disabled Mode** (`ti,eq-mode=<0>`):
- Equalizer: On/Off toggle (no effect, EQ bypassed)
- No frequency controls available

All EQ modes include the "Equalizer" control which enables/disables the entire EQ processing. This allows runtime control even when using crossover filters.

#### 15-Band Parametric EQ

I decided to split the audio range into 15 sections, defining for each -15Db..+15Db adjustment range and appropriate bandwidth to cause mild overlap. This allows both to keep the curve flat enough to not cause distortions even in extreme settings but also allows a wide range of transfer characteristics. This EQ setup is a common approach for full-range speakers.

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

### Mixer settings

Mixer settings control how left and right input channels are routed to the output channels. The driver provides two ways to configure the mixer:

#### Device Tree Configuration (Recommended)

Set `ti,mixer-mode` in device tree to lock the mixer configuration. This prevents runtime changes that could damage speakers:

| Mode | Value | L→L | R→L | L→R | R→R | Use Case |
|------|-------|-----|-----|-----|-----|----------|
| Stereo | 0 | 0dB | Mute | Mute | 0dB | Normal stereo speakers (default) |
| Mono | 1 | -6dB | -6dB | -6dB | -6dB | Subwoofer (mix L+R with compensation) |
| Left | 2 | 0dB | Mute | 0dB | Mute | Both outputs from left channel |
| Right | 3 | Mute | 0dB | Mute | 0dB | Both outputs from right channel |

When `ti,mixer-mode` is set, all mixer controls are hidden from ALSA.

#### Runtime Configuration

If `ti,mixer-mode` is **not** set in device tree, the driver exposes both simplified and advanced mixer controls:

1. **Mixer Mode** control: Select preset (Stereo/Mono/Left/Right) - applies values from table above
2. **Individual sliders** (L2L, R2L, L2R, R2R): Fine-tune each routing with -110dB to 0dB range

The typical setup for the mixer is to send Left channel audio to the Left driver, and Right to the Right:

![image](https://github.com/user-attachments/assets/d1a24adf-a417-48a1-b35d-39ee9d199587)

A common alternative is to combine both channels into true Mono (reduced to -6dB to compensate for signal doubling):

![image](https://github.com/user-attachments/assets/390d1ecb-e3cd-4fff-8951-80fc318ec7d9)

**Warning:** When manually adjusting mixer sliders, keep in mind that the sum of signals may cause clipping if not compensated properly. For production systems, use device tree configuration to prevent accidents.

## Known issues

### DSP initialization timing

The driver initializes the DSP when the PCM trigger START event fires, ensuring the I2S clock is already stable. In multi-codec setups (like the dual DAC configuration), the driver maintains a global list of all TAS5805M devices and triggers initialization for all of them simultaneously when playback starts. This ensures both primary and secondary DACs are configured correctly regardless of when audio playback begins.

### ALSA state restoration

The driver stores control values internally and applies them when the device is powered and the I2S clock is stable. However, settings changed via `alsamixer` will not persist across reboots unless you save the ALSA state:

```bash
sudo alsactl store
```

The `alsa-restore` service will then restore your settings on boot. Note that because DSP initialization happens on first playback, the settings are applied at that time rather than during boot.

# TODO

- [x] Dynamic EQ controls (15-band parametric EQ)
- [x] Mixer controls (L2L, R2L, L2R, R2R)
- [x] Modulation scheme control (BD, 1SPW, Hybrid)
- [x] Switching frequency control
- [x] Bridge mode support
- [x] Analog gain control
- [o] Spread spectrum switch - tested in connection with power consumption and decided not implementing, as it does nothing
- [o] Soft clipper settings - tested on ESP32 driver and proved useless outside of AGL/DRC context
- [o] DRC/AGL controls - tested and lost interest, as it sound compressors are no fun to me in general
- [o] FIR filter controls - requires more investigation on how to generate coefficients
- [x] Power consumption testing for different modulation schemes
- [x] Detailed performance benchmarks for different configurations

## References

- [TAS5805M Datasheet](https://www.ti.com/lit/ds/symlink/tas5805m.pdf?ts=1711108445083) 
- [TAS5805M: Linux driver for TAS58xx family](https://e2e.ti.com/support/audio-group/audio/f/audio-forum/1165952/tas5805m-linux-driver-for-tas58xx-family)
- [Linux/TAS5825M: Linux drivers](https://e2e.ti.com/support/audio-group/audio/f/audio-forum/722027/linux-tas5825m-linux-drivers)
