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

#include <linux/firmware.h>
#include <linux/slab.h>
#include <sound/soc.h>

#include "pcm9211-reg.h"
#include "tasdevice.h"
#include "tasdevice-rw.h"

void pcm9211s_irq_work_func(struct tasdevice_priv *pcm_dev)
{
	unsigned int i, j, array_size, reg_val;
	unsigned int int_reg_array[] = {
		PCM9211_REG_INT0_OUTPUT,
		PCM9211_REG_INT1_OUTPUT
	};
	int rc;

	tasdevice_enable_irq(pcm_dev, false);

	array_size = ARRAY_SIZE(int_reg_array);
	for (i = 0; i < pcm_dev->ndev; i++) {
		for (j = 0; j < array_size; j++) {
			rc = tasdevice_dev_read(pcm_dev, i,
				int_reg_array[j], &reg_val);
			if (!rc)
				dev_info(pcm_dev->dev, "%s-%d INT STAT REG 0x%04x=0x%02x\n",
					pcm_dev->dev_name, i, int_reg_array[j], reg_val);
			else
				dev_err(pcm_dev->dev, "%s-%d Read Reg 0x%04x rc=%d\n",
					pcm_dev->dev_name, i, int_reg_array[j], rc);
		}
	}
}

int pcm9211s_dev_rst(struct tasdevice_priv *pcm_dev, unsigned int chn)
{
	return tasdevice_dev_update_bits(pcm_dev, chn, PCM9211_REG_SW_CTRL,
		PCM9211_REG_SW_CTRL_MRST_MSK, PCM9211_REG_SW_CTRL_MRST);
}
