// SPDX-License-Identifier: GPL-2.0
//
// Driver for the TAS5805M Audio Amplifier
//
// Author: Andy Liu <andy-liu@ti.com>
// Author: Daniel Beer <daniel.beer@igorinstitute.com>
//
// This is based on a driver originally written by Andy Liu at TI and
// posted here:
//
//    https://e2e.ti.com/support/audio-group/audio/f/audio-forum/722027/linux-tas5825m-linux-drivers
//
// It has been simplified a little and reworked for the 5.x ALSA SoC API.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tas5805m.h"
#include "eq/tas5805m_eq.h"

#if defined(TAS5805M_DSP_CUSTOM)
    // pick one of the following configurations 
    // you may provide your own configuration exported from the Ti's PurePath Console
    // DSP ALSA controls will be disabled in that case

    // #pragma message("tas5805m_2.0+eq(+9db_20Hz)(-1Db_500Hz)(+3Db_8kHz)(+3Db_15kHz) config is used")
    // #include "startup/custom/tas5805m_2.0+eq(+9db_20Hz)(-1Db_500Hz)(+3Db_8kHz)(+3Db_15kHz).h"
    // #pragma message("tas5805m_2.0+eq(+9db_20Hz)(-3Db_500Hz)(+3Db_8kHz)(+3Db_15kHz) config is used")
    // #include "startup/custom/tas5805m_2.0+eq(+9db_20Hz)(-3Db_500Hz)(+3Db_8kHz)(+3Db_15kHz).h"
    // #pragma message("tas5805m_2.0+eq(+12db_30Hz)(-3Db_500Hz)(+3Db_8kHz)(+3Db_15kHz) config is used")
    // #include "startup/custom/tas5805m_2.0+eq(+12db_30Hz)(-3Db_500Hz)(+3Db_8kHz)(+3Db_15kHz).h"
    #pragma message("tas5805m_2.0+basic config is used")
    #include "startup/tas5805m_2.0+basic.h"
    // #pragma message("tas5805m_1.0+basic config is used")
    // #include "startup/tas5805m_1.0+basic.h"
    // #pragma message("tas5805m_0.1+eq_40Hz_cutoff config is used")
    // #include "startup/tas5805m_0.1+eq_40Hz_cutoff.h"
    // #pragma message("tas5805m_0.1+eq_60Hz_cutoff config is used")
    // #include "startup/tas5805m_0.1+eq_60Hz_cutoff.h"
    // #pragma message("tas5805m_0.1+eq_100Hz_cutoff config is used")
    // #include "startup/tas5805m_0.1+eq_100Hz_cutoff.h"// works: yes // <- purepath (PBTL) subwoofer mode 
    // #pragma message("tas5805m_1.1+eq_60Hz_cutoff+mono config is used")
    // #include "startup/tas5805m_1.1+eq_60Hz_cutoff+mono.h"
    // #pragma message("tas5805m_1.1+eq_60Hz_cutoff+left config is used")
    // #include "startup/tas5805m_1.1+eq_60Hz_cutoff+left.h"
    // #pragma message("tas5805m_1.1+eq_60Hz_cutoff+right config is used") 
    // #include "startup/tas5805m_1.1+eq_60Hz_cutoff+right.h"
#else
    #include "startup/tas5805m_2.0+minimal.h"
#endif

/* This sequence of register writes must always be sent, prior to the
 * 5ms delay while we wait for the DSP to boot.
 */
static const uint8_t tas5805m_dsp_cfg_preboot[] = {
    TAS5805M_REG_PAGE_SET, 0x00, 
    TAS5805M_REG_BOOK_SET, 0x00, 
    0x03, 0x02, 
    0x01, 0x11,
	0x00, 0x00, 
    0x00, 0x00, 
    0x00, 0x00, 
    0x00, 0x00,
    TAS5805M_REG_PAGE_SET, 0x00, 
    TAS5805M_REG_BOOK_SET, 0x00, 
    0x03, 0x02,
};

