// SPDX-License-Identifier: GPL-2.0
//
// Driver for the TAS5805M Audio Amplifier
//
// Author: Andy Liu <andy-liu@ti.com>
// Author: Daniel Beer <daniel.beer@igorinstitute.com>
// Author: J.P. van Coolwijk <jpvc36@gmail.com>
//
// This is based on a driver originally written by Andy Liu at TI and
// posted here:
//
//    https://e2e.ti.com/support/audio-group/audio/f/audio-forum/722027/linux-tas5825m-linux-drivers
//
// It has been simplified a little and reworked for the 5.x ALSA SoC API.
// Silicon available to me has a functional TAS5805M_REG_VOL_CTL (0x4c), so the original dsp workaround is not necessary.
// This driver works with a binary firmware file. When missing or invalid a minimal config for PVDD=24V is used.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/version.h>
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

#include "eq/tas5805m_eq.h"

static void map_db_to_9_23(int db_value, u8 buffer[4]);

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

#define IS_KERNEL_MAJOR_BELOW_5 (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
#define IS_KERNEL_BELOW_6_2 (LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0))

#define TAS5805M_RATES      (SNDRV_PCM_RATE_8000_96000)
#define TAS5805M_FORMATS    (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
                		     SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

/* This sequence of register writes must always be sent, prior to the
 * 5ms delay while we wait for the DSP to boot. 
 * Sending 0x11 to DCTL2 resets the volume setting, so we send 0x01.
 */
static const u8 dsp_cfg_preboot[] = {
    TAS5805M_REG_PAGE_SET, 0x00, 
    TAS5805M_REG_BOOK_SET, 0x00, 
    0x03, 0x02, 
    0x01, 0x10,   // reset DSP & write TAS5805M_DCTRL2_MODE_HIZ to DCTL2, keeps TLDV setting
    0x00, 0x00, 
    0x00, 0x00, 
    0x00, 0x00, 
    0x00, 0x00,
    TAS5805M_REG_PAGE_SET, 0x00, 
    TAS5805M_REG_BOOK_SET, 0x00, 
    0x03, 0x02,
};

struct tas5805m_priv {
    struct regulator        *pvdd;
    struct gpio_desc        *gpio_pdn_n;

    u8                      *dsp_cfg_data;
    int                      dsp_cfg_len;

    struct regmap           *regmap;

    bool                     is_started; 

	struct workqueue_struct *my_wq;
	struct work_struct work;
	struct snd_soc_component *component;
    int trigger_cmd;
};

#define SET_BOOK_AND_PAGE(rm, BOOK, PAGE) \
    do { \
        regmap_write(rm, TAS5805M_REG_PAGE_SET, TAS5805M_REG_PAGE_ZERO); \
        /* printk(KERN_DEBUG "@page: %#x\n", TAS5805M_REG_PAGE_ZERO); */ \
        regmap_write(rm, TAS5805M_REG_BOOK_SET, BOOK);                   \
        /* printk(KERN_DEBUG "@book: %#x\n", BOOK);                   */ \
        regmap_write(rm, TAS5805M_REG_PAGE_SET, PAGE);                   \
        /* printk(KERN_DEBUG "@page: %#x\n", PAGE);                   */ \
    } while (0)

static void send_cfg(struct regmap *rm,
             const u8 *s, unsigned int len)
{
    unsigned int i;
    int ret;

    printk(KERN_DEBUG "send_cfg: Sending configuration to the device\n");

    for (i = 0; i + 1 < len; i += 2) {
        // printk(KERN_DEBUG "\tregmap_write: %#02x %#02x", s[i], s[i + 1]);
        ret = regmap_write(rm, s[i], s[i + 1]);
        if (ret)
            printk(KERN_ERR "send_cfg: regmap_write failed for %#02x: %d\n", s[i], ret);
    }

    printk(KERN_DEBUG "send_cfg: %d registers sent\n", len / 2); 
}

