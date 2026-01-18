// SPDX-License-Identifier: GPL-2.0
//
// Driver for the TAS5805M Audio Amplifier
//
// Author: Andy Liu <andy-liu@ti.com>
// Author: Daniel Beer <daniel.beer@igorinstitute.com>
// Author: Andriy Malyshenko <andriy@sonocotta.com>
//
// This is based on a driver originally written by Andy Liu at TI and
// posted here:
//
//    https://e2e.ti.com/support/audio-group/audio/f/audio-forum/722027/linux-tas5825m-linux-drivers
//
// It has been simplified a little and reworked for the 5.x ALSA SoC API.

#define DEBUG

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
#include "eq/tas5805m_eq_profiles.h"

/* Text arrays for enum controls */
static const char * const dac_mode_text[] = {
	"Normal",  /* Normal mode */
	"Bridge"   /* Bridge mode */
};

static const char * const eq_mode_text[] = {
	"On",   /* EQ enabled */
	"Off"   /* EQ disabled */
};

static const char * const modulation_mode_text[] = {
	"BD",     /* BD modulation */
	"1SPW",   /* 1SPW modulation */
	"Hybrid"  /* Hybrid modulation */
};

static const char * const switch_freq_text[] = {
	"768K",  /* 768kHz */
	"384K",  /* 384kHz */
	"480K",  /* 480kHz */
	"576K"   /* 576kHz */
};

/* EQ mode types (configured via device tree) */
enum tas5805m_eq_mode_type {
	TAS5805M_EQ_MODE_OFF = 0,
	TAS5805M_EQ_MODE_15BAND = 1,
	TAS5805M_EQ_MODE_CROSSOVER = 2,
};

static const char * const crossover_freq_text[] = {
	"OFF",
	"60 Hz",
	"70 Hz",
	"80 Hz",
	"90 Hz",
	"100 Hz",
	"110 Hz",
	"120 Hz",
	"130 Hz",
	"140 Hz",
	"150 Hz",
};

static const char * const mixer_mode_text[] = {
	"Stereo",  /* Normal stereo: L->L, R->R at 0dB */
	"Mono",    /* Mono mix: all paths at -6dB */
	"Left",    /* Left only: L->L, L->R at 0dB */
	"Right",   /* Right only: R->L, R->R at 0dB */
};

/* This sequence of register writes must always be sent, prior to the
 * 5ms delay while we wait for the DSP to boot.
 */
static const uint8_t dsp_cfg_preboot[] = {
	REG_PAGE, TAS5805M_REG_PAGE_0, 
	REG_BOOK, TAS5805M_BOOK_CONTROL_PORT, 
	TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MODE_HIZ, 
	TAS5805M_REG_RESET_CTRL, TAS5805M_RESET_CONTROL_PORT | TAS5805M_RESET_DSP,
	REG_PAGE, TAS5805M_REG_PAGE_0, 
	REG_PAGE, TAS5805M_REG_PAGE_0, 
	REG_PAGE, TAS5805M_REG_PAGE_0, 
	REG_PAGE, TAS5805M_REG_PAGE_0,
	REG_PAGE, TAS5805M_REG_PAGE_0, 
	REG_BOOK, TAS5805M_BOOK_CONTROL_PORT, 
	TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MODE_HIZ,
	TAS5805M_REG_SDOUT_SEL, TAS5805M_REG_SDOUT_SEL_PRE_DSP
};

#define SET_BOOK_AND_PAGE(rm, book, page) \
    do { \
        regmap_write(rm, TAS5805M_REG_PAGE_SET, TAS5805M_REG_PAGE_0); \
        regmap_write(rm, TAS5805M_REG_BOOK_SET, book);                   \
        regmap_write(rm, TAS5805M_REG_PAGE_SET, page);                   \
    } while (0)

struct tas5805m_priv {
	struct i2c_client		*i2c;
	struct regulator		*pvdd;
	struct gpio_desc		*gpio_pdn_n;

	uint8_t					*dsp_cfg_data;
	int						dsp_cfg_len;

	struct regmap			*regmap;

	int						vol;
	int						gain;
	int						mixer_l2l;  /* Left to Left mixer gain in dB */
	int						mixer_r2l;  /* Right to Left mixer gain in dB */
	int						mixer_l2r;  /* Left to Right mixer gain in dB */
	int						mixer_r2r;  /* Right to Right mixer gain in dB */
	unsigned int			mixer_mode;  /* Simplified mixer mode: 0=Stereo, 1=Mono, 2=Left, 3=Right */
	bool					mixer_mode_from_dt;  /* True if mixer mode is set from device tree */
	int						eq_band[TAS5805M_EQ_BANDS];  /* EQ band gains in dB */
	unsigned int			modulation_mode;
	unsigned int			switch_freq;
	unsigned int			bridge_mode;
	unsigned int			eq_mode;
	enum tas5805m_eq_mode_type	eq_mode_type;  /* EQ mode type from device tree */
	unsigned int			crossover_freq;  /* Crossover frequency index */
	bool					is_powered;
	bool					is_muted;
	bool					dsp_initialized;

	struct work_struct		work;
	struct mutex			lock;
};

static void tas5805m_decode_faults(struct device *dev, unsigned int chan,
				   unsigned int global1, unsigned int global2,
				   unsigned int ot_warning)
{
	if (chan) {
		if (chan & BIT(0))
			dev_warn(dev, "%s: Right channel over current fault\n", __func__);

		if (chan & BIT(1))
			dev_warn(dev, "%s: Left channel over current fault\n", __func__);

		if (chan & BIT(2))
			dev_warn(dev, "%s: Right channel DC fault\n", __func__);

		if (chan & BIT(3))
			dev_warn(dev, "%s: Left channel DC fault\n", __func__);
	}

	if (global1) {
		if (global1 & BIT(0))
			dev_warn(dev, "%s: PVDD UV fault\n", __func__);

		if (global1 & BIT(1))
			dev_warn(dev, "%s: PVDD OV fault\n", __func__);

		// This fault is often triggered by lack of I2S clock, which is expected
		// during longer pauses (when mute state is triggeered).
		if (global1 & BIT(2))
			dev_dbg(dev, "%s: Clock fault\n", __func__);

		if (global1 & BIT(6))
			dev_warn(dev, "%s: The recent BQ write failed\n", __func__);

		if (global1 & BIT(7))
			dev_warn(dev, "%s: OTP CRC check error\n", __func__);
	}

	if (global2) {
		if (global2 & BIT(0))
			dev_warn(dev, "%s: Over temperature shut down fault\n", __func__);
	}

	if (ot_warning) {
		if (ot_warning & BIT(2))
			dev_warn(dev, "%s: Over temperature warning\n", __func__);
	}
}

