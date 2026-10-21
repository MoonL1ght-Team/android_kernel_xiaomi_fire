// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi fire: hand the bootloader framebuffer to simpledrm
 *
 * There is no MediaTek display driver on this kernel yet, but LK leaves
 * the panel running in video mode, scanning out a framebuffer through
 * the OVL engine. Read the first enabled OVL layer back from the
 * hardware and register a "simple-framebuffer" device for exactly that
 * buffer, so simpledrm and fbcon can draw on the screen.
 */

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>

#define OVL_SRC_CON		0x002c
#define OVL_CON(n)		(0x0030 + 0x20 * (n))
#define OVL_SRC_SIZE(n)		(0x0038 + 0x20 * (n))
#define OVL_PITCH(n)		(0x0044 + 0x20 * (n))
#define OVL_ADDR(n)		(0x0f40 + 0x20 * (n))
#define OVL_LAYERS		4
#define OVL_REG_SIZE		0x1000

#define OVL_CON_CLRFMT		GENMASK(15, 12)
#define OVL_CON_BYTE_SWAP	BIT(24)
#define OVL_CON_RGB_SWAP	BIT(25)

/* disp_ovl0 and disp_ovl0_2l */
static const phys_addr_t fire_ovl_bases[] = { 0x1400b000, 0x1400c000 };

static const char *fire_bootfb_format(u32 con, u32 *bpp)
{
	bool byte_swap = con & OVL_CON_BYTE_SWAP;
	bool rgb_swap = con & OVL_CON_RGB_SWAP;

	switch (FIELD_GET(OVL_CON_CLRFMT, con)) {
	case 0:
		*bpp = 2;
		return "r5g6b5";
	case 1:
		*bpp = 3;
		return "r8g8b8";
	case 2:
		*bpp = 4;
		return byte_swap || rgb_swap ? "x8b8g8r8" : "x8r8g8b8";
	case 3:
		/* RGBA/BGRA have no simplefb name; show it anyway */
		*bpp = 4;
		return byte_swap ? "x8r8g8b8" : "x8b8g8r8";
	default:
		return NULL;
	}
}

static int __init fire_bootfb_probe_ovl(phys_addr_t base)
{
	struct simplefb_platform_data pdata = {};
	struct platform_device *pdev;
	struct resource res = {};
	u32 src_con, con, size, addr, pitch, bpp = 0;
	void __iomem *regs;
	int layer;

	regs = ioremap(base, OVL_REG_SIZE);
	if (!regs)
		return -ENOMEM;

	src_con = readl(regs + OVL_SRC_CON);
	for (layer = 0; layer < OVL_LAYERS; layer++)
		if (src_con & BIT(layer))
			break;
	if (layer == OVL_LAYERS) {
		iounmap(regs);
		return -ENODEV;
	}

	con = readl(regs + OVL_CON(layer));
	size = readl(regs + OVL_SRC_SIZE(layer));
	pitch = readl(regs + OVL_PITCH(layer)) & 0xffff;
	addr = readl(regs + OVL_ADDR(layer));
	iounmap(regs);

	pdata.width = size & 0x1fff;
	pdata.height = (size >> 16) & 0x1fff;
	pdata.stride = pitch;
	pdata.format = fire_bootfb_format(con, &bpp);

	pr_info("fire-bootfb: ovl@%pa layer %d: %ux%u pitch %u con 0x%08x addr 0x%08x\n",
		&base, layer, pdata.width, pdata.height, pitch, con, addr);

	if (!pdata.format || !addr || !pdata.width || !pdata.height ||
	    pitch < pdata.width * bpp)
		return -EINVAL;

	res.start = addr;
	res.end = addr + (resource_size_t)pitch * pdata.height - 1;
	res.flags = IORESOURCE_MEM;

	pdev = platform_device_register_resndata(NULL, "simple-framebuffer", 0,
						 &res, 1, &pdata, sizeof(pdata));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	pr_info("fire-bootfb: registered %s framebuffer\n", pdata.format);
	return 0;
}

static int __init fire_bootfb_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fire_ovl_bases); i++)
		if (!fire_bootfb_probe_ovl(fire_ovl_bases[i]))
			return 0;

	pr_warn("fire-bootfb: no active OVL layer found\n");
	return 0;
}
device_initcall(fire_bootfb_init);
