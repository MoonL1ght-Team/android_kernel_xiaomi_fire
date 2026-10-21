// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bring-up helper for Xiaomi fire: panic after a delay.
 *
 * Without a working display or USB the only way to read the kernel log
 * is the bootloader copying ramoops into expdb on the next boot. It
 * reliably does so after a kernel panic; holding the power key cuts DRAM
 * and the log is lost. Booting with fire_debug_reboot.delay=<seconds>
 * makes the kernel panic by itself so the log survives. The timer is
 * armed from core_initcall so it still fires if a driver hangs probing.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/panic.h>
#include <linux/workqueue.h>

static unsigned int delay;
module_param(delay, uint, 0444);
MODULE_PARM_DESC(delay, "Seconds after boot before a deliberate panic (0 = off)");

static void fire_debug_reboot_fn(struct work_struct *work)
{
	panic("fire-debug-reboot: %u s elapsed, saving the log", delay);
}

static DECLARE_DELAYED_WORK(fire_debug_reboot_work, fire_debug_reboot_fn);

static int __init fire_debug_reboot_init(void)
{
	if (!delay)
		return 0;

	pr_info("fire-debug-reboot: panic scheduled in %u s\n", delay);
	schedule_delayed_work(&fire_debug_reboot_work, delay * HZ);
	return 0;
}
core_initcall(fire_debug_reboot_init);

MODULE_DESCRIPTION("Xiaomi fire bring-up delayed panic");
MODULE_LICENSE("GPL");
