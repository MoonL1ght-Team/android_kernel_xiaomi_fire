// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: minimal battery and USB power supplies
 *
 * The vendor fuel gauge and charger stack is not ported. Without any
 * battery Android reports 0% and shuts the phone down, so estimate the
 * charge from the battery voltage the MT6358 AUXADC measures (BATADC)
 * with a Li-ion open circuit voltage curve once at boot, then follow
 * the MT6358 coulomb counter. Report USB as online from the charger, or
 * when the charger detect voltage (VCDT) is up.
 */

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#include <dt-bindings/iio/adc/mediatek,mt6358-auxadc.h>

#include "fire_charger.h"

#define FIRE_BAT_POLL		(10 * HZ)
/* VCDT reads about 1/10 of VBUS; anything above ~3 V of VBUS is a charger */
#define FIRE_VCDT_ONLINE_MV	300
/* 5000 mAh, in the gauge's 0.1 mAh */
#define FIRE_BAT_Q		50000
/* rough cell + path resistance, to get an OCV from a loaded voltage */
#define FIRE_BAT_R_MOHM		150
/* below this, discharging, Android is told 0% so it shuts down */
#define FIRE_VBAT_EMPTY_MV	3400
/* how far the RTC level may be from the boot voltage estimate */
#define FIRE_SOC_TRUST		30

struct fire_ocv_point {
	int mv;
	int percent;
};

/* Typical 4.45 V Li-ion polymer curve at rest */
static const struct fire_ocv_point fire_ocv[] = {
	{ 3400, 0 }, { 3500, 2 }, { 3600, 5 }, { 3680, 10 }, { 3720, 20 },
	{ 3760, 30 }, { 3800, 40 }, { 3850, 50 }, { 3910, 60 }, { 3980, 70 },
	{ 4070, 80 }, { 4170, 90 }, { 4280, 97 }, { 4380, 100 },
};

struct fire_battery {
	struct iio_dev *adc;
	struct power_supply *bat, *usb;
	struct delayed_work work;
	int vbat_mv, vcdt_mv;
	int ma;			/* gauge current, + while charging */
	bool have_gauge;
	int charge;		/* 0.1 mAh left */
	int car_ref, charge_ref;	/* counter and charge at the anchor */
	int empty_cnt;
	int saved_soc;		/* last level written to the RTC */
};

static int fire_adc_mv(struct iio_dev *adc, int channel, int *mv)
{
	const struct iio_chan_spec *chan = NULL;
	int raw, num, den = 1, ret, i, unused;

	for (i = 0; i < adc->num_channels; i++)
		if (adc->channels[i].channel == channel)
			chan = &adc->channels[i];
	if (!chan)
		return -EINVAL;

	ret = adc->info->read_raw(adc, chan, &raw, &unused, IIO_CHAN_INFO_RAW);
	if (ret < 0)
		return ret;
	ret = adc->info->read_raw(adc, chan, &num, &den, IIO_CHAN_INFO_SCALE);
	if (ret < 0)
		return ret;
	if (ret != IIO_VAL_FRACTIONAL)
		den = 1;

	/* The scale is the full range; the reading has realbits of it. */
	*mv = div_s64((s64)raw * num, den) >> chan->scan_type.realbits;
	return 0;
}

static int fire_ocv_percent(int mv)
{
	const struct fire_ocv_point *lo, *hi;
	unsigned int i;

	if (mv <= fire_ocv[0].mv)
		return 0;
	for (i = 1; i < ARRAY_SIZE(fire_ocv); i++) {
		hi = &fire_ocv[i];
		if (mv > hi->mv)
			continue;
		lo = &fire_ocv[i - 1];
		return lo->percent + (mv - lo->mv) * (hi->percent - lo->percent) /
				     (hi->mv - lo->mv);
	}
	return 100;
}

static void fire_battery_anchor(struct fire_battery *fb, int car, int charge)
{
	fb->charge = clamp(charge, 0, FIRE_BAT_Q);
	fb->charge_ref = fb->charge;
	fb->car_ref = car;
}

static void fire_battery_update(struct fire_battery *fb)
{
	struct fire_charger_state st;
	bool charger = !fire_charger_get(&st);
	int mv, ma, car;

	if (!fire_adc_mv(fb->adc, MT6358_AUXADC_BATADC, &mv))
		fb->vbat_mv = mv;
	if (!fire_adc_mv(fb->adc, MT6358_AUXADC_VCDT, &mv))
		fb->vcdt_mv = mv;

	if (fire_gauge_read(&ma, &car))
		return;
	fb->ma = ma;
	if (!fb->have_gauge) {
		int ocv = fb->vbat_mv - ma * FIRE_BAT_R_MOHM / 1000;
		int est = fire_ocv_percent(ocv);
		int saved = fire_gauge_soc_load();
		int soc = est;

		/*
		 * The voltage under the boot load is a rough guess; prefer
		 * the level saved in the RTC unless it is far off (battery
		 * swapped, long time off).
		 */
		if (saved >= 0 && abs(saved - est) <= FIRE_SOC_TRUST)
			soc = saved;
		fb->have_gauge = true;
		fire_battery_anchor(fb, car, soc * FIRE_BAT_Q / 100);
		fb->saved_soc = soc;
		pr_info("fire-battery: vbat %d mV, %d mA, ocv %d mV -> %d%%, rtc %d%%, using %d%%\n",
			fb->vbat_mv, ma, ocv, est, saved, soc);
		return;
	}

	fb->charge = clamp(fb->charge_ref + car - fb->car_ref, 0, FIRE_BAT_Q);
	if (fb->charge == 0 || fb->charge == FIRE_BAT_Q ||
	    (charger && st.online && st.chrg_stat == 3))
		fire_battery_anchor(fb, car,
				    charger && st.chrg_stat == 3 ? FIRE_BAT_Q :
				    fb->charge);

	if (DIV_ROUND_CLOSEST(fb->charge * 100, FIRE_BAT_Q) != fb->saved_soc) {
		fb->saved_soc = DIV_ROUND_CLOSEST(fb->charge * 100, FIRE_BAT_Q);
		fire_gauge_soc_save(fb->saved_soc);
	}

	if (fb->vbat_mv < FIRE_VBAT_EMPTY_MV && fb->ma <= 0)
		fb->empty_cnt++;
	else
		fb->empty_cnt = 0;
}