/**
 * Convert a dB value into a 4-byte buffer in "9.23" fixed-point format.
 * @param db_value Integer dB value to convert.
 * @param buffer 4-byte buffer to store the result.
 */
static void tas5805m_map_db_to_9_23(int db_value, uint8_t buffer[4]) {
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

static void tas5805m_refresh(struct tas5805m_priv *tas5805m)
{
	unsigned int chan, global1, global2, ot_warning;
	struct regmap *rm = tas5805m->regmap;
	int db_value = 24 - (tas5805m->vol / 2);  /* 0x00=+24dB, each step is 0.5dB */
	int db_gain = -(tas5805m->gain / 2);      /* TAS5805M_AGAIN_MAX=0dB, TAS5805M_AGAIN_MIN=-15.5dB, each step is -0.5dB */

	dev_dbg(&tas5805m->i2c->dev, "%s: is_muted=%d, vol=0x%02x (%ddB), gain=0x%02x (%ddB)\n", 
		__func__, tas5805m->is_muted, tas5805m->vol, db_value, tas5805m->gain, db_gain);

	SET_BOOK_AND_PAGE(rm, TAS5805M_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_0);

	/* Validate fault states */
	regmap_read(rm, TAS5805M_REG_CHAN_FAULT, &chan);
	regmap_read(rm, TAS5805M_REG_GLOBAL_FAULT1, &global1);
	regmap_read(rm, TAS5805M_REG_GLOBAL_FAULT2, &global2);
	regmap_read(rm, TAS5805M_REG_OT_WARNING, &ot_warning);

	tas5805m_decode_faults(&tas5805m->i2c->dev, chan, global1, global2, ot_warning);

	if (chan != 0 || global1 != 0 || global2 != 0 || ot_warning != 0) {
		dev_warn(&tas5805m->i2c->dev, "%s: fault detected: CHAN=0x%02x, GLOBAL1=0x%02x, GLOBAL2=0x%02x, OT_WARNING=0x%02x\n",
			__func__, chan, global1, global2, ot_warning);

		/* Optionally, we could take further action here, such as muting the device */
		dev_dbg(&tas5805m->i2c->dev, "%s: clearing faults\n",
			__func__);
		regmap_write(rm, TAS5805M_REG_FAULT, TAS5805M_ANALOG_FAULT_CLEAR);
	}

	/* Write hardware volume register. Applies to both channels.
	 * Register value 0x00=+24dB, 0x30=0dB, 0xFE=-103dB, 0xFF=Mute
	 */
	dev_dbg(&tas5805m->i2c->dev, "%s: writing volume reg 0x%02x\n",
				__func__, tas5805m->vol);
	regmap_write(rm, TAS5805M_REG_VOL_CTRL, tas5805m->vol);

	/* Write analog gain register
	 * Register value 0=0dB, 31=-15.5dB, 0.5dB steps
	 */
	dev_dbg(&tas5805m->i2c->dev, "%s: writing analog gain reg 0x%02x\n",
				__func__, tas5805m->gain);
	regmap_write(rm, TAS5805M_REG_ANALOG_GAIN, tas5805m->gain);

	/* Write device control 1 register (modulation, switching freq, bridge mode)
	 * Combine: modulation_mode (bits 1:0), bridge_mode (bit 2), switch_freq (bits 6:4)
	 */
	dev_dbg(&tas5805m->i2c->dev, "%s: modulation_mode=%u, bridge_mode=%u, switch_freq=%u, eq_mode=%u\n",
				__func__, tas5805m->modulation_mode,
				tas5805m->bridge_mode,
				tas5805m->switch_freq,
				tas5805m->eq_mode);
	unsigned int dctrl1_value = (tas5805m->modulation_mode & 0x3) |
							   ((tas5805m->bridge_mode & 0x1) << 2) |
							   ((tas5805m->switch_freq & 0x7) << 4);
	dev_dbg(&tas5805m->i2c->dev, "%s: writing device ctrl 1 reg 0x%02x\n",
				__func__, dctrl1_value);
	regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_1, dctrl1_value);

	/* Write DSP misc register (EQ enable/disable)
	 * bit 0 controls EQ
	 */
	dev_dbg(&tas5805m->i2c->dev, "%s: writing dsp misc reg 0x%02x\n",
				__func__, tas5805m->eq_mode);
	regmap_write(rm, TAS5805M_REG_DSP_MISC, tas5805m->eq_mode & 0x1);

	/* Write mixer gain registers
	 * Convert dB values to 9.23 fixed-point format and write to registers
	 */
	u8 mixer_buf[4];
	SET_BOOK_AND_PAGE(rm, TAS5805M_BOOK_5, TAS5805M_BOOK_5_MIXER_PAGE);
	
	dev_dbg(&tas5805m->i2c->dev, "%s: mixer gains: L2L=%ddB, R2L=%ddB, L2R=%ddB, R2R=%ddB\n",
				__func__, tas5805m->mixer_l2l, tas5805m->mixer_r2l,
				tas5805m->mixer_l2r, tas5805m->mixer_r2r);

	tas5805m_map_db_to_9_23(tas5805m->mixer_l2l, mixer_buf);
	regmap_bulk_write(rm, TAS5805M_REG_LEFT_TO_LEFT_GAIN, mixer_buf, 4);
	
	tas5805m_map_db_to_9_23(tas5805m->mixer_r2l, mixer_buf);
	regmap_bulk_write(rm, TAS5805M_REG_RIGHT_TO_LEFT_GAIN, mixer_buf, 4);
	
	tas5805m_map_db_to_9_23(tas5805m->mixer_l2r, mixer_buf);
	regmap_bulk_write(rm, TAS5805M_REG_LEFT_TO_RIGHT_GAIN, mixer_buf, 4);
	
	tas5805m_map_db_to_9_23(tas5805m->mixer_r2r, mixer_buf);
	regmap_bulk_write(rm, TAS5805M_REG_RIGHT_TO_RIGHT_GAIN, mixer_buf, 4);

	/* Write EQ band registers or apply crossover
	 * Apply EQ coefficients for each band based on stored dB values
	 */
	if (tas5805m->eq_mode_type == TAS5805M_EQ_MODE_15BAND) { 
		int current_page = -1;
		
		dev_dbg(&tas5805m->i2c->dev, "%s: applying 15-band EQ\n", __func__);
		
		for (int band = 0; band < TAS5805M_EQ_BANDS; band++) {
			int db_value = tas5805m->eq_band[band];
			int row = db_value + TAS5805M_EQ_MAX_DB;  /* Convert dB to array index */
			int base_offset = band * TAS5805M_EQ_KOEF_PER_BAND * TAS5805M_EQ_REG_PER_KOEF;
			
			for (int i = 0; i < TAS5805M_EQ_KOEF_PER_BAND * TAS5805M_EQ_REG_PER_KOEF; i++) {
				const reg_sequence_eq *reg_value = &tas5805m_eq_registers[row][base_offset + i];
				
				if (reg_value->page != current_page) {
					current_page = reg_value->page;
					SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_EQ, reg_value->page);
				}
				
				regmap_write(rm, reg_value->offset, reg_value->value);
			}
		}
	} else if (tas5805m->eq_mode_type == TAS5805M_EQ_MODE_CROSSOVER) {
		/* Apply crossover filter coefficients */
		unsigned int freq_index = tas5805m->crossover_freq;
		
		if (freq_index >= ARRAY_SIZE(crossover_freq_text)) {
			dev_warn(&tas5805m->i2c->dev, "%s: Invalid crossover frequency index %u, using OFF\n", __func__, freq_index);
			freq_index = 0;
		}
		
		dev_dbg(&tas5805m->i2c->dev, "%s: applying crossover filter: frequency=%s\n", 
				__func__, crossover_freq_text[freq_index]);
		
		/* Get the appropriate coefficient array (uses left channel for both channels in bridge mode) */
		const reg_sequence_eq *coefficients = tas5805m_crossover_lf_registers[freq_index];
		int current_page = -1;
		
		/* Apply all coefficient registers (3 bands * 5 coefficients * 4 registers = 60 registers) */
		for (int i = 0; i < TAS5805M_EQ_PROFILE_REG_PER_STEP; i++) {
			const reg_sequence_eq *reg_value = &coefficients[i];
			
			if (reg_value->page != current_page) {
				current_page = reg_value->page;
				SET_BOOK_AND_PAGE(rm, TAS5805M_REG_BOOK_EQ, reg_value->page);
			}
			
			regmap_write(rm, reg_value->offset, reg_value->value);
		}
	} else {
		dev_dbg(&tas5805m->i2c->dev, "%s: EQ mode is OFF\n", __func__);
	}

	/* Return to control port page 0 */	
	SET_BOOK_AND_PAGE(rm, TAS5805M_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_0);
	
	/* Set/clear digital soft-mute */
	uint8_t device_state = (tas5805m->is_muted ? TAS5805M_DCTRL2_MUTE : 0) |
			TAS5805M_DCTRL2_MODE_PLAY;
	dev_dbg(&tas5805m->i2c->dev, "%s: writing device state 0x%02x\n",
				__func__, device_state);
	regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, device_state);
}

