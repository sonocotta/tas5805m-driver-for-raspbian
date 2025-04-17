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

#ifndef __TAS257X_REG_H__
#define __TAS257X_REG_H__

#define TAS257X_REG_INT_LTCH0	TASDEVICE_REG(0X0, 0x0, 0x60)
#define TAS257X_REG_INT_LTCH1	TASDEVICE_REG(0X0, 0x0, 0x61)
#define TAS257X_REG_INT_LTCH2	TASDEVICE_REG(0X0, 0x0, 0x62)
#define TAS257X_REG_INT_LTCH3	TASDEVICE_REG(0X0, 0x0, 0x63)
#define TAS257X_REG_INT_LTCH4	TASDEVICE_REG(0X0, 0x0, 0x64)

#endif
