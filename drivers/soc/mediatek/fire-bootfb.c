// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi fire: show the kernel log on the bootloader framebuffer
 *
 * There is no MediaTek display driver on this kernel yet, but LK leaves
 * the panel running in video mode, scanning out a framebuffer through
 * the OVL engine. Read the first enabled OVL layer back from the
 * hardware, turn the other layers off so they cannot cover it, and draw
 * the tail of the kernel log into that buffer once a second.
 *
 * This deliberately avoids fbcon: Android puts the VT into graphics mode
 * and the console never shows up, while drawing straight into the
 * scanned-out buffer is known to reach the panel.
 *
 * Android's SELinux policy hides the kernel log from adb shell, so the
 * values read from each OVL are also exposed as the name of an empty
 * platform device, visible with "ls /sys/bus/platform/devices".
 */

#include <linux/bitfield.h>
#include <linux/ctype.h>
#include <linux/font.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kmsg_dump.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define OVL_SRC_CON		0x002c
#define OVL_CON(n)		(0x0030 + 0x20 * (n))
#define OVL_SRC_SIZE(n)		(0x0038 + 0x20 * (n))
#define OVL_PITCH(n)		(0x0044 + 0x20 * (n))
#define OVL_ADDR(n)		(0x0f40 + 0x20 * (n))
#define OVL_LAYERS		4
#define OVL_REG_SIZE		0x1000

#define OVL_CON_CLRFMT		GENMASK(15, 12)
/* 32-bit ARGB8888, verified on the panel with pure R/G/B bars */
#define OVL_CLRFMT_ARGB8888	2

/* disp_ovl0 and disp_ovl0_2l */
static const phys_addr_t fire_ovl_bases[] = { 0x1400b000, 0x1400c000 };

#define RDMA0_BASE		0x1400d000
#define RDMA_GLOBAL_CON		0x0010
#define RDMA_SIZE_CON_0		0x0014
#define RDMA_SIZE_CON_1		0x0018
#define RDMA_MEM_CON		0x0024
#define RDMA_MEM_SRC_PITCH	0x002c
#define RDMA_MEM_START_ADDR	0x0f00

#define FIRE_LOG_FG		0xffd0d0d0
#define FIRE_LOG_BG		0xff000000
#define FIRE_LOG_BUF		(64 * 1024)

static struct {
	void __iomem *fb;
	u32 width, height, pitch;
	const struct font_desc *font;
	unsigned int cols, rows;
	char *log;
	char *screen;
} fire_log;

static void fire_bootfb_report(const char *name)
{
	platform_device_register_simple(name, PLATFORM_DEVID_NONE, NULL, 0);
}

static void __init fire_bootfb_report_rdma(void)
{
	void __iomem *regs = ioremap(RDMA0_BASE, OVL_REG_SIZE);
	char name[96];

	if (!regs)
		return;
	snprintf(name, sizeof(name), "rdinfo-g%x-w%x-h%x-m%x-p%x-a%x",
		 readl(regs + RDMA_GLOBAL_CON), readl(regs + RDMA_SIZE_CON_0),
		 readl(regs + RDMA_SIZE_CON_1), readl(regs + RDMA_MEM_CON),
		 readl(regs + RDMA_MEM_SRC_PITCH),
		 readl(regs + RDMA_MEM_START_ADDR));
	iounmap(regs);
	fire_bootfb_report(name);
}

static void fire_log_draw_char(unsigned int col, unsigned int row, u8 c)
{
	const struct font_desc *font = fire_log.font;
	unsigned int bpr = DIV_ROUND_UP(font->width, 8);
	const u8 *glyph = (const u8 *)font->data + c * bpr * font->height;
	void __iomem *line = fire_log.fb + row * font->height * fire_log.pitch +
			     col * font->width * 4;
	unsigned int x, y;

	for (y = 0; y < font->height; y++, line += fire_log.pitch)
		for (x = 0; x < font->width; x++)
			writel(glyph[y * bpr + x / 8] & (0x80 >> (x % 8)) ?
			       FIRE_LOG_FG : FIRE_LOG_BG, line + x * 4);
}

/* Lay the log tail out as cols x rows cells, wrapping long lines. */
static void fire_log_layout(const char *log, size_t len, char *screen)
{
	unsigned int cols = fire_log.cols, rows = fire_log.rows;
	unsigned int nlines = 0, row, col;
	const char *p, *start;

	memset(screen, ' ', cols * rows);

	/* Walk back from the end until enough wrapped lines are collected. */
	p = log + len;
	while (p > log && nlines < rows) {
		const char *end = p;

		if (end > log && end[-1] == '\n')
			end--;
		start = end;
		while (start > log && start[-1] != '\n')
			start--;
		nlines += max_t(unsigned int, 1,
				DIV_ROUND_UP((unsigned int)(end - start), cols));
		p = start;
	}

	row = nlines > rows ? 0 : rows - nlines;
	col = 0;
	/* Skip wrapped rows that do not fit at the top. */
	for (nlines = nlines > rows ? nlines - rows : 0; p < log + len; p++) {
		if (*p == '\n') {
			if (nlines)
				nlines--;
			else
				row++;
			col = 0;
			continue;
		}
		if (col == cols) {
			if (nlines)
				nlines--;
			else
				row++;
			col = 0;
		}
		if (!nlines && row < rows)
			screen[row * cols + col] = isprint(*p) ? *p : '?';
		col++;
	}
}

