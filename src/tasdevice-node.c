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
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <sound/soc.h>

#include "tasdevice.h"
#include "tasdevice-node.h"
#include "tasdevice-rw.h"

extern const char *tas_blocktype[5];
extern const char *regbin_name;

static char sys_cmd_log[max_cmd][256];

ssize_t tas_active_address_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	unsigned short addr;
	const int size = 32;
	int n = 0;

	if(tas_dev != NULL) {
		struct i2c_client *client =
			(struct i2c_client *)tas_dev->client;
		addr = client->addr;
		n += scnprintf(buf, size,
			"Active SmartPA-0x%02x\n", addr);
	} else {
		n += scnprintf(buf, size, "Invalid data\n");
	}

	return n;
}

ssize_t tas_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	const int size = PAGE_SIZE;
	ssize_t len = 0;
	int data, ret;

	if( tas_dev != NULL) {
		struct tas_syscmd *sys_cmd = &tas_dev->sys_cmd[regset_cmd];

		if(sys_cmd->is_cmderr == true) {
			len += scnprintf(buf, sys_cmd->buflen,
				sys_cmd_log[regset_cmd]);
			goto out;
		}
		//15 bytes
		len += scnprintf(buf + len, size - len, "i2c-addr: 0x%02x\n",
			tas_dev->tasdevice[sys_cmd->cur_chl].addr);
		//2560 bytes

		ret = tasdevice_dev_read(tas_dev,
			sys_cmd->cur_chl,
			TASDEVICE_REG(sys_cmd->bk, sys_cmd->pg, sys_cmd->rg),
			&data);
		if (ret < 0) {
			len += scnprintf(buf, size - len,
				"[tasdevice]reg_show: read register failed\n");
			goto out;
		}
		//20 bytes
		if(len + 25 <= size)
			len += scnprintf(buf + len, size - len,
				"Chn%dB0x%02xP0x%02xR0x%02x:0x%02x\n",
				sys_cmd->cur_chl, sys_cmd->bk,
				sys_cmd->pg, sys_cmd->rg, data);
		else {
			scnprintf(buf + PAGE_SIZE - 100, 100,
				"[tasdevice]reg_show: mem is not enough: "
				"PAGE_SIZE = %lu\n", PAGE_SIZE);
			len = PAGE_SIZE;
		}

	}
out:
	return len;
}

ssize_t tas_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	struct tas_syscmd *sys_cmd;
	unsigned char kbuf[5];
	int ret;

	if( tas_dev == NULL) {
		pr_err("%s:handle is NULL\n", __func__);
		return count;
	}
	dev_info(tas_dev->dev, "reg: count = %ld\n", (long)count);
	sys_cmd = &tas_dev->sys_cmd[regset_cmd];
	sys_cmd->buflen = snprintf(sys_cmd_log[regset_cmd], 256,
		"command: echo chn 0xBK 0xPG 0xRG 0xXX > NODE\n"
		"chn is channel no, should be 1-digital;"
		"BK, PG, RG & XX must be 2-digital HEX\n"
		"eg: echo 0 0x00 0x00 0x2 0xE1 > NODE\n\r");

	if (count < 20) {
		sys_cmd->is_cmderr = true;
		dev_err(tas_dev->dev, "[tasdevice]reg: count error.\n");
		goto out;
	}

	ret = sscanf(buf, "%hd 0x%hhx 0x%hhx 0x%hhx 0x%hhx",
		(unsigned short *)&kbuf[0], &kbuf[1], &kbuf[2],
		&kbuf[3], &kbuf[4]);
	if (!ret) {
		sys_cmd->is_cmderr = true;
		goto out;
	}
	dev_info(tas_dev->dev,
		"[tasdevice]reg: chn=%d, book=0x%02x page=0x%02x "
		"reg=0x%02x val=0x%02x, cnt=%d\n", kbuf[0], kbuf[1],
		kbuf[2], kbuf[3], kbuf[4], (int)count);

	if (kbuf[0]  >= tas_dev->ndev) {
		sys_cmd->is_cmderr = true;
		sys_cmd->buflen += snprintf(sys_cmd_log[regset_cmd],
			20, "Channel no err!\n\r");
		goto out;
	}

	if (kbuf[3] & 0x80) {
		sys_cmd->is_cmderr = true;
		sys_cmd->buflen += snprintf(sys_cmd_log[regset_cmd],
			46, "Register NO is larger than 0x7F!\n\r");
		goto out;
	}

	ret = tasdevice_dev_write(tas_dev, kbuf[0],
		TASDEVICE_REG(kbuf[1], kbuf[2], kbuf[3]), kbuf[4]);
	if (ret < 0)
		sys_cmd->buflen += snprintf(sys_cmd_log[regset_cmd],
			256 - sys_cmd->buflen, "[tasdevice]reg: "
			"write chn%dB0x%02xP0x%02xR0x%02x failed.\n\r",
			kbuf[0], kbuf[1], kbuf[2], kbuf[3]);
	else {
		sys_cmd->is_cmderr = false;
		sys_cmd->cur_chl = kbuf[0];
		sys_cmd->bk = kbuf[1];
		sys_cmd->pg = kbuf[2];
		sys_cmd->rg = kbuf[3];
		sys_cmd_log[regset_cmd][0] = '\0';
		sys_cmd->buflen = 0;
	}

