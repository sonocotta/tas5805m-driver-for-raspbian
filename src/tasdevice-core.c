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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <asm/unaligned.h>

#include "pcm9211-reg.h"
#include "tas2118-tlv.h"
#include "tasdevice.h"
#include "tasdevice-node.h"
#include "tasdevice-rw.h"
#include "tas5805-reg.h"

#define PCM9211_REG_CH1_DIGITAL_GAIN	TASDEVICE_REG(0X0, 0X0, 0x46)
#define PCM9211_REG_CH2_DIGITAL_GAIN	TASDEVICE_REG(0X0, 0X0, 0x47)

#define TAS2110_DVC_LVL			TASDEVICE_REG(0x0, 0x2, 0x0C)

#define TAS2770_DVC_PCM			TASDEVICE_REG(0x0, 0x0, 0x05)
#define TAS2770_DVC_PDM			TASDEVICE_REG(0x0, 0x0, 0x06)

#define TAS2780_DVC_LVL			TASDEVICE_REG(0x0, 0x0, 0x1A)
#define TAS2780_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x03)

#define TAS257X_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x04)

#define TAS58XX_RST_MOD_MSK		BIT(4)
#define TAS58XX_RST_MOD_CLR		BIT(4)
#define TAS58XX_RST_CONTROL_REG_MSK	BIT(0)
#define TAS58XX_RST_CONTROL_REG_CLR	BIT(0)

#define TASDEVICE_CLK_DIR_OUT		1

/* mixer control */
struct tasdevice_mixer_control {
	unsigned int invert;
	unsigned int dev_no;
	unsigned int shift;
	int max;
	int reg;
};
struct tasdev_ctrl_info {
	struct tasdevice_mixer_control *tasdev_ctrl;
	unsigned int ctrl_array_size;
	const unsigned int *gain;
};

static const struct i2c_device_id tasdevice_i2c_id[] = {
	{ "pcm9211s", PCM9211 },
	{ "tas2020", TAS2020 },
	{ "tas2110", TAS2110 },
	{ "tas2118", TAS2118 },
	{ "tas2120", TAS2120 },
	{ "tas2320", TAS2320 },
	{ "tas2560", TAS2560 },
	{ "tas2562", TAS2562 },
	{ "tas2564", TAS2564 },
	{ "tas257x", TAS257X },
	{ "tas2764", TAS2764 },
	{ "tas2770", TAS2770 },
	{ "tas2780", TAS2780 },
	{ "tas5802", TAS5802 },
	{ "tas5805", TAS5805 },
	{ "tas5806m", TAS5806M },
	/* TAS5806M with Headphone */
	{ "tas5806md", TAS5806MD },
	{ "tas5815", TAS5815 },
	{ "tas5822", TAS5822 },
	{ "tas5825m", TAS5825M },
	{ "tas5825p", TAS5825P },
	{ "tas5827", TAS5827 },
	{ "tas5828", TAS5828 },
	{},
};

static const char *dts_tag[] = {
	"Primary-device",
	"Secondary-device",
	"Tertiary-device",
	"Quaternary-device",
};

struct tasdevice_dvc {
	char db;
	unsigned char regvalue[4];
};

/*pow(10, db/20)*pow(2,30)*/
static const struct tasdevice_dvc dvc_mapping_table[] = {
	{ -110, { 0X00, 0X00, 0X0D, 0X43 } },
	{ -109, { 0X00, 0X00, 0X0E, 0XE1 } },
	{ -108, { 0X00, 0X00, 0X10, 0XB2 } },
	{ -107, { 0X00, 0X00, 0X12, 0XBC } },
	{ -106, { 0X00, 0X00, 0X15, 0X05 } },
	{ -105, { 0X00, 0X00, 0X17, 0X96 } },
	{ -104, { 0X00, 0X00, 0X1A, 0X76 } },
	{ -103, { 0X00, 0X00, 0X1D, 0XB1 } },
	{ -102, { 0X00, 0X00, 0X21, 0X51 } },
	{ -101, { 0X00, 0X00, 0X25, 0X61 } },
	{ -100, { 0X00, 0X00, 0X29, 0XF1 } },
	{  -99, { 0X00, 0X00, 0X2F, 0X0F } },
	{  -98, { 0X00, 0X00, 0X34, 0XCD } },
	{  -97, { 0X00, 0X00, 0X3B, 0X3F } },
	{  -96, { 0X00, 0X00, 0X42, 0X79 } },
	{  -95, { 0X00, 0X00, 0X4A, 0X96 } },
	{  -94, { 0X00, 0X00, 0X53, 0XAF } },
	{  -93, { 0X00, 0X00, 0X5D, 0XE6 } },
	{  -92, { 0X00, 0X00, 0X69, 0X5B } },
	{  -91, { 0X00, 0X00, 0X76, 0X36 } },
	{  -90, { 0X00, 0X00, 0X84, 0XA2 } },
	{  -89, { 0X00, 0X00, 0X94, 0XD1 } },
	{  -88, { 0X00, 0X00, 0XA6, 0XFA } },
	{  -87, { 0X00, 0X00, 0XBB, 0X5A } },
	{  -86, { 0X00, 0X00, 0XD2, 0X36 } },
	{  -85, { 0X00, 0X00, 0XEB, 0XDC } },
	{  -84, { 0X00, 0X01, 0X08, 0XA4 } },
	{  -83, { 0X00, 0X01, 0X28, 0XEF } },
	{  -82, { 0X00, 0X01, 0X4D, 0X2A } },
	{  -81, { 0X00, 0X01, 0X75, 0XD1 } },
	{  -80, { 0X00, 0X01, 0XA3, 0X6E } },
	{  -79, { 0X00, 0X01, 0XD6, 0X9B } },
	{  -78, { 0X00, 0X02, 0X10, 0X08 } },
	{  -77, { 0X00, 0X02, 0X50, 0X76 } },
	{  -76, { 0X00, 0X02, 0X98, 0XC0 } },
	{  -75, { 0X00, 0X02, 0XE9, 0XDD } },
	{  -74, { 0X00, 0X03, 0X44, 0XDF } },
	{  -73, { 0X00, 0X03, 0XAA, 0XFC } },
	{  -72, { 0X00, 0X04, 0X1D, 0X8F } },
	{  -71, { 0X00, 0X04, 0X9E, 0X1D } },
	{  -70, { 0X00, 0X05, 0X2E, 0X5A } },
	{  -69, { 0X00, 0X05, 0XD0, 0X31 } },
	{  -68, { 0X00, 0X06, 0X85, 0XC8 } },
	{  -67, { 0X00, 0X07, 0X51, 0X86 } },
	{  -66, { 0X00, 0X08, 0X36, 0X21 } },
	{  -65, { 0X00, 0X09, 0X36, 0XA1 } },
	{  -64, { 0X00, 0X0A, 0X56, 0X6D } },
	{  -63, { 0X00, 0X0B, 0X99, 0X56 } },
	{  -62, { 0X00, 0X0D, 0X03, 0XA7 } },
	{  -61, { 0X00, 0X0E, 0X9A, 0X2D } },
	{  -60, { 0X00, 0X10, 0X62, 0X4D } },
	{  -59, { 0X00, 0X12, 0X62, 0X16 } },
	{  -58, { 0X00, 0X14, 0XA0, 0X50 } },
	{  -57, { 0X00, 0X17, 0X24, 0X9C } },
	{  -56, { 0X00, 0X19, 0XF7, 0X86 } },
	{  -55, { 0X00, 0X1D, 0X22, 0XA4 } },
	{  -54, { 0X00, 0X20, 0XB0, 0XBC } },
	{  -53, { 0X00, 0X24, 0XAD, 0XE0 } },
	{  -52, { 0X00, 0X29, 0X27, 0X9D } },
	{  -51, { 0X00, 0X2E, 0X2D, 0X27 } },
	{  -50, { 0X00, 0X33, 0XCF, 0X8D } },
	{  -49, { 0X00, 0X3A, 0X21, 0XF3 } },
	{  -48, { 0X00, 0X41, 0X39, 0XD3 } },
	{  -47, { 0X00, 0X49, 0X2F, 0X44 } },
	{  -46, { 0X00, 0X52, 0X1D, 0X50 } },
	{  -45, { 0X00, 0X5C, 0X22, 0X4E } },
	{  -44, { 0X00, 0X67, 0X60, 0X44 } },
	{  -43, { 0X00, 0X73, 0XFD, 0X65 } },
	{  -42, { 0X00, 0X82, 0X24, 0X8A } },
	{  -41, { 0X00, 0X92, 0X05, 0XC6 } },
	{  -40, { 0X00, 0XA3, 0XD7, 0X0A } },
	{  -39, { 0X00, 0XB7, 0XD4, 0XDD } },
	{  -38, { 0X00, 0XCE, 0X43, 0X28 } },
	{  -37, { 0X00, 0XE7, 0X6E, 0X1E } },
	{  -36, { 0X01, 0X03, 0XAB, 0X3D } },
	{  -35, { 0X01, 0X23, 0X5A, 0X71 } },
	{  -34, { 0X01, 0X46, 0XE7, 0X5D } },
	{  -33, { 0X01, 0X6E, 0XCA, 0XC5 } },
	{  -32, { 0X01, 0X9B, 0X8C, 0X27 } },
	{  -31, { 0X01, 0XCD, 0XC3, 0X8C } },
	{  -30, { 0X02, 0X06, 0X1B, 0X89 } },
	{  -29, { 0X02, 0X45, 0X53, 0X85 } },
	{  -28, { 0X02, 0X8C, 0X42, 0X3F } },
	{  -27, { 0X02, 0XDB, 0XD8, 0XAD } },
	{  -26, { 0X03, 0X35, 0X25, 0X29 } },
	{  -25, { 0X03, 0X99, 0X57, 0X0C } },
	{  -24, { 0X04, 0X09, 0XC2, 0XB0 } },
	{  -23, { 0X04, 0X87, 0XE5, 0XFB } },
	{  -22, { 0X05, 0X15, 0X6D, 0X68 } },
	{  -21, { 0X05, 0XB4, 0X39, 0XBC } },
	{  -20, { 0X06, 0X66, 0X66, 0X66 } },
	{  -19, { 0X07, 0X2E, 0X50, 0XA6 } },
	{  -18, { 0X08, 0X0E, 0X9F, 0X96 } },
	{  -17, { 0X09, 0X0A, 0X4D, 0X2F } },
	{  -16, { 0X0A, 0X24, 0XB0, 0X62 } },
	{  -15, { 0X0B, 0X61, 0X88, 0X71 } },
	{  -14, { 0X0C, 0XC5, 0X09, 0XAB } },
	{  -13, { 0X0E, 0X53, 0XEB, 0XB3 } },
	{  -12, { 0X10, 0X13, 0X79, 0X87 } },
	{  -11, { 0X12, 0X09, 0XA3, 0X7A } },
	{  -10, { 0X14, 0X3D, 0X13, 0X62 } },
	{   -9, { 0X16, 0XB5, 0X43, 0X37 } },
	{   -8, { 0X19, 0X7A, 0X96, 0X7F } },
	{   -7, { 0X1C, 0X96, 0X76, 0XC6 } },
	{   -6, { 0X20, 0X13, 0X73, 0X9E } },
	{   -5, { 0X23, 0XFD, 0X66, 0X78 } },
	{   -4, { 0X28, 0X61, 0X9A, 0XE9 } },
	{   -3, { 0X2D, 0X4E, 0XFB, 0XD5 } },
	{   -2, { 0X32, 0XD6, 0X46, 0X17 } },
	{   -1, { 0X39, 0X0A, 0X41, 0X5F } },
	{    0, { 0X40, 0X00, 0X00, 0X00 } },
	{    1, { 0X47, 0XCF, 0X26, 0X7D } },
	{    2, { 0X50, 0X92, 0X3B, 0XE3 } },
};

