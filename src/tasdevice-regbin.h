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

#ifndef __TASDEVICE_REGBIN_H__
#define __TASDEVICE_REGBIN_H__

#define TASDEVICE_CONFIG_SUM  (64)
#define TASDEVICE_DEVICE_SUM  (8)

#define TASDEVICE_CMD_SING_W  (0x1)
#define TASDEVICE_CMD_BURST  (0x2)
#define TASDEVICE_CMD_DELAY  (0x3)
#define TASDEVICE_CMD_FIELD_W  (0x4)

enum tasdevice_bin_blk_type {
	TASDEVICE_BIN_BLK_COEFF = 1,
	TASDEVICE_BIN_BLK_POST_POWER_UP,
	TASDEVICE_BIN_BLK_PRE_SHUTDOWN,
	TASDEVICE_BIN_BLK_PRE_POWER_UP,
	TASDEVICE_BIN_BLK_POST_SHUTDOWN
};

struct tasdevice_regbin_hdr {
	unsigned int img_sz;
	unsigned int checksum;
	unsigned int binary_version_num;
	unsigned int drv_fw_version;
	unsigned int timestamp;
	unsigned char plat_type;
	unsigned char dev_family;
	unsigned char reserve;
	unsigned char ndev;
	unsigned char devs[TASDEVICE_DEVICE_SUM];
	unsigned int nconfig;
	unsigned int config_size[TASDEVICE_CONFIG_SUM];
};

struct tasdev_blk_data {
	unsigned char dev_idx;
	unsigned char block_type;
	unsigned short yram_checksum;
	unsigned int block_size;
	unsigned int nSublocks;
	unsigned char *regdata;
};

struct tasdevice_config_info {
	unsigned char active_dev;
	char name[64];
	unsigned int nblocks;
	unsigned int real_nblocks;
	struct tasdev_blk_data **blk_data;
};

struct tasdevice_regbin {
	struct tasdevice_regbin_hdr fw_hdr;
	struct tasdevice_config_info **cfg_info;
	int ncfgs;
};

void tasdevice_regbin_ready(const struct firmware *fW,
	void *context);
void tasdevice_config_info_remove(void *context);
void tasdevice_select_cfg_blk(void *context, int conf_no,
	unsigned char block_type);
int tasdevice_process_block(void *context,
	unsigned char *data, unsigned char dev_idx, int sublocksize);
int tasdevice_process_block_show(void *context,
	unsigned char *data, unsigned char dev_idx, int sublocksize,
	char *buf, ssize_t *len);
#endif
