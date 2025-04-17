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
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "tasdevice.h"
#include "tasdevice-rw.h"

const char *tas_blocktype[5] = {
	"COEFF",
	"POST_POWER_UP",
	"PRE_SHUTDOWN",
	"PRE_POWER_UP",
	"POST_SHUTDOWN"
};

int tasdevice_process_block(void *context, unsigned char *data,
	unsigned char dev_idx, int sublocksize)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *)context;
	unsigned char subblk_typ = data[1];
	int chn, chnend, rc, i;
	int subblk_offset = 2;

	if(dev_idx) {
		chn = dev_idx - 1;
		chnend = dev_idx;
	} else {
		chn = 0;
		chnend = tas_dev->ndev;
	}

	for (; chn < chnend; chn++) {
		if (tas_dev->tasdevice[chn].is_loading == false)
			continue;

		subblk_offset = 2;
		switch (subblk_typ) {
		case TASDEVICE_CMD_SING_W: {
	/*
	 *		dev_idx		: one byte
	 *		subblk_type	: one byte
	 *		payload_len	: two bytes
	 *		{
	 *			book	: one byte
	 *			page	: one byte
	 *			reg		: one byte
	 *			val		: one byte
	 *		}[payload_len/4]
	 */
			unsigned short len = get_unaligned_be16(&data[2]);

			subblk_offset += 2;
			if (subblk_offset + 4 * len > sublocksize) {
				dev_err(tas_dev->dev,
					"process_block: Out of boundary\n");
				break;
			}

			for (i = 0; i < len; i++) {
				rc = tasdevice_dev_write(tas_dev,chn,
					TASDEVICE_REG(data[subblk_offset],
						data[subblk_offset + 1],
						data[subblk_offset + 2]),
					data[subblk_offset + 3]);
				if (rc < 0)
					dev_err(tas_dev->dev,
						"process_block: "
						"single write error\n");

				subblk_offset += 4;
			}
		}
			break;
		case TASDEVICE_CMD_BURST: {
	/*
	 *		dev_idx		: one byte
	 *		subblk_type : one byte
	 *		payload_len	: two bytes
	 *		book		: one byte
	 *		page		: one byte
	 *		reg		: one byte
	 *		reserve		: one byte
	 *		payload		: payload_len bytes
	 */
			unsigned short len = get_unaligned_be16(&data[2]);

			subblk_offset += 2;
			if (subblk_offset + 4 + len > sublocksize) {
				dev_err(tas_dev->dev,
					"%s: BURST Out of boundary\n",
					__func__);
				break;
			}
			if (len % 4) {
				dev_err(tas_dev->dev, "process_block: Burst "
					"len(%u) can be divided by 4\n", len);
				break;
			}

			rc = tasdevice_dev_bulk_write(tas_dev, chn,
				TASDEVICE_REG(data[subblk_offset],
					data[subblk_offset + 1],
					data[subblk_offset + 2]),
					&(data[subblk_offset + 4]), len);
			if(rc < 0)
				dev_err(tas_dev->dev,
					"%s: bulk_write error = %d\n",
					__func__, rc);

			subblk_offset += (len + 4);
		}
			break;
		case TASDEVICE_CMD_DELAY: {
	/*
	 *		dev_idx		: one byte
	 *		subblk_type : one byte
	 *		delay_time	: two bytes
	 */
			unsigned int sleep_time = 0;

			if (subblk_offset + 2 > sublocksize) {
				dev_err(tas_dev->dev,
					"%s: deley Out of boundary\n",
					__func__);
				break;
			}
			sleep_time = get_unaligned_be16(&data[2]) * 1000;
			usleep_range(sleep_time, sleep_time + 50);
			subblk_offset += 2;
		}
			break;
		case TASDEVICE_CMD_FIELD_W:
	/*
	 *		dev_idx		: one byte
	 *		subblk_type : one byte
	 *		reserve		: one byte
	 *		mask		: one byte
	 *		book		: one byte
	 *		page		: one byte
	 *		reg		: one byte
	 *		reserve		: one byte
	 *		payload		: payload_len bytes
	 */
		if (subblk_offset + 6 > sublocksize) {
			dev_err(tas_dev->dev,
				"%s: bit write Out of boundary\n",
				__func__);
			break;
		}
		rc = tasdevice_dev_update_bits(tas_dev, chn,
			TASDEVICE_REG(data[subblk_offset + 2],
				data[subblk_offset + 3],
				data[subblk_offset + 4]),
				data[subblk_offset + 1],
				data[subblk_offset + 5]);
		if (rc < 0)
			dev_err(tas_dev->dev,
				"process_block: update_bits error = %d\n", rc);
		subblk_offset += 6;
			break;
		default:
			break;
		};
	}
	return subblk_offset;
}