static ssize_t tas_devinfo_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);
	int n = 0;
	int i;

	if(tas_dev != NULL) {
		n += scnprintf(buf + n, 32, "No.\tDevTyp\tAddr\n");
		for(i = 0; i < tas_dev->ndev; i++) {
			n += scnprintf(buf + n, 16, "%d\t", i);
			if(tas_dev->tasdevice[i].dev_id <
				ARRAY_SIZE(tasdevice_i2c_id))
				n += scnprintf(buf + n, 32,
					"%s\t", tasdevice_i2c_id
					[tas_dev->tasdevice[i].dev_id].name);
			else
				n += scnprintf(buf + n, 16, "Invalid\t");
			n += scnprintf(buf + n, 16, "0x%02x\n",
				tas_dev->tasdevice[i].addr);
		}
	} else
		n += scnprintf(buf + n, 16, "Invalid data\n");

	return n;
}

static DEVICE_ATTR(reg, 0664, tas_reg_show, tas_reg_store);
static DEVICE_ATTR(regdump, 0664, tas_regdump_show, tas_regdump_store);
static DEVICE_ATTR(act_addr, 0664, tas_active_address_show, NULL);
static DEVICE_ATTR(regbininfo_list, 0664, tas_regbininfo_list_show,
	NULL);
static DEVICE_ATTR(regcfg_list, 0664, tas_regcfg_list_show,
	tas_regcfg_list_store);
static DEVICE_ATTR(fwload, 0664, NULL, tas_fwload_store);
static DEVICE_ATTR(devinfo, 0664, tas_devinfo_show, NULL);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_reg.attr,
	&dev_attr_regdump.attr,
	&dev_attr_act_addr.attr,
	&dev_attr_regbininfo_list.attr,
	&dev_attr_regcfg_list.attr,
	&dev_attr_fwload.attr,
	&dev_attr_devinfo.attr,
	NULL
};

//nodes are in /sys/devices/platform/XXXXXXXX.i2cX/i2c-X/
// Or /sys/bus/i2c/devices/7-00xx/
const struct attribute_group tasdevice_attribute_group = {
	.attrs = sysfs_attrs
};

static const struct regmap_range_cfg tasdevice_ranges[] = {
	{
		.range_min = 0,
		.range_max = 256 * 128,
		.selector_reg = TASDEVICE_PAGE_SELECT,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct regmap_config tasdevice_i2c_regmap = {
	.reg_bits      = 8,
	.val_bits      = 8,
	.cache_type    = REGCACHE_RBTREE,
	.ranges = tasdevice_ranges,
	.num_ranges = ARRAY_SIZE(tasdevice_ranges),
	.max_register = 256 * 128,
};

static struct tasdevice_mixer_control pcm9211_digital_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCM9211_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM9211_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static int tasdevice_digital_getvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int val;
	int ret = 0;

	/* Read the primary device as the whole */
	ret = tasdevice_dev_read(tas_dev, 0, mc->reg, &val);
	if (ret) {
		dev_err(tas_dev->dev, "%s, get digital vol error\n", __func__);
		goto out;
	}
	val = (val > mc->max) ? mc->max : val;
	val = mc->invert ? mc->max - val : val;
	ucontrol->value.integer.value[0] = val;

out:
	return ret;
}

static int tasdevice_digital_putvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int val;
	int i, ret = 0;

	val = ucontrol->value.integer.value[0];
	val = (val > mc->max) ? mc->max : val;
	val = mc->invert ? mc->max - val : val;
	val = (val < 0) ? 0 : val;

	for (i = 0; i < tas_dev->ndev; i++) {
		ret = tasdevice_dev_write(tas_dev, i, mc->reg, val);
		if (ret)
			dev_err(tas_dev->dev, "%s, set digital vol error in device %d\n",
				__func__, i);
	}

	return ret;
}