static const uint32_t tas5805m_volume[] = {
	0x0000001B, /*   0, -110dB */ 0x0000001E, /*   1, -109dB */
	0x00000021, /*   2, -108dB */ 0x00000025, /*   3, -107dB */
	0x0000002A, /*   4, -106dB */ 0x0000002F, /*   5, -105dB */
	0x00000035, /*   6, -104dB */ 0x0000003B, /*   7, -103dB */
	0x00000043, /*   8, -102dB */ 0x0000004B, /*   9, -101dB */
	0x00000054, /*  10, -100dB */ 0x0000005E, /*  11,  -99dB */
	0x0000006A, /*  12,  -98dB */ 0x00000076, /*  13,  -97dB */
	0x00000085, /*  14,  -96dB */ 0x00000095, /*  15,  -95dB */
	0x000000A7, /*  16,  -94dB */ 0x000000BC, /*  17,  -93dB */
	0x000000D3, /*  18,  -92dB */ 0x000000EC, /*  19,  -91dB */
	0x00000109, /*  20,  -90dB */ 0x0000012A, /*  21,  -89dB */
	0x0000014E, /*  22,  -88dB */ 0x00000177, /*  23,  -87dB */
	0x000001A4, /*  24,  -86dB */ 0x000001D8, /*  25,  -85dB */
	0x00000211, /*  26,  -84dB */ 0x00000252, /*  27,  -83dB */
	0x0000029A, /*  28,  -82dB */ 0x000002EC, /*  29,  -81dB */
	0x00000347, /*  30,  -80dB */ 0x000003AD, /*  31,  -79dB */
	0x00000420, /*  32,  -78dB */ 0x000004A1, /*  33,  -77dB */
	0x00000532, /*  34,  -76dB */ 0x000005D4, /*  35,  -75dB */
	0x0000068A, /*  36,  -74dB */ 0x00000756, /*  37,  -73dB */
	0x0000083B, /*  38,  -72dB */ 0x0000093C, /*  39,  -71dB */
	0x00000A5D, /*  40,  -70dB */ 0x00000BA0, /*  41,  -69dB */
	0x00000D0C, /*  42,  -68dB */ 0x00000EA3, /*  43,  -67dB */
	0x0000106C, /*  44,  -66dB */ 0x0000126D, /*  45,  -65dB */
	0x000014AD, /*  46,  -64dB */ 0x00001733, /*  47,  -63dB */
	0x00001A07, /*  48,  -62dB */ 0x00001D34, /*  49,  -61dB */
	0x000020C5, /*  50,  -60dB */ 0x000024C4, /*  51,  -59dB */
	0x00002941, /*  52,  -58dB */ 0x00002E49, /*  53,  -57dB */
	0x000033EF, /*  54,  -56dB */ 0x00003A45, /*  55,  -55dB */
	0x00004161, /*  56,  -54dB */ 0x0000495C, /*  57,  -53dB */
	0x0000524F, /*  58,  -52dB */ 0x00005C5A, /*  59,  -51dB */
	0x0000679F, /*  60,  -50dB */ 0x00007444, /*  61,  -49dB */
	0x00008274, /*  62,  -48dB */ 0x0000925F, /*  63,  -47dB */
	0x0000A43B, /*  64,  -46dB */ 0x0000B845, /*  65,  -45dB */
	0x0000CEC1, /*  66,  -44dB */ 0x0000E7FB, /*  67,  -43dB */
	0x00010449, /*  68,  -42dB */ 0x0001240C, /*  69,  -41dB */
	0x000147AE, /*  70,  -40dB */ 0x00016FAA, /*  71,  -39dB */
	0x00019C86, /*  72,  -38dB */ 0x0001CEDC, /*  73,  -37dB */
	0x00020756, /*  74,  -36dB */ 0x000246B5, /*  75,  -35dB */
	0x00028DCF, /*  76,  -34dB */ 0x0002DD96, /*  77,  -33dB */
	0x00033718, /*  78,  -32dB */ 0x00039B87, /*  79,  -31dB */
	0x00040C37, /*  80,  -30dB */ 0x00048AA7, /*  81,  -29dB */
	0x00051884, /*  82,  -28dB */ 0x0005B7B1, /*  83,  -27dB */
	0x00066A4A, /*  84,  -26dB */ 0x000732AE, /*  85,  -25dB */
	0x00081385, /*  86,  -24dB */ 0x00090FCC, /*  87,  -23dB */
	0x000A2ADB, /*  88,  -22dB */ 0x000B6873, /*  89,  -21dB */
	0x000CCCCD, /*  90,  -20dB */ 0x000E5CA1, /*  91,  -19dB */
	0x00101D3F, /*  92,  -18dB */ 0x0012149A, /*  93,  -17dB */
	0x00144961, /*  94,  -16dB */ 0x0016C311, /*  95,  -15dB */
	0x00198A13, /*  96,  -14dB */ 0x001CA7D7, /*  97,  -13dB */
	0x002026F3, /*  98,  -12dB */ 0x00241347, /*  99,  -11dB */
	0x00287A27, /* 100,  -10dB */ 0x002D6A86, /* 101,  -9dB */
	0x0032F52D, /* 102,   -8dB */ 0x00392CEE, /* 103,   -7dB */
	0x004026E7, /* 104,   -6dB */ 0x0047FACD, /* 105,   -5dB */
	0x0050C336, /* 106,   -4dB */ 0x005A9DF8, /* 107,   -3dB */
	0x0065AC8C, /* 108,   -2dB */ 0x00721483, /* 109,   -1dB */
	0x00800000, /* 110,    0dB */ 0x008F9E4D, /* 111,    1dB */
	0x00A12478, /* 112,    2dB */ 0x00B4CE08, /* 113,    3dB */
	0x00CADDC8, /* 114,    4dB */ 0x00E39EA9, /* 115,    5dB */
	0x00FF64C1, /* 116,    6dB */ 0x011E8E6A, /* 117,    7dB */
	0x0141857F, /* 118,    8dB */ 0x0168C0C6, /* 119,    9dB */
	0x0194C584, /* 120,   10dB */ 0x01C62940, /* 121,   11dB */
	0x01FD93C2, /* 122,   12dB */ 0x023BC148, /* 123,   13dB */
	0x02818508, /* 124,   14dB */ 0x02CFCC01, /* 125,   15dB */
	0x0327A01A, /* 126,   16dB */ 0x038A2BAD, /* 127,   17dB */
	0x03F8BD7A, /* 128,   18dB */ 0x0474CD1B, /* 129,   19dB */
	0x05000000, /* 130,   20dB */ 0x059C2F02, /* 131,   21dB */
	0x064B6CAE, /* 132,   22dB */ 0x07100C4D, /* 133,   23dB */
	0x07ECA9CD, /* 134,   24dB */ 0x08E43299, /* 135,   25dB */
	0x09F9EF8E, /* 136,   26dB */ 0x0B319025, /* 137,   27dB */
	0x0C8F36F2, /* 138,   28dB */ 0x0E1787B8, /* 139,   29dB */
	0x0FCFB725, /* 140,   30dB */ 0x11BD9C84, /* 141,   31dB */
	0x13E7C594, /* 142,   32dB */ 0x16558CCB, /* 143,   33dB */
	0x190F3254, /* 144,   34dB */ 0x1C1DF80E, /* 145,   35dB */
	0x1F8C4107, /* 146,   36dB */ 0x2365B4BF, /* 147,   37dB */
	0x27B766C2, /* 148,   38dB */ 0x2C900313, /* 149,   39dB */
	0x32000000, /* 150,   40dB */ 0x3819D612, /* 151,   41dB */
	0x3EF23ECA, /* 152,   42dB */ 0x46A07B07, /* 153,   43dB */
	0x4F3EA203, /* 154,   44dB */ 0x58E9F9F9, /* 155,   45dB */
	0x63C35B8E, /* 156,   46dB */ 0x6FEFA16D, /* 157,   47dB */
	0x7D982575, /* 158,   48dB */
};