out:
	return count;
}

ssize_t tas_regdump_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	struct tas_syscmd *sys_cmd;
	const int size = PAGE_SIZE;
	int i, data, n_result;
	ssize_t len = 0;

	if (!tas_dev)
		goto out;

	sys_cmd = &tas_dev->sys_cmd[regdump_cmd];

	if(sys_cmd->is_cmderr == true) {
		len = scnprintf(buf, PAGE_SIZE, sys_cmd_log[regdump_cmd]);
		goto out;
	}
	//20 bytes
	if(len + 20 <= size)
		len += scnprintf(buf + len, size - len,
			"i2c-addr: 0x%02x\n\r",
			tas_dev->tasdevice[sys_cmd->cur_chl].addr);
	else {
		scnprintf(buf + PAGE_SIZE - 64, 64,
			"[tasdevice]regdump: mem is not enough: "
			"PAGE_SIZE = %lu\n", PAGE_SIZE);
		len = PAGE_SIZE;
	}

	if(len + 30 <= size)
		len += scnprintf(buf + len, size - len, "DeviceID: 0x%02x\n\r",
			tas_dev->tasdevice[sys_cmd->cur_chl].dev_id);
	else {
		scnprintf(buf + PAGE_SIZE - 64, 64,
			"[tasdevice]regdump: mem is not enough: "
			"PAGE_SIZE = %lu\n", PAGE_SIZE);
		len = PAGE_SIZE;
	}
	//2560 bytes
	for (i = 0; i < 128; i++) {
		n_result = tasdevice_dev_read(tas_dev,
			sys_cmd->cur_chl,
			TASDEVICE_REG(sys_cmd->bk,
			sys_cmd->pg, i), &data);
		if (n_result < 0) {
			len += scnprintf(buf + len, size - len,
				"[tasdevice]regdump: "
				"read register failed!\n\r");
			break;
		}
		//20 bytes
		if(len + 20 <= size)
			len += scnprintf(buf + len, size - len,
				"Chn%dB0x%02xP0x%02xR0x%02x:0x%02x\n",
				sys_cmd->cur_chl, sys_cmd->bk,
				sys_cmd->pg, i, data);
		else {
			scnprintf(buf + PAGE_SIZE - 64, 64,
				"[tasdevice]regdump: "
				"mem is not enough: "
				"PAGE_SIZE = %lu\n", PAGE_SIZE);
			len = PAGE_SIZE;
			break;
		}
	}
	if (len + 40 <= size)
		len += scnprintf(buf + len, size - len,
			"======caught smartpa reg end ======\n\r");

out:
	return len;
}

ssize_t tas_regdump_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	struct tas_syscmd *sys_cmd = NULL;
	int ret = 0;
	unsigned char kbuf[3];

	if( tas_dev == NULL) {
		pr_err("%s:handle is NULL\n", __func__);
		return count;
	}
	dev_info(tas_dev->dev, "regdump: count = %ld\n", (long)count);
	sys_cmd = &tas_dev->sys_cmd[regdump_cmd];
	sys_cmd->buflen = snprintf(sys_cmd_log[regdump_cmd],
		256, "command: echo chn 0xBK 0xPG > NODE\n"
		"chn is channel no, 1-digital; "
		"BK & PG must be 2-digital HEX\n"
		"PG must be 2-digital HEX and less than or equeal to 4.\n"
		"eg: echo 0x00\n\r");

	if (count > 9) {
		ret = sscanf(buf, "%hd 0x%hhx 0x%hhx",
			(unsigned short *)&kbuf[0], &kbuf[1],&kbuf[2]);
		if (!ret) {
			sys_cmd->is_cmderr = true;
			sys_cmd->buflen += snprintf(sys_cmd_log[regdump_cmd],
				20, "Command err!\n\r");
			goto out;
		}

		if(kbuf[0] >= tas_dev->ndev) {
			sys_cmd->is_cmderr = true;
			sys_cmd->buflen += snprintf(sys_cmd_log[regdump_cmd],
				20, "Channel no err!\n\r");
			goto out;
		}

		sys_cmd->is_cmderr = false;
		sys_cmd->cur_chl = kbuf[0];
		sys_cmd->bk = kbuf[1];
		sys_cmd->pg = kbuf[2];
		sys_cmd_log[regdump_cmd][0] = '\0';
		sys_cmd->buflen = 0;
	} else {
		sys_cmd->is_cmderr = true;
		sys_cmd->buflen += snprintf(sys_cmd_log[regdump_cmd],
			30, "Input params must be 3!\n\r");
		dev_err(tas_dev->dev, "[regdump] count error.\n");
	}
