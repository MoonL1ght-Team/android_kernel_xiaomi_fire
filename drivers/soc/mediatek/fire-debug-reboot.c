// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bring-up helper for Xiaomi fire: warm-reboot after a delay.
 *
 * Without a working display or USB the only way to read the kernel log
 * is the bootloader copying ramoops into expdb on the next boot. That
 * only happens after a warm reset; holding the power key cuts DRAM and
 * the log is lost. Booting with fire_debug_reboot.delay=<seconds> makes
 * the kernel restart by itself so the log survives.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/workqueue.h>

static unsigned int delay;
module_param(delay, uint, 0444);
MODULE_PARM_DESC(delay, "Seconds after boot before a warm reboot (0 = off)");

static void fire_debug_reboot_fn(struct work_struct *work)
{
	pr_emerg("fire-debug-reboot: rebooting after %u s to preserve the log\n",
		 delay);
	emergency_restart();
}

static DECLARE_DELAYED_WORK(fire_debug_reboot_work, fire_debug_reboot_fn);

static int __init fire_debug_reboot_init(void)
{
	if (!delay)
		return 0;

	pr_info("fire-debug-reboot: warm reboot scheduled in %u s\n", delay);
	schedule_delayed_work(&fire_debug_reboot_work, delay * HZ);
	return 0;
}
late_initcall(fire_debug_reboot_init);

MODULE_DESCRIPTION("Xiaomi fire bring-up delayed warm reboot");
MODULE_LICENSE("GPL");