#define TAS5805M_VOLUME_MAX	((int)ARRAY_SIZE(tas5805m_volume) - 1)
#define TAS5805M_VOLUME_MIN	0

#define SET_BOOK_AND_PAGE(rm, BOOK, PAGE) \
    do { \
        regmap_write(rm, TAS5805M_REG_PAGE_SET, TAS5805M_REG_PAGE_ZERO); \
        /* printk(KERN_DEBUG "@page: %#x\n", TAS5805M_REG_PAGE_ZERO); */ \
        regmap_write(rm, TAS5805M_REG_BOOK_SET, BOOK);                   \
        /* printk(KERN_DEBUG "@book: %#x\n", BOOK);                   */ \
        regmap_write(rm, TAS5805M_REG_PAGE_SET, PAGE);                   \
        /* printk(KERN_DEBUG "@page: %#x\n", PAGE);                   */ \
    } while (0)

struct tas5805m_priv {
	struct i2c_client		*i2c;
	struct regulator		*pvdd;
	struct gpio_desc		*gpio_pdn_n;

	uint8_t				    *dsp_cfg_data;
	int				         dsp_cfg_len;

	struct regmap			*regmap;

	int				         vol[2];
	bool				     is_powered;
	bool				     is_muted;
    bool                     is_started;

	struct work_struct		 work;
	struct mutex			 lock;
};

/**
 * Convert a dB value into a 4-byte buffer in "9.23" fixed-point format.
 * @param db_value Integer dB value to convert.
 * @param buffer 4-byte buffer to store the result.
 */
static void map_db_to_9_23(int db_value, uint8_t buffer[4]) {
    // Reference value for 0 dB in 9.23 format
    const uint32_t reference = 0x00800000; // 1.0 in 9.23 format
    uint32_t value = reference; // Start with the 0 dB reference

    if (db_value > 0) {
        // Positive dB: Scale up the value
        while (db_value >= 6) {
            value <<= 1; // Multiply by 2
            db_value -= 6;
        }
    } else if (db_value < 0) {
        // Negative dB: Scale down the value
        while (db_value <= -6) {
            value >>= 1; // Divide by 2
            db_value += 6;
        }
    }

    // Handle fractional dB values (fine-tuning)
    if (db_value != 0) {
        // Approximation for fractional scaling (using linear interpolation)
        // For simplicity: 6 dB corresponds to a factor of 2, so smaller steps
        // scale proportionally using integer math.
        if (db_value > 0) {
            value += (value >> 1) * db_value / 6; // Scale up
        } else {
            value -= (value >> 1) * (-db_value) / 6; // Scale down
        }
    }

    // Write the 32-bit value into the buffer
    buffer[0] = (value >> 24) & 0xFF;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
}

static void set_dsp_scale(struct regmap *rm, int offset, int vol)
{
	uint8_t v[4];
	uint32_t x = tas5805m_volume[vol];
	int i;

	for (i = 0; i < 4; i++) {
		v[3 - i] = x;
		x >>= 8;
	}

	regmap_bulk_write(rm, offset, v, ARRAY_SIZE(v));
}