out:
	return count;
}

ssize_t tas_regbininfo_list_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
  	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	struct tasdevice_regbin *regbin = &(tas_dev->regbin);
	struct tasdevice_config_info **cfg_info = regbin->cfg_info;
	int n = 0, i = 0;

	if(tas_dev == NULL) {
		if(n + 42 < PAGE_SIZE)
			n += scnprintf(buf + n, PAGE_SIZE - n,
				"ERROR: Can't find tasdevice_priv handle!\n\r");
		else {
			scnprintf(buf  +PAGE_SIZE - 64, 64,
				"\n[regbininfo] Out of boundary!\n\r");
			n = PAGE_SIZE;
		}
		return n;
	}
	mutex_lock(&tas_dev->codec_lock);
	if(n + 128 < PAGE_SIZE) {
		n += scnprintf(buf + n, PAGE_SIZE - n,
			"Regbin File Version: 0x%04X ",
			regbin->fw_hdr.binary_version_num);
		if (regbin->fw_hdr.binary_version_num < 0x105)
			n += scnprintf(buf + n, PAGE_SIZE - n,
				"No confname in this version");
		n += scnprintf(buf + n, PAGE_SIZE - n, "\n\r");
	} else {
		scnprintf(buf + PAGE_SIZE - 64, 64,
			"\n[regbininfo] Out of boundary!\n\r");
		n = PAGE_SIZE;
		goto out;
	}

	for(i = 0; i < regbin->ncfgs; i++) {
		if(n + 16 < PAGE_SIZE)
			n += scnprintf(buf + n, PAGE_SIZE - n, "conf %02d", i);
		else {
			scnprintf(buf + PAGE_SIZE - 64, 64,
				"\n[regbininfo] Out of boundary!\n\r");
			n = PAGE_SIZE;
			break;
		}
		if (regbin->fw_hdr.binary_version_num >= 0x105) {
			if(n + 100 < PAGE_SIZE)
				n += scnprintf(buf + n, PAGE_SIZE - n,
					": %s\n\r", cfg_info[i]->name);
			else {
				scnprintf(buf + PAGE_SIZE - 64, 64,
					"\n[regbininfo] Out of boundary!\n\r");
				n = PAGE_SIZE;
				break;
			}
		} else
			n += scnprintf(buf + n, PAGE_SIZE - n, "\n\r");
	}
out:
	mutex_unlock(&tas_dev->codec_lock);
	return n;
}

ssize_t tas_regcfg_list_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	struct tasdevice_regbin *regbin = NULL;
	int ret = 0;
	struct tas_syscmd *sys_cmd = NULL;

	if(!tas_dev)
		return count;
	mutex_lock(&tas_dev->codec_lock);
	regbin = &(tas_dev->regbin);
	sys_cmd = &tas_dev->sys_cmd[regcfglst_cmd];
	sys_cmd->buflen = snprintf(sys_cmd_log[regcfglst_cmd],
		256, "command: echo CG > NODE\n"
		"CG is conf NO, it should be 2-digital decimal\n"
		"eg: echo 01 > NODE\n\r");

	if (count >= 1) {
		ret = sscanf(buf, "%hhd", &(sys_cmd->pg));
		if (!ret) {
			sys_cmd->is_cmderr = true;
			goto out;
		}
		dev_info(tas_dev->dev, "[regcfg_list]cfg=%2d, cnt=%d\n",
			sys_cmd->pg, (int)count);
		if(sys_cmd->pg >= (unsigned char)regbin->ncfgs) {
			sys_cmd->is_cmderr = true;
			sys_cmd->buflen += snprintf(sys_cmd_log[regcfglst_cmd],
				30, "Wrong conf NO!\n\r");
		} else {
			sys_cmd->is_cmderr = false;
			sys_cmd_log[regcfglst_cmd][0] = '\0';
			sys_cmd->buflen = 0;
		}
	} else {
		sys_cmd->is_cmderr = true;
		dev_err(tas_dev->dev, "[regcfg_list]: count error.\n");
	}
