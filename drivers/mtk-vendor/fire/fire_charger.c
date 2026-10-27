// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: SYV690 charger (BQ25890 register compatible)
 *
 * The vendor charger framework is not ported. The SYV690 charges on its
 * own with its power-on defaults, but its I2C watchdog expires 40 s after
 * the last host access and puts the registers back to those defaults.
 * Program the limits the stock device tree gives, turn the watchdog off,
 * and report VBUS/charge state and the ADC readings to fire_battery.
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>
#include <linux/suspend.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#include "fire_charger.h"

#define REG00			0x00	/* EN_HIZ, EN_ILIM, IINLIM */
#define  REG00_IINLIM		GENMASK(5, 0)	/* 100 mA + 50 mA */
#define REG02			0x02
#define  REG02_CONV_START	BIT(7)
#define  REG02_CONV_RATE	BIT(6)	/* continuous ADC, 1 s */
#define REG03			0x03
#define  REG03_CHG_CONFIG	BIT(4)
#define REG04			0x04
#define  REG04_ICHG		GENMASK(6, 0)	/* 64 mA */
#define REG06			0x06
#define  REG06_VREG		GENMASK(7, 2)	/* 3840 mV + 16 mV */
#define REG07			0x07
#define  REG07_WATCHDOG		GENMASK(5, 4)
#define REG0B			0x0b
#define  REG0B_VBUS_STAT	GENMASK(7, 5)
#define  REG0B_CHRG_STAT	GENMASK(4, 3)
#define  REG0B_PG_STAT		BIT(2)
#define REG0C			0x0c	/* faults */
#define REG0E			0x0e
#define  REG0E_BATV		GENMASK(6, 0)	/* 2304 mV + 20 mV */
#define REG11			0x11
#define  REG11_VBUS_GD		BIT(7)
#define  REG11_VBUSV		GENMASK(6, 0)	/* 2600 mV + 100 mV */
#define REG12			0x12
#define  REG12_ICHGR		GENMASK(6, 0)	/* 50 mA */
#define REG14			0x14
#define  REG14_PN		GENMASK(5, 3)

#define FIRE_CHG_POLL		(5 * HZ)

static const struct regmap_config fire_chg_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG14,
};

static struct {
	struct regmap *map;
	struct delayed_work work;
	struct fire_charger_state st;
	struct wakeup_source *ws;
	bool ready;
} fire_chg;

int fire_charger_get(struct fire_charger_state *st)
{
	if (!READ_ONCE(fire_chg.ready))
		return -ENODEV;
	*st = fire_chg.st;
	return 0;
}

static void fire_chg_update(void)
{
	struct fire_charger_state st = {};
	unsigned int r0b, r0c, r0e, r11, r12;

	if (regmap_read(fire_chg.map, REG0B, &r0b) ||
	    regmap_read(fire_chg.map, REG0C, &r0c) ||
	    regmap_read(fire_chg.map, REG0E, &r0e) ||
	    regmap_read(fire_chg.map, REG11, &r11) ||
	    regmap_read(fire_chg.map, REG12, &r12))
		return;

	st.vbus_type = FIELD_GET(REG0B_VBUS_STAT, r0b);
	st.chrg_stat = FIELD_GET(REG0B_CHRG_STAT, r0b);
	st.online = (r0b & REG0B_PG_STAT) || (r11 & REG11_VBUS_GD);
	st.faults = r0c;
	st.vbat_mv = 2304 + 20 * FIELD_GET(REG0E_BATV, r0e);
	st.vbus_mv = st.online ? 2600 + 100 * FIELD_GET(REG11_VBUSV, r11) : 0;
	st.ichg_ma = 50 * FIELD_GET(REG12_ICHGR, r12);

	/*
	 * Stay awake while plugged in, as the stock USB driver does: suspend
	 * takes USB (and adb) down with it.
	 */
	if (st.online && !fire_chg.ws->active)
		__pm_stay_awake(fire_chg.ws);
	else if (!st.online && fire_chg.ws->active)
		__pm_relax(fire_chg.ws);

	if (st.online != fire_chg.st.online || st.chrg_stat != fire_chg.st.chrg_stat)
		pr_info("fire-charger: online %d type %u stat %u vbus %d mV ichg %d mA fault %#x\n",
			st.online, st.vbus_type, st.chrg_stat, st.vbus_mv,
			st.ichg_ma, st.faults);
	fire_chg.st = st;
	WRITE_ONCE(fire_chg.ready, true);
}