static void tas5805m_refresh(struct tas5805m_priv *tas5805m)
{
	struct regmap *rm = tas5805m->regmap;

	dev_dbg(&tas5805m->i2c->dev, "refresh: is_muted=%d, vol=%d/%d\n",
		tas5805m->is_muted, tas5805m->vol[0], tas5805m->vol[1]);

	regmap_write(rm, TAS5805M_REG_PAGE_SET, 0x00);
	regmap_write(rm, TAS5805M_REG_BOOK_SET, 0x8c);
	regmap_write(rm, TAS5805M_REG_PAGE_SET, 0x2a);

	/* Refresh volume. The actual volume control documented in the
	 * datasheet doesn't seem to work correctly. This is a pair of
	 * DSP registers which are *not* documented in the datasheet.
	 */
	set_dsp_scale(rm, 0x24, tas5805m->vol[0]);
	set_dsp_scale(rm, 0x28, tas5805m->vol[1]);

	regmap_write(rm, TAS5805M_REG_PAGE_SET, 0x00);
	regmap_write(rm, TAS5805M_REG_BOOK_SET, 0x00);

	/* Set/clear digital soft-mute */
	regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2,
		(tas5805m->is_muted ? TAS5805M_DCTRL2_MUTE : 0) |
		TAS5805M_DCTRL2_MODE_PLAY);
}

static int tas5805m_vol_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;

	uinfo->value.integer.min = TAS5805M_VOLUME_MIN;
	uinfo->value.integer.max = TAS5805M_VOLUME_MAX;
	return 0;
}

static int tas5805m_vol_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&tas5805m->lock);
	ucontrol->value.integer.value[0] = tas5805m->vol[0];
	ucontrol->value.integer.value[1] = tas5805m->vol[1];
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static inline int volume_is_valid(int v)
{
	return (v >= TAS5805M_VOLUME_MIN) && (v <= TAS5805M_VOLUME_MAX);
}

static int tas5805m_vol_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	int ret = 0;

	if (!(volume_is_valid(ucontrol->value.integer.value[0]) &&
	      volume_is_valid(ucontrol->value.integer.value[1])))
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (tas5805m->vol[0] != ucontrol->value.integer.value[0] ||
	    tas5805m->vol[1] != ucontrol->value.integer.value[1]) {
		tas5805m->vol[0] = ucontrol->value.integer.value[0];
		tas5805m->vol[1] = ucontrol->value.integer.value[1];
		dev_dbg(component->dev, "set vol=%d/%d (is_powered=%d)\n",
			tas5805m->vol[0], tas5805m->vol[1],
			tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

static const struct soc_enum dac_mode_enum = SOC_ENUM_SINGLE(
    TAS5805M_REG_DEVICE_CTRL_1,   /* Register address where the control resides */
    2,                   /* Bit shift (bit 1 corresponds to the second bit) */
    2,                   /* Number of items (2 possible modes: Normal, Bridge) */
    dac_mode_text        /* Array of text values */
);

static const struct soc_enum eq_mode_enum = SOC_ENUM_SINGLE(
    TAS5805M_REG_DSP_MISC,   /* Register address where the control resides */
    0,                       /* Bit shift (bit 1 corresponds to the second bit) */
    2,                       /* Number of items (2 possible modes: Off, On) */
    eq_mode_text            /* Array of text values */
);


static const struct soc_enum dac_modulation_mode_enum = SOC_ENUM_SINGLE(
    TAS5805M_REG_DEVICE_CTRL_1,   /* Register address where the control resides */
    0,                   /* Bit shift (bit 1 corresponds to the second bit) */
    3,                   /* Number of items (2 possible modes: Normal, Bridge) */
    modulation_mode_text /* Array of text values */
);

static const struct soc_enum dac_switch_freq_enum = SOC_ENUM_SINGLE(
    TAS5805M_REG_DEVICE_CTRL_1,   /* Register address where the control resides */
    4,                   /* Bit shift (bit 1 corresponds to the second bit) */
    4,                   /* Number of items (4 possible modes: Normal, Bridge) */
    switch_freq_text     /* Array of text values */
);
    
// Macro to define ALSA controls for meters
#define MIXER_CONTROL_DECL(ix, alias, reg, control_name)                  \
static int alias##_get(struct snd_kcontrol *kcontrol,                     \
                      struct snd_ctl_elem_value *ucontrol) {              \
    ucontrol->value.integer.value[0] = kcontrol->private_value;           \
    /* printk(KERN_DEBUG "tas5805m: MXR get %d %d\n", ix, (int)(kcontrol->private_value)); */ \
    return 0;                                                             \
}                                                                         \
                                                                          \
static int alias##_set(struct snd_kcontrol *kcontrol,                     \
                      struct snd_ctl_elem_value *ucontrol) {              \
    struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);    \
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component); \
    if (!tas5805m->is_started) {                                           \
        printk(KERN_ERR "tas5805m: Can't set mixer control until DSP has started \n"); \
        return -EBUSY;                                                    \
    }                                                                     \
    struct regmap *rm = tas5805m->regmap;                                 \
    int value = ucontrol->value.integer.value[0];                         \
    printk(KERN_DEBUG "tas5805m: MXR set %d %d\n", ix, value);            \
                                                                          \
    if (value < TAS5805M_MIXER_MIN_DB || value > TAS5805M_MIXER_MAX_DB)   \
        return -EINVAL;                                                   \
                                                                          \
    if (kcontrol->private_value != value) {                               \
        kcontrol->private_value = value;                                  \
                                                                          \
        int ret;                                                          \
        u8 buf[4] = {0};                                                  \
        map_db_to_9_23(value, buf);                                       \
        SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_5, TAS5805M_REG_BOOK_5_MIXER_PAGE); \
        ret = regmap_bulk_write(rm, reg, buf, 4);                         \
        /* printk(KERN_DEBUG "write register %#x: %#x %#x %#x %#x\n", reg, buf[0], buf[1], buf[2], buf[3]); */ \
        if (ret != 0) {                                                   \
            printk(KERN_ERR "tas5805m: Failed to write register %d: %d\n", reg, ret); \
            return ret;                                                   \
        }                                                                 \
        /* printk(KERN_DEBUG "\t which is %d", value);    */              \
        SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_ZERO); \
        return 1;                                                         \
    }                                                                     \
    return 0;                                                             \
}                                                                         \
                                                                          \
