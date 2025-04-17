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
#include <linux/regmap.h>

#include "tasdevice.h"
#include "tasdevice-rw.h"

static int tasdevice_regmap_write(
	struct tasdevice_priv *tas_dev,
	unsigned int reg, unsigned int value)
{
	int retry_count = TASDEVICE_I2C_RETRY_COUNT;
	int ret;

	while (retry_count--) {
		ret = regmap_write(tas_dev->regmap, reg, value);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return TASDEVICE_ERROR_I2C_FAILED;
	else
		return 0;
}

static int tasdevice_regmap_bulk_write(
	struct tasdevice_priv *tas_dev, unsigned int reg,
	unsigned char *pData, unsigned int nLength)
{
	int retry_count = TASDEVICE_I2C_RETRY_COUNT;
	int ret;

	while (retry_count--) {
		ret = regmap_bulk_write(tas_dev->regmap, reg,
				pData, nLength);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return TASDEVICE_ERROR_I2C_FAILED;
	else
		return 0;
}

static int tasdevice_regmap_read(
	struct tasdevice_priv *tas_dev,
	unsigned int reg, unsigned int *value)
{
	int retry_count = TASDEVICE_I2C_RETRY_COUNT;
	int ret;

	while (retry_count--) {
		ret = regmap_read(tas_dev->regmap, reg, value);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return TASDEVICE_ERROR_I2C_FAILED;
	else
		return 0;
}

static int tasdevice_regmap_bulk_read(
	struct tasdevice_priv *tas_dev, unsigned int reg,
	unsigned char *pData, unsigned int nLength)
{
	int retry_count = TASDEVICE_I2C_RETRY_COUNT;
	int ret;

	while (retry_count--) {
		ret = regmap_bulk_read(tas_dev->regmap, reg, pData, nLength);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return TASDEVICE_ERROR_I2C_FAILED;
	else
		return 0;
}

static int tasdevice_regmap_update_bits(
	struct tasdevice_priv *tas_dev, unsigned int reg,
	unsigned int mask, unsigned int value)
{
	int retry_count = TASDEVICE_I2C_RETRY_COUNT;
	int ret;

	while (retry_count--) {
		ret = regmap_update_bits(tas_dev->regmap, reg, mask, value);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return TASDEVICE_ERROR_I2C_FAILED;
	else
		return 0;
}

static int tasdevice_change_chn_book(
	struct tasdevice_priv *tas_priv, unsigned int chn, int book)
{
	struct i2c_client *client = (struct i2c_client *)tas_priv->client;
	struct tasdevice *tasdev;
	bool chip_switching = false;
	int ret = 0;

	if(chn < tas_priv->ndev) {
		tasdev = &tas_priv->tasdevice[chn];

		if (client->addr != tasdev->addr) {
			client->addr = tasdev->addr;
			chip_switching = true;
		}

		if (tasdev->book == book) {
			if (chip_switching) {
				/* All tas2781s share the same regmap, clear the page
				 * inside regmap once switching to another tas2781.
				 * Register 0 at any pages and any books inside tas2781
				 * is the same one for page-switching. Book has already
				 * inside the current tas2781.
				 */
				ret = tasdevice_regmap_write(tas_priv,
					TASDEVICE_PAGE_SELECT, 0);
				if (ret < 0)
					dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
			}
		} else {
			/* Book switching in other cases */
			ret = tasdevice_regmap_write(tas_priv,
				TASDEVICE_BOOKCTL_REG, book);
			if (ret < 0) {
				dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
				goto out;
			}
			tasdev->book = book;
		}
	} else
		dev_err(tas_priv->dev,
			"%s, ERROR, no such channel(%d)\n", __func__, chn);

out:
	return ret;
}

int tasdevice_dev_read(struct tasdevice_priv *tas_dev,
	unsigned int chn, unsigned int reg, unsigned int *pValue)
{
	int n_result = 0;

	mutex_lock(&tas_dev->dev_lock);
	if (chn >= tas_dev->ndev) {
		dev_err(tas_dev->dev, "%s, ERROR, L=%d no such channel(%d)\n",
			__func__, __LINE__, chn);
		goto out;
	}

	n_result = tasdevice_change_chn_book(tas_dev, chn,
		TASDEVICE_BOOK_ID(reg));
	if (n_result < 0)
		goto out;

	n_result = tasdevice_regmap_read(tas_dev, TASDEVICE_PGRG(reg), pValue);
	if (n_result < 0)
		dev_err(tas_dev->dev, "%s, E=%d\n", __func__,
			n_result);
	else
		dev_dbg(tas_dev->dev, "%s: chn:0x%02x:BOOK:PAGE:REG "
			"0x%02x:0x%02x:0x%02x,0x%02x\n", __func__,
			tas_dev->client->addr, TASDEVICE_BOOK_ID(reg),
			TASDEVICE_PAGE_ID(reg), TASDEVICE_PAGE_REG(reg),
			*pValue);

out:
	mutex_unlock(&tas_dev->dev_lock);
	return n_result;
}


int tasdevice_dev_write(struct tasdevice_priv *tas_dev,
	unsigned int chn, unsigned int reg, unsigned int value)
{
	int n_result = 0;

	mutex_lock(&tas_dev->dev_lock);
	if (chn >= tas_dev->ndev) {
		dev_err(tas_dev->dev, "%s, no such channel(%d)\n",
			__func__, chn);
		goto out;
	}

	n_result = tasdevice_change_chn_book(tas_dev, chn,
		TASDEVICE_BOOK_ID(reg));
	if (n_result < 0)
		goto out;

	n_result = tasdevice_regmap_write(tas_dev, TASDEVICE_PGRG(reg), value);
	if (n_result < 0)
		dev_err(tas_dev->dev, "%s, E=%d\n", __func__, n_result);
	else
		dev_dbg(tas_dev->dev, "%s: chn0x%02x:BOOK:PAGE:REG "
			"0x%02x:0x%02x:0x%02x, VAL: 0x%02x\n",
			__func__, tas_dev->client->addr,
			TASDEVICE_BOOK_ID(reg), TASDEVICE_PAGE_ID(reg),
			TASDEVICE_PAGE_REG(reg), value);

out:
	mutex_unlock(&tas_dev->dev_lock);
	return n_result;
}

int tasdevice_dev_bulk_write(
	struct tasdevice_priv *tas_dev, unsigned int chn,
	unsigned int reg, unsigned char *p_data,
	unsigned int n_length)
{
	int n_result = 0;

	mutex_lock(&tas_dev->dev_lock);
	if (chn >= tas_dev->ndev) {
		dev_err(tas_dev->dev, "%s, no such channel(%d)\n",
			__func__, chn);
		goto out;
	}

	n_result = tasdevice_change_chn_book(tas_dev, chn,
		TASDEVICE_BOOK_ID(reg));
	if (n_result < 0)
		goto out;

	n_result = tasdevice_regmap_bulk_write(tas_dev,
		TASDEVICE_PGRG(reg), p_data, n_length);
	if (n_result < 0)
		dev_err(tas_dev->dev, "%s, E=%d\n", __func__, n_result);
	else
		dev_dbg(tas_dev->dev, "%s: chn0x%02x:BOOK:PAGE:REG "
			"0x%02x:0x%02x:0x%02x, len: 0x%02x\n", __func__,
			tas_dev->client->addr, TASDEVICE_BOOK_ID(reg),
			TASDEVICE_PAGE_ID(reg), TASDEVICE_PAGE_REG(reg),
			n_length);

out:
	mutex_unlock(&tas_dev->dev_lock);
	return n_result;
}


int tasdevice_dev_bulk_read(struct tasdevice_priv *tas_dev,
	unsigned int chn, unsigned int reg, unsigned char *p_data,
	unsigned int n_length)
{
	int n_result = 0;

	mutex_lock(&tas_dev->dev_lock);

	if (chn >= tas_dev->ndev) {
		dev_err(tas_dev->dev, "%s, no such channel(%d)\n",
			__func__, chn);
		goto out;
	}

	n_result = tasdevice_change_chn_book(tas_dev, chn,
		TASDEVICE_BOOK_ID(reg));
	if (n_result < 0)
		goto out;

	n_result = tasdevice_regmap_bulk_read(tas_dev,
		TASDEVICE_PGRG(reg), p_data, n_length);
	if (n_result < 0)
		dev_err(tas_dev->dev, "%s, E=%d\n", __func__, n_result);
	else
		dev_dbg(tas_dev->dev, "%s: chn0x%02x:BOOK:PAGE:REG "
			"0x%02x:0x%02x:0x%02x, len: 0x%02x\n", __func__,
			tas_dev->client->addr, TASDEVICE_BOOK_ID(reg),
			TASDEVICE_PAGE_ID(reg), TASDEVICE_PAGE_REG(reg),
			n_length);

out:
	mutex_unlock(&tas_dev->dev_lock);
	return n_result;
}


int tasdevice_dev_update_bits(struct tasdevice_priv *tas_dev,
	unsigned int chn, unsigned int reg, unsigned int mask,
	unsigned int value)
{
	int n_result = 0;

	mutex_lock(&tas_dev->dev_lock);
	if (chn >= tas_dev->ndev) {
		dev_err(tas_dev->dev, "%s, no such channel(%d)\n",
			__func__, chn);
		goto out;
	}
	n_result = tasdevice_change_chn_book(tas_dev, chn,
		TASDEVICE_BOOK_ID(reg));
	if (n_result < 0)
		goto out;

	n_result = tasdevice_regmap_update_bits(tas_dev,
		TASDEVICE_PGRG(reg), mask, value);
	if (n_result < 0)
		dev_err(tas_dev->dev, "%s, E=%d\n", __func__, n_result);
	else
		dev_dbg(tas_dev->dev,
			"%s: chn0x%02x:BOOK:PAGE:REG 0x%02x:0x%02x:0x%02x, "
			"mask: 0x%02x, val: 0x%02x\n", __func__,
			tas_dev->client->addr, TASDEVICE_BOOK_ID(reg),
			TASDEVICE_PAGE_ID(reg), TASDEVICE_PAGE_REG(reg), mask,
			value);

out:
	mutex_unlock(&tas_dev->dev_lock);
	return n_result;
}
