// SPDX-License-Identifier: GPL-2.0
/*
 * /proc/bootconfig - Extra boot configuration
 */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/bootconfig.h>
#include <linux/slab.h>

static char *saved_boot_config;

static int boot_config_proc_show(struct seq_file *m, void *v)
{
	if (saved_boot_config)
		seq_puts(m, saved_boot_config);
	return 0;
}

/* Rest size of buffer */
#define rest(dst, end) ((end) > (dst) ? (end) - (dst) : 0)

/* Return the needed total length if @size is 0 */
static int __init copy_xbc_key_value_list(char *dst, size_t size)
{
	struct xbc_node *leaf, *vnode;
	char *key, *end = dst + size;
	const char *val;
	char q;
	int ret = 0;

	key = kzalloc(XBC_KEYLEN_MAX, GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	xbc_for_each_key_value(leaf, val) {
		ret = xbc_node_compose_key(leaf, key, XBC_KEYLEN_MAX);
		if (ret < 0)
			break;
		ret = snprintf(dst, rest(dst, end), "%s = ", key);
		if (ret < 0)
			break;
		dst += ret;
		vnode = xbc_node_get_child(leaf);
		if (vnode) {
			xbc_array_for_each_value(vnode, val) {
				if (strchr(val, '"'))
					q = '\'';
				else
					q = '"';
				ret = snprintf(dst, rest(dst, end), "%c%s%c%s",
					q, val, q, xbc_node_is_array(vnode) ? ", " : "\n");
				if (ret < 0)
					goto out;
				dst += ret;
			}
		} else {
			ret = snprintf(dst, rest(dst, end), "\"\"\n");
			if (ret < 0)
				break;
			dst += ret;
		}
	}
	if (cmdline_has_extra_options() && ret >= 0 && boot_command_line[0]) {
		ret = snprintf(dst, rest(dst, end), "# Parameters from bootloader:\n# %s\n",
			       boot_command_line);
		if (ret > 0)
			dst += ret;
	}
out:
	kfree(key);

	return ret < 0 ? ret : dst - (end - size);
}

static int __init proc_boot_config_init(void)
{
	int len;

	len = copy_xbc_key_value_list(NULL, 0);
	if (len < 0)
		return len;

	if (len > 0) {
		saved_boot_config = kzalloc(len + 1, GFP_KERNEL);
		if (!saved_boot_config)
			return -ENOMEM;

		len = copy_xbc_key_value_list(saved_boot_config, len + 1);
		if (len < 0) {
			kfree(saved_boot_config);
			return len;
		}
#ifdef CONFIG_FIRE_DEBUG_REBOOT
		/*
		 * Xiaomi fire bring-up: fire_debug_reboot.no_charger=1 boots
		 * Android instead of the off-mode charger, which LK may ask
		 * for in the bootconfig too. Same length rewrite.
		 */
		if (strstr(saved_command_line, "fire_debug_reboot.no_charger=1")) {
			char *mode = strstr(saved_boot_config,
					    "androidboot.mode = \"charger\"");

			if (mode)
				memcpy(mode + strlen("androidboot.mode = \""),
				       "normal\" ", 8);
		}
#endif
	}

	proc_create_single("bootconfig", 0, NULL, boot_config_proc_show);

	return 0;
}
fs_initcall(proc_boot_config_init);
