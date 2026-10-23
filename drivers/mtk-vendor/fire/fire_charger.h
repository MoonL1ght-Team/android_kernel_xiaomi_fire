/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FIRE_CHARGER_H
#define __FIRE_CHARGER_H

struct fire_charger_state {
	bool online;
	unsigned int vbus_type;	/* REG0B VBUS_STAT: 1 SDP, 2 CDP, 3 DCP, ... */
	unsigned int chrg_stat;	/* 0 none, 1 pre, 2 fast, 3 done */
	unsigned int faults;
	int vbat_mv, vbus_mv, ichg_ma;
};

#if IS_ENABLED(CONFIG_FIRE_CHARGER)
int fire_charger_get(struct fire_charger_state *st);
#else
static inline int fire_charger_get(struct fire_charger_state *st)
{
	return -ENODEV;
}
#endif

/* fire_gauge.c: MT6358 coulomb counter */
int fire_gauge_read(int *ma, int *car_01mah);

#endif
