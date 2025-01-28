#pragma once

#define CFG_META_SWITCH (255)
#define CFG_META_DELAY (254)
#define CFG_META_BURST (253)
#define CFG_END_1 (0Xaa)
#define CFG_END_2 (0Xcc)
#define CFG_END_3 (0Xee)

#define REG_PAGE_SET 0x00
#define REG_BOOK_SET 0x7f

#define REG_BOOK_CONTROL_PORT 0x00
#define REG_PAGE_ZERO 0x00

/* Datasheet-defined registers on page 0, book 0 */
#define REG_DEVICE_CTRL_1   0x02
#define REG_DEVICE_CTRL_2   0x03
#define REG_SIG_CH_CTRL     0x28
#define REG_SAP_CTRL_1      0x33
#define REG_FS_MON      0x37
#define REG_BCK_MON     0x38
#define REG_CLKDET_STATUS   0x39
#define REG_VOL_CTL     0x4c
#define REG_AGAIN       0x54
#define REG_ADR_PIN_CTRL    0x60
#define REG_ADR_PIN_CONFIG  0x61
#define REG_CHAN_FAULT      0x70
#define REG_GLOBAL_FAULT1   0x71
#define REG_GLOBAL_FAULT2   0x72
#define REG_FAULT       0x78

/* DEVICE_CTRL_2 register values */
#define DCTRL2_MODE_DEEP_SLEEP 0x00
#define DCTRL2_MODE_SLEEP  0x01
#define DCTRL2_MODE_HIZ    0x02
#define DCTRL2_MODE_PLAY   0x03

#define DCTRL2_MUTE        0x08
#define DCTRL2_DIS_DSP     0x10

/* REG_FAULT register values */
#define ANALOG_FAULT_CLEAR 0x80

#define REG_BOOK_4 0x78
#define REG_BOOK_4_LEVEL_METER_PAGE 0x02
#define REG_LEVEL_METER_LEFT 0x60
#define REG_LEVEL_METER_RIGHT 0x64

// Mixer registers
#define REG_BOOK_5 0x8c
#define REG_BOOK_5_MIXER_PAGE 0x29
#define REG_LEFT_TO_LEFT_GAIN 0x18
#define REG_RIGHT_TO_LEFT_GAIN 0x1c
#define REG_LEFT_TO_RIGHT_GAIN 0x20
#define REG_RIGHT_TO_RIGHT_GAIN 0x24

enum {
    NORMAL_MODE = 0,  /* Normal mode (bit 1 = 0) */
    BRIDGE_MODE = 1   /* Bridge mode (bit 1 = 1) */
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