int tasdevice_process_block_show(void *context, unsigned char *data,
	unsigned char dev_idx, int sublocksize, char *buf, ssize_t *length)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *)context;
	unsigned char subblk_typ = data[1];
	int subblk_offset = 2;
	int chn, chnend;

	if(dev_idx) {
		chn = dev_idx - 1;
		chnend = dev_idx;
	} else {
		chn = 0;
		chnend = tas_dev->ndev;
	}

	for(; chn < chnend; chn++) {
		subblk_offset = 2;
		switch (subblk_typ) {
		case TASDEVICE_CMD_SING_W: {
	/*
	 *		dev_idx 	: one byte
	 *		subblk_type : one byte
	 *		payload_len : two bytes
	 *		{
	 *			book	: one byte
	 *			page	: one byte
	 *			reg 	: one byte
	 *			val 	: one byte
	 *		}[payload_len/4]
	 */
			int i = 0;
			unsigned short len = get_unaligned_be16(&data[2]);

			subblk_offset += 2;
			if(*length + 16 < PAGE_SIZE)
				*length += scnprintf(buf + *length,
					PAGE_SIZE - *length,
					"\t\tSINGLE BYTE:\n");
			else {
				*length += scnprintf(buf + PAGE_SIZE - 16,
					16, "\nNo memory!\n");
				break;
			}
			if (subblk_offset + 4 * len > sublocksize) {
				if(*length + 32 < PAGE_SIZE) {
					*length += scnprintf(buf + *length,
						PAGE_SIZE - *length,
						"SING_W: Out of boundary\n");
				} else
					*length += scnprintf(buf + PAGE_SIZE -
						16, 16, "\nNo PAGE_SIZE!\n");
				break;
			}

			for (i = 0; i < len; i++) {
				if(*length + 64 < PAGE_SIZE) {
					*length += scnprintf(buf + *length,
						PAGE_SIZE - *length,
						"\t\t\tBOOK0x%02x "
						"PAGE0x%02x REG0x%02x "
						"VALUE = 0x%02x\n",
						data[subblk_offset],
						data[subblk_offset + 1],
						data[subblk_offset + 2],
						data[subblk_offset + 3]);
					subblk_offset += 4;
				} else {
					*length += scnprintf(buf +
						PAGE_SIZE - 16, 16,
						"\nNo PAGE_SIZE!\n");
					break;
				}
			}
		}
			break;
		case TASDEVICE_CMD_BURST: {
	/*
	 *		dev_idx 	: one byte
	 *		subblk_type : one byte
	 *		payload_len : two bytes
	 *		book		: one byte
	 *		page		: one byte
	 *		reg 	: one byte
	 *		reserve 	: one byte
	 *		payload 	: payload_len bytes
	 */
	 		int i = 0;
			unsigned short len = get_unaligned_be16(&data[2]);
			unsigned char reg = 0;

			subblk_offset += 2;
			if(*length + 16 < PAGE_SIZE)
				*length += scnprintf(buf + *length,
					PAGE_SIZE - *length,
					"\t\tBURST:\n");
			else {
				*length += scnprintf(buf + PAGE_SIZE - 16,
						16, "\nNo PAGE_SIZE!\n");
				break;
			}
			if (subblk_offset + 4 + len > sublocksize) {
				if(*length + 32 < PAGE_SIZE)
					*length += scnprintf(buf + *length,
						PAGE_SIZE - *length,
						"CMD_BURST: Out of memory.\n");
				else
					*length += scnprintf(buf + PAGE_SIZE -
						16, 16, "\nNo PAGE_SIZE!\n");
				break;
			}
			if (len % 4) {
				if(*length + 32 < PAGE_SIZE)
					*length += scnprintf(buf + *length,
						PAGE_SIZE - *length,
						"CMD_BURST: "
						"Burst len is wrong\n");
				else
					*length += scnprintf(buf +
						PAGE_SIZE - 16,
						16, "\nNo PAGE_SIZE!\n");
				break;
			}
			reg = data[subblk_offset + 2];
			if(*length + 32 < PAGE_SIZE) {
				*length += scnprintf(buf + *length,
					PAGE_SIZE - *length,
					"\t\t\tBOOK0x%02x PAGE0x%02x\n",
					data[subblk_offset],
					data[subblk_offset + 1]);
			} else {
				*length += scnprintf(buf + PAGE_SIZE - 16,
						16, "\nNo PAGE_SIZE!\n");
				break;
			}
			subblk_offset += 4;
			for (i = 0; i < len / 4; i++) {
				if(*length + 128 < PAGE_SIZE) {
					*length += scnprintf(buf + *length,
						PAGE_SIZE - *length,
						"\t\t\tREG0x%02x = 0x%02x "
						"REG0x%02x = 0x%02x "
						"REG0x%02x = 0x%02x "
						"REG0x%02x = 0x%02x\n",
						reg + i * 4,
						data[subblk_offset + 0],
						reg + i * 4 + 1,
						data[subblk_offset + 1],
						reg + i * 4 + 2,
						data[subblk_offset + 2],
						reg + i * 4 + 3,
						data[subblk_offset + 3]);
				} else {
					*length += scnprintf(buf +
						PAGE_SIZE - 16, 16,
						"\nNo PAGE_SIZE!\n");
					break;
				}
				subblk_offset += 4;
			}
		}
			break;
		case TASDEVICE_CMD_DELAY: {
	/*
	 *		dev_idx 	: one byte
	 *		subblk_type : one byte
	 *		delay_time	: two bytes
	 */
			unsigned short sleep_time = 0;

			if (subblk_offset + 2 > sublocksize) {
				if(*length + 32 < PAGE_SIZE) {
					*length += scnprintf(buf + *length,
						PAGE_SIZE - *length,
						"CMD_DELAY: Out of boundary\n");
				} else
					*length += scnprintf(buf + PAGE_SIZE -
						16, 16, "\nNo PAGE_SIZE!\n");
				break;
			}
			sleep_time = get_unaligned_be16(&data[2]);
			if(*length + 32 < PAGE_SIZE) {
				*length += scnprintf(buf + *length,
					PAGE_SIZE - *length,
					"\t\tDELAY = %ums\n", sleep_time);
			} else {
				*length += scnprintf(buf + PAGE_SIZE - 16,
					16, "\nNo PAGE_SIZE!\n");
				break;
			}
			subblk_offset += 2;
		}
			break;
		case TASDEVICE_CMD_FIELD_W:
	/*
	 *		dev_idx 	: one byte
	 *		subblk_type : one byte
	 *		reserve 	: one byte
	 *		mask		: one byte
	 *		book		: one byte
	 *		page		: one byte
	 *		reg 	: one byte
	 *		reserve 	: one byte
	 *		payload 	: payload_len bytes
	 */
		if (subblk_offset + 6 > sublocksize) {
			if(*length + 32 < PAGE_SIZE)
				*length += scnprintf(buf + *length,
					PAGE_SIZE - *length,
					"FIELD_W: Out of boundary\n");
			else
				*length += scnprintf(buf + PAGE_SIZE - 16,
					16, "\nNo PAGE_SIZE!\n");
			break;
		}
		if(*length + 32 < PAGE_SIZE)
			*length += scnprintf(buf + *length,
				PAGE_SIZE - *length, "\t\tFIELD:\n");
		else {
			*length += scnprintf(buf + PAGE_SIZE - 16,
				16, "\nNo PAGE_SIZE!\n");
			break;
		}
		if(*length + 64 < PAGE_SIZE)
			*length += scnprintf(buf + *length,
				PAGE_SIZE - *length,
				"\t\t\tBOOK0x%02x PAGE0x%02x REG0x%02x "
				"MASK0x%02x VALUE = 0x%02x\n",
				data[subblk_offset + 2],
				data[subblk_offset + 3],
				data[subblk_offset + 4],
				data[subblk_offset + 1],
				data[subblk_offset + 5]);
		else {
			*length += scnprintf(buf + PAGE_SIZE - 16,
				16, "\nNo PAGE_SIZE!\n");
			break;
		}
		subblk_offset += 6;
			break;
		default:
			break;
		};
	}
	return subblk_offset;
}

