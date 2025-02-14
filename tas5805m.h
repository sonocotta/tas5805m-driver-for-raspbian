// SPDX-License-Identifier: GPL-2.0
//
// Header file for the TAS5805M Audio Amplifier Driver

#ifndef _TAS5805M_H
#define _TAS5805M_H

#define TAS5805M_CFG_META_SWITCH (255)
#define TAS5805M_CFG_META_DELAY (254)
#define TAS5805M_CFG_META_BURST (253)

#define TAS5805M_REG_PAGE_SET 0x00
#define TAS5805M_REG_BOOK_SET 0x7f

#define TAS5805M_REG_BOOK_CONTROL_PORT 0x00
#define TAS5805M_REG_PAGE_ZERO 0x00

/* Datasheet-defined registers on page 0, book 0 */
#define TAS5805M_REG_DEVICE_CTRL_1   0x02
#define TAS5805M_REG_DEVICE_CTRL_2   0x03
#define TAS5805M_REG_VOL_CTL     0x4c
#define TAS5805M_REG_ANALOG_GAIN       0x54
#define TAS5805M_REG_DSP_MISC        0x66
#define TAS5805M_REG_CHAN_FAULT      0x70
#define TAS5805M_REG_GLOBAL_FAULT1   0x71
#define TAS5805M_REG_GLOBAL_FAULT2   0x72
#define TAS5805M_REG_FAULT       0x78

/* DEVICE_CTRL_2 register values */
#define TAS5805M_DCTRL2_MODE_DEEP_SLEEP 0x00
#define TAS5805M_DCTRL2_MODE_SLEEP  0x01
#define TAS5805M_DCTRL2_MODE_HIZ    0x02
#define TAS5805M_DCTRL2_MODE_PLAY   0x03

#define TAS5805M_DCTRL2_MUTE        0x08
#define TAS5805M_DCTRL2_DIS_DSP     0x10

/* TAS5805M_REG_FAULT register values */
#define TAS5805M_ANALOG_FAULT_CLEAR 0x80

#define TAS5805M_REG_BOOK_4 0x78
#define TAS5805M_REG_BOOK_4_LEVEL_METER_PAGE 0x02
#define TAS5805M_REG_LEVEL_METER_LEFT 0x60
#define TAS5805M_REG_LEVEL_METER_RIGHT 0x64

// Mixer registers
#define TAS5805M_REG_BOOK_5 0x8c
#define TAS5805M_REG_BOOK_5_MIXER_PAGE 0x29
#define TAS5805M_REG_LEFT_TO_LEFT_GAIN 0x18
#define TAS5805M_REG_RIGHT_TO_LEFT_GAIN 0x1c
#define TAS5805M_REG_LEFT_TO_RIGHT_GAIN 0x20
#define TAS5805M_REG_RIGHT_TO_RIGHT_GAIN 0x24

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
};

#endif // _TAS5805M_H