static int tasdevice_amp_getvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned char mask = 0;
	unsigned int val;
	int ret = 0;

	/* Read the primary device */
	ret = tasdevice_dev_read(tas_dev, 0, mc->reg, &val);
	if (ret) {
		dev_err(tas_dev->dev,
		"%s, get AMP vol error\n",
		__func__);
		goto out;
	}

	mask = (1 << fls(mc->max)) - 1;
	mask <<= mc->shift;
	val = (val & mask) >> mc->shift;
	val = (val > mc->max) ? mc->max : val;
	val = mc->invert ? mc->max - val : val;
	ucontrol->value.integer.value[0] = val;

out:
	return ret;
}

static int tasdevice_amp_putvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned char mask = 0;
	unsigned int val;
	int i, ret = 0;

	mask = (1 << fls(mc->max)) - 1;
	mask <<= mc->shift;
	val = ucontrol->value.integer.value[0];
	val = (val > mc->max) ? mc->max : val;
	val = mc->invert ? mc->max - val : val;
	val = (val < 0) ? 0 : val;

	for (i = 0; i < tas_dev->ndev; i++) {
		ret = tasdevice_dev_update_bits(tas_dev, i, mc->reg, mask,
			val << mc->shift);
		if (ret)
			dev_err(tas_dev->dev, "%s, set AMP vol error in device %d\n",
				__func__, i);
	}

	return ret;
}

static int tas2110_digital_gain_get(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	unsigned int l = 0, r = ARRAY_SIZE(dvc_mapping_table) - 1;
	unsigned int target, ar_mid, mid, ar_l, ar_r;
	unsigned int reg = mc->reg;
	unsigned char data[4];
	int ret;

	/* Read the primary device */
	ret =  tasdevice_dev_bulk_read(tas_dev, 0, reg, data, 4);
	if (ret) {
		dev_err(tas_dev->dev, "%s, get AMP vol error\n", __func__);
		goto out;
	}

	target = get_unaligned_be32(&data[0]);

	while (r > 1 + l) {
		mid = (l + r) / 2;
		ar_mid = get_unaligned_be32(&dvc_mapping_table[mid].regvalue);
		if (target < ar_mid)
			r = mid;
		else
			l = mid;
	}

	ar_l = get_unaligned_be32(&dvc_mapping_table[l].regvalue);
	ar_r = get_unaligned_be32(&dvc_mapping_table[r].regvalue);

	ucontrol->value.integer.value[0] =
		abs(target - ar_l) <= abs(target - ar_r) ? l : r;
out:
	return 0;
}

static int tas2110_digital_gain_put(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	unsigned int reg = mc->reg;
	unsigned int volrd, volwr;
	int vol = ucontrol->value.integer.value[0];
	int max = mc->max, i, ret = 0;
	unsigned char data[4];

	vol = vol < max ? vol: max;

	/* Read the primary device */
	ret =  tasdevice_dev_bulk_read(tas_dev, 0, reg, data, 4);
	if (ret) {
		dev_err(tas_dev->dev, "%s, get AMP vol error\n", __func__);
		goto out;
	}

	volrd = get_unaligned_be32(&data[0]);
	volwr = get_unaligned_be32(&dvc_mapping_table[vol].regvalue);

	if (volrd == volwr)
		goto out;

	for (i = 0; i < tas_dev->ndev; i++) {
		ret = tasdevice_dev_bulk_write(tas_dev, i, reg,
			(unsigned char *)dvc_mapping_table[vol].regvalue, 4);
		if (ret)
			dev_err(tas_dev->dev, "%s, set digital vol error in device %d\n",
				__func__, i);
	}

	if ( i == tas_dev->ndev)
		ret = 1;
out:
	return ret;
}

static int tas2118_digital_gain_get(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	unsigned int l = 0, r = mc->max;
	unsigned int target, ar_mid, mid, ar_l, ar_r;
	unsigned int reg = mc->reg;
	unsigned char data[4];
	int ret;

	mutex_lock(&tas_dev->codec_lock);
	/* Read the primary device */
	ret = tasdevice_dev_bulk_read(tas_dev, 0, reg, data, 4);
	if (ret) {
		dev_err(tas_dev->dev, "%s, get AMP vol error\n", __func__);
		goto out;
	}

	target = get_unaligned_be32(&data[0]);

	while (r > 1 + l) {
		mid = (l + r) / 2;
		ar_mid = get_unaligned_be32(tas2118_dvc_table[mid]);
		if (target < ar_mid)
			r = mid;
		else
			l = mid;
	}

	ar_l = get_unaligned_be32(tas2118_dvc_table[l]);
	ar_r = get_unaligned_be32(tas2118_dvc_table[r]);

	/* find out the member same as or closer to the current volume */
	ucontrol->value.integer.value[0] =
		abs(target - ar_l) <= abs(target - ar_r) ? l : r;
out:
	mutex_unlock(&tas_dev->codec_lock);
	return 0;
}

static int tas2118_digital_gain_put(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	int vol = ucontrol->value.integer.value[0];
	int status = 0, max = mc->max, rc = 1;
	int i, ret;
	unsigned int reg = mc->reg;
	unsigned int volrd, volwr;
	unsigned char data[4];

	vol = clamp(vol, 0, max);
	mutex_lock(&tas_dev->codec_lock);
	/* Read the primary device */
	ret = tasdevice_dev_bulk_read(tas_dev, 0, reg, data, 4);
	if (ret) {
		dev_err(tas_dev->dev, "%s, get AMP vol error\n", __func__);
		rc = -1;
		goto out;
	}

	volrd = get_unaligned_be32(&data[0]);
	volwr = get_unaligned_be32(tas2118_dvc_table[vol]);

	if (volrd == volwr) {
		rc = 0;
		goto out;
	}

	for (i = 0; i < tas_dev->ndev; i++) {
		ret = tasdevice_dev_bulk_write(tas_dev, i, reg,
			(unsigned char *)tas2118_dvc_table[vol], 4);
		if (ret) {
			dev_err(tas_dev->dev,
				"%s, set digital vol error in dev %d\n",
				__func__, i);
			status |= BIT(i);
		}
	}

	if (status)
		rc = -1;
out:
	mutex_unlock(&tas_dev->codec_lock);
	return rc;
}

static const DECLARE_TLV_DB_MINMAX_MUTE(pcm9211_dig_gain_tlv,
	-11450, 2000);

static const DECLARE_TLV_DB_SCALE(tas2110_amp_vol_tlv, 850, 50, 0);
static const DECLARE_TLV_DB_SCALE(tas2110_dvc_tlv, -11000, 100, 0);

static const DECLARE_TLV_DB_SCALE(tas257x_amp_vol_tlv, 0, 50, 0);

/* -100.5 means mute in this definition */
static const DECLARE_TLV_DB_SCALE(tas2780_dvc_tlv, -10050, 50, 1);
static const DECLARE_TLV_DB_SCALE(tas2780_amp_vol_tlv, 1100, 50, 0);

static const struct snd_kcontrol_new tas2110_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("tas2110-amp-gain-volume", TAS2780_AMP_LEVEL,
		1, 0, 0x14, 0, tasdevice_amp_getvol,
		tasdevice_amp_putvol, tas2110_amp_vol_tlv),
	SOC_SINGLE_RANGE_EXT_TLV("tas2110-digital-pcm-volume", TAS2110_DVC_LVL,
		0, 0, 0xFF, 0, tas2110_digital_gain_get,
		tas2110_digital_gain_put, tas2110_dvc_tlv)
};

static const struct snd_kcontrol_new tas2118_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("Speaker Digital Volume", TAS2110_DVC_LVL, 0,
		0, ARRAY_SIZE(tas2118_dvc_table) - 1, 0,
		tas2118_digital_gain_get, tas2118_digital_gain_put,
		tas2118_dvc_tlv),
};