out:
	mutex_unlock(&tas_dev->codec_lock);
	return count;
}

ssize_t tas_regcfg_list_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	ssize_t len = 0;
	int j, k;

	if (tas_dev) {
		struct tas_syscmd *sys_cmd = &tas_dev->sys_cmd[regcfglst_cmd];
		struct tasdevice_regbin *regbin = &(tas_dev->regbin);
		struct tasdevice_config_info **ci = regbin->cfg_info;
		struct tasdev_blk_data **bk_da;

		mutex_lock(&tas_dev->codec_lock);
		if(sys_cmd->is_cmderr == true ||
			sys_cmd->pg >= regbin->ncfgs) {
			len += scnprintf(buf, sys_cmd->buflen,
				sys_cmd_log[regcfglst_cmd]);
			goto out;
		}

		len += scnprintf(buf + len, PAGE_SIZE - len, "Conf %02d",
			sys_cmd->pg);
		if (regbin->fw_hdr.binary_version_num >= 0x105) {
			if (len + 100 < PAGE_SIZE) {
				len += scnprintf(buf + len, PAGE_SIZE - len,
					": %s\n\r", ci[sys_cmd->pg]->name);
			} else {
				scnprintf(buf + PAGE_SIZE - 64, 64,
					"\n[tasdevice-regcfg_list] "
					"Out of boundary!\n\r");
				len = PAGE_SIZE;
				goto out;
			}
		} else
			len += scnprintf(buf + len, PAGE_SIZE - len, "\n\r");

		bk_da = ci[sys_cmd->pg]->blk_data;
		for (j = 0; j < (int)ci[sys_cmd->pg]->real_nblocks;
			j++) {
			unsigned int length = 0;
			unsigned rc = 0;

			len += scnprintf(buf + len, PAGE_SIZE - len,
				"block type:%s\t device idx = 0x%02x\n",
				tas_blocktype[bk_da[j]->block_type - 1],
				bk_da[j]->dev_idx);
			for (k = 0; k < (int)bk_da[j]->nSublocks; k++) {
				rc = tasdevice_process_block_show(tas_dev,
					bk_da[j]->regdata + length,
					bk_da[j]->dev_idx,
					bk_da[j]->block_size - length,
					buf, &len);
				length += rc;
				if (bk_da[j]->block_size < length) {
					len += scnprintf(buf + len,
						PAGE_SIZE - len,
						"tasdevice-regcfg_list: "
						"ERROR:%u %u "
						"out of memory\n", length,
						bk_da[j]->block_size);
					break;
				}
			}
			if (length != bk_da[j]->block_size) {
				len += scnprintf(buf + len, PAGE_SIZE - len,
					"tasdevice-regcfg_list: ERROR: %u %u "
					"size is not same\n", length,
					bk_da[j]->block_size);
			}
		}
out:
		mutex_unlock(&tas_dev->codec_lock);
	}else {
		if(len + 42 < PAGE_SIZE) {
			len += scnprintf(buf + len, PAGE_SIZE - len, "ERROR: "
				"Can't find tasdevice_priv handle!\n\r");
		} else {
			scnprintf(buf + PAGE_SIZE - 100, 100,
				"\n[regbininfo] Out of boundary!\n\r");
			len = PAGE_SIZE;
		}
	}
	return len;
}

ssize_t tas_fwload_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	int ret;

	dev_info(tas_dev->dev, "fwload: count = %ld\n", (long)count);
	if(tas_dev) {
		mutex_lock(&tas_dev->codec_lock);
		if(tas_dev->tas_ctrl.tasdevice_profile_controls) {
			dev_info(tas_dev->dev, "fw %s already loaded\n",
				tas_dev->regbin_binaryname);
			goto out;
		}

		ret = request_firmware_nowait(THIS_MODULE,
#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
			FW_ACTION_UEVENT
#else
			FW_ACTION_HOTPLUG
#endif
			, tas_dev->regbin_binaryname, tas_dev->dev, GFP_KERNEL, tas_dev,
			tasdevice_regbin_ready);
		if (ret)
			dev_err(tas_dev->dev, "load %s error = %d\n",
				tas_dev->regbin_binaryname, ret);
out:
		mutex_unlock(&tas_dev->codec_lock);
	}
	return count;
}
