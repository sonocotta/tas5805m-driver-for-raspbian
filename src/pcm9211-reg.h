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

#ifndef __PCM9211_REG_H__
#define __PCM9211_REG_H__

#define PCM9211_REG_INT0_OUTPUT	TASDEVICE_REG(0X0, 0X0, 0x2c)
#define PCM9211_REG_INT1_OUTPUT	TASDEVICE_REG(0X0, 0X0, 0x2d)
#define PCM9211_REG_SW_CTRL		TASDEVICE_REG(0X0, 0X0, 0x40)
#define PCM9211_REG_SW_CTRL_MRST_MSK	BIT(7)
#define PCM9211_REG_SW_CTRL_MRST	0x0

#endif
