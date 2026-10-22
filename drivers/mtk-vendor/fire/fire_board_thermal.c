// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: board NTC thermistors as the stock MTK thermal zones
 *
 * The device thermal HAL reads zones by their 6.6 vendor names, and
 * system_server will not boot unless at least one is readable. Expose
 * the AP and modem PA thermistors (AUXADC channels 0 and 1, described by
 * the stock DT as mtboard-thermistor4/5) as "mtktsAP" and
 * "mtktsbtsmdpa", converted like the vendor mtk_ts_bts drivers do by
 * default: NCP15WF104F03RC 100k NTC, 390k pull-up to 1.8 V.
 */

#include <linux/iio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define FIRE_NTC_PULL_UP_OHM	390000
#define FIRE_NTC_PULL_UP_MV	1800

struct fire_ntc_point {
	int temp;	/* degrees C */
	int ohm;
};

/* NCP15WF104F03RC, from the vendor mtk_ts_bts table 7 */
static const struct fire_ntc_point fire_ntc_table[] = {
	{ -40, 4397119 }, { -35, 3088599 }, { -30, 2197225 }, { -25, 1581881 },
	{ -20, 1151037 }, { -15, 846579 }, { -10, 628988 }, { -5, 471632 },
	{ 0, 357012 }, { 5, 272500 }, { 10, 209710 }, { 15, 162651 },
	{ 20, 127080 }, { 25, 100000 }, { 30, 79222 }, { 35, 63167 },
	{ 40, 50677 }, { 45, 40904 }, { 50, 33195 }, { 55, 27091 },
	{ 60, 22224 }, { 65, 18323 }, { 70, 15184 }, { 75, 12635 },
	{ 80, 10566 }, { 85, 8873 }, { 90, 7481 }, { 95, 6337 },
	{ 100, 5384 }, { 105, 4594 }, { 110, 3934 }, { 115, 3380 },
	{ 120, 2916 }, { 125, 2522 },
};

struct fire_ntc {
	struct iio_channel *chan;
};

/* Resistance to millidegrees, interpolating between table points. */
static int fire_ntc_ohm_to_mdeg(long long ohm)
{
	const struct fire_ntc_point *lo, *hi;
	unsigned int i;

	if (ohm >= fire_ntc_table[0].ohm)
		return fire_ntc_table[0].temp * 1000;

	for (i = 1; i < ARRAY_SIZE(fire_ntc_table); i++) {
		hi = &fire_ntc_table[i];
		if (ohm < hi->ohm)
			continue;
		lo = &fire_ntc_table[i - 1];
		return lo->temp * 1000 +
		       (int)div64_s64((lo->ohm - ohm) * (hi->temp - lo->temp) * 1000,
				      lo->ohm - hi->ohm);
	}
	return fire_ntc_table[ARRAY_SIZE(fire_ntc_table) - 1].temp * 1000;
}

static int fire_ntc_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct fire_ntc *ntc = thermal_zone_device_priv(tz);
	int mv, ret;

	ret = iio_read_channel_processed(ntc->chan, &mv);
	if (ret < 0)
		return ret;
	/* Open thermistor or a reading at the rail */
	if (mv <= 0 || mv >= FIRE_NTC_PULL_UP_MV)
		return -ENODATA;

	*temp = fire_ntc_ohm_to_mdeg((long long)FIRE_NTC_PULL_UP_OHM * mv /
				     (FIRE_NTC_PULL_UP_MV - mv));
	return 0;
}

static const struct thermal_zone_device_ops fire_ntc_ops = {
	.get_temp = fire_ntc_get_temp,
};

struct fire_ntc_desc {
	const char *channel;
	const char *zone;
};

static int fire_ntc_probe(struct platform_device *pdev)
{
	const struct fire_ntc_desc *desc = of_device_get_match_data(&pdev->dev);
	struct thermal_zone_device *tz;
	struct fire_ntc *ntc;

	ntc = devm_kzalloc(&pdev->dev, sizeof(*ntc), GFP_KERNEL);
	if (!ntc)
		return -ENOMEM;

	ntc->chan = devm_iio_channel_get(&pdev->dev, desc->channel);
	if (IS_ERR(ntc->chan))
		return dev_err_probe(&pdev->dev, PTR_ERR(ntc->chan),
				     "no %s\n", desc->channel);

	tz = thermal_tripless_zone_device_register(desc->zone, ntc,
						   &fire_ntc_ops, NULL);
	if (IS_ERR(tz))
		return PTR_ERR(tz);
	platform_set_drvdata(pdev, tz);

	return thermal_zone_device_enable(tz);
}

static void fire_ntc_remove(struct platform_device *pdev)
{
	thermal_zone_device_unregister(platform_get_drvdata(pdev));
}

static const struct fire_ntc_desc fire_ntc_ap = {
	.channel = "thermistor-ch0",
	.zone = "mtktsAP",
};

static const struct fire_ntc_desc fire_ntc_mdpa = {
	.channel = "thermistor-ch1",
	.zone = "mtktsbtsmdpa",
};

static const struct of_device_id fire_ntc_match[] = {
	{ .compatible = "mediatek,mtboard-thermistor4", .data = &fire_ntc_ap },
	{ .compatible = "mediatek,mtboard-thermistor5", .data = &fire_ntc_mdpa },
	{ }
};

static struct platform_driver fire_ntc_driver = {
	.probe = fire_ntc_probe,
	.remove = fire_ntc_remove,
	.driver = {
		.name = "fire-board-thermal",
		.of_match_table = fire_ntc_match,
	},
};
module_platform_driver(fire_ntc_driver);

MODULE_DESCRIPTION("Xiaomi fire board NTC thermal zones");
MODULE_LICENSE("GPL");