static void fire_chg_work(struct work_struct *work)
{
	fire_chg_update();
	schedule_delayed_work(&fire_chg.work, FIRE_CHG_POLL);
}

/*
 * After any wakeup (a cable being plugged in wakes us through the PMIC)
 * look at the charger right away, and stay up long enough to do so.
 */
static int fire_chg_pm_notify(struct notifier_block *nb, unsigned long ev,
			      void *unused)
{
	if (ev == PM_POST_SUSPEND) {
		__pm_wakeup_event(fire_chg.ws, 3000);
		mod_delayed_work(system_wq, &fire_chg.work, 0);
	}
	return NOTIFY_OK;
}

static struct notifier_block fire_chg_pm_nb = {
	.notifier_call = fire_chg_pm_notify,
};

static int fire_chg_u32(struct device *dev, const char *prop, u32 def)
{
	u32 v;

	return of_property_read_u32(dev->of_node, prop, &v) ? def : v;
}

static int fire_chg_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	unsigned int pn;
	u32 ilim, ichg, vreg;
	int ret;

	fire_chg.map = devm_regmap_init_i2c(client, &fire_chg_regmap);
	if (IS_ERR(fire_chg.map))
		return PTR_ERR(fire_chg.map);

	ret = regmap_read(fire_chg.map, REG14, &pn);
	if (ret)
		return dev_err_probe(dev, ret, "no reply\n");

	ilim = fire_chg_u32(dev, "silergy,syv690,usb-ilim", 500);
	ichg = fire_chg_u32(dev, "silergy,syv690,usb-ichg", 1000);
	vreg = fire_chg_u32(dev, "silergy,syv690,usb-vreg", 4400);
	ilim = clamp(ilim, 100U, 3250U);
	ichg = clamp(ichg, 0U, 3008U);
	vreg = clamp(vreg, 3840U, 4608U);

	/* watchdog off first, so the limits stay */
	ret = regmap_update_bits(fire_chg.map, REG07, REG07_WATCHDOG, 0);
	ret = ret ?: regmap_update_bits(fire_chg.map, REG00, REG00_IINLIM,
					FIELD_PREP(REG00_IINLIM, (ilim - 100) / 50));
	ret = ret ?: regmap_update_bits(fire_chg.map, REG04, REG04_ICHG,
					FIELD_PREP(REG04_ICHG, ichg / 64));
	ret = ret ?: regmap_update_bits(fire_chg.map, REG06, REG06_VREG,
					FIELD_PREP(REG06_VREG, (vreg - 3840) / 16));
	ret = ret ?: regmap_update_bits(fire_chg.map, REG03, REG03_CHG_CONFIG,
					REG03_CHG_CONFIG);
	ret = ret ?: regmap_update_bits(fire_chg.map, REG02,
					REG02_CONV_START | REG02_CONV_RATE,
					REG02_CONV_START | REG02_CONV_RATE);
	if (ret)
		return dev_err_probe(dev, ret, "setup failed\n");

	dev_info(dev, "fire-charger: PN %lu, ilim %u mA, ichg %u mA, vreg %u mV\n",
		 FIELD_GET(REG14_PN, pn), ilim, ichg, vreg);

	fire_chg.ws = wakeup_source_register(dev, "fire-usb");
	if (!fire_chg.ws)
		return -ENOMEM;
	INIT_DELAYED_WORK(&fire_chg.work, fire_chg_work);
	fire_chg_update();
	schedule_delayed_work(&fire_chg.work, FIRE_CHG_POLL);
	register_pm_notifier(&fire_chg_pm_nb);
	return 0;
}

static const struct of_device_id fire_chg_of_match[] = {
	{ .compatible = "silergy,syv690" },
	{ }
};

static struct i2c_driver fire_chg_driver = {
	.probe = fire_chg_probe,
	.driver = {
		.name = "fire-charger",
		.of_match_table = fire_chg_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_i2c_driver(fire_chg_driver);
