/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fire 6.12: the MTK tpd notifier hub (tpd_notify.c) is not ported.
 * Nothing sends charger or proximity-enable events, so registration
 * just succeeds.
 */
#ifndef __LINUX_TPD_NOTIFY_H__
#define __LINUX_TPD_NOTIFY_H__
#include <linux/notifier.h>

#define TPD_VSP_VSN_UP_EVENT_BLANK	0x01
#define TPD_DETECT_USB_IN		2
#define TPD_DETECT_USB_OUT		3

static inline int tpd_register_psenable_callback(void) { return 0; }
static inline int ps_enable_register_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline int tpd_charger_detect_register_client(struct notifier_block *nb)
{
	return 0;
}
static inline int tpd_charger_detect_unregister_client(struct notifier_block *nb)
{
	return 0;
}
#endif /* __LINUX_TPD_NOTIFY_H__ */
