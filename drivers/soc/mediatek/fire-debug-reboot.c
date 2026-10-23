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
 * fire_debug_reboot.grep=a,b copies kernel log lines containing any of the
 * substrings into the names of empty platform devices 150 s after boot,
 * then every 30 s with the newest matches:
 * Android's SELinux policy keeps adb from reading the log, but not from
 * listing /sys/bus/platform/devices.
 *
 * fire_debug_reboot.hung_panic=1 panics, after dumping its stack, when a
 * user task stays in D state without being scheduled for about 2 minutes:
 * Android turns the kernel's hung task detector off, and a D state
 * deadlock of init can not be rebooted from adb or the keys. The warm
 * reboot keeps the log.
 *
 * fire_debug_reboot.nosuspend=1 holds a wakeup source forever: system
 * suspend resets the SoC until the SPM low power firmware is set up.
 *
 * Layout at FIRE_KMSG_OFFSET: "FIREKMSG", __le32 length, console log;
 * FIRE_PMSG_SLOT bytes later: "FIREPMSG", __le32 length, raw pmsg
 * (Android userspace log) records.
 */

#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/kmsg_dump.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/panic.h>
#include <linux/pm_wakeup.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
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

static char *grep;
module_param(grep, charp, 0444);
MODULE_PARM_DESC(grep, "Report log lines with these ',' separated substrings");

#define FIRE_GREP_DELAY		(150 * HZ)
#define FIRE_GREP_PERIOD	(30 * HZ)
#define FIRE_GREP_MAX		200
#define FIRE_GREP_NAME		120

static bool hung_panic;
module_param(hung_panic, bool, 0444);
MODULE_PARM_DESC(hung_panic, "Panic when the hung task detector reports a task");

static bool nosuspend;
module_param(nosuspend, bool, 0444);
MODULE_PARM_DESC(nosuspend, "Keep the system from suspending");

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

static bool fire_grep_match(const char *line)
{
	const char *p = grep, *end;
	char pat[64];
	size_t len;

	while (p && *p) {
		end = strchrnul(p, ',');
		len = min_t(size_t, end - p, sizeof(pat) - 1);
		memcpy(pat, p, len);
		pat[len] = 0;
		if (len && strstr(line, pat))
			return true;
		p = *end ? end + 1 : end;
	}
	return false;
}

static char fire_grep_lines[FIRE_GREP_MAX][FIRE_GREP_NAME];
static struct platform_device *fire_grep_devs[FIRE_GREP_MAX];

static void fire_grep_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(fire_grep_work, fire_grep_fn);

#define FIRE_HUNG_SLOTS		64
#define FIRE_HUNG_SCANS		4	/* x FIRE_GREP_PERIOD */

static struct {
	pid_t pid;
	unsigned long switches;
	unsigned int seen;
} fire_hung[FIRE_HUNG_SLOTS];

/* Called every FIRE_GREP_PERIOD: track D state tasks that never run. */
static void fire_hung_scan(void)
{
	struct task_struct *g, *t, *stuck = NULL;
	bool alive[FIRE_HUNG_SLOTS] = {};
	unsigned int i, free_slot;

	rcu_read_lock();
	for_each_process_thread(g, t) {
		unsigned long sw = t->nvcsw + t->nivcsw;

		/*
		 * Only user tasks: some vendor kthreads (cmdq_buffer_usage,
		 * disp_echk) idle in D state on a healthy system.
		 */
		if (READ_ONCE(t->__state) != TASK_UNINTERRUPTIBLE ||
		    (t->flags & PF_KTHREAD))
			continue;
		free_slot = FIRE_HUNG_SLOTS;
		for (i = 0; i < FIRE_HUNG_SLOTS; i++) {
			if (fire_hung[i].pid == t->pid)
				break;
			if (!fire_hung[i].pid && free_slot == FIRE_HUNG_SLOTS)
				free_slot = i;
		}
		if (i == FIRE_HUNG_SLOTS) {
			if (free_slot == FIRE_HUNG_SLOTS)
				continue;
			i = free_slot;
			fire_hung[i].pid = t->pid;
			fire_hung[i].switches = sw;
			fire_hung[i].seen = 0;
		}
		alive[i] = true;
		if (fire_hung[i].switches != sw) {
			fire_hung[i].switches = sw;
			fire_hung[i].seen = 0;
		} else if (++fire_hung[i].seen >= FIRE_HUNG_SCANS) {
			pr_err("fire-debug-reboot: %s/%d stuck in D state\n",
			       t->comm, t->pid);
			sched_show_task(t);
			stuck = t;
		}
	}
	rcu_read_unlock();

	for (i = 0; i < FIRE_HUNG_SLOTS; i++)
		if (!alive[i])
			fire_hung[i].pid = 0;
	if (stuck) {
		show_state_filter(TASK_UNINTERRUPTIBLE);
		panic("fire-debug-reboot: task stuck in D state");
	}
}

/* Keeps the last FIRE_GREP_MAX matches, refreshed every FIRE_GREP_PERIOD. */
static void fire_grep_fn(struct work_struct *work)
{
	struct kmsg_dump_iter iter;
	char line[512], name[FIRE_GREP_NAME + 16];
	unsigned int n = 0, i, o, first;
	size_t len;

	kmsg_dump_rewind(&iter);
	while (kmsg_dump_get_line(&iter, false, line, sizeof(line) - 1, &len)) {
		char *dst = fire_grep_lines[n++ % FIRE_GREP_MAX];

		line[len] = 0;
		if (!fire_grep_match(line)) {
			n--;
			continue;
		}
		for (i = 0, o = 0; line[i] && line[i] != '\n' &&
		     o < FIRE_GREP_NAME - 1; i++)
			dst[o++] = isalnum(line[i]) || strchr("._-:=", line[i]) ?
				   line[i] : '_';
		dst[o] = 0;
	}

	for (i = 0; i < FIRE_GREP_MAX; i++) {
		if (fire_grep_devs[i])
			platform_device_unregister(fire_grep_devs[i]);
		fire_grep_devs[i] = NULL;
	}
	first = n > FIRE_GREP_MAX ? n - FIRE_GREP_MAX : 0;
	for (i = first; i < n; i++) {
		snprintf(name, sizeof(name), "kmsg-%03u-%s", i - first,
			 fire_grep_lines[i % FIRE_GREP_MAX]);
		fire_grep_devs[i - first] =
			platform_device_register_simple(name,
							PLATFORM_DEVID_NONE,
							NULL, 0);
		if (IS_ERR(fire_grep_devs[i - first]))
			fire_grep_devs[i - first] = NULL;
	}
	if (hung_panic)
		fire_hung_scan();
	queue_delayed_work(system_unbound_wq, &fire_grep_work,
			   FIRE_GREP_PERIOD);
}

/* After the wakeup source class exists */
static int __init fire_nosuspend_init(void)
{
	struct wakeup_source *ws;

	if ((grep && *grep) || hung_panic)
		queue_delayed_work(system_unbound_wq, &fire_grep_work,
				   FIRE_GREP_DELAY);
	if (!nosuspend)
		return 0;
	ws = wakeup_source_register(NULL, "fire-nosuspend");
	if (!ws)
		return -ENOMEM;
	__pm_stay_awake(ws);
	return 0;
}
late_initcall(fire_nosuspend_init);

MODULE_DESCRIPTION("Xiaomi fire bring-up log keeper");
MODULE_LICENSE("GPL");