static void fire_log_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(fire_log_work, fire_log_fn);

static void fire_log_fn(struct work_struct *work)
{
	static char *prev;
	struct kmsg_dump_iter iter;
	unsigned int n = fire_log.cols * fire_log.rows, i;
	size_t len = 0;

	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, fire_log.log, FIRE_LOG_BUF, &len);
	fire_log_layout(fire_log.log, len, fire_log.screen);

	if (!prev)
		prev = kmalloc(n, GFP_KERNEL);
	for (i = 0; i < n; i++) {
		if (prev && prev[i] == fire_log.screen[i])
			continue;
		fire_log_draw_char(i % fire_log.cols, i / fire_log.cols,
				   fire_log.screen[i]);
	}
	if (prev)
		memcpy(prev, fire_log.screen, n);

	queue_delayed_work(system_unbound_wq, &fire_log_work, HZ);
}

static int __init fire_log_start(u32 addr, u32 width, u32 height, u32 pitch)
{
	fire_log.font = find_font("TER16x32");
	if (!fire_log.font)
		fire_log.font = find_font("VGA8x16");
	if (!fire_log.font)
		return -ENOENT;

	fire_log.fb = ioremap_wc(addr, (size_t)pitch * height);
	if (!fire_log.fb)
		return -ENOMEM;

	fire_log.width = width;
	fire_log.height = height;
	fire_log.pitch = pitch;
	fire_log.cols = width / fire_log.font->width;
	fire_log.rows = height / fire_log.font->height;
	fire_log.log = vmalloc(FIRE_LOG_BUF);
	fire_log.screen = kmalloc(fire_log.cols * fire_log.rows, GFP_KERNEL);
	if (!fire_log.log || !fire_log.screen)
		return -ENOMEM;

	queue_delayed_work(system_unbound_wq, &fire_log_work, 0);
	return 0;
}

static int __init fire_bootfb_probe_ovl(int idx, phys_addr_t base, bool use)
{
	u32 src_con, con, size, addr, pitch, width, height;
	void __iomem *regs;
	char name[96];
	int layer, first = -1;

	regs = ioremap(base, OVL_REG_SIZE);
	if (!regs)
		return -ENOMEM;

	src_con = readl(regs + OVL_SRC_CON);
	for (layer = OVL_LAYERS - 1; layer >= 0; layer--) {
		if (!(src_con & BIT(layer)))
			continue;
		first = layer;
		snprintf(name, sizeof(name),
			 "fbinfo-%d-sc%x-l%d-s%x-p%u-c%08x-a%08x", idx, src_con,
			 layer, readl(regs + OVL_SRC_SIZE(layer)),
			 readl(regs + OVL_PITCH(layer)) & 0xffff,
			 readl(regs + OVL_CON(layer)),
			 readl(regs + OVL_ADDR(layer)));
		fire_bootfb_report(name);
	}
	if (first < 0) {
		iounmap(regs);
		return -ENODEV;
	}

	layer = first;
	con = readl(regs + OVL_CON(layer));
	size = readl(regs + OVL_SRC_SIZE(layer));
	pitch = readl(regs + OVL_PITCH(layer)) & 0xffff;
	addr = readl(regs + OVL_ADDR(layer));
	width = size & 0x1fff;
	height = (size >> 16) & 0x1fff;

	if (!use || FIELD_GET(OVL_CON_CLRFMT, con) != OVL_CLRFMT_ARGB8888 ||
	    !addr || !width || !height || pitch < width * 4) {
		iounmap(regs);
		return use ? -EINVAL : 0;
	}

	/* LK also enables layer 3 on top; keep only the layer we draw on. */
	if (src_con != BIT(layer))
		writel(BIT(layer), regs + OVL_SRC_CON);
	iounmap(regs);

	pr_info("fire-bootfb: drawing the log on ovl@%pa layer %d (%ux%u)\n",
		&base, layer, width, height);
	return fire_log_start(addr, width, height, pitch);
}

static int __init fire_bootfb_init(void)
{
	bool found = false;
	int i;

	fire_bootfb_report_rdma();
	for (i = 0; i < ARRAY_SIZE(fire_ovl_bases); i++)
		if (!fire_bootfb_probe_ovl(i, fire_ovl_bases[i], !found))
			found = true;

	if (!found)
		pr_warn("fire-bootfb: no usable OVL layer found\n");
	return 0;
}
late_initcall(fire_bootfb_init);