static int alias##_info                                                   \
    (struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)      \
{                                                                         \
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;                            \
    uinfo->count = 1;                                                     \
    uinfo->value.integer.min = TAS5805M_MIXER_MIN_DB;                     \
    uinfo->value.integer.max = TAS5805M_MIXER_MAX_DB;                     \
    return 0;                                                             \
}                                                                         \
                                                                          \
static const struct snd_kcontrol_new alias##_control = {                  \
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,                                  \
    .name = control_name,                                                 \
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,                            \
    .info = alias##_info,                                                 \
    .get = alias##_get,                                                   \
    .put = alias##_set,                                                   \
    .private_value = 0                                                    \
};                                                                        \

// Mixer controls
MIXER_CONTROL_DECL(0, left_to_left_mixer, TAS5805M_REG_LEFT_TO_LEFT_GAIN, "L>L Mixer Gain");
MIXER_CONTROL_DECL(1, right_to_left_mixer, TAS5805M_REG_RIGHT_TO_LEFT_GAIN, "R>L Mixer Gain");
MIXER_CONTROL_DECL(2, left_to_right_mixer, TAS5805M_REG_LEFT_TO_RIGHT_GAIN, "L>R Mixer Gain");
MIXER_CONTROL_DECL(3, right_to_right_mixer, TAS5805M_REG_RIGHT_TO_RIGHT_GAIN, "R>R Mixer Gain");

// Macro to define ALSA controls for EQ bands
#define DEFINE_EQ_BAND_CONTROL(ix, freq) \
static int eq_band_##freq##_get(struct snd_kcontrol *kcontrol,              \
                      struct snd_ctl_elem_value *ucontrol) {                \
    ucontrol->value.integer.value[0] = kcontrol->private_value;             \
    /* printk(KERN_DEBUG "tas5805m: EQ get %d %d\n", ix, (int)(kcontrol->private_value)); */ \
    return 0;                                                               \
}                                                                           \
                                                                            \
static int eq_band_##freq##_set(struct snd_kcontrol *kcontrol,              \
                      struct snd_ctl_elem_value *ucontrol) {                \
    struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);      \
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component); \
    struct regmap *rm = tas5805m->regmap;                                   \
    if (!tas5805m->is_started) {                                            \
        printk(KERN_ERR "tas5805m: Can't set EQ control until DSP has started \n"); \
        return -EBUSY;                                                      \
    }                                                                        \
    int freq_index = ix;                                                    \
    int value = ucontrol->value.integer.value[0];                           \
    int current_page = 0;                                                   \
    printk(KERN_DEBUG "tas5805m: EQ set %d %d\n", ix, value);               \
                                                                            \
    if (value < TAS5805M_EQ_MIN_DB || value > TAS5805M_EQ_MAX_DB)           \
        return -EINVAL;                                                     \
                                                                            \
    if (kcontrol->private_value != value) {                                 \
        kcontrol->private_value = value;                                    \
                                                                            \
        int x = value + TAS5805M_EQ_MAX_DB;                                 \
        int y = freq_index * TAS5805M_EQ_KOEF_PER_BAND * TAS5805M_EQ_REG_PER_KOEF; \
                                                                            \
        for (int i = 0; i < TAS5805M_EQ_KOEF_PER_BAND * TAS5805M_EQ_REG_PER_KOEF; i++) { \
            const reg_sequence_eq *reg_value = &tas5805m_eq_registers[x][y + i]; \
            if (reg_value == NULL) {                                        \
                printk(KERN_ERR "tas5805m: NULL pointer encountered at row[%d]\n", y + i); \
                continue;                                                   \
            }                                                               \
                                                                            \
            if (reg_value->page != current_page) {                          \
                current_page = reg_value->page;                             \
                SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_EQ, reg_value->page); \
            }                                                               \
                                                                            \
            /* printk(KERN_DEBUG "+ %d: w 0x%x 0x%x\n", i, reg_value->offset, reg_value->value); */ \
            regmap_write(rm, reg_value->offset, reg_value->value);          \
        }                                                                   \
                                                                            \
        SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_ZERO); \
        return 1;                                                           \
    }                                                                       \
    return 0;                                                               \
}                                                                           \
                                                                            \
static int eq_band_##freq##_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo) { \
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;                              \
    uinfo->count = 1;                                                       \
    uinfo->value.integer.min = TAS5805M_EQ_MIN_DB;                          \
    uinfo->value.integer.max = TAS5805M_EQ_MAX_DB;                          \
    return 0;                                                               \
}                                                                           \
                                                                            \