static void tas5805m_startup(struct snd_soc_component *component)
{
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
    struct regmap *rm = tas5805m->regmap;
    int ret;

    usleep_range(5000, 10000);
    printk(KERN_DEBUG "tas5805m_startup: sending preboot register settings\n");
    send_cfg(rm, dsp_cfg_preboot, ARRAY_SIZE(dsp_cfg_preboot));
    usleep_range(5000, 15000);

    if (tas5805m->dsp_cfg_data) {
        send_cfg(rm, tas5805m->dsp_cfg_data, tas5805m->dsp_cfg_len);
    } else {
        ret = regmap_register_patch(rm, tas5805m_init_sequence, ARRAY_SIZE(tas5805m_init_sequence));
        if (ret != 0)
        {
            printk(KERN_ERR "tas5805m_startup: failed to initialize TAS5805M: %d\n",ret);
            return;
        } else {
            printk(KERN_DEBUG "tas5805m_startup: sent %d register settings\n", ARRAY_SIZE(tas5805m_init_sequence));
        }
    }

    tas5805m->is_started = true;
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

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_vol_tlv, -10350, 50, 1);    // New (name, min, step, mute)
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_again_tlv, -1550, 50, 1);    // New (name, min, step, mute)

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
    2,                   /* Number of items (2 possible modes: Normal, Bridge) */
    switch_freq_text     /* Array of text values */
);
    
// Macro to define ALSA controls for meters
#define MIXER_CONTROL_DECL(ix, alias, reg, control_name)                  \
static int alias##_get(struct snd_kcontrol *kcontrol,                     \
                      struct snd_ctl_elem_value *ucontrol) {              \
    ucontrol->value.integer.value[0] = kcontrol->private_value;           \
    printk(KERN_DEBUG "tas5805m: MXR get %d %d\n", ix, (int)(kcontrol->private_value)); \
    return 0;                                                             \
}                                                                         \
                                                                          \
static int alias##_set(struct snd_kcontrol *kcontrol,                     \
                      struct snd_ctl_elem_value *ucontrol) {              \
    struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);    \
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component); \
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
    printk(KERN_DEBUG "tas5805m: EQ get %d %d\n", ix, (int)(kcontrol->private_value)); \
    return 0;                                                               \
}                                                                           \
                                                                            \
static int eq_band_##freq##_set(struct snd_kcontrol *kcontrol,              \
                      struct snd_ctl_elem_value *ucontrol) {                \
    struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);      \
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component); \
    struct regmap *rm = tas5805m->regmap;                                   \
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

static const struct snd_kcontrol_new tas5805m_snd_controls[] = 
{        
    SOC_SINGLE_TLV ("Volume Digital", TAS5805M_REG_VOL_CTL, 0, 255, 1, tas5805m_vol_tlv), // (xname, reg, shift, max, invert, tlv_array)
    SOC_SINGLE_TLV ("Volume Analog", TAS5805M_REG_ANALOG_GAIN, 0, 31, 1, tas5805m_again_tlv), // (xname, reg, shift, max, invert, tlv_array)

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

static void tas5805m_work_handler(struct work_struct *work) 
{
    struct tas5805m_priv *tas5805m = container_of(work, struct tas5805m_priv, work);
    struct regmap *rm = tas5805m->regmap;
    int ret;

    switch (tas5805m->trigger_cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_RESUME:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
            printk(KERN_DEBUG "tas5805m_work_handler: DSP startup\n");

            if (!tas5805m->is_started)
                tas5805m_startup(tas5805m->component);

            printk(KERN_DEBUG "tas5805m_work_handler: DAC mode -> PLAY\n");
            ret = regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MODE_PLAY);
            break;

        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_SUSPEND:
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
            printk(KERN_DEBUG "tas5805m_work_handler: DSP shutdown\n");
            tas5805m_check_faults(tas5805m->component);
            ret = regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MUTE);
            break;

        default:
            printk(KERN_ERR "tas5805m_work_handler: Invalid command\n");
            break;
    }
}

/* The TAS5805M DSP can't be configured until the I2S clock has been
 * present and stable for 5ms, or else it won't boot and we get no
 * sound.
 */