static const struct snd_kcontrol_new tas257x_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("tas257x-amp-gain-volume", TAS257X_AMP_LEVEL,
		1, 0, 0x2A, 0, tasdevice_amp_getvol,
		tasdevice_amp_putvol, tas257x_amp_vol_tlv)
};

static const struct snd_kcontrol_new tas2770_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("tas2770-amp-gain-volume", TAS2780_AMP_LEVEL,
		0, 0, 0x14, 0, tasdevice_amp_getvol,
		tasdevice_amp_putvol, tas2780_amp_vol_tlv),
	SOC_SINGLE_RANGE_EXT_TLV("tas2770-digital-pcm-volume", TAS2770_DVC_PCM,
		0, 0, 201, 1, tasdevice_digital_getvol,
		tasdevice_digital_putvol, tas2780_dvc_tlv),
	SOC_SINGLE_RANGE_EXT_TLV("tas2770-digital-pdm-volume", TAS2770_DVC_PDM,
		0, 0, 201, 1, tasdevice_digital_getvol,
		tasdevice_digital_putvol, tas2780_dvc_tlv),
};

static const struct snd_kcontrol_new tas2780_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("tas2780-amp-gain-volume", TAS2780_AMP_LEVEL,
		1, 0, 0x14, 0, tasdevice_amp_getvol,
		tasdevice_amp_putvol, tas2780_amp_vol_tlv),
	SOC_SINGLE_RANGE_EXT_TLV("tas2780-digital-volume", TAS2780_DVC_LVL,
		0, 0, 201, 1, tasdevice_digital_getvol,
		tasdevice_digital_putvol, tas2780_dvc_tlv),
};

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_again_tlv, (-1550), 50, 1);

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(tas5805m_vol_tlv, (-10350), 50, 1);

static const struct soc_enum dac_modulation_mode_enum = SOC_ENUM_SINGLE(
    TAS5805M_REG_DEVICE_CTRL_1,   /* Register address where the control resides */
    0,                   /* Bit shift (bit 1 corresponds to the second bit) */
    3,                   /* Number of items (2 possible modes: Normal, Bridge) */
    modulation_mode_text /* Array of text values */
);

static const struct soc_enum dac_switch_freq_enum = SOC_ENUM_SINGLE(
    TAS5805M_REG_DEVICE_CTRL_1,   /* Register address where the control resides */
    4,                   /* Bit shift (bit 1 corresponds to the second bit) */
    4,                   /* Number of items (4 possible modes: Normal, Bridge) */
    switch_freq_text     /* Array of text values */
);

static const struct snd_kcontrol_new tas5805_snd_controls[] = {
	// SOC_SINGLE_RANGE_EXT_TLV("tas5805-amp-gain-volume", TAS5805M_REG_ANALOG_GAIN,
	// 	1, 0, 0x14, 0, tasdevice_amp_getvol,
	// 	tasdevice_amp_putvol, tas2780_amp_vol_tlv),
	SOC_SINGLE_TLV ("Volume Analog", TAS5805M_REG_ANALOG_GAIN, 0, 31, 1, tas5805m_again_tlv),
	// SOC_SINGLE_RANGE_EXT_TLV("tas5805-digital-volume", TAS5805M_REG_VOL_CTL,
	// 	0, 0, 201, 1, tasdevice_digital_getvol,
	// 	tasdevice_digital_putvol, tas2780_dvc_tlv),
	SOC_SINGLE_TLV ("Volume Digital", TAS5805M_REG_VOL_CTL, 0, 255, 1, tas5805m_vol_tlv),
	SOC_ENUM("Driver Modulation Scheme", dac_modulation_mode_enum),
    SOC_ENUM("Driver Switching freq", dac_switch_freq_enum),
};

static const struct snd_soc_dapm_widget tasdevice_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI", "ASI Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ASI1 OUT", "ASI1 Capture",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_INPUT("DMIC"),
};

static const struct snd_soc_dapm_route tasdevice_audio_map[] = {
	{"OUT", NULL, "ASI"},
	{"ASI1 OUT", NULL, "DMIC"},
};

static int tasdevice_info_profile(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec
		= snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev =
		snd_soc_component_get_drvdata(codec);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max(0, tas_dev->regbin.ncfgs - 1);

	return 0;
}

static int tasdevice_get_profile_id(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = tas_dev->cur_conf;

	return 0;
}

static int tasdevice_set_profile_id(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_dev = snd_soc_component_get_drvdata(codec);
	int val = ucontrol->value.integer.value[0];
	int max = tas_dev->regbin.ncfgs - 1;
	int ret = 0;

	val = clamp(val, 0, max);

	if (tas_dev->cur_conf != val) {
		tas_dev->cur_conf = ucontrol->value.integer.value[0];
		ret = 1;
	}

	return ret;
}

static int tasdevice_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_mixer_control *mc =
		(struct tasdevice_mixer_control *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mc->max;
	return 0;
}

static int tasdevice_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct tasdevice_priv *pcm_dev = snd_soc_component_get_drvdata(component);
	struct tasdevice_mixer_control *mc =
		(struct tasdevice_mixer_control *)kcontrol->private_value;
	unsigned int dev_no = mc->dev_no;
	unsigned int shift = mc->shift;
	unsigned int reg = mc->reg;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int val;
	int rc = 0;

	mutex_lock(&pcm_dev->codec_lock);
	rc = tasdevice_dev_read(pcm_dev, dev_no, reg, &val);
	if (rc) {
		dev_err(pcm_dev->dev, "%s:read, ERROR, E=%d\n",
			__func__, rc);
		goto out;
	}

	val = (val >> shift) & mask;
	val = (val > max) ? max : val;
	val = mc->invert ? max - val : val;
	ucontrol->value.integer.value[0] = val;
out:
	mutex_unlock(&pcm_dev->codec_lock);
	return rc;
}

static int tasdevice_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct tasdevice_priv *pcm_dev = snd_soc_component_get_drvdata(component);
	struct tasdevice_mixer_control *mc =
		(struct tasdevice_mixer_control *)kcontrol->private_value;
	unsigned int dev_no = mc->dev_no;
	unsigned int shift = mc->shift;
	unsigned int reg = mc->reg;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int val, val_mask;
	int err = 0;

	mutex_lock(&pcm_dev->codec_lock);
	val = ucontrol->value.integer.value[0] & mask;
	val = (val > max) ? max : val;
	val = mc->invert ? max - val : val;
	val_mask = mask << shift;
	val = val << shift;
	err = tasdevice_dev_update_bits(pcm_dev, dev_no, reg, val_mask, val);
	if (err) {
		dev_err(pcm_dev->dev, "%s:update_bits, ERROR, E=%d\n",
			__func__, err);
		goto out;
	}
out:
	mutex_unlock(&pcm_dev->codec_lock);
	return err;
}