static const struct snd_kcontrol_new eq_band_##freq##_control = {           \
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,                                    \
    .name = #freq " Hz",                                                    \
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,                              \
    .info = eq_band_##freq##_info,                                          \
    .get = eq_band_##freq##_get,                                            \
    .put = eq_band_##freq##_set,                                            \
    .private_value = 0                                                      \
};

// EQ controls
DEFINE_EQ_BAND_CONTROL(0,  00020)
DEFINE_EQ_BAND_CONTROL(1,  00032)
DEFINE_EQ_BAND_CONTROL(2,  00050)
DEFINE_EQ_BAND_CONTROL(3,  00080)
DEFINE_EQ_BAND_CONTROL(4,  00125)
DEFINE_EQ_BAND_CONTROL(5,  00200)
DEFINE_EQ_BAND_CONTROL(6,  00315)
DEFINE_EQ_BAND_CONTROL(7,  00500)
DEFINE_EQ_BAND_CONTROL(8,  00800)
DEFINE_EQ_BAND_CONTROL(9,  01250)
DEFINE_EQ_BAND_CONTROL(10, 02000)
DEFINE_EQ_BAND_CONTROL(11, 03150)
DEFINE_EQ_BAND_CONTROL(12, 05000)
DEFINE_EQ_BAND_CONTROL(13, 08000)
DEFINE_EQ_BAND_CONTROL(14, 16000)

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_vol_tlv, (-10350), 50, 1);

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_again_tlv, (-1550), 50, 1);

static const struct snd_kcontrol_new tas5805m_snd_controls[] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Volume Digital",
		.access	= SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			  SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= tas5805m_vol_info,
		.get	= tas5805m_vol_get,
		.put	= tas5805m_vol_put,
	},
 
    SOC_SINGLE_TLV ("Volume Analog", TAS5805M_REG_ANALOG_GAIN, 0, 31, 1, tas5805m_again_tlv),

    SOC_ENUM("Driver Modulation Scheme", dac_modulation_mode_enum),
    SOC_ENUM("Driver Switching freq", dac_switch_freq_enum),

#if !defined(TAS5805M_DSP_CUSTOM)

    SOC_ENUM("Driver Bridge Mode", dac_mode_enum),
    SOC_ENUM("Equalizer", eq_mode_enum),
 
    // channel mixer controls
    left_to_left_mixer_control,
    right_to_left_mixer_control,
    left_to_right_mixer_control,
    right_to_right_mixer_control,

    eq_band_00020_control,
    eq_band_00032_control,
    eq_band_00050_control,
    eq_band_00080_control,
    eq_band_00125_control,
    eq_band_00200_control,
    eq_band_00315_control,
    eq_band_00500_control,
    eq_band_00800_control,
    eq_band_01250_control,
    eq_band_02000_control,
    eq_band_03150_control,
    eq_band_05000_control,
    eq_band_08000_control,
    eq_band_16000_control,

#endif
};

static void tas5805m_send_cfg(struct regmap *rm,
		     const uint8_t *s, unsigned int len)
{
	unsigned int i;
    int ret;

    printk(KERN_DEBUG "tas5805m_send_cfg: Sending configuration to the device\n");

	for (i = 0; i + 1 < len; i += 2) {
		ret = regmap_write(rm, s[i], s[i + 1]);
        if (ret)
            printk(KERN_ERR "tas5805m_send_cfg: regmap_write failed for %#02x: %d\n", s[i], ret);
    }

    printk(KERN_DEBUG "tas5805m_send_cfg: %d registers sent\n", len / 2); 
}

/* The TAS5805M DSP can't be configured until the I2S clock has been
 * present and stable for 5ms, or else it won't boot and we get no
 * sound.
 */
static int tas5805m_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(component->dev, "DAC clock start\n");
		schedule_work(&tas5805m->work);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void tas5805m_do_work(struct work_struct *work)
{
	struct tas5805m_priv *tas5805m =
	       container_of(work, struct tas5805m_priv, work);
	struct regmap *rm = tas5805m->regmap;

	dev_dbg(&tas5805m->i2c->dev, "DSP startup\n");

	mutex_lock(&tas5805m->lock);
	/* We mustn't issue any I2C transactions until the I2S
	 * clock is stable. Furthermore, we must allow a 5ms
	 * delay after the first set of register writes to
	 * allow the DSP to boot before configuring it.
	 */
	usleep_range(5000, 10000);
	tas5805m_send_cfg(rm, tas5805m_dsp_cfg_preboot, ARRAY_SIZE(tas5805m_dsp_cfg_preboot));
	usleep_range(5000, 15000);
	tas5805m_send_cfg(rm, tas5805m->dsp_cfg_data, tas5805m->dsp_cfg_len);
    tas5805m->is_started = true;

	tas5805m->is_powered = true;
	tas5805m_refresh(tas5805m);
	mutex_unlock(&tas5805m->lock);
}