static int tas5805m_trigger(struct snd_pcm_substream *substream, int cmd,
                            struct snd_soc_dai *dai) 
{
    struct snd_soc_component *component = dai->component;
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);

    printk(KERN_DEBUG "tas5805m_trigger: cmd=%d\n", cmd);

    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
    case SNDRV_PCM_TRIGGER_RESUME:
    case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
    case SNDRV_PCM_TRIGGER_STOP:
    case SNDRV_PCM_TRIGGER_SUSPEND:
    case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        tas5805m->trigger_cmd = cmd;
        tas5805m->component = component;
        schedule_work(&tas5805m->work);
        break;

    default:
        printk(KERN_ERR "tas5805m_trigger: Invalid command\n");
        return -EINVAL;
    }

    return 0;
}

static const struct snd_soc_dapm_route tas5805m_audio_map[] = {
    { "DAC", NULL, "DAC IN" },
    { "OUT", NULL, "DAC" },
};

static const struct snd_soc_dapm_widget tas5805m_dapm_widgets[] = {
    SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_component_driver soc_codec_dev_tas5805m = {
    .controls           = tas5805m_snd_controls,
    .num_controls       = ARRAY_SIZE(tas5805m_snd_controls),
    .dapm_widgets       = tas5805m_dapm_widgets,
    .num_dapm_widgets   = ARRAY_SIZE(tas5805m_dapm_widgets),
    .dapm_routes        = tas5805m_audio_map,
    .num_dapm_routes    = ARRAY_SIZE(tas5805m_audio_map),
    .use_pmdown_time    = 1,
    .endianness     = 1,
};

static int tas5805m_mute(struct snd_soc_dai *dai, int mute, int direction)
{
    struct snd_soc_component *component = dai->component;
    struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
    struct regmap *rm = tas5805m->regmap;
    int ret;

    SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_ZERO);
    if (mute) {
        printk(KERN_DEBUG "tas5805m_mute: DAC mode -> SLEEP\n");
        ret = regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MODE_SLEEP);
    } else {
        printk(KERN_DEBUG "tas5805m_mute: DAC mode -> MUTE\n");
        ret = regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MUTE);
    }

    return 0;
}

static const struct snd_soc_dai_ops tas5805m_dai_ops = {
    .trigger        = tas5805m_trigger,
    .mute_stream        = tas5805m_mute,
    .no_capture_mute    = 1,
};

static struct snd_soc_dai_driver tas5805m_dai = {
    .name       = "tas5805m-amplifier",
    .playback   = {
        .stream_name    = "Playback",
        .channels_min   = 2,
        .channels_max   = 2,
        .rates      = TAS5805M_RATES,
        .formats    = TAS5805M_FORMATS,
    },
    .ops        = &tas5805m_dai_ops,
};

static const struct regmap_config tas5805m_regmap = {
    .reg_bits   = 8,
    .val_bits   = 8,
    /* We have quite a lot of multi-level bank switching and a
     * relatively small number of register writes between bank
     * switches.
     */
    .cache_type = REGCACHE_NONE,
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
        printk(KERN_ERR "unable to allocate register map: %d\n", ret);
        return ret;
    }

    tas5805m = devm_kzalloc(dev, sizeof(struct tas5805m_priv), GFP_KERNEL);
    if (!tas5805m)
        return -ENOMEM;

    tas5805m->pvdd = devm_regulator_get(dev, "pvdd");
    if (IS_ERR(tas5805m->pvdd)) {
        printk(KERN_ERR "failed to get pvdd supply: %ld\n",
            PTR_ERR(tas5805m->pvdd));
        return PTR_ERR(tas5805m->pvdd);
    }

    dev_set_drvdata(dev, tas5805m);
    tas5805m->regmap = regmap;
    tas5805m->gpio_pdn_n = devm_gpiod_get(dev, "pdn", GPIOD_OUT_LOW);
    if (IS_ERR(tas5805m->gpio_pdn_n)) {
        printk(KERN_ERR "error requesting PDN gpio: %ld\n",
            PTR_ERR(tas5805m->gpio_pdn_n));
        return PTR_ERR(tas5805m->gpio_pdn_n);
    }

    printk(KERN_DEBUG "tas5805m_i2c_probe: Requesting firmware\n");

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
            printk(KERN_ERR "firmware is not loaded, using default config\n");
            goto err;
        }
        
        // tas5805m->dsp_cfg_data = (u8 *)tas5805m_init_sequence;
        for (size_t i = 0; i < tas5805m_init_sequence_len; i++) {
            tas5805m->dsp_cfg_data[2 * i] = tas5805m_init_sequence[i].reg;
            tas5805m->dsp_cfg_data[2 * i + 1] = tas5805m_init_sequence[i].def; 
        }

        printk(KERN_INFO "Loaded %d register values\n", tas5805m->dsp_cfg_len / 2);
    } else {
        snprintf(filename, sizeof(filename), "tas5805m_dsp_%s.bin",
            config_name);
        ret = request_firmware(&fw, filename, dev);
        if (ret) {        
            printk(KERN_WARNING "firmware not found, using default config\n");
            goto err;
        }
        if ((fw->size < 2) || (fw->size & 1)) {
            printk(KERN_ERR "firmware is invalid, using default config\n");
            release_firmware(fw);
            goto err;
        }

        tas5805m->dsp_cfg_len = fw->size;
        tas5805m->dsp_cfg_data = devm_kmalloc(dev, fw->size, GFP_KERNEL);
        if (!tas5805m->dsp_cfg_data) {
            printk(KERN_ERR "firmware is not loaded, using default config\n");
            release_firmware(fw);
            goto err;
        }
        memcpy(tas5805m->dsp_cfg_data, fw->data, fw->size);
        release_firmware(fw);
    }