void tasdevice_select_cfg_blk(void *context, int conf_no,
	unsigned char block_type)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *) context;
	struct tasdevice_regbin *regbin = &(tas_dev->regbin);
	struct tasdevice_config_info **ci = regbin->cfg_info;
	struct tasdev_blk_data **blk_data;
	int j, k, chn, chnend;

	if (conf_no >= regbin->ncfgs || conf_no < 0 || NULL == ci) {
		dev_err(tas_dev->dev,
			"conf_no should be not more than %u\n",
			regbin->ncfgs);
		goto out;
	} else {
		dev_info(tas_dev->dev,
			"select_cfg_blk: profile_conf_id = %d\n",
			conf_no);
	}

	blk_data =  ci[conf_no]->blk_data;

	for (j = 0; j < (int)ci[conf_no]->real_nblocks; j++) {
		unsigned int length = 0, rc = 0;

		if (block_type > 5 || block_type < 2) {
			dev_err(tas_dev->dev,
				"ERROR!!!block_type should be in "
				"range from 2 to 5\n");
			goto out;
		}
		if (block_type != blk_data[j]->block_type)
			continue;
		dev_info(tas_dev->dev, "select_cfg_blk: conf %d, "
			"block type:%s\t device idx = 0x%02x\n",
			conf_no, tas_blocktype[blk_data[j]->block_type - 1],
			blk_data[j]->dev_idx);

		for (k = 0; k < (int)blk_data[j]->nSublocks; k++) {
			if(blk_data[j]->dev_idx) {
				chn = blk_data[j]->dev_idx - 1;
				chnend = blk_data[j]->dev_idx;
			} else {
				chn = 0;
				chnend = tas_dev->ndev;
			}
			for (; chn < chnend; chn++)
				tas_dev->tasdevice[chn].is_loading = true;

			rc = tasdevice_process_block(tas_dev,
				blk_data[j]->regdata + length,
				blk_data[j]->dev_idx,
				blk_data[j]->block_size - length);
			length += rc;
			if (blk_data[j]->block_size < length) {
				dev_err(tas_dev->dev, "select_cfg_blk: "
					"ERROR:%u %u out of boundary\n",
					length, blk_data[j]->block_size);
				break;
			}
		}
		if (length != blk_data[j]->block_size)
			dev_err(tas_dev->dev,"select_cfg_blk: ERROR: "
				"%u %u size is not same\n",
				length, blk_data[j]->block_size);
	}