static int fire_battery_capacity(struct fire_battery *fb)
{
	/* twice in a row, not on a single load dip */
	if (fb->empty_cnt >= 2)
		return 0;
	if (!fb->have_gauge)
		return max(fire_ocv_percent(fb->vbat_mv), 1);
	return clamp(DIV_ROUND_CLOSEST(fb->charge * 100, FIRE_BAT_Q), 1, 100);
}

static void fire_battery_work(struct work_struct *work)
{
	struct fire_battery *fb = container_of(to_delayed_work(work),
					       struct fire_battery, work);

	fire_battery_update(fb);
	power_supply_changed(fb->bat);
	power_supply_changed(fb->usb);
	schedule_delayed_work(&fb->work, FIRE_BAT_POLL);
}

static bool fire_usb_online(struct fire_battery *fb)
{
	struct fire_charger_state st;

	if (!fire_charger_get(&st))
		return st.online;
	return fb->vcdt_mv > FIRE_VCDT_ONLINE_MV;
}

static int fire_bat_status(struct fire_battery *fb)
{
	struct fire_charger_state st;

	if (fire_charger_get(&st))
		return fire_usb_online(fb) ? POWER_SUPPLY_STATUS_CHARGING :
					     POWER_SUPPLY_STATUS_DISCHARGING;
	if (!st.online)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	switch (st.chrg_stat) {
	case 1:
	case 2:
		return POWER_SUPPLY_STATUS_CHARGING;
	case 3:
		return POWER_SUPPLY_STATUS_FULL;
	default:
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
}

static int fire_bat_get(struct power_supply *psy, enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct fire_battery *fb = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = fire_bat_status(fb);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW: {
		struct fire_charger_state st;

		/* positive while charging */
		if (fb->have_gauge)
			val->intval = fb->ma * 1000;
		else
			val->intval = fire_charger_get(&st) ? 0 : st.ichg_ma * 1000;
		break;
	}
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = fb->vbat_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = fire_battery_capacity(fb);
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = fb->charge * 100;	/* uAh */
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = FIRE_BAT_Q * 100;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int fire_usb_get(struct power_supply *psy, enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct fire_battery *fb = power_supply_get_drvdata(psy);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;
	val->intval = fire_usb_online(fb);
	return 0;
}

static const enum power_supply_property fire_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};

static const enum power_supply_property fire_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc fire_bat_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = fire_bat_props,
	.num_properties = ARRAY_SIZE(fire_bat_props),
	.get_property = fire_bat_get,
};

static const struct power_supply_desc fire_usb_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = fire_usb_props,
	.num_properties = ARRAY_SIZE(fire_usb_props),
	.get_property = fire_usb_get,
};

static int fire_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = {};
	struct device_node *np;
	struct fire_battery *fb;
	struct device *adc_dev;
	int temp_mv = 0;

	fb = devm_kzalloc(&pdev->dev, sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return -ENOMEM;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mt6358-auxadc");
	if (!np)
		return -ENODEV;
	adc_dev = bus_find_device_by_of_node(&iio_bus_type, np);
	of_node_put(np);
	if (!adc_dev)
		return -EPROBE_DEFER;
	fb->adc = dev_to_iio_dev(adc_dev);

	fire_battery_update(fb);
	fire_adc_mv(fb->adc, MT6358_AUXADC_BAT_TEMP, &temp_mv);
	dev_info(&pdev->dev, "vbat %d mV (%d%%), vcdt %d mV, bat_temp %d mV\n",
		 fb->vbat_mv, fire_ocv_percent(fb->vbat_mv), fb->vcdt_mv,
		 temp_mv);

	cfg.drv_data = fb;
	fb->bat = devm_power_supply_register(&pdev->dev, &fire_bat_desc, &cfg);
	if (IS_ERR(fb->bat))
		return PTR_ERR(fb->bat);
	fb->usb = devm_power_supply_register(&pdev->dev, &fire_usb_desc, &cfg);
	if (IS_ERR(fb->usb))
		return PTR_ERR(fb->usb);

	INIT_DELAYED_WORK(&fb->work, fire_battery_work);
	schedule_delayed_work(&fb->work, FIRE_BAT_POLL);
	return 0;
}

static struct platform_driver fire_battery_driver = {
	.probe = fire_battery_probe,
	.driver = {
		.name = "battery",
		.suppress_bind_attrs = true,
	},
};

static int __init fire_battery_init(void)
{
	struct platform_device *pdev;
	int ret;

	ret = platform_driver_register(&fire_battery_driver);
	if (ret)
		return ret;
	/* /sys/devices/platform/battery is labelled sysfs_batteryinfo */
	pdev = platform_device_register_simple("battery", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&fire_battery_driver);
		return PTR_ERR(pdev);
	}
	return 0;
}
late_initcall(fire_battery_init);
