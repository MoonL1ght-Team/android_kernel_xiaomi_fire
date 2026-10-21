// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bring-up helper for Xiaomi fire: keep the previous boot's kernel log.
 *
 * Without a working display or USB there is no way to read the 6.12
 * kernel log: the bootloader does not copy it anywhere, holding the
 * power key cuts DRAM, and raw writes to eMMC from the kernel hang once
 * Android is past first stage init.
 *
 * fire_debug_reboot.delay=<seconds> panics after that time. The panic
 * reboot is warm, so the console log survives in ramoops.
 * fire_debug_reboot.save_prev=1 makes the next boot copy that log from
 * pstore into an unused area of the expdb partition right after eMMC
 * comes up, while writes still work. It can be read back from BROM with
 * mtkclient. The timers are armed from core_initcall so they still fire
 * if a driver hangs probing.
 *
 * Layout at FIRE_KMSG_OFFSET: "FIREKMSG", __le32 length, console log;
 * FIRE_PMSG_SLOT bytes later: "FIREPMSG", __le32 length, raw pmsg
 * (Android userspace log) records.
 */

#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/panic.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define FIRE_KMSG_PART		"expdb"
#define FIRE_KMSG_OFFSET	(16 << 20)
#define FIRE_KMSG_SIZE		(2 << 20)
#define FIRE_KMSG_HDR		12
#define FIRE_PSTORE_CONSOLE	"console-ramoops-0"
#define FIRE_PSTORE_PMSG	"pmsg-ramoops-0"
#define FIRE_PMSG_SLOT		(1 << 20)
#define FIRE_SAVE_RETRIES	20

static unsigned int delay;
module_param(delay, uint, 0444);
MODULE_PARM_DESC(delay, "Seconds after boot before a deliberate panic (0 = off)");

static bool save_prev;
module_param(save_prev, bool, 0444);
MODULE_PARM_DESC(save_prev, "Copy the previous boot's ramoops console log to expdb");

static int fire_match_part(struct device *dev, const void *name)
{
	struct block_device *bdev = dev_to_bdev(dev);

	return bdev->bd_meta_info &&
	       !strcmp(bdev->bd_meta_info->volname, name);
}

/* Read a record of the previous boot from pstore into buf. */
static ssize_t fire_read_prev_log(const char *name, char *buf, size_t size)
{
	struct file_system_type *type;
	struct vfsmount *mnt;
	struct file *file;
	ssize_t len = 0, ret;
	loff_t pos = 0;

	type = get_fs_type("pstore");
	if (!type)
		return -ENODEV;
	mnt = vfs_kern_mount(type, 0, "pstore", NULL);
	put_filesystem(type);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	file = file_open_root_mnt(mnt, name, O_RDONLY, 0);
	if (IS_ERR(file)) {
		len = PTR_ERR(file);
		goto unmount;
	}

	while (len < size) {
		ret = kernel_read(file, buf + len, size - len, &pos);
		if (ret <= 0)
			break;
		len += ret;
	}
	fput(file);
unmount:
	kern_unmount(mnt);
	return len;
}

static int fire_write_expdb(struct device *dev, const char *buf)
{
	struct file *bdev_file;
	loff_t pos = FIRE_KMSG_OFFSET;
	ssize_t ret;

	bdev_file = bdev_file_open_by_dev(dev->devt,
					  BLK_OPEN_READ | BLK_OPEN_WRITE,
					  NULL, NULL);
	if (IS_ERR(bdev_file))
		return PTR_ERR(bdev_file);

	ret = kernel_write(bdev_file, buf, FIRE_KMSG_SIZE, &pos);
	sync_blockdev(file_bdev(bdev_file));
	fput(bdev_file);
	return ret == FIRE_KMSG_SIZE ? 0 : (ret < 0 ? ret : -EIO);
}

static void fire_save_prev_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(fire_save_prev_work, fire_save_prev_fn);

static void fire_save_prev_fn(struct work_struct *work)
{
	static unsigned int tries;
	struct device *dev;
	ssize_t len, plen;
	char *buf;
	int ret;

	dev = class_find_device(&block_class, NULL, FIRE_KMSG_PART,
				fire_match_part);
	if (!dev) {
		if (++tries < FIRE_SAVE_RETRIES)
			queue_delayed_work(system_unbound_wq,
					   &fire_save_prev_work, HZ / 4);
		else
			pr_err("fire-debug-reboot: no %s partition\n",
			       FIRE_KMSG_PART);
		return;
	}

	buf = vzalloc(FIRE_KMSG_SIZE);
	if (!buf)
		goto put_dev;

	len = fire_read_prev_log(FIRE_PSTORE_CONSOLE, buf + FIRE_KMSG_HDR,
				 FIRE_PMSG_SLOT - FIRE_KMSG_HDR);
	if (len < 0)
		len = 0;
	memcpy(buf, "FIREKMSG", 8);
	put_unaligned_le32(len, buf + 8);

	plen = fire_read_prev_log(FIRE_PSTORE_PMSG,
				  buf + FIRE_PMSG_SLOT + FIRE_KMSG_HDR,
				  FIRE_KMSG_SIZE - FIRE_PMSG_SLOT - FIRE_KMSG_HDR);
	if (plen < 0)
		plen = 0;
	memcpy(buf + FIRE_PMSG_SLOT, "FIREPMSG", 8);
	put_unaligned_le32(plen, buf + FIRE_PMSG_SLOT + 8);

	ret = fire_write_expdb(dev, buf);
	pr_info("fire-debug-reboot: saved %zd + %zd bytes of previous log to %s: %d\n",
		len, plen, FIRE_KMSG_PART, ret);
	vfree(buf);
put_dev:
	put_device(dev);
}

static void fire_debug_reboot_fn(struct work_struct *work)
{
	panic("fire-debug-reboot: %u s elapsed", delay);
}

static DECLARE_DELAYED_WORK(fire_debug_reboot_work, fire_debug_reboot_fn);

static int __init fire_debug_reboot_init(void)
{
	if (save_prev)
		queue_delayed_work(system_unbound_wq, &fire_save_prev_work,
				   HZ / 2);
	if (delay) {
		pr_info("fire-debug-reboot: panicking in %u s\n", delay);
		queue_delayed_work(system_unbound_wq, &fire_debug_reboot_work,
				   delay * HZ);
	}
	return 0;
}
core_initcall(fire_debug_reboot_init);

MODULE_DESCRIPTION("Xiaomi fire bring-up log keeper");
MODULE_LICENSE("GPL");