static int tas5805m_vol_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;

	/* ALSA range: 0 (min) to 127 (max), 1dB steps */
	uinfo->value.integer.min = TAS5805M_VOLUME_MAX;
	uinfo->value.integer.max = TAS5805M_VOLUME_MIN / 2;
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
	/* Invert and convert: hardware has 0.5dB steps, ALSA gets 1dB steps */
	ucontrol->value.integer.value[0] = (TAS5805M_VOLUME_MIN - tas5805m->vol) / 2;
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static inline int volume_is_valid(int v)
{
	/* ALSA range: 0 to 127 (1dB steps, hardware 0xFE-0x00 is 254 steps of 0.5dB) */
	return (v >= 0) && (v <= (TAS5805M_VOLUME_MIN / 2));
}

static int tas5805m_vol_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	int alsa_vol = ucontrol->value.integer.value[0];
	int hw_vol;
	int ret = 0;

	dev_dbg(component->dev, "%s: alsa_vol=%d\n", 
		__func__, alsa_vol);

	if (!volume_is_valid(alsa_vol))
		return -EINVAL;

	/* Convert ALSA 1dB steps to hardware 0.5dB steps and invert */
	hw_vol = TAS5805M_VOLUME_MIN - (alsa_vol * 2);

	mutex_lock(&tas5805m->lock);
	if (tas5805m->vol != hw_vol) {
		int db_value = 24 - (hw_vol / 2);  /* Calculate dB: 0x00=+24dB, each step is 0.5dB */
		tas5805m->vol = hw_vol;
		dev_dbg(component->dev, "%s: set vol=%d (hw_reg=0x%02x, %ddB, is_powered=%d)\n",
			__func__, alsa_vol, hw_vol, db_value, tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: volume change deferred until power-up\n", 
				__func__);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

static int tas5805m_again_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TAS5805M_AGAIN_MAX;
	uinfo->value.integer.max = TAS5805M_AGAIN_MIN;
	return 0;
}

static int tas5805m_again_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&tas5805m->lock);
	/* Invert: register TAS5805M_AGAIN_MAX (0dB) -> control 31, register TAS5805M_AGAIN_MIN (-15.5dB) -> control 0 */
	ucontrol->value.integer.value[0] = TAS5805M_AGAIN_MIN - (tas5805m->gain & TAS5805M_AGAIN_MIN);
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static int tas5805m_again_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	unsigned int control_value = ucontrol->value.integer.value[0];
	unsigned int reg_value;
	int ret = 0;

	if (control_value > TAS5805M_AGAIN_MIN)
		return -EINVAL;

	/* Invert: control 31 (0dB) -> register TAS5805M_AGAIN_MAX, control 0 (-15.5dB) -> register TAS5805M_AGAIN_MIN */
	reg_value = TAS5805M_AGAIN_MIN - control_value;

	mutex_lock(&tas5805m->lock);
	
	if (tas5805m->gain != reg_value) {
		tas5805m->gain = reg_value;
		dev_dbg(component->dev, "%s: set gain control=%u (hw_reg=0x%02x, is_powered=%d)\n",
			__func__, control_value, reg_value, tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: gain change deferred until power-up\n",
				__func__);
		ret = 1;
	}

	mutex_unlock(&tas5805m->lock);
	return ret;
}

/* TLV for analog gain control: -15.5dB to 0dB in 0.5dB steps (32 steps, 0-31) */
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_again_tlv, -1550, 50, 0);

/* Generic enum control handlers */
struct tas5805m_enum_ctrl {
	const char * const *texts;
	unsigned int num_items;
	unsigned int offset; /* Offset in tas5805m_priv structure */
};