int tasdevice_create_controls(struct tasdevice_priv *tas_dev)
{
	struct snd_kcontrol_new *tasdevice_profile_controls = NULL;
	int  nr_controls = 1, ret = 0, mix_index = 0, i, chn;
	struct tasdev_ctrl_info dig_ctl_info = {0};
	char *name;

	switch (tas_dev->chip_id) {
	case PCM9211:
		dig_ctl_info.gain = pcm9211_dig_gain_tlv;
		dig_ctl_info.tasdev_ctrl = pcm9211_digital_gain_ctl;
		dig_ctl_info.ctrl_array_size =
			ARRAY_SIZE(pcm9211_digital_gain_ctl);
		nr_controls += tas_dev->ndev * dig_ctl_info.ctrl_array_size;
		break;
	case TAS257X:
		ret = snd_soc_add_component_controls(tas_dev->component,
			(struct snd_kcontrol_new *)tas257x_snd_controls,
			ARRAY_SIZE(tas257x_snd_controls));
		if (ret < 0) {
			dev_err(tas_dev->dev, "%s, add %s vol ctrl failed\n", __func__,
				tas_dev->dev_name);
			goto out;
		}
		break;
	case TAS2110:
		ret = snd_soc_add_component_controls(tas_dev->component,
			(struct snd_kcontrol_new *)tas2110_snd_controls,
			ARRAY_SIZE(tas2110_snd_controls));
		if (ret < 0) {
			dev_err(tas_dev->dev, "%s, add %s vol ctrl failed\n", __func__,
				tas_dev->dev_name);
			goto out;
		}
		break;
	case TAS2118:
		ret = snd_soc_add_component_controls(tas_dev->component,
			(struct snd_kcontrol_new *)tas2118_snd_controls,
			ARRAY_SIZE(tas2118_snd_controls));
		if (ret < 0) {
			dev_err(tas_dev->dev, "%s, add %s vol ctrl failed\n", __func__,
				tas_dev->dev_name);
			goto out;
		}
		break;
	case TAS2770:
		ret = snd_soc_add_component_controls(tas_dev->component,
			(struct snd_kcontrol_new *)tas2770_snd_controls,
			ARRAY_SIZE(tas2770_snd_controls));
		if (ret < 0) {
			dev_err(tas_dev->dev, "%s, add %s vol ctrl failed\n", __func__,
				tas_dev->dev_name);
			goto out;
		}
		break;
	case TAS2780:
		ret = snd_soc_add_component_controls(tas_dev->component,
			(struct snd_kcontrol_new *)tas2780_snd_controls,
			ARRAY_SIZE(tas2780_snd_controls));
		if (ret < 0) {
			dev_err(tas_dev->dev, "%s, add %s vol ctrl failed\n", __func__,
				tas_dev->dev_name);
			goto out;
		}
		break;
	case TAS5805:
		ret = snd_soc_add_component_controls(tas_dev->component,
			(struct snd_kcontrol_new *)tas5805_snd_controls,
			ARRAY_SIZE(tas5805_snd_controls));
		if (ret < 0) {
			dev_err(tas_dev->dev, "%s, add %s vol ctrl failed\n", __func__,
				tas_dev->dev_name);
			goto out;
		}
		break;
	}

	tasdevice_profile_controls = devm_kzalloc(tas_dev->dev,
		nr_controls * sizeof(struct snd_kcontrol_new),
		GFP_KERNEL);
	if (tasdevice_profile_controls == NULL) {
		ret = -ENOMEM;
		tas_dev->tas_ctrl.tasdevice_profile_controls = NULL;
		goto out;
	}
	tas_dev->tas_ctrl.tasdevice_profile_controls =
		tasdevice_profile_controls;

	/* Create a mixer item for selecting the active profile */
	name = devm_kzalloc(tas_dev->dev,
		SNDRV_CTL_ELEM_ID_NAME_MAXLEN, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto out;
	}
	scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		"Profile");
	tasdevice_profile_controls[mix_index].name = name;
	tasdevice_profile_controls[mix_index].iface =
		SNDRV_CTL_ELEM_IFACE_MIXER;
	tasdevice_profile_controls[mix_index].info =
		tasdevice_info_profile;
	tasdevice_profile_controls[mix_index].get =
		tasdevice_get_profile_id;
	tasdevice_profile_controls[mix_index].put =
		tasdevice_set_profile_id;
	mix_index++;

	if (dig_ctl_info.ctrl_array_size != 0) {
		for (i = 0; i < tas_dev->ndev; i++) {
			if(mix_index >= nr_controls) {
				dev_info(tas_dev->dev, "%s: mix_index = %d nr_controls = %d\n",
					__func__, mix_index, nr_controls);
				break;
			}
			for (chn = 1; chn <= dig_ctl_info.ctrl_array_size; chn++) {
				if(mix_index >= nr_controls) {
					dev_info(tas_dev->dev,
						"%s: mix_index = %d nr_controls = %d\n",
						__func__, mix_index,
						nr_controls);
					break;
				}
				name = devm_kzalloc(tas_dev->dev,
					SNDRV_CTL_ELEM_ID_NAME_MAXLEN, GFP_KERNEL);
				if (!name) {
					dev_err(tas_dev->dev, "%s: fail to name the kcontrol\n",
						__func__);
					ret = -ENOMEM;
					goto out;
				}
				scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
					"%s-dev%d-ch%d-digital-gain",
					tasdevice_i2c_id[tas_dev->chip_id].name, i, chn);
				tasdevice_profile_controls[mix_index].tlv.p =
					dig_ctl_info.gain;
				dig_ctl_info.tasdev_ctrl[chn - 1].dev_no = i;
				tasdevice_profile_controls[mix_index].private_value =
						(unsigned long)&dig_ctl_info.tasdev_ctrl
						[chn - 1];
				tasdevice_profile_controls[mix_index].name =
					name;
				tasdevice_profile_controls[mix_index].access =
					SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			 		SNDRV_CTL_ELEM_ACCESS_READWRITE;
				tasdevice_profile_controls[mix_index].iface =
					SNDRV_CTL_ELEM_IFACE_MIXER;
				tasdevice_profile_controls[mix_index].info =
					tasdevice_info_volsw;
				tasdevice_profile_controls[mix_index].get =
					tasdevice_get_volsw;
				tasdevice_profile_controls[mix_index].put =
					tasdevice_put_volsw;
				mix_index++;
			}
		}
	}

	ret = snd_soc_add_component_controls(tas_dev->component,
		tasdevice_profile_controls,
		nr_controls < mix_index ? nr_controls : mix_index);
	tas_dev->tas_ctrl.nr_controls =
		nr_controls < mix_index ? nr_controls : mix_index;
out:
	return ret;
}

static int tasdevice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct tasdevice_priv *tas_dev = snd_soc_dai_get_drvdata(dai);
	unsigned int fsrate;
	unsigned int slot_width;
	int bclk_rate;
	int rc = 0;

	dev_info(tas_dev->dev, "%s: %s\n",
		__func__, substream->stream == SNDRV_PCM_STREAM_PLAYBACK?
		"Playback":"Capture");

	fsrate = params_rate(params);
	switch (fsrate) {
	case 96000:
		break;
	case 88200:
		break;
	case 48000:
		break;
	case 44100:
		break;
	default:
		dev_err(tas_dev->dev,
			"%s: incorrect sample rate = %u\n",
			__func__, fsrate);
		rc = -EINVAL;
		goto out;
	}

	slot_width = params_width(params);
	switch (slot_width) {
	case 16:
		break;
	case 20:
		break;
	case 24:
		break;
	case 32:
		break;
	default:
		dev_err(tas_dev->dev,
			"%s: incorrect slot width = %u\n",
			__func__, slot_width);
		rc = -EINVAL;
		goto out;
	}

	bclk_rate = snd_soc_params_to_bclk(params);
	if (bclk_rate < 0) {
		dev_err(tas_dev->dev,
			"%s: incorrect bclk rate = %d\n",
			__func__, bclk_rate);
		rc = bclk_rate;
		goto out;
	}
	dev_info(tas_dev->dev, "%s: BCLK rate = %d Channel = %d"
		"Sample rate = %u slot width = %u\n",
		__func__, bclk_rate, params_channels(params),
		fsrate, slot_width);
out:
	return rc;
}

static int tasdevice_set_dai_sysclk(struct snd_soc_dai *codec_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct tasdevice_priv *tas_dev = snd_soc_dai_get_drvdata(codec_dai);

	dev_info(tas_dev->dev,
		"%s: clk_id = %d, freq = %u, CLK direction %s\n",
		__func__, clk_id, freq,
		dir == TASDEVICE_CLK_DIR_OUT ? "OUT":"IN");

	return 0;
}

void tasdevice_enable_irq(
	struct tasdevice_priv *tas_dev, bool is_enable)
{
	if (is_enable == tas_dev->irqinfo.irq_enable &&
		!gpio_is_valid(tas_dev->irqinfo.irq_gpio))
		return;