out:
	return;
}

static struct tasdevice_config_info *tasdevice_add_config(
	void *context, unsigned char *config_data,
	unsigned int config_size, int *status)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *)context;
	struct tasdevice_config_info *cfg_info;
	struct tasdev_blk_data **bk_da;
	int config_offset = 0;
	int i;

	cfg_info = kzalloc(sizeof(struct tasdevice_config_info), GFP_KERNEL);
	if (!cfg_info) {
		*status = -ENOMEM;
		goto out;
	}

	if (tas_dev->regbin.fw_hdr.binary_version_num >= 0x105) {
		if (config_offset + 64 > (int)config_size) {
			dev_err(tas_dev->dev, "add config: Out of boundary\n");
			goto out;
		}
		memcpy(cfg_info->name, &config_data[config_offset], 64);
		config_offset += 64;
	}

	if (config_offset + 4 > (int)config_size) {
		*status = -EINVAL;
		dev_err(tas_dev->dev, "add config: Out of boundary\n");
		goto out;
	}
	/* convert data[offset], data[offset + 1], data[offset + 2] and
	 * data[offset + 3] into host
	 */
	cfg_info->nblocks =
		get_unaligned_be32(&config_data[config_offset]);
	config_offset +=  4;

	bk_da = cfg_info->blk_data = kcalloc(cfg_info->nblocks,
		sizeof(struct tasdev_blk_data *), GFP_KERNEL);
	if (!bk_da) {
		*status = -ENOMEM;
		goto out;
	}
	cfg_info->real_nblocks = 0;
	for (i = 0; i < (int)cfg_info->nblocks; i++) {
		if (config_offset + 12 > config_size) {
			dev_err(tas_dev->dev,
				"%s:: Out of boundary: i = %d nblocks = %u!\n",
				__func__, i, cfg_info->nblocks);
			break;
		}
		bk_da[i] = kzalloc(sizeof(struct tasdev_blk_data), GFP_KERNEL);
		if (!bk_da[i]) {
			*status = -ENOMEM;
			break;
		}
		bk_da[i]->dev_idx = config_data[config_offset];
		config_offset++;

		bk_da[i]->block_type = config_data[config_offset];
		config_offset++;

		if(bk_da[i]->block_type == TASDEVICE_BIN_BLK_PRE_POWER_UP) {
			if(0 == bk_da[i]->dev_idx)
				cfg_info->active_dev = 1;
			else
				cfg_info->active_dev =
					(1 << (bk_da[i]->dev_idx - 1));
		}
		bk_da[i]->yram_checksum =
			get_unaligned_be16(&config_data[config_offset]);
		config_offset += 2;
		bk_da[i]->block_size =
			get_unaligned_be32(&config_data[config_offset]);
		config_offset += 4;

		bk_da[i]->nSublocks =
			get_unaligned_be32(&config_data[config_offset]);

		config_offset += 4;

		if (config_offset + bk_da[i]->block_size > config_size) {
			*status = -EINVAL;
			dev_err(tas_dev->dev,
				"%s: Out of boundary: i = %d blks = %u!\n",
				__func__, i, cfg_info->nblocks);
			break;
		}
		/* instead of kzalloc+memcpy */
		bk_da[i]->regdata = kmemdup(&config_data[config_offset],
			bk_da[i]->block_size, GFP_KERNEL);
		if (!bk_da[i]->regdata) {
			*status = -ENOMEM;
			goto out;
		}

		config_offset += bk_da[i]->block_size;
		cfg_info->real_nblocks += 1;
	}
