// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TASDEVICE Audio Smart Amplifier
//
// Copyright (C) 2022 - 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS5805/TAS5825 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS5805/TAS5825 chips.
//
// Author: Andriy Malyshenko <andriy@sonocotta.com>
//

#ifndef __TAS5805_REG_H__
#define __TAS5805_REG_H__

/* Datasheet-defined registers on page 0, book 0 */
#define TAS5805M_REG_DEVICE_CTRL_1   TASDEVICE_REG(0X0, 0x0, 0x02)
#define TAS5805M_REG_DEVICE_CTRL_2   TASDEVICE_REG(0X0, 0x0, 0x03)
#define TAS5805M_REG_VOL_CTL         TASDEVICE_REG(0X0, 0x0, 0x4c)
#define TAS5805M_REG_ANALOG_GAIN     TASDEVICE_REG(0X0, 0x0, 0x54)
#define TAS5805M_REG_DSP_MISC        TASDEVICE_REG(0X0, 0x0, 0x66)
#define TAS5805M_REG_CHAN_FAULT      TASDEVICE_REG(0X0, 0x0, 0x70)
#define TAS5805M_REG_GLOBAL_FAULT1   TASDEVICE_REG(0X0, 0x0, 0x71)
#define TAS5805M_REG_GLOBAL_FAULT2   TASDEVICE_REG(0X0, 0x0, 0x72)
#define TAS5805M_REG_FAULT           TASDEVICE_REG(0X0, 0x0, 0x78)
#define TAS5805M_REG_CLKDET_STATUS   TASDEVICE_REG(0X0, 0x0, 0x39)

/* DEVICE_CTRL_2 register values */
#define TAS5805M_DCTRL2_MODE_DEEP_SLEEP 0x00
#define TAS5805M_DCTRL2_MODE_SLEEP      0x01
#define TAS5805M_DCTRL2_MODE_HIZ        0x02
#define TAS5805M_DCTRL2_MODE_PLAY       0x03

#define TAS5805M_DCTRL2_MUTE            0x08
#define TAS5805M_DCTRL2_DIS_DSP         0x10

/* TAS5805M_REG_FAULT register values */
#define TAS5805M_ANALOG_FAULT_CLEAR     0x80

#define TAS5805M_REG_LEVEL_METER_LEFT  TASDEVICE_REG(0X78, 0x02, 0x60)
#define TAS5805M_REG_LEVEL_METER_RIGHT TASDEVICE_REG(0X78, 0x02, 0x64)

// Mixer registers
#define TAS5805M_REG_BOOK_5 0x8c
#define TAS5805M_REG_BOOK_5_MIXER_PAGE 0x29

#define TAS5805M_REG_LEFT_TO_LEFT_GAIN      TASDEVICE_REG(0X8c, 0x29, 0x18)
#define TAS5805M_REG_RIGHT_TO_LEFT_GAIN     TASDEVICE_REG(0X8c, 0x29, 0x1c)
#define TAS5805M_REG_LEFT_TO_RIGHT_GAIN     TASDEVICE_REG(0X8c, 0x29, 0x20)
#define TAS5805M_REG_RIGHT_TO_RIGHT_GAIN    TASDEVICE_REG(0X8c, 0x29, 0x24)

#define TAS5805M_MIXER_MIN_DB -110
#define TAS5805M_MIXER_MAX_DB 0

enum {
    NORMAL_MODE = 0,  /* Normal mode (bit 1 = 0) */
    BRIDGE_MODE = 1   /* Bridge mode (bit 1 = 1) */
};

enum {
    EQ_DISABLED = 0, 
    EQ_ENABLED  = 1
};

static const char * const eq_mode_text[] = {
    "On",  /* Text for value 0 */
    "Off"   /* Text for value 1 */
};

static const char * const dac_mode_text[] = {
    "Normal",  /* Text for value 0 */
    "Bridge"   /* Text for value 1 */
};

static const char * const modulation_mode_text[] = {
    "BD",  /* Text for value 0 */
    "1SPW",   /* Text for value 1 */
    "Hybrid"   /* Text for value 2 */
};

static const char * const switch_freq_text[] = {
    "768K",  /* Text for value 0 */
    "384K",   /* Text for value 1 */
    "480K",   /* Text for value 2 */
    "576K",   /* Text for value 3 */
};

#endif