err:
    printk(KERN_DEBUG "tas5805m_i2c_probe: Powering on the device\n");

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

    ret = regulator_enable(tas5805m->pvdd);
    if (ret < 0) {
        printk(KERN_ERR "failed to enable pvdd: %d\n", ret);
        return ret;
    }

    usleep_range(100000, 150000);
    gpiod_set_value(tas5805m->gpio_pdn_n, 1);
    usleep_range(10000, 15000);

    /* Don't register through devm. We need to be able to unregister
     * the component prior to deasserting PDN#
     */
    ret = snd_soc_register_component(dev, &soc_codec_dev_tas5805m, &tas5805m_dai, 1);
    if (ret < 0) {
        printk(KERN_ERR "unable to register codec: %d\n", ret);
        gpiod_set_value(tas5805m->gpio_pdn_n, 0);
        regulator_disable(tas5805m->pvdd);
        return ret;
    }

	printk(KERN_DEBUG "tas5805m_i2c_probe: Allocating work queue\n");
    tas5805m->my_wq = create_singlethread_workqueue("_wq");
    INIT_WORK(&tas5805m->work, tas5805m_work_handler);

    return 0;
}

#if IS_KERNEL_MAJOR_BELOW_5
static int tas5805m_i2c_remove(struct i2c_client *i2c)   // for linux-5.xx
#else
static void tas5805m_i2c_remove(struct i2c_client *i2c)    // for linux-6.xx
#endif
{
    struct device *dev = &i2c->dev;
    struct tas5805m_priv *tas5805m = dev_get_drvdata(dev);

    printk(KERN_DEBUG "tas5805m_i2c_remove: Removing the I2C device\n");

    snd_soc_unregister_component(dev);
    gpiod_set_value(tas5805m->gpio_pdn_n, 0);
    usleep_range(10000, 15000);
    regulator_disable(tas5805m->pvdd);

	cancel_work_sync(&tas5805m->work);
    flush_workqueue(tas5805m->my_wq);
    destroy_workqueue(tas5805m->my_wq);
#if IS_KERNEL_MAJOR_BELOW_5
	return 0;
#endif
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
#if IS_KERNEL_BELOW_6_2
    .probe_new  = tas5805m_i2c_probe,
#else
    .probe      = tas5805m_i2c_probe,
#endif
    .remove     = tas5805m_i2c_remove,
    .id_table   = tas5805m_i2c_id,
    .driver     = {
        .name       = "tas5805m",
        .of_match_table = of_match_ptr(tas5805m_of_match),
    },
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

module_i2c_driver(tas5805m_i2c_driver);

MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_AUTHOR("Daniel Beer <daniel.beer@igorinstitute.com>");
MODULE_AUTHOR("J.P. van Coolwijk <jpvc36@gmail.com>");
MODULE_DESCRIPTION("TAS5805M Audio Amplifier Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andriy Malyshenko <andriy@sonocotta.com>");