// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TASDEVICE Audio Smart Amplifier
//
// Copyright (C) 2022 - 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2563/TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2563/TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
//

#ifndef __TASDEVICE_H__
#define __TASDEVICE_H__
#include "tasdevice-regbin.h"

#define TASDEVICE_ASYNC_POWER_CONTROL

#define MAX_DEV_NUM			4
#define TASDEVICE_NAME_LEN	16
#define TASDEVICE_REGBIN_FILENAME_LEN	32

#define TASDEVICE_I2C_RETRY_COUNT	3
#define TASDEVICE_ERROR_I2C_FAILED	-2

#define TASDEVICE_RATES			(SNDRV_PCM_RATE_44100 |\
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_88200)
#define TASDEVICE_MAX_CHANNELS		8
#define TASDEVICE_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

/* PAGE Control Register (available in page0 of each book) */
#define TASDEVICE_PAGE_SELECT		(0x00)
#define TASDEVICE_BOOKCTL_PAGE		(0x00)
#define TASDEVICE_BOOKCTL_REG		(127)
#define TASDEVICE_BOOK_ID(reg)		(reg / (256 * 128))
#define TASDEVICE_PAGE_ID(reg)		((reg % (256 * 128)) / 128)
#define TASDEVICE_PAGE_REG(reg)		((reg % (256 * 128)) % 128)
#define TASDEVICE_PGRG(reg)		((reg % (256 * 128)))
#define TASDEVICE_REG(book, page, reg)	(((book * 256 * 128) + \
					(page * 128)) + reg)
#define TASDEVICE_REG_SWRESET		TASDEVICE_REG(0X0, 0X0, 0x01)
#define TASDEVICE_REG_SWRESET_RESET	(0x1 << 0)

enum audio_device {
	PCM9211 = 0,
	TAS2020,
	TAS2110,
	TAS2118,
	TAS2120,
	TAS2320,
	TAS2560,
	TAS2562,
	TAS2564,
	TAS257X,
	TAS2764,
	TAS2770,
	TAS2780,
	TAS5802,
	TAS5805,
	TAS5806M,
	/* TAS5806M with Headphone */
	TAS5806MD,
	TAS5815,
	TAS5822,
	TAS5825M,
	TAS5825P,
	TAS5827,
	TAS5828,
	MAX_DEVICE
};

struct tas_syscmd {
	bool is_cmderr;
	unsigned char bk;
	unsigned char pg;
	unsigned char rg;
	unsigned short buflen;
	unsigned int cur_chl;
};

struct tasdevice {
	unsigned int dev_id;
	unsigned int addr;
	unsigned char book;
	bool is_loading;
};

enum syscmds {
	regset_cmd	= 0,
	regdump_cmd	= 1,
	regcfglst_cmd	= 2,
	max_cmd
};

struct tas_control {
	struct snd_kcontrol_new *tasdevice_profile_controls;
	int nr_controls;
};

struct tasdevice_irqinfo {
	struct delayed_work irq_work;
	int irq_gpio;
	int irq;
	bool irq_enable;
};

struct tasdevice_priv {
	struct snd_soc_component *component;
	struct i2c_client *client;
	struct regmap *regmap;
	struct device *dev;
	struct gpio_desc *rst_gpio;
	struct mutex dev_lock;
	struct mutex codec_lock;
	struct tasdevice tasdevice[MAX_DEV_NUM];
	struct tas_syscmd sys_cmd[max_cmd];
	struct tasdevice_regbin regbin;
	struct tas_control tas_ctrl;
	struct tasdevice_irqinfo irqinfo;
	unsigned int chip_id;
	int cur_conf;
	int pstream;
	int cstream;
	unsigned char dev_name[TASDEVICE_NAME_LEN];
	unsigned char regbin_binaryname[TASDEVICE_REGBIN_FILENAME_LEN];
	unsigned char ndev;
#ifdef TASDEVICE_ASYNC_POWER_CONTROL
	struct delayed_work powercontrol_work;
#endif
	bool is_runtime_suspend;
	void (*irq_work_func)(struct tasdevice_priv *tas_dev);
	void (*hwrst)(struct gpio_desc *desc);
	int (*dev_swrst)(struct tasdevice_priv *tas_dev, unsigned int chn);
};

int tasdevice_create_controls(struct tasdevice_priv *tas_dev);

void tasdevice_enable_irq(struct tasdevice_priv *tas_dev, bool enable);

void tas2780_irq_work_func(struct tasdevice_priv *tas_dev);
void tas2770_irq_work_func(struct tasdevice_priv *tas_dev);
void tas257x_irq_work_func(struct tasdevice_priv *tas_dev);
void tas2564_irq_work_func(struct tasdevice_priv *tas_dev);
void tas2560_irq_work_func(struct tasdevice_priv *tas_dev);
void pcm9211s_irq_work_func(struct tasdevice_priv *pcm_dev);
int pcm9211s_dev_rst(struct tasdevice_priv *pcm_dev, unsigned int chn);

#endif /* __TASDEVICE_H__ */