static void tas5805m_check_faults(struct snd_soc_component *component)
{
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
    struct regmap *rm = tas5805m->regmap;
    int ret;
    unsigned int chan = 0, global1 = 0, global2 = 0;

    SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_ZERO);

    ret = regmap_read(rm, TAS5805M_REG_CHAN_FAULT, &chan);
    if (chan) {
        if (chan & (1 << 0))  
            printk(KERN_WARNING "tas5805m: Right channel over current fault");

        if (chan & (1 << 1))
            printk(KERN_WARNING "tas5805m: Left channel over current fault");

        if (chan & (1 << 2)) 
            printk(KERN_WARNING "tas5805m: Right channel DC fault");

        if (chan & (1 << 3))  
            printk(KERN_WARNING "tas5805m: Left channel DC fault");
    }

    ret = regmap_read(rm, TAS5805M_REG_GLOBAL_FAULT1, &global1);
    if (global1) {
        if (global1 & (1 << 0))  
            printk(KERN_WARNING "tas5805m: PVDD UV fault");

        if (global1 & (1 << 1))
            printk(KERN_WARNING "tas5805m: PVDD OV fault");

        if (global1 & (1 << 2)) 
            printk(KERN_DEBUG "tas5805m: Clock fault");

        if (global1 & (1 << 6))  
            printk(KERN_WARNING "tas5805m: The recent BQ is written failed");

        if (global1 & (1 << 7))  
            printk(KERN_WARNING "tas5805m: Indicate OTP CRC check error");

    }

    ret = regmap_read(rm, TAS5805M_REG_GLOBAL_FAULT2, &global2);
    if (global2) {
        if (global2 & (1 << 0))  
            printk(KERN_WARNING "tas5805m: Over temperature shut down fault");
    }

    ret = regmap_write(rm, TAS5805M_REG_FAULT, TAS5805M_ANALOG_FAULT_CLEAR);    // Is necessary for compatibility with TAS5828m

}

static int tas5805m_dac_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	struct regmap *rm = tas5805m->regmap;

	if (event & SND_SOC_DAPM_PRE_PMD) {
		dev_dbg(component->dev, "DSP shutdown\n");
		cancel_work_sync(&tas5805m->work);

		mutex_lock(&tas5805m->lock);
		if (tas5805m->is_powered) {
			tas5805m->is_powered = false;

			tas5805m_check_faults(component);

			regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MODE_HIZ);
		}
		mutex_unlock(&tas5805m->lock);
	}

	return 0;
}

static const struct snd_soc_dapm_route tas5805m_audio_map[] = {
	{ "DAC", NULL, "DAC IN" },
	{ "OUT", NULL, "DAC" },
};

static const struct snd_soc_dapm_widget tas5805m_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
		tas5805m_dac_event, SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_component_driver soc_codec_dev_tas5805m = {
	.controls		    = tas5805m_snd_controls,
	.num_controls		= ARRAY_SIZE(tas5805m_snd_controls),
	.dapm_widgets		= tas5805m_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas5805m_dapm_widgets),
	.dapm_routes		= tas5805m_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas5805m_audio_map),
	.use_pmdown_time	= 1,
	.endianness		    = 1,
};

static int tas5805m_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&tas5805m->lock);
	dev_dbg(component->dev, "set mute=%d (is_powered=%d)\n",
		mute, tas5805m->is_powered);

	tas5805m->is_muted = mute;
	if (tas5805m->is_powered)
		tas5805m_refresh(tas5805m);
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static const struct snd_soc_dai_ops tas5805m_dai_ops = {
	.trigger		= tas5805m_trigger,
	.mute_stream		= tas5805m_mute,
	.no_capture_mute	= 1,
};

static struct snd_soc_dai_driver tas5805m_dai = {
	.name		= "tas5805m-amplifier",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_48000,
		.formats	= SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops		= &tas5805m_dai_ops,
};

static const struct regmap_config tas5805m_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,

	/* We have quite a lot of multi-level bank switching and a
	 * relatively small number of register writes between bank
	 * switches.
	 */
	.cache_type	= REGCACHE_NONE,
};