static int tas5805m_enum_info(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_info *uinfo)
{
	struct tas5805m_enum_ctrl *ctrl = (struct tas5805m_enum_ctrl *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ctrl->num_items;

	if (uinfo->value.enumerated.item >= ctrl->num_items)
		uinfo->value.enumerated.item = ctrl->num_items - 1;

	strscpy(uinfo->value.enumerated.name,
			ctrl->texts[uinfo->value.enumerated.item],
			sizeof(uinfo->value.enumerated.name));

	return 0;
}

static int tas5805m_enum_get(struct snd_kcontrol *kcontrol,
						 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	struct tas5805m_enum_ctrl *ctrl = (struct tas5805m_enum_ctrl *)kcontrol->private_value;
	unsigned int *value_ptr = (unsigned int *)((char *)tas5805m + ctrl->offset);

	mutex_lock(&tas5805m->lock);
	ucontrol->value.enumerated.item[0] = *value_ptr;
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static int tas5805m_enum_put(struct snd_kcontrol *kcontrol,
						 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	struct tas5805m_enum_ctrl *ctrl = (struct tas5805m_enum_ctrl *)kcontrol->private_value;
	unsigned int *value_ptr = (unsigned int *)((char *)tas5805m + ctrl->offset);
	unsigned int new_value = ucontrol->value.enumerated.item[0];
	int ret = 0;

	if (new_value >= ctrl->num_items)
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (*value_ptr != new_value) {
		*value_ptr = new_value;
		dev_dbg(component->dev, "%s: set %s=%u (is_powered=%d)\n",
				__func__, kcontrol->id.name, new_value, tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: change deferred until power-up\n",
					__func__);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

/* Define enum control structures */
static struct tas5805m_enum_ctrl eq_mode_ctrl = {
	.texts = eq_mode_text,
	.num_items = ARRAY_SIZE(eq_mode_text),
	.offset = offsetof(struct tas5805m_priv, eq_mode),
};

#define TAS5805M_ENUM(xname, xenum_ctrl) \
{\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname,\
	.info = tas5805m_enum_info,\
	.get = tas5805m_enum_get,\
	.put = tas5805m_enum_put,\
	.private_value = (unsigned long)&xenum_ctrl,\
}

/* Mixer control handlers */
static int tas5805m_mixer_info(struct snd_kcontrol *kcontrol,
						   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TAS5805M_MIXER_MIN_DB;
	uinfo->value.integer.max = TAS5805M_MIXER_MAX_DB;
	return 0;
}

static int tas5805m_mixer_get(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	unsigned int offset = kcontrol->private_value;
	int *mixer_ptr = (int *)((char *)tas5805m + offset);

	mutex_lock(&tas5805m->lock);
	ucontrol->value.integer.value[0] = *mixer_ptr;
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static int tas5805m_mixer_put(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	unsigned int offset = kcontrol->private_value;
	int *mixer_ptr = (int *)((char *)tas5805m + offset);
	int value = ucontrol->value.integer.value[0];
	int ret = 0;

	if (value < TAS5805M_MIXER_MIN_DB || value > TAS5805M_MIXER_MAX_DB)
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (*mixer_ptr != value) {
		*mixer_ptr = value;
		dev_dbg(component->dev, "%s: set %s=%ddB (is_powered=%d)\n",
				__func__, kcontrol->id.name, value, tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: mixer change deferred until power-up\n",
					__func__);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

#define TAS5805M_MIXER(xname, xoffset) \
{\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname,\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = tas5805m_mixer_info,\
	.get = tas5805m_mixer_get,\
	.put = tas5805m_mixer_put,\
	.private_value = offsetof(struct tas5805m_priv, xoffset),\
}

/* EQ control handlers */
static int tas5805m_eq_info(struct snd_kcontrol *kcontrol,
						   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TAS5805M_EQ_MIN_DB;
	uinfo->value.integer.max = TAS5805M_EQ_MAX_DB;
	return 0;
}

static int tas5805m_eq_get(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	unsigned int band_index = kcontrol->private_value;

	if (band_index >= TAS5805M_EQ_BANDS)
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	ucontrol->value.integer.value[0] = tas5805m->eq_band[band_index];
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static int tas5805m_eq_put(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	unsigned int band_index = kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];
	int ret = 0;

	if (band_index >= TAS5805M_EQ_BANDS)
		return -EINVAL;

	if (value < TAS5805M_EQ_MIN_DB || value > TAS5805M_EQ_MAX_DB)
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (tas5805m->eq_band[band_index] != value) {
		tas5805m->eq_band[band_index] = value;
		dev_dbg(component->dev, "%s: set %s=%ddB (is_powered=%d)\n",
				__func__, kcontrol->id.name, value, tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: EQ change deferred until power-up\n",
					__func__);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

#define TAS5805M_EQ_BAND(xname, xband) \
{\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname,\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = tas5805m_eq_info,\
	.get = tas5805m_eq_get,\
	.put = tas5805m_eq_put,\
	.private_value = xband,\
}

/* Crossover control handlers */
static int tas5805m_crossover_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ARRAY_SIZE(crossover_freq_text);

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;

	strcpy(uinfo->value.enumerated.name,
	       crossover_freq_text[uinfo->value.enumerated.item]);

	return 0;
}

static int tas5805m_crossover_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = tas5805m->crossover_freq;

	return 0;
}

static int tas5805m_crossover_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	unsigned int val = ucontrol->value.enumerated.item[0];
	int ret = 0;

	if (val >= ARRAY_SIZE(crossover_freq_text))
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (tas5805m->crossover_freq != val) {
		tas5805m->crossover_freq = val;
		dev_dbg(component->dev, "%s: set crossover=%s (is_powered=%d)\n",
				__func__, crossover_freq_text[val], tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: Crossover change deferred until power-up\n",
					__func__);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

/* Mixer mode control handlers */
static int tas5805m_mixer_mode_info(struct snd_kcontrol *kcontrol,
								struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ARRAY_SIZE(mixer_mode_text);

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;

	strcpy(uinfo->value.enumerated.name,
	       mixer_mode_text[uinfo->value.enumerated.item]);

	return 0;
}

static int tas5805m_mixer_mode_get(struct snd_kcontrol *kcontrol,
								  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = tas5805m->mixer_mode;

	return 0;
}

static int tas5805m_mixer_mode_put(struct snd_kcontrol *kcontrol,
								  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m = snd_soc_component_get_drvdata(component);
	unsigned int val = ucontrol->value.enumerated.item[0];
	int ret = 0;

	if (val >= ARRAY_SIZE(mixer_mode_text))
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (tas5805m->mixer_mode != val) {
		tas5805m->mixer_mode = val;
		
		/* Apply preset mixer values based on mode */
		switch (val) {
		case 0: /* Stereo */
			tas5805m->mixer_l2l = TAS5805M_MIXER_MAX_DB;   /* 0dB */
			tas5805m->mixer_r2l = TAS5805M_MIXER_MIN_DB; /* Mute */
			tas5805m->mixer_l2r = TAS5805M_MIXER_MIN_DB; /* Mute */
			tas5805m->mixer_r2r = TAS5805M_MIXER_MAX_DB;   /* 0dB */
			break;
		case 1: /* Mono */
			tas5805m->mixer_l2l = TAS5805M_MIXER_HALFMAX_DB;  /* -6dB */
			tas5805m->mixer_r2l = TAS5805M_MIXER_HALFMAX_DB;  /* -6dB */
			tas5805m->mixer_l2r = TAS5805M_MIXER_HALFMAX_DB;  /* -6dB */
			tas5805m->mixer_r2r = TAS5805M_MIXER_HALFMAX_DB;  /* -6dB */
			break;
		case 2: /* Left */
			tas5805m->mixer_l2l = TAS5805M_MIXER_MAX_DB;   /* 0dB */
			tas5805m->mixer_r2l = TAS5805M_MIXER_MIN_DB; /* Mute */
			tas5805m->mixer_l2r = TAS5805M_MIXER_MAX_DB;   /* 0dB */
			tas5805m->mixer_r2r = TAS5805M_MIXER_MIN_DB; /* Mute */
			break;
		case 3: /* Right */
			tas5805m->mixer_l2l = TAS5805M_MIXER_MIN_DB; /* Mute */
			tas5805m->mixer_r2l = TAS5805M_MIXER_MAX_DB;   /* 0dB */
			tas5805m->mixer_l2r = TAS5805M_MIXER_MIN_DB; /* Mute */
			tas5805m->mixer_r2r = TAS5805M_MIXER_MAX_DB;   /* 0dB */
			break;
		}
		
		dev_dbg(component->dev, "%s: set mixer_mode=%s (is_powered=%d)\n",
				__func__, mixer_mode_text[val], tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		else
			dev_dbg(component->dev, "%s: Mixer mode change deferred until power-up\n",
					__func__);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

/* Base controls (always registered) */
static const struct snd_kcontrol_new tas5805m_snd_controls_base[] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Digital Volume",
		.access	= SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			  SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= tas5805m_vol_info,
		.get	= tas5805m_vol_get,
		.put	= tas5805m_vol_put,
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Analog Gain",
		.access	= SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			  SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= tas5805m_again_info,
		.get	= tas5805m_again_get,
		.put	= tas5805m_again_put,
		.tlv.p	= tas5805m_again_tlv,
	},

	TAS5805M_ENUM("Equalizer", eq_mode_ctrl),
};

/* Mixer controls (conditionally registered based on device tree) */
static const struct snd_kcontrol_new tas5805m_snd_controls_mixer[] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Mixer Mode",
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= tas5805m_mixer_mode_info,
		.get	= tas5805m_mixer_mode_get,
		.put	= tas5805m_mixer_mode_put,
	},

	TAS5805M_MIXER("Mixer L2L Gain", mixer_l2l),
	TAS5805M_MIXER("Mixer R2L Gain", mixer_r2l),
	TAS5805M_MIXER("Mixer L2R Gain", mixer_l2r),
	TAS5805M_MIXER("Mixer R2R Gain", mixer_r2r),
};

/* EQ band controls (conditionally registered based on device tree) */
static const struct snd_kcontrol_new tas5805m_snd_controls_eq_15band[] = {
	TAS5805M_EQ_BAND("00020 Hz", 0),
	TAS5805M_EQ_BAND("00032 Hz", 1),
	TAS5805M_EQ_BAND("00050 Hz", 2),
	TAS5805M_EQ_BAND("00080 Hz", 3),
	TAS5805M_EQ_BAND("00125 Hz", 4),
	TAS5805M_EQ_BAND("00200 Hz", 5),
	TAS5805M_EQ_BAND("00315 Hz", 6),
	TAS5805M_EQ_BAND("00500 Hz", 7),
	TAS5805M_EQ_BAND("00800 Hz", 8),
	TAS5805M_EQ_BAND("01250 Hz", 9),
	TAS5805M_EQ_BAND("02000 Hz", 10),
	TAS5805M_EQ_BAND("03150 Hz", 11),
	TAS5805M_EQ_BAND("05000 Hz", 12),
	TAS5805M_EQ_BAND("08000 Hz", 13),
	TAS5805M_EQ_BAND("16000 Hz", 14),
};

/* Crossover controls (registered when EQ mode is crossover) */
static const struct snd_kcontrol_new tas5805m_snd_controls_crossover[] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Crossover Frequency",
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= tas5805m_crossover_info,
		.get	= tas5805m_crossover_get,
		.put	= tas5805m_crossover_put,
	},
};

static void send_cfg(struct regmap *rm,
		     const uint8_t *s, unsigned int len)
{
	unsigned int i;

	pr_debug("%s: len=%u\n", 
		__func__, len);
	for (i = 0; i + 1 < len; i += 2)
		regmap_write(rm, s[i], s[i + 1]);
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

	dev_dbg(component->dev, "%s: cmd=%d\n", 
		__func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(component->dev, "%s: clock start\n", __func__);
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

static void do_work(struct work_struct *work)
{
	struct tas5805m_priv *tas5805m =
	       container_of(work, struct tas5805m_priv, work);
	struct regmap *rm = tas5805m->regmap;

	dev_dbg(&tas5805m->i2c->dev, "%s: DSP startup\n", 
		__func__);

	mutex_lock(&tas5805m->lock);
	/* We mustn't issue any I2C transactions until the I2S
	 * clock is stable. Furthermore, we must allow a 5ms
	 * delay after the first set of register writes to
	 * allow the DSP to boot before configuring it.
	 */
	usleep_range(5000, 10000);
	
	/* Only send preboot config once per PDN cycle */
	if (!tas5805m->dsp_initialized) {
		dev_dbg(&tas5805m->i2c->dev, "%s: sending preboot config\n", __func__);
		send_cfg(rm, dsp_cfg_preboot, ARRAY_SIZE(dsp_cfg_preboot));
		// Need to wait until clock is read by the DAC
		usleep_range(5000, 10000);
		if (tas5805m->dsp_cfg_len > 0)
		{
			send_cfg(rm, tas5805m->dsp_cfg_data, tas5805m->dsp_cfg_len);
		}
		
		/* Apply bridge mode setting from device tree after DSP boot */
		SET_BOOK_AND_PAGE(rm, TAS5805M_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_0);
		unsigned int dctrl1_init = (tas5805m->modulation_mode & 0x3) |
								  ((tas5805m->bridge_mode & 0x1) << 2) |
								  ((tas5805m->switch_freq & 0x7) << 4);
		regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_1, dctrl1_init);
		dev_info(&tas5805m->i2c->dev, "%s: Device configuration: modulation=%u, bridge_mode=%u (%s), switch_freq=%u\n",
				 __func__, tas5805m->modulation_mode, tas5805m->bridge_mode,
				 tas5805m->bridge_mode ? "Bridge/PBTL" : "Normal/Stereo",
				 tas5805m->switch_freq);
		
		tas5805m->dsp_initialized = true;
	} else {
		dev_dbg(&tas5805m->i2c->dev, "%s: DSP already initialized, skipping preboot config\n", __func__);
	}

	tas5805m->is_powered = true;
	tas5805m_refresh(tas5805m);
	mutex_unlock(&tas5805m->lock);
}

static int tas5805m_dac_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	struct regmap *rm = tas5805m->regmap;

	dev_dbg(component->dev, "%s: event=0x%x\n", 
		__func__, event);

	if (event & SND_SOC_DAPM_POST_PMU) {
		dev_dbg(component->dev, "%s: DSP startup\n", __func__);
		
		mutex_lock(&tas5805m->lock);
		if (!tas5805m->is_powered) {
			tas5805m->is_powered = true;
			
			/* Wait for I2S clock to stabilize */
			usleep_range(5000, 10000);
			
			/* Only send preboot config once per PDN cycle */
			if (!tas5805m->dsp_initialized) {
				dev_dbg(component->dev, "%s: sending preboot config\n", __func__);
				send_cfg(rm, dsp_cfg_preboot, ARRAY_SIZE(dsp_cfg_preboot));
				/* Wait for DSP to boot */
				usleep_range(5000, 15000);
				if (tas5805m->dsp_cfg_len > 0) {
					send_cfg(rm, tas5805m->dsp_cfg_data, tas5805m->dsp_cfg_len);
				}
				
				/* Apply bridge mode setting from device tree after DSP boot */
				SET_BOOK_AND_PAGE(rm, TAS5805M_BOOK_CONTROL_PORT, TAS5805M_REG_PAGE_0);
				unsigned int dctrl1_init = (tas5805m->modulation_mode & 0x3) |
										  ((tas5805m->bridge_mode & 0x1) << 2) |
										  ((tas5805m->switch_freq & 0x7) << 4);
				regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_1, dctrl1_init);
				dev_info(component->dev, "%s: Device configuration: modulation=%u, bridge_mode=%u (%s), switch_freq=%u\n",
						 __func__, tas5805m->modulation_mode, tas5805m->bridge_mode,
						 tas5805m->bridge_mode ? "Bridge/PBTL" : "Normal/Stereo",
						 tas5805m->switch_freq);
				
				tas5805m->dsp_initialized = true;
			} else {
				dev_dbg(component->dev, "%s: DSP already initialized, skipping preboot config\n", __func__);
			}
			
			/* Power up and configure the device */
			tas5805m_refresh(tas5805m);
		}
		mutex_unlock(&tas5805m->lock);
	}

	if (event & SND_SOC_DAPM_PRE_PMD) {
		dev_dbg(component->dev, "%s: DSP shutdown\n", __func__);
		cancel_work_sync(&tas5805m->work);

		mutex_lock(&tas5805m->lock);
		if (tas5805m->is_powered) {
			tas5805m->is_powered = false;
			dev_dbg(component->dev, "%s: writing device state 0x%02x\n",
				__func__, TAS5805M_DCTRL2_MODE_DEEP_SLEEP);
			regmap_write(rm, TAS5805M_REG_DEVICE_CTRL_2, TAS5805M_DCTRL2_MODE_DEEP_SLEEP);
		}
		mutex_unlock(&tas5805m->lock);
	}

	return 0;
}

static const struct snd_soc_dapm_route tas5805m_audio_map[] = {
	{ "DAC", NULL, "DAC IN" },
	{ "OUTA", NULL, "DAC" },
	{ "OUTB", NULL, "DAC" },
};

static const struct snd_soc_dapm_widget tas5805m_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
		tas5805m_dac_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUTA"),
	SND_SOC_DAPM_OUTPUT("OUTB")
};

static int tas5805m_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
		
	mutex_lock(&tas5805m->lock);

	dev_dbg(component->dev, "%s: mute=%d, direction=%d, is_powered=%d\n", 
		__func__, mute, direction, tas5805m->is_powered);

	tas5805m->is_muted = mute;
	if (tas5805m->is_powered)
		tas5805m_refresh(tas5805m);
	else
		dev_dbg(component->dev, "%s: mute change deferred until power-up\n", 
			__func__);
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static const struct snd_soc_dai_ops tas5805m_dai_ops = {
	.trigger		    = tas5805m_trigger,
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

	dev_dbg(dev, "%s on %s\n", 
		__func__, dev_name(dev));

	regmap = devm_regmap_init_i2c(i2c, &tas5805m_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "%s: unable to allocate register map: %d\n", 
			__func__, ret);
		return ret;
	}

	tas5805m = devm_kzalloc(dev, sizeof(struct tas5805m_priv), GFP_KERNEL);
	if (!tas5805m)
		return -ENOMEM;

	tas5805m->i2c = i2c;
	tas5805m->pvdd = devm_regulator_get(dev, "pvdd");
	if (IS_ERR(tas5805m->pvdd)) {
		dev_err(dev, "%s: failed to get pvdd supply: %ld\n", 
			__func__, PTR_ERR(tas5805m->pvdd));
		return PTR_ERR(tas5805m->pvdd);
	}

	dev_set_drvdata(dev, tas5805m);
	tas5805m->regmap = regmap;
	tas5805m->gpio_pdn_n = devm_gpiod_get(dev, "pdn", GPIOD_OUT_LOW);
	if (IS_ERR(tas5805m->gpio_pdn_n)) {
		dev_err(dev, "%s: error requesting PDN gpio: %ld\n",
			__func__, PTR_ERR(tas5805m->gpio_pdn_n));
		return PTR_ERR(tas5805m->gpio_pdn_n);
	}

	/* This configuration must be generated by PPC3. The file loaded
	 * consists of a sequence of register writes, where bytes at
	 * even indices are register addresses and those at odd indices
	 * are register values.
	 *
	 * The fixed portion of PPC3's output prior to the 5ms delay
	 * should be omitted.
	 *
	 * If the device node does not
	 * provide `ti,dsp-config-name` just warn and continue with an
	 * empty configuration set. If a name is provided, attempt to
	 * load the firmware and fail probe on error.
	 */
	if (device_property_read_string(dev, "ti,dsp-config-name",
					&config_name)) {
		dev_warn(dev, "%s: no ti,dsp-config-name provided; continuing without DSP config\n", 
			__func__);
		config_name = NULL;
	}

	if (config_name) {
		snprintf(filename, sizeof(filename), "tas5805m_dsp_%s.bin",
			 config_name);
		ret = request_firmware(&fw, filename, dev);
		if (ret)
			return ret;

		if ((fw->size < 2) || (fw->size & 1)) {
			dev_err(dev, "%s: firmware is invalid\n", 
				__func__);
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
	} else {
		/* No config provided: initialize empty configset */
		tas5805m->dsp_cfg_len = 0;
		tas5805m->dsp_cfg_data = NULL;
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
	tas5805m->vol = TAS5805M_VOLUME_ZERO_DB;
	tas5805m->gain = TAS5805M_AGAIN_MAX; /* 0dB analog gain */
	/* Initialize all EQ bands to 0dB (flat response) */
	for (int i = 0; i < TAS5805M_EQ_BANDS; i++)
		tas5805m->eq_band[i] = 0;
	tas5805m->eq_mode = 0; /* EQ On */
	tas5805m->crossover_freq = 0; /* OFF */

	/* Read EQ mode type from device tree (default: off)
	 * 0 = OFF - no EQ processing
	 * 1 = 15-band - traditional 15-band parametric EQ
	 * 2 = crossover - crossover filter mode
	 */
	tas5805m->eq_mode_type = TAS5805M_EQ_MODE_OFF;
	if (!device_property_read_u32(dev, "ti,eq-mode", (u32 *)&tas5805m->eq_mode_type)) {
		if (tas5805m->eq_mode_type > TAS5805M_EQ_MODE_CROSSOVER) {
			dev_warn(dev, "%s: Invalid EQ mode %u, using OFF\n", __func__, tas5805m->eq_mode_type);
			tas5805m->eq_mode_type = TAS5805M_EQ_MODE_OFF;
		}
		switch (tas5805m->eq_mode_type) {
		case TAS5805M_EQ_MODE_OFF:
			dev_info(dev, "%s: EQ mode: OFF\n", __func__);
			break;
		case TAS5805M_EQ_MODE_15BAND:
			dev_info(dev, "%s: EQ mode: 15-band parametric EQ\n", __func__);
			break;
		case TAS5805M_EQ_MODE_CROSSOVER:
			dev_info(dev, "%s: EQ mode: Crossover filter\n", __func__);
			break;
		}
	} else {
		dev_dbg(dev, "%s: EQ mode: 15-band parametric EQ (default)\n", __func__);
	}

	/* Read modulation mode from device tree (default: Hybrid mode)
	 * 0 = BD modulation
	 * 1 = 1SPW modulation
	 * 2 = Hybrid modulation (default)
	 * This is not runtime configurable for stability
	 */
	tas5805m->modulation_mode = 2; /* Default to Hybrid mode */
	if (!device_property_read_u32(dev, "ti,modulation-mode", &tas5805m->modulation_mode)) {
		if (tas5805m->modulation_mode > 2) {
			dev_warn(dev, "%s: Invalid modulation mode %u, using Hybrid\n", __func__, tas5805m->modulation_mode);
			tas5805m->modulation_mode = 2;
		}
		dev_info(dev, "%s: Modulation mode: %s\n", __func__, modulation_mode_text[tas5805m->modulation_mode]);
	} else {
		dev_dbg(dev, "%s: Modulation mode: Hybrid (default)\n", __func__);
	}

	/* Read switching frequency from device tree (default: 768kHz)
	 * 0 = 768kHz (default)
	 * 1 = 384kHz
	 * 2 = 480kHz
	 * 3 = 576kHz
	 * This is not runtime configurable for stability
	 */
	tas5805m->switch_freq = 0; /* Default to 768kHz */
	if (!device_property_read_u32(dev, "ti,switching-freq", &tas5805m->switch_freq)) {
		if (tas5805m->switch_freq > 3) {
			dev_warn(dev, "%s: Invalid switching frequency %u, using 768kHz\n", __func__, tas5805m->switch_freq);
			tas5805m->switch_freq = 0;
		}
		dev_info(dev, "%s: Switching frequency: %s\n", __func__, switch_freq_text[tas5805m->switch_freq]);
	} else {
		dev_dbg(dev, "%s: Switching frequency: 768K (default)\n", __func__);
	}

	/* Read mixer mode from device tree (optional)
	 * If set, individual mixer sliders are hidden from ALSA
	 * 0 = Stereo (default for primary DAC)
	 * 1 = Mono (default for secondary DAC)
	 * 2 = Left
	 * 3 = Right
	 */
	if (!device_property_read_u32(dev, "ti,mixer-mode", &tas5805m->mixer_mode)) {
		if (tas5805m->mixer_mode > 3) {
			dev_warn(dev, "%s: Invalid mixer mode %u, using Stereo\n", __func__, tas5805m->mixer_mode);
			tas5805m->mixer_mode = 0;
		}
		tas5805m->mixer_mode_from_dt = true;
		
		/* Apply the mixer mode values */
		switch (tas5805m->mixer_mode) {
		case 0: /* Stereo */
			tas5805m->mixer_l2l = 0;
			tas5805m->mixer_r2l = -110;
			tas5805m->mixer_l2r = -110;
			tas5805m->mixer_r2r = 0;
			break;
		case 1: /* Mono */
			tas5805m->mixer_l2l = -6;
			tas5805m->mixer_r2l = -6;
			tas5805m->mixer_l2r = -6;
			tas5805m->mixer_r2r = -6;
			break;
		case 2: /* Left */
			tas5805m->mixer_l2l = 0;
			tas5805m->mixer_r2l = -110;
			tas5805m->mixer_l2r = 0;
			tas5805m->mixer_r2r = -110;
			break;
		case 3: /* Right */
			tas5805m->mixer_l2l = -110;
			tas5805m->mixer_r2l = 0;
			tas5805m->mixer_l2r = -110;
			tas5805m->mixer_r2r = 0;
			break;
		}
		dev_info(dev, "%s: Mixer mode: %s (from device tree)\n", __func__, mixer_mode_text[tas5805m->mixer_mode]);
	} else {
		/* Not set in device tree - use runtime defaults and expose controls */
		tas5805m->mixer_mode = 0; /* Stereo by default */
		tas5805m->mixer_mode_from_dt = false;
		tas5805m->mixer_l2l = TAS5805M_MIXER_MAX_DB; /* 0dB L2L */
		tas5805m->mixer_r2l = TAS5805M_MIXER_MIN_DB; /* Muted R2L */
		tas5805m->mixer_l2r = TAS5805M_MIXER_MIN_DB; /* Muted L2R */
		tas5805m->mixer_r2r = TAS5805M_MIXER_MAX_DB; /* 0dB R2R */
		dev_dbg(dev, "%s: Mixer controls enabled (runtime configurable)\n", __func__);
	}

	/* Read bridge mode from device tree (default: normal mode)
	 * 0 = Normal mode (PBTL disabled)
	 * 1 = Bridge mode (PBTL enabled)
	 * This is not runtime configurable to prevent speaker damage
	 */
	tas5805m->bridge_mode = 0; /* Default to normal mode */
	if (device_property_read_bool(dev, "ti,bridge-mode")) {
		tas5805m->bridge_mode = 1;
		dev_info(dev, "%s: Bridge mode (PBTL) enabled via device tree\n", __func__);
	} else {
		dev_dbg(dev, "%s: Normal mode (stereo) enabled (default)\n", __func__);
	}

	ret = regulator_enable(tas5805m->pvdd);
	if (ret < 0) {
		dev_err(dev, "%s: failed to enable pvdd: %d\n", 
			__func__, ret);
		return ret;
	}

	usleep_range(100000, 150000);
	gpiod_set_value(tas5805m->gpio_pdn_n, 1);
	usleep_range(10000, 15000);

	INIT_WORK(&tas5805m->work, do_work);
	mutex_init(&tas5805m->lock);

	/* Build component driver dynamically based on EQ mode */
	struct snd_soc_component_driver *soc_codec_dev;
	struct snd_kcontrol_new *controls;
	int num_controls;
	const struct snd_kcontrol_new *eq_controls = NULL;
	size_t eq_controls_size = 0;

	soc_codec_dev = devm_kzalloc(dev, sizeof(*soc_codec_dev), GFP_KERNEL);
	if (!soc_codec_dev) {
		gpiod_set_value(tas5805m->gpio_pdn_n, 0);
		regulator_disable(tas5805m->pvdd);
		return -ENOMEM;
	}

	/* Determine which EQ controls to add based on mode */
	switch (tas5805m->eq_mode_type) {
	case TAS5805M_EQ_MODE_15BAND:
		eq_controls = tas5805m_snd_controls_eq_15band;
		eq_controls_size = sizeof(tas5805m_snd_controls_eq_15band);
		break;
	case TAS5805M_EQ_MODE_CROSSOVER:
		eq_controls = tas5805m_snd_controls_crossover;
		eq_controls_size = sizeof(tas5805m_snd_controls_crossover);
		break;
	case TAS5805M_EQ_MODE_OFF:
	default:
		eq_controls = NULL;
		eq_controls_size = 0;
		break;
	}

	/* Calculate total number of controls */
	num_controls = ARRAY_SIZE(tas5805m_snd_controls_base);
	if (!tas5805m->mixer_mode_from_dt)
		num_controls += ARRAY_SIZE(tas5805m_snd_controls_mixer);
	if (eq_controls)
		num_controls += eq_controls_size / sizeof(struct snd_kcontrol_new);

	/* Allocate and build control array */
	controls = devm_kmalloc(dev, num_controls * sizeof(struct snd_kcontrol_new), GFP_KERNEL);
	if (!controls) {
		gpiod_set_value(tas5805m->gpio_pdn_n, 0);
		regulator_disable(tas5805m->pvdd);
		return -ENOMEM;
	}

	/* Copy base controls */
	memcpy(controls, tas5805m_snd_controls_base, sizeof(tas5805m_snd_controls_base));
	int offset = ARRAY_SIZE(tas5805m_snd_controls_base);

	/* Add mixer controls if not controlled by device tree */
	if (!tas5805m->mixer_mode_from_dt) {
		memcpy(&controls[offset], tas5805m_snd_controls_mixer, sizeof(tas5805m_snd_controls_mixer));
		offset += ARRAY_SIZE(tas5805m_snd_controls_mixer);
	}

	/* Add EQ or crossover controls if applicable */
	if (eq_controls) {
		memcpy(&controls[offset], eq_controls, eq_controls_size);
	}

	/* Log control registration */
	if (tas5805m->mixer_mode_from_dt && eq_controls)
		dev_dbg(dev, "%s: Registered %d controls (mixer from DT, with %s)\n", 
			__func__, num_controls, tas5805m->eq_mode_type == TAS5805M_EQ_MODE_15BAND ? "15-band EQ" : "crossover");
	else if (tas5805m->mixer_mode_from_dt)
		dev_dbg(dev, "%s: Registered %d controls (mixer from DT)\n", __func__, num_controls);
	else if (eq_controls)
		dev_dbg(dev, "%s: Registered %d controls (with %s)\n", 
			__func__, num_controls, tas5805m->eq_mode_type == TAS5805M_EQ_MODE_15BAND ? "15-band EQ" : "crossover");
	else
		dev_dbg(dev, "%s: Registered %d controls\n", __func__, num_controls);

	/* Build component driver structure */
	soc_codec_dev->controls = controls;
	soc_codec_dev->num_controls = num_controls;
	soc_codec_dev->dapm_widgets = tas5805m_dapm_widgets;
	soc_codec_dev->num_dapm_widgets = ARRAY_SIZE(tas5805m_dapm_widgets);
	soc_codec_dev->dapm_routes = tas5805m_audio_map;
	soc_codec_dev->num_dapm_routes = ARRAY_SIZE(tas5805m_audio_map);
	soc_codec_dev->use_pmdown_time = 1;
	soc_codec_dev->endianness = 1;

	/* Don't register through devm. We need to be able to unregister
	 * the component prior to deasserting PDN#
	 */
	ret = snd_soc_register_component(dev, soc_codec_dev,
					 &tas5805m_dai, 1);
	if (ret < 0) {
		dev_err(dev, "%s: unable to register codec: %d\n", 
			__func__, ret);
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

	dev_dbg(dev, "%s on %s\n", 
		__func__, dev_name(dev));

	cancel_work_sync(&tas5805m->work);
	snd_soc_unregister_component(dev);
	mutex_lock(&tas5805m->lock);
	tas5805m->dsp_initialized = false;
	mutex_unlock(&tas5805m->lock);
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

MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_AUTHOR("Daniel Beer <daniel.beer@igorinstitute.com>");
MODULE_AUTHOR("Andriy Malyshenko <andriy@sonocotta.com>");
MODULE_DESCRIPTION("TAS5805M Audio Amplifier Driver");
MODULE_LICENSE("GPL v2");