	if (is_enable)
		enable_irq(tas_dev->irqinfo.irq);
	else
		disable_irq_nosync(tas_dev->irqinfo.irq);

	tas_dev->irqinfo.irq_enable = is_enable;
}

static void irq_work_routine(struct work_struct *work)
{
	struct tasdevice_priv *tas_dev =
		container_of(work, struct tasdevice_priv,
		irqinfo.irq_work.work);

	dev_info(tas_dev->dev, "%s enter\n", __func__);

	mutex_lock(&tas_dev->codec_lock);
	if (tas_dev->is_runtime_suspend) {
		dev_info(tas_dev->dev,"%s, Runtime Suspended\n", __func__);
		goto end;
	}
	/*Logical Layer IRQ function, return is ignored*/
	if(tas_dev->irq_work_func)
		tas_dev->irq_work_func(tas_dev);
	else
		dev_info(tas_dev->dev, "%s, irq_work_func is NULL\n",
			__func__);

end:
	mutex_unlock(&tas_dev->codec_lock);
	dev_info(tas_dev->dev, "%s leave\n", __func__);
	return;
}

static irqreturn_t tasdevice_irq_handler(int irq,
	void *dev_id)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *)dev_id;
;
	/* get IRQ status after 100 ms */
	schedule_delayed_work(&tas_dev->irqinfo.irq_work,
		msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static void lpa_hwrst(struct gpio_desc *desc)
{
	gpiod_set_value_cansleep(desc, 1);
	usleep_range(500, 1000);
	gpiod_set_value_cansleep(desc, 0);
}

static void mpa_hwrst(struct gpio_desc *desc)
{
	gpiod_set_value_cansleep(desc, 0);
	usleep_range(500, 1000);
	gpiod_set_value_cansleep(desc, 1);
}

static int tasdevice_codec_probe(
	struct snd_soc_component *component)
{
	struct tasdevice_priv *tas_dev =
		snd_soc_component_get_drvdata(component);
	int ret = 0;

	mutex_lock(&tas_dev->codec_lock);
	tas_dev->component = component;

	strcpy(tas_dev->dev_name, tasdevice_i2c_id[tas_dev->chip_id].name);

	if (component->name_prefix)
		scnprintf(tas_dev->regbin_binaryname,
			TASDEVICE_REGBIN_FILENAME_LEN, "%s-%s-%uamp-reg.bin",
			component->name_prefix, tas_dev->dev_name,
			tas_dev->ndev);
	else
		scnprintf(tas_dev->regbin_binaryname,
			TASDEVICE_REGBIN_FILENAME_LEN, "%s-%uamp-reg.bin",
			tas_dev->dev_name, tas_dev->ndev);

	dev_info(tas_dev->dev, "load %s error = %d\n",
		tas_dev->regbin_binaryname, ret);
	ret = request_firmware_nowait(THIS_MODULE,
#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
		FW_ACTION_UEVENT
#else
		FW_ACTION_HOTPLUG
#endif
		, tas_dev->regbin_binaryname, tas_dev->dev, GFP_KERNEL, tas_dev,
		tasdevice_regbin_ready);
	if(ret) {
		dev_err(tas_dev->dev, "load %s error = %d\n",
			tas_dev->regbin_binaryname, ret);
		goto out;
	}

out:
	mutex_unlock(&tas_dev->codec_lock);
	return ret;
}

#ifdef TASDEVICE_ASYNC_POWER_CONTROL
static void tasdevice_set_power_state(
	struct tasdevice_priv *tas_dev, int state)
{
	switch (state) {
	case 0:
		schedule_delayed_work(&tas_dev->powercontrol_work,
			msecs_to_jiffies(20));
		break;
	default:
		if(!(tas_dev->pstream || tas_dev->cstream)) {
			if (tas_dev->irq_work_func != NULL)
				tasdevice_enable_irq(tas_dev, false);
			tasdevice_select_cfg_blk(tas_dev, tas_dev->cur_conf,
				TASDEVICE_BIN_BLK_PRE_SHUTDOWN);
		}
		break;
	}
}
#endif

static int tasdevice_mute(struct snd_soc_dai *dai, int mute,
	int stream)
{
	struct snd_soc_component *codec = dai->component;
	struct tasdevice_priv *tas_dev =
		snd_soc_component_get_drvdata(codec);

	mutex_lock(&tas_dev->codec_lock);

	if(mute) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			tas_dev->pstream = 0;
		else
			tas_dev->cstream = 0;
#ifndef TASDEVICE_ASYNC_POWER_CONTROL
		tasdevice_enable_irq(tas_dev, false);
		tasdevice_select_cfg_blk(tas_dev, tas_dev->cur_conf,
			TASDEVICE_BIN_BLK_PRE_SHUTDOWN);
#endif
	} else {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			tas_dev->pstream = 1;
		else
			tas_dev->cstream = 1;
#ifndef TASDEVICE_ASYNC_POWER_CONTROL
		tasdevice_select_cfg_blk(tas_dev, tas_dev->cur_conf,
			TASDEVICE_BIN_BLK_PRE_POWER_UP);
		tasdevice_enable_irq(tas_dev, true);
#endif
	}
#ifdef TASDEVICE_ASYNC_POWER_CONTROL
	tasdevice_set_power_state(tas_dev, mute);
#endif
	mutex_unlock(&tas_dev->codec_lock);
	return 0;
}

static void tasdevice_codec_remove(struct snd_soc_component *codec)
{
	struct tasdevice_priv *tas_dev =
		snd_soc_component_get_drvdata(codec);
	int mix_index = 0;

	if(tas_dev) {
		mutex_lock(&tas_dev->codec_lock);
		if(tas_dev->tas_ctrl.tasdevice_profile_controls) {
			for(mix_index = 0; mix_index <
				tas_dev->tas_ctrl.nr_controls; mix_index++)
				kfree(tas_dev->tas_ctrl.
					tasdevice_profile_controls
					[mix_index].name);
			tas_dev->tas_ctrl.nr_controls = 0;
			devm_kfree(tas_dev->dev,
				tas_dev->tas_ctrl.tasdevice_profile_controls);
		}
		tasdevice_config_info_remove(tas_dev);
		mutex_unlock(&tas_dev->codec_lock);
	}
	return;
}

static const struct snd_soc_component_driver soc_codec_driver_tasdevice = {
	.probe			= tasdevice_codec_probe,
	.remove			= tasdevice_codec_remove,
	.dapm_widgets		= tasdevice_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tasdevice_dapm_widgets),
	.dapm_routes		= tasdevice_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tasdevice_audio_map),
	.idle_bias_on		= 1,
	.endianness		= 1,
};