static int tas5805m_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regmap *regmap;
	struct tas5805m_priv *tas5805m;
	char filename[128];
	const char *config_name;
	const struct firmware *fw;
	int ret;

    printk(KERN_DEBUG "tas5805m_i2c_probe: Probing the I2C device\n");

	regmap = devm_regmap_init_i2c(i2c, &tas5805m_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "unable to allocate register map: %d\n", ret);
		return ret;
	}

	tas5805m = devm_kzalloc(dev, sizeof(struct tas5805m_priv), GFP_KERNEL);
	if (!tas5805m)
		return -ENOMEM;

	tas5805m->i2c = i2c;
	tas5805m->pvdd = devm_regulator_get(dev, "pvdd");
	if (IS_ERR(tas5805m->pvdd)) {
		dev_err(dev, "failed to get pvdd supply: %ld\n",
			PTR_ERR(tas5805m->pvdd));
		return PTR_ERR(tas5805m->pvdd);
	}

	dev_set_drvdata(dev, tas5805m);
	tas5805m->regmap = regmap;
	tas5805m->gpio_pdn_n = devm_gpiod_get(dev, "pdn", GPIOD_OUT_LOW);
	if (IS_ERR(tas5805m->gpio_pdn_n)) {
		dev_err(dev, "error requesting PDN gpio: %ld\n",
			PTR_ERR(tas5805m->gpio_pdn_n));
		return PTR_ERR(tas5805m->gpio_pdn_n);
	}

	/* This configuration must be generated by PPC3. The file loaded
	 * consists of a sequence of register writes, where bytes at
	 * even indices are register addresses and those at odd indices
	 * are register values.
	 *
	 * The fixed portion of PPC3's output prior to the 5ms delay
	 * should be omitted.
	 */
	if (!device_property_read_string(dev, "ti,dsp-config-name", &config_name)) 
    {
        printk(KERN_WARNING "dsp-config-name is not set, using default config\n");
    
        size_t tas5805m_init_sequence_len = sizeof(tas5805m_init_sequence) / sizeof(tas5805m_init_sequence[0]);
        tas5805m->dsp_cfg_len = tas5805m_init_sequence_len * 2;
        tas5805m->dsp_cfg_data = devm_kmalloc(dev, tas5805m->dsp_cfg_len, GFP_KERNEL);
        if (!tas5805m->dsp_cfg_data) {
            printk(KERN_ERR "tas5805m_i2c_probe: firmware is not loaded, using default config\n");
        } else {
        
            for (size_t i = 0; i < tas5805m_init_sequence_len; i++) {
                tas5805m->dsp_cfg_data[2 * i] = tas5805m_init_sequence[i].reg;
                tas5805m->dsp_cfg_data[2 * i + 1] = tas5805m_init_sequence[i].def; 
            }

            printk(KERN_INFO "tas5805m_i2c_probe: Loaded %d register values\n", tas5805m->dsp_cfg_len / 2);
        }
    } else {
        snprintf(filename, sizeof(filename), "tas5805m_dsp_%s.bin",
            config_name);
        ret = request_firmware(&fw, filename, dev);
        if (ret)
            return ret;

        if ((fw->size < 2) || (fw->size & 1)) {
            dev_err(dev, "firmware is invalid\n");
            release_firmware(fw);
            return -EINVAL;
        }

        tas5805m->dsp_cfg_len = fw->size;
        tas5805m->dsp_cfg_data = devm_kmemdup(dev, fw->data, fw->size, GFP_KERNEL);
        if (!tas5805m->dsp_cfg_data) {
            release_firmware(fw);
            return -ENOMEM;
        }

        release_firmware(fw);
    }

	/* Do the first part of the power-on here, while we can expect
	 * the I2S interface to be quiet. We must raise PDN# and then
	 * wait 5ms before any I2S clock is sent, or else the internal
	 * regulator apparently won't come on.
	 *
	 * Also, we must keep the device in power down for 100ms or so
	 * after PVDD is applied, or else the ADR pin is sampled
	 * incorrectly and the device comes up with an unpredictable I2C
	 * address.
	 */
	tas5805m->vol[0] = TAS5805M_VOLUME_MIN;
	tas5805m->vol[1] = TAS5805M_VOLUME_MIN;

	ret = regulator_enable(tas5805m->pvdd);
	if (ret < 0) {
		dev_err(dev, "failed to enable pvdd: %d\n", ret);
		return ret;
	}

	usleep_range(100000, 150000);
	gpiod_set_value(tas5805m->gpio_pdn_n, 1);
	usleep_range(10000, 15000);

	INIT_WORK(&tas5805m->work, tas5805m_do_work);
	mutex_init(&tas5805m->lock);

	/* Don't register through devm. We need to be able to unregister
	 * the component prior to deasserting PDN#
	 */
	ret = snd_soc_register_component(dev, &soc_codec_dev_tas5805m,
					 &tas5805m_dai, 1);
	if (ret < 0) {
		dev_err(dev, "unable to register codec: %d\n", ret);
		gpiod_set_value(tas5805m->gpio_pdn_n, 0);
		regulator_disable(tas5805m->pvdd);
		return ret;
	}

	return 0;
}

static void tas5805m_i2c_remove(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct tas5805m_priv *tas5805m = dev_get_drvdata(dev);

	cancel_work_sync(&tas5805m->work);
	snd_soc_unregister_component(dev);
	gpiod_set_value(tas5805m->gpio_pdn_n, 0);
	usleep_range(10000, 15000);
	regulator_disable(tas5805m->pvdd);
}

static const struct i2c_device_id tas5805m_i2c_id[] = {
	{ "tas5805m", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5805m_i2c_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5805m_of_match[] = {
	{ .compatible = "ti,tas5805m", },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5805m_of_match);
#endif

static struct i2c_driver tas5805m_i2c_driver = {
	.probe		= tas5805m_i2c_probe,
	.remove		= tas5805m_i2c_remove,
	.id_table	= tas5805m_i2c_id,
	.driver		= {
		.name		= "tas5805m",
		.of_match_table = of_match_ptr(tas5805m_of_match),
	},
};

module_i2c_driver(tas5805m_i2c_driver);

MODULE_AUTHOR("Andriy Malyshenko <andriy@sonocotta.com>");
MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_AUTHOR("Daniel Beer <daniel.beer@igorinstitute.com>");
MODULE_DESCRIPTION("TAS5805M Audio Amplifier Driver");
MODULE_LICENSE("GPL v2");