out:
	return cfg_info;
}

void tasdevice_regbin_ready(const struct firmware *fmw, void *context)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *) context;
	struct tasdevice_config_info **cfg_info;
	struct tasdevice_regbin_hdr *fw_hdr;
	struct tasdevice_regbin *regbin;
	unsigned int total_config_sz = 0;
	unsigned char *buf;
	int offset = 0;
	int ret = 0;
	int i;

	if(tas_dev == NULL) {
		dev_err(tas_dev->dev,
			"tasdev: regbin_ready: handle is NULL\n");
		return;
	}
	mutex_lock(&tas_dev->codec_lock);
	regbin = &(tas_dev->regbin);
	fw_hdr = &(regbin->fw_hdr);
	if (!fmw || !fmw->data) {
		dev_err(tas_dev->dev,
			"Failed to read %s, no side-effect on driver running\n",
			tas_dev->regbin_binaryname);
		ret = -1;
		goto out;
	}
	buf = (unsigned char *)fmw->data;

	dev_info(tas_dev->dev, "tasdev: regbin_ready start\n");
	fw_hdr->img_sz = get_unaligned_be32(&buf[offset]);
	offset += 4;
	if (fw_hdr->img_sz != fmw->size) {
		dev_err(tas_dev->dev,
			"File size not match, %d %u", (int)fmw->size,
			fw_hdr->img_sz);
		ret = -1;
		goto out;
	}

	fw_hdr->checksum = get_unaligned_be32(&buf[offset]);
	offset += 4;
	fw_hdr->binary_version_num = get_unaligned_be32(&buf[offset]);
	if(fw_hdr->binary_version_num < 0x103) {
		dev_err(tas_dev->dev,
			"File version 0x%04x is too low",
			fw_hdr->binary_version_num);
		ret = -1;
		goto out;
	}
	offset += 4;
	fw_hdr->drv_fw_version = get_unaligned_be32(&buf[offset]);
	offset += 4;
	fw_hdr->timestamp = get_unaligned_be32(&buf[offset]);
	offset += 4;
	fw_hdr->plat_type = buf[offset];
	offset += 1;
	fw_hdr->dev_family = buf[offset];
	offset += 1;
	fw_hdr->reserve = buf[offset];
	offset += 1;
	fw_hdr->ndev = buf[offset];
	offset += 1;
	if(fw_hdr->ndev != tas_dev->ndev) {
		dev_err(tas_dev->dev, "ndev(%u) from Regbin and ndev(%u)"
			"from DTS does not match\n", fw_hdr->ndev,
			tas_dev->ndev);
		ret = -1;
		goto out;
	}
	if (offset + TASDEVICE_DEVICE_SUM > fw_hdr->img_sz) {
		dev_err(tas_dev->dev,
			"regbin_ready: Out of boundary!\n");
		ret = -1;
		goto out;
	}

	for (i = 0; i < TASDEVICE_DEVICE_SUM; i++, offset++)
		fw_hdr->devs[i] = buf[offset];

	fw_hdr->nconfig = get_unaligned_be32(&buf[offset]);
	offset += 4;
	dev_info(tas_dev->dev, "nconfig = %u\n", fw_hdr->nconfig);
	for (i = 0; i < TASDEVICE_CONFIG_SUM; i++) {
		fw_hdr->config_size[i] = get_unaligned_be32(&buf[offset]);
		offset += 4;
		total_config_sz += fw_hdr->config_size[i];
	}
	dev_info(tas_dev->dev,
		"img_sz = %u total_config_sz = %u offset = %d\n",
		fw_hdr->img_sz, total_config_sz, offset);
	if (fw_hdr->img_sz - total_config_sz != (unsigned int)offset) {
		dev_err(tas_dev->dev, "Bin file error!\n");
		ret = -1;
		goto out;
	}
	cfg_info = kcalloc(fw_hdr->nconfig, sizeof(* cfg_info), GFP_KERNEL);
	if (!cfg_info) {
		ret = -ENOMEM;
		goto out;
	}
	regbin->cfg_info = cfg_info;
	regbin->ncfgs = 0;
	for (i = 0; i < (int)fw_hdr->nconfig; i++) {
		cfg_info[i] = tasdevice_add_config(context, &buf[offset],
				fw_hdr->config_size[i], &ret);
		if (ret)
			goto out;
		offset += (int)fw_hdr->config_size[i];
		regbin->ncfgs += 1;
	}

	tasdevice_create_controls(tas_dev);

	// init
	for (i = 0; i < tas_dev->regbin.ncfgs; i++)
		if (strstr(tas_dev->regbin.cfg_info[i]->name, "init")) {
			tasdevice_select_cfg_blk(tas_dev, i,
				TASDEVICE_BIN_BLK_PRE_POWER_UP);
			dev_info(tas_dev->dev, "init id = %d!\n", i);
			break;
		}

out:
	if (ret)
		/*If FW loding fails, kcontrol won't be created */
		tasdevice_config_info_remove(tas_dev);

	mutex_unlock(&tas_dev->codec_lock);
	if (fmw) release_firmware(fmw);
	dev_info(tas_dev->dev, "Firmware init complete\n");
}

void tasdevice_config_info_remove(void *context)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *)context;
	struct tasdevice_regbin *regbin = &(tas_dev->regbin);
	struct tasdevice_config_info **ci = regbin->cfg_info;
	int i, j;

	if (!ci)
		return;

	for (i = 0; i < regbin->ncfgs; i++) {
		if (!ci[i])
			continue;
		if (ci[i]->blk_data) {
			for (j = 0; j < (int)ci[i]->real_nblocks; j++) {
				if (!ci[i]->blk_data[j])
					continue;
				kfree(ci[i]->blk_data[j]->regdata);
				kfree(ci[i]->blk_data[j]);
			}
			kfree(ci[i]->blk_data);
		}
		kfree(ci[i]);
	}
	kfree(ci);
}
