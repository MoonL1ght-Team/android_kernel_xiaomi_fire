// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bring-up helper for Xiaomi fire: save the kernel log, then panic.
 *
 * Without a working display or USB there is no way to read the 6.12
 * kernel log: the bootloader does not copy it anywhere and holding the
 * power key cuts DRAM. fire_debug_reboot.interval=<seconds> makes the
 * kernel keep writing its log into an unused area of the expdb
 * partition (readable from BROM with mtkclient), so the log survives
 * even a watchdog reset. fire_debug_reboot.delay=<seconds> additionally
 * saves the log and panics, so the device keeps cycling. The timers are
 * armed from core_initcall so they still fire if a driver hangs probing.
 * Around FIRE_DUMP_AT seconds the blocked tasks and all CPU backtraces
 * are logged once, to show where a hang sits.
 *
 * Layout at FIRE_KMSG_OFFSET: "FIREKMSG", __le32 length, log text.
 */

#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kmsg_dump.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/panic.h>
#include <linux/sched/debug.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define FIRE_KMSG_PART		"expdb"
#define FIRE_KMSG_OFFSET	(16 << 20)
#define FIRE_KMSG_SIZE		(2 << 20)
#define FIRE_KMSG_HDR		12
#define FIRE_DUMP_AT		18

static unsigned int delay;
module_param(delay, uint, 0444);
MODULE_PARM_DESC(delay, "Seconds after boot before saving the log and panicking (0 = off)");

static unsigned int interval;
module_param(interval, uint, 0444);
MODULE_PARM_DESC(interval, "Save the log every this many seconds (0 = off)");

static int fire_match_part(struct device *dev, const void *name)
{
	struct block_device *bdev = dev_to_bdev(dev);

	return bdev->bd_meta_info &&
	       !strcmp(bdev->bd_meta_info->volname, name);
}

static void fire_save_kmsg(void)
{
	struct kmsg_dump_iter iter;
	struct file *bdev_file;
	struct device *dev;
	size_t len = 0;
	loff_t pos;
	char *buf;
	ssize_t ret;

	dev = class_find_device(&block_class, NULL, FIRE_KMSG_PART,
				fire_match_part);
	if (!dev) {
		pr_err("fire-debug-reboot: no %s partition\n", FIRE_KMSG_PART);
		return;
	}

	buf = vzalloc(FIRE_KMSG_SIZE);
	if (!buf)
		goto put_dev;

	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, buf + FIRE_KMSG_HDR,
			     FIRE_KMSG_SIZE - FIRE_KMSG_HDR, &len);
	memcpy(buf, "FIREKMSG", 8);
	put_unaligned_le32(len, buf + 8);

	bdev_file = bdev_file_open_by_dev(dev->devt,
					  BLK_OPEN_READ | BLK_OPEN_WRITE,
					  NULL, NULL);
	if (IS_ERR(bdev_file)) {
		pr_err("fire-debug-reboot: open %s: %ld\n", FIRE_KMSG_PART,
		       PTR_ERR(bdev_file));
		goto free_buf;
	}

	pos = FIRE_KMSG_OFFSET;
	ret = kernel_write(bdev_file, buf, FIRE_KMSG_SIZE, &pos);
	sync_blockdev(file_bdev(bdev_file));
	if (ret != FIRE_KMSG_SIZE)
		pr_err("fire-debug-reboot: write %s: %zd\n", FIRE_KMSG_PART, ret);
	fput(bdev_file);
free_buf:
	vfree(buf);
put_dev:
	put_device(dev);
}

static DEFINE_MUTEX(fire_save_lock);

static void fire_save_kmsg_locked(void)
{
	mutex_lock(&fire_save_lock);
	fire_save_kmsg();
	mutex_unlock(&fire_save_lock);
}

static void fire_debug_save_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(fire_debug_save_work, fire_debug_save_fn);

static void fire_debug_save_fn(struct work_struct *work)
{
	static unsigned int nsave;
	static bool dumped;

	nsave++;
	pr_info("fire-debug-reboot: save #%u\n", nsave);
	if (!dumped && nsave * interval >= FIRE_DUMP_AT) {
		dumped = true;
		show_state_filter(TASK_UNINTERRUPTIBLE);
		trigger_all_cpu_backtrace();
	}
	fire_save_kmsg_locked();
	queue_delayed_work(system_unbound_wq, &fire_debug_save_work,
			   interval * HZ);
}

static void fire_debug_reboot_fn(struct work_struct *work)
{
	fire_save_kmsg_locked();
	panic("fire-debug-reboot: %u s elapsed", delay);
}

static DECLARE_DELAYED_WORK(fire_debug_reboot_work, fire_debug_reboot_fn);

static int __init fire_debug_reboot_init(void)
{
	if (interval) {
		pr_info("fire-debug-reboot: saving log every %u s\n", interval);
		queue_delayed_work(system_unbound_wq, &fire_debug_save_work,
				   interval * HZ);
	}
	if (delay) {
		pr_info("fire-debug-reboot: saving log and panicking in %u s\n",
			delay);
		queue_delayed_work(system_unbound_wq, &fire_debug_reboot_work,
				   delay * HZ);
	}
	return 0;
}
core_initcall(fire_debug_reboot_init);

MODULE_DESCRIPTION("Xiaomi fire bring-up log saver");
MODULE_LICENSE("GPL");
