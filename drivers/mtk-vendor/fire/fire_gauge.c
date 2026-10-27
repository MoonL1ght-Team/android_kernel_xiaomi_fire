// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: MT6358 fuel gauge readout
 *
 * The bootloader starts the MT6358 coulomb counter; read its latched
 * instant current and charge counter the way the vendor mt6358-gauge
 * driver does (RG_FGADC_*, R_FG 5 mOhm, CAR_TUNE 1.00).
 */

#include <linux/delay.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "fire_charger.h"

#define RG_FGADC_CON1		0xd0a
#define  FG_LATCHDATA_ST	BIT(15)
#define  FG_SW_CLEAR		BIT(3)
#define  FG_SW_READ_PRE		BIT(0)
#define RG_FGADC_CAR_CON0	0xd14
#define RG_FGADC_CAR_CON1	0xd16
#define RG_FGADC_CUR_CON0	0xd8a

/* vendor units: current LSB 381470e-5 x 0.1 mA, charge LSB 108507e-3 */
#define UNIT_FGCURRENT		381470
#define UNIT_FGCAR		108507
/* the units are for a 10 mOhm sense resistor; fire has 5 mOhm */
#define FIRE_RFG_SCALE		2

static struct regmap *fire_gauge_map;
static DEFINE_MUTEX(fire_gauge_lock);

static struct regmap *fire_gauge_regmap(void)
{
	struct platform_device *pmic;
	struct device_node *np;
	struct mt6397_chip *chip;

	if (fire_gauge_map)
		return fire_gauge_map;
	np = of_find_compatible_node(NULL, NULL, "mediatek,mt6358");
	pmic = np ? of_find_device_by_node(np) : NULL;
	of_node_put(np);
	if (!pmic)
		return NULL;
	chip = platform_get_drvdata(pmic);
	if (chip)
		fire_gauge_map = chip->regmap;
	put_device(&pmic->dev);
	return fire_gauge_map;
}

static int fire_gauge_poll(struct regmap *map, bool set)
{
	unsigned int val;
	int i;

	for (i = 0; i < 1000; i++) {
		if (regmap_read(map, RG_FGADC_CON1, &val))
			return -EIO;
		if (!!(val & FG_LATCHDATA_ST) == set)
			return 0;
	}
	return -ETIMEDOUT;
}

/* Current in mA (positive while charging), charge counter in 0.1 mAh. */
int fire_gauge_read(int *ma, int *car_01mah)
{
	struct regmap *map = fire_gauge_regmap();
	unsigned int cur, lo, hi;
	s64 car;
	s32 raw;
	int ret;

	if (!map)
		return -ENODEV;

	mutex_lock(&fire_gauge_lock);
	ret = regmap_write(map, RG_FGADC_CON1, FG_SW_READ_PRE);
	ret = ret ?: fire_gauge_poll(map, true);
	ret = ret ?: regmap_read(map, RG_FGADC_CUR_CON0, &cur);
	ret = ret ?: regmap_read(map, RG_FGADC_CAR_CON0, &lo);
	ret = ret ?: regmap_read(map, RG_FGADC_CAR_CON1, &hi);
	regmap_update_bits(map, RG_FGADC_CON1, FG_SW_CLEAR | FG_SW_READ_PRE,
			   FG_SW_CLEAR);
	fire_gauge_poll(map, false);
	regmap_update_bits(map, RG_FGADC_CON1, FG_SW_CLEAR, 0);
	mutex_unlock(&fire_gauge_lock);
	if (ret)
		return ret;

	*ma = div_s64((s64)(s16)cur * UNIT_FGCURRENT * FIRE_RFG_SCALE,
		      100000 * 10);

	/* 21-bit two's complement, bits 31:11 of CAR_CON1:CAR_CON0 */
	raw = (s32)((hi << 16 | lo) & 0xfffff800) >> 11;
	car = (s64)raw * UNIT_FGCAR * FIRE_RFG_SCALE;
	*car_01mah = div_s64(car, 100000);
	return 0;
}

/*
 * The UI level is kept across reboots in the RTC, as the stock 4.19
 * gauge does (RTC_FGSOC spare: RTC_AL_MTH bits 15:8, bit 7 = valid,
 * bits 6:0 = percent). The RTC runs from the battery.
 */
#define MT6358_RTC_BASE		0x588
#define RTC_BBPU		(MT6358_RTC_BASE + 0x00)
#define  RTC_BBPU_CBUSY		BIT(6)
#define RTC_AL_MTH		(MT6358_RTC_BASE + 0x22)
#define RTC_WRTGR		(MT6358_RTC_BASE + 0x3a)
#define RTC_FGSOC_VALID		0x80

int fire_gauge_soc_load(void)
{
	struct regmap *map = fire_gauge_regmap();
	unsigned int v;

	if (!map || regmap_read(map, RTC_AL_MTH, &v))
		return -ENODEV;
	v >>= 8;
	if (!(v & RTC_FGSOC_VALID) || (v & 0x7f) > 100)
		return -ENODATA;
	return v & 0x7f;
}

void fire_gauge_soc_save(int soc)
{
	struct regmap *map = fire_gauge_regmap();
	unsigned int v;
	int i;

	if (!map)
		return;
	mutex_lock(&fire_gauge_lock);
	if (regmap_update_bits(map, RTC_AL_MTH, 0xff00,
			       (RTC_FGSOC_VALID | clamp(soc, 0, 100)) << 8))
		goto out;
	/* latch into the RTC domain */
	regmap_write(map, RTC_WRTGR, 1);
	for (i = 0; i < 100; i++) {
		if (regmap_read(map, RTC_BBPU, &v) || !(v & RTC_BBPU_CBUSY))
			break;
		udelay(10);
	}
out:
	mutex_unlock(&fire_gauge_lock);
}
