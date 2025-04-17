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

#include "tas2770-reg.h"
#include "tasdevice.h"
#include "tasdevice-rw.h"

void tas2770_irq_work_func(struct tasdevice_priv *tas_dev)
{
	unsigned int reg_val, array_size, i, j;
	unsigned int int_reg_array[]= {
		TAS2770_REG_INT_LTCH0,
		TAS2770_REG_INT_LTCH1
	};
	int rc;

	tasdevice_enable_irq(tas_dev, false);

	array_size = ARRAY_SIZE(int_reg_array);

	for(i = 0; i < tas_dev->ndev; i++) {
		for(j = 0; j < array_size; j++) {
			rc = tasdevice_dev_read(tas_dev,
				i, int_reg_array[j], &reg_val);
			if(!rc)
				dev_info(tas_dev->dev, "%s-%d INT STAT REG 0x%04x=0x%02x\n",
					tas_dev->dev_name, i, int_reg_array[j], reg_val);
			else
				dev_err(tas_dev->dev, "%s-%d Read Reg 0x%04x rc=%d\n",
					tas_dev->dev_name, i, int_reg_array[j], rc);
		}
	}
}
