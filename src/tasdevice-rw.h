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

#ifndef __TASDEVICE_RW_H__
#define __TASDEVICE_RW_H__
int tasdevice_dev_read(struct tasdevice_priv *tasdev,
	unsigned int chn, unsigned int reg, unsigned int *val);

int tasdevice_dev_write(struct tasdevice_priv *tasdev,
	unsigned int chn, unsigned int reg, unsigned int value);

int tasdevice_dev_bulk_write(
	struct tasdevice_priv *tasdev, unsigned int chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length);

int tasdevice_dev_bulk_read(struct tasdevice_priv *tasdev,
	unsigned int chn, unsigned int reg, unsigned char *p_data,
	unsigned int n_length);

int tasdevice_dev_update_bits(
	struct tasdevice_priv *tasdev, unsigned int chn,
	unsigned int reg, unsigned int mask, unsigned int value);
#endif
