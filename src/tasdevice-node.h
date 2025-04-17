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

#ifndef __TASDEVICE_NODE_H__
#define __TASDEVICE_NODE_H__

ssize_t tas_active_address_show(struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t tas_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t tas_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t tas_regdump_show(struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t tas_regdump_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t tas_regbininfo_list_show(struct device *dev,
				 struct device_attribute *attr, char *buf);
ssize_t tas_regcfg_list_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t tas_regcfg_list_show(struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t tas_fwload_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
#endif