static struct snd_soc_dai_ops tasdevice_dai_ops = {
	.hw_params	= tasdevice_hw_params,
	.set_sysclk	= tasdevice_set_dai_sysclk,
	.mute_stream = tasdevice_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver tasdevice_dai_driver[] = {
	{
		.name = "tasdevice-codec",
		.playback = {
			.stream_name    = "Playback",
			.channels_min   = 2,
			.channels_max   = TASDEVICE_MAX_CHANNELS,
			.rates      = TASDEVICE_RATES,
			.formats    = TASDEVICE_FORMATS,
		},
		.capture = {
			.stream_name	 = "Capture",
			.channels_min	 = 2,
			.channels_max	 = TASDEVICE_MAX_CHANNELS,
			.rates		 = TASDEVICE_RATES,
			.formats	 = TASDEVICE_FORMATS,
		},
		.ops = &tasdevice_dai_ops,
#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
		.symmetric_rate = 1,
#else
		.symmetric_rates = 1,
#endif
	}
};

#ifdef TASDEVICE_ASYNC_POWER_CONTROL
static void powercontrol_routine(struct work_struct *work)
{
	struct tasdevice_priv *tas_dev =
		container_of(work, struct tasdevice_priv,
		powercontrol_work.work);

	dev_info(tas_dev->dev, "%s:%u: enter\n", __func__, __LINE__);

	mutex_lock(&tas_dev->codec_lock);
	tasdevice_select_cfg_blk(tas_dev, tas_dev->cur_conf,
		TASDEVICE_BIN_BLK_PRE_POWER_UP);
	if (tas_dev->irq_work_func != NULL)
		tasdevice_enable_irq(tas_dev, true);
	mutex_unlock(&tas_dev->codec_lock);

	dev_info(tas_dev->dev, "%s:%u: leave\n", __func__, __LINE__);
}
#endif

static int tasdevice_remove(struct tasdevice_priv *tas_dev)
{
	mutex_lock(&tas_dev->codec_lock);
	if (tas_dev->irq_work_func != NULL) {
		if (gpio_is_valid(tas_dev->irqinfo.irq_gpio)) {
			if (delayed_work_pending(&tas_dev->irqinfo.irq_work)) {
				dev_info(tas_dev->dev, "cancel IRQ work\n");
				cancel_delayed_work(&tas_dev->irqinfo.irq_work);
			}
			gpio_free(tas_dev->irqinfo.irq_gpio);
			free_irq(tas_dev->irqinfo.irq, tas_dev);
		}
	}
	mutex_unlock(&tas_dev->codec_lock);
	sysfs_remove_group(&tas_dev->dev->kobj,
		&tasdevice_attribute_group);
	mutex_destroy(&tas_dev->dev_lock);
	mutex_destroy(&tas_dev->codec_lock);

	return 0;
}

static int lpa_dev_swrst(struct tasdevice_priv *tas_dev, unsigned int chn)
{
	return tasdevice_dev_write(tas_dev, chn, TASDEVICE_REG_SWRESET,
		TASDEVICE_REG_SWRESET_RESET);
}

static int mpa_dev_swrst(struct tasdevice_priv *tas_dev, unsigned int chn)
{
	return tasdevice_dev_update_bits(tas_dev, chn, TASDEVICE_REG_SWRESET,
		TAS58XX_RST_MOD_MSK | TAS58XX_RST_CONTROL_REG_MSK,
		TAS58XX_RST_MOD_CLR | TAS58XX_RST_CONTROL_REG_CLR);
}

#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
	static int tasdevice_i2c_probe(struct i2c_client *i2c)
#else
	static int tasdevice_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
#endif
{
#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
	const struct i2c_device_id *id = i2c_match_id(tasdevice_i2c_id, i2c);
#endif
	struct tasdevice_priv *tas_dev = NULL;
	const struct acpi_device_id *acpi_id;
	unsigned int dev_addrs[MAX_DEV_NUM];
	int ret = 0, i = 0, ndev = 0;
	bool isacpi = true;

	tas_dev = devm_kzalloc(&i2c->dev, sizeof(*tas_dev), GFP_KERNEL);
	if (!tas_dev) {
		dev_err(&i2c->dev,
			"probe devm_kzalloc failed %d\n", ret);
		ret = -ENOMEM;
		goto out;
	}

	if (ACPI_HANDLE(&i2c->dev)) {
		acpi_id = acpi_match_device(i2c->dev.driver->acpi_match_table,
				&i2c->dev);
		if (!acpi_id) {
			dev_err(&i2c->dev, "No driver data\n");
			ret = -EINVAL;
			goto out;
		}
		tas_dev->chip_id = acpi_id->driver_data;
	} else {
		tas_dev->chip_id = (id != NULL) ? id->driver_data : 0;
		isacpi = false;
	}

	tas_dev->dev = &i2c->dev;
	tas_dev->client = i2c;

	if (tas_dev->chip_id >= MAX_DEVICE)
		tas_dev->chip_id = 0;

	tas_dev->regmap = devm_regmap_init_i2c(i2c, &tasdevice_i2c_regmap);
	if (IS_ERR(tas_dev->regmap)) {
		ret = PTR_ERR(tas_dev->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (isacpi) {
		ndev = device_property_read_u32_array(&i2c->dev,
			"ti,audio-devices", NULL, 0);
		if (ndev <= 0) {
			ndev = 1;
			dev_addrs[0] = i2c->addr;
		} else {
			ndev = (ndev < ARRAY_SIZE(dev_addrs))
				? ndev : ARRAY_SIZE(dev_addrs);
			ndev = device_property_read_u32_array(&i2c->dev,
				"ti,audio-devices", dev_addrs, ndev);
		}

		tas_dev->irqinfo.irq_gpio =
			acpi_dev_gpio_irq_get(ACPI_COMPANION(&i2c->dev), 0);
	} else if (IS_ENABLED(CONFIG_OF)) {
		struct device_node *np = tas_dev->dev->of_node;
		u64 addr;

		for (i = 0; i < TASDEVICE_MAX_CHANNELS; i++) {
			if (of_property_read_reg(np, i, &addr, NULL))
				break;
			dev_addrs[ndev++] = addr;
		}

		tas_dev->irqinfo.irq_gpio = of_irq_get(np, 0);
	} else {
		ndev = 1;
		dev_addrs[0] = i2c->addr;
	}

	for(i = 0; i < ndev; i++) {
		tas_dev->tasdevice[i].addr = dev_addrs[i];
		tas_dev->tasdevice[i].book = -1;
		tas_dev->tasdevice[i].dev_id = tas_dev->chip_id;
		dev_info(tas_dev->dev, "%s = 0x%02x", dts_tag[i],
			tas_dev->tasdevice[i].addr);
	}
	tas_dev->ndev = ndev;

	switch (tas_dev->chip_id) {
	case TAS2020:
	case TAS2110:
	case TAS2118:
	case TAS2120:
	case TAS2320:
	case TAS2560:
	case TAS2562:
	case TAS2564:
	case TAS257X:
	case TAS2764:
	case TAS2770:
	case TAS2780:
	tas_dev->dev_swrst = lpa_dev_swrst;
		break;
	case PCM9211:
	tas_dev->dev_swrst = pcm9211s_dev_rst;
		break;
	default:
	tas_dev->dev_swrst = mpa_dev_swrst;
		break;
	}

	tas_dev->rst_gpio = devm_gpiod_get_optional(&i2c->dev, "reset",
		GPIOD_OUT_HIGH);
	if (IS_ERR(tas_dev->rst_gpio))
		dev_err(&i2c->dev, "%s ERROR: Can't get reset GPIO\n", __func__);
	else
		switch (tas_dev->chip_id) {
		case TAS2020:
		case TAS2110:
		case TAS2118:
		case TAS2120:
		case TAS2320:
		case TAS2560:
		case TAS2562:
		case TAS2564:
		case TAS257X:
		case TAS2764:
		case TAS2770:
		case TAS2780:
		case PCM9211:
		tas_dev->hwrst = lpa_hwrst;
			break;
		default:
		tas_dev->hwrst = mpa_hwrst;
			break;
		}

	if (gpio_is_valid(tas_dev->irqinfo.irq_gpio)) {
		dev_dbg(tas_dev->dev, "irq-gpio = %d", tas_dev->irqinfo.irq_gpio);
		INIT_DELAYED_WORK(&tas_dev->irqinfo.irq_work, irq_work_routine);

		ret = gpio_request(tas_dev->irqinfo.irq_gpio, "TASDEV-IRQ");
		if (!ret) {
			gpio_direction_input(tas_dev->irqinfo.irq_gpio);

			tas_dev->irqinfo.irq = gpio_to_irq(tas_dev->irqinfo.irq_gpio);
			dev_info(tas_dev->dev, "irq = %d\n", tas_dev->irqinfo.irq);

			ret = request_threaded_irq(tas_dev->irqinfo.irq,
				tasdevice_irq_handler, NULL, IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT, tas_dev->client->name, tas_dev);
			if (!ret)
				disable_irq_nosync(tas_dev->irqinfo.irq);
			else
				dev_err(tas_dev->dev, "request_irq failed, %d\n", ret);
		} else {
			dev_err(tas_dev->dev, "%s: GPIO %d request error\n", __func__,
				tas_dev->irqinfo.irq_gpio);
		}
	} else {
		dev_err(tas_dev->dev, "Looking up irq gpio property failed %d\n",
			tas_dev->irqinfo.irq_gpio);
		ret = -1;
	}

	if (ret == 0) {
		switch (tas_dev->chip_id) {
		case TAS257X:
		tas_dev->irq_work_func = tas257x_irq_work_func;
			break;
		case TAS2564:
		tas_dev->irq_work_func = tas2564_irq_work_func;
			break;
		case TAS2560:
		tas_dev->irq_work_func = tas2560_irq_work_func;
			break;
		case PCM9211:
		tas_dev->irq_work_func = pcm9211s_irq_work_func;
			break;
		case TAS2110:
		case TAS2562:
		case TAS2770:
		tas_dev->irq_work_func = tas2770_irq_work_func;
			break;
		case TAS2780:
		case TAS2764:
		tas_dev->irq_work_func = tas2780_irq_work_func;
			break;
		}
	}

	mutex_init(&tas_dev->dev_lock);
	mutex_init(&tas_dev->codec_lock);

	i2c_set_clientdata(i2c, tas_dev);

	ret = sysfs_create_group(&tas_dev->dev->kobj,
		&tasdevice_attribute_group);
	if (ret < 0) {
		dev_err(&i2c->dev, "sysfs registration failed\n");
		goto out;
	}

	if (tas_dev->hwrst) {
		tas_dev->hwrst(tas_dev->rst_gpio);
	} else {
		for (i = 0; i < tas_dev->ndev; i++) {
			if (tas_dev->dev_swrst) {
				ret = tas_dev->dev_swrst(tas_dev, i);
				if (ret < 0)
					dev_err(tas_dev->dev,
						"dev %d swreset fail, %d\n",
						i, ret);
			} else {
				dev_info(tas_dev->dev,
					"Not define dev_rst()\n");
				break;
			}
		}
	}

#ifdef TASDEVICE_ASYNC_POWER_CONTROL
	INIT_DELAYED_WORK(&tas_dev->powercontrol_work,
		powercontrol_routine);
#endif
	ret = devm_snd_soc_register_component(&i2c->dev,
		&soc_codec_driver_tasdevice,
		tasdevice_dai_driver, ARRAY_SIZE(tasdevice_dai_driver));
	if (ret < 0) {
  		dev_err(&i2c->dev,
			"probe register component failed %d\n", ret);
	}

out:
	if (ret < 0 && tas_dev != NULL)
		tasdevice_remove(tas_dev);
	return ret;
}

#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
	static void tasdevice_i2c_remove(struct i2c_client *client)
#else
	static int tasdevice_i2c_remove(struct i2c_client *client)
#endif
{
	struct tasdevice_priv *tas_dev = i2c_get_clientdata(client);

	if(tas_dev)
		tasdevice_remove(tas_dev);
#if KERNEL_VERSION(6, 4, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

static int tasdevice_pm_suspend(struct device *dev)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);

	if (!tas_dev) {
		dev_err(tas_dev->dev, "%s:%u:drvdata is NULL\n",
			__func__,__LINE__);
		return -EINVAL;
	}

	mutex_lock(&tas_dev->codec_lock);

	tas_dev->is_runtime_suspend = true;

	if (tas_dev->irq_work_func != NULL) {
		if (delayed_work_pending(&tas_dev->irqinfo.irq_work)) {
			dev_dbg(tas_dev->dev, "cancel IRQ work\n");
			cancel_delayed_work_sync(&tas_dev->irqinfo.irq_work);
		}
	}
	mutex_unlock(&tas_dev->codec_lock);
	return 0;
}

static int tasdevice_pm_resume(struct device *dev)
{
	struct tasdevice_priv *tas_dev = dev_get_drvdata(dev);

	if (!tas_dev) {
		dev_err(tas_dev->dev, "%s:%u:drvdata is NULL\n",
			__func__,__LINE__);
		return -EINVAL;
	}

	mutex_lock(&tas_dev->codec_lock);
	tas_dev->is_runtime_suspend = false;
	mutex_unlock(&tas_dev->codec_lock);
	return 0;
}

static const struct dev_pm_ops tasdevice_pm_ops = {
	.suspend = tasdevice_pm_suspend,
	.resume = tasdevice_pm_resume
};

#if defined(CONFIG_OF)
static const struct of_device_id tasdevice_of_match[] = {
	{ .compatible = "ti,pcm9211s" },
	{ .compatible = "ti,tas2020" },
	{ .compatible = "ti,tas2110" },
	{ .compatible = "ti,tas2118" },
	{ .compatible = "ti,tas2120" },
	{ .compatible = "ti,tas2320" },
	{ .compatible = "ti,tas2560" },
	{ .compatible = "ti,tas2562" },
	{ .compatible = "ti,tas2564" },
	{ .compatible = "ti,tas257x" },
	{ .compatible = "ti,tas2764" },
	{ .compatible = "ti,tas2770" },
	{ .compatible = "ti,tas2780" },
	{ .compatible = "ti,tas5802" },
	{ .compatible = "ti,tas5805" },
	{ .compatible = "ti,tas5806m" },
	/* TAS5806M with Headphone */
	{ .compatible = "ti,tas5806md" },
	{ .compatible = "ti,tas5815" },
	{ .compatible = "ti,tas5822" },
	{ .compatible = "ti,tas5825m" },
	{ .compatible = "ti,tas5825p" },
	{ .compatible = "ti,tas5827" },
	{ .compatible = "ti,tas5828" },
	{},
};
MODULE_DEVICE_TABLE(of, tasdevice_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id tasdevice_acpi_match[] = {
	{ "PCM9211S", PCM9211 },
	{ "TAS2020", TAS2020 },
	{ "TAS2110", TAS2110 },
	{ "TAS2118", TAS2118 },
	{ "TAS2120", TAS2120 },
	{ "TAS2320", TAS2320 },
	{ "TAS2560", TAS2560 },
	{ "TAS2562", TAS2562 },
	{ "TAS2564", TAS2564 },
	{ "TAS257X", TAS257X },
	{ "TAS2764", TAS2764 },
	{ "TAS2770", TAS2770 },
	{ "TAS2780", TAS2780 },
	{ "TAS5802", TAS5802 },
	{ "TAS5805", TAS5805 },
	{ "TAS5806m", TAS5806M },
	/* TAS5806M with Headphone */
	{ "TS5806MD", TAS5806MD },
	{ "TAS5815", TAS5815 },
	{ "TAS5822", TAS5822 },
	{ "TAS5825M", TAS5825M },
	{ "TAS5825P", TAS5825P },
	{ "TAS5827", TAS5827 },
	{ "TAS5828", TAS5828 },
	{},
};
MODULE_DEVICE_TABLE(acpi, tasdevice_acpi_match);
#endif

MODULE_DEVICE_TABLE(i2c, tasdevice_i2c_id);

static struct i2c_driver tasdevice_i2c_driver = {
	.driver = {
		.name	= "tasdevice-codec",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tasdevice_of_match),
		.acpi_match_table = ACPI_PTR(tasdevice_acpi_match),
		.pm = &tasdevice_pm_ops,
	},
	.probe		= tasdevice_i2c_probe,
	.remove = tasdevice_i2c_remove,
	.id_table	= tasdevice_i2c_id,
};

module_i2c_driver(tasdevice_i2c_driver);

MODULE_AUTHOR("Shenghao Ding <shenghao-ding@ti.com>");
MODULE_DESCRIPTION("ASoC TASDEVICE Driver");
MODULE_LICENSE("GPL");
