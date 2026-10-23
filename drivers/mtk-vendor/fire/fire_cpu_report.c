// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire bring-up: report the CPU clocks LK left running
 *
 * There is no CPU DVFS driver yet, so the cores keep whatever the
 * bootloader programmed. Read the ARMPLL and divider registers once at
 * boot and log the resulting frequencies (formula from the vendor
 * cpufreq_v2 plat_k68 driver).
 */

#include <linux/bits.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>

#define FIRE_APMIXED_BASE	0x1000c000
#define FIRE_MCUCFG_BASE	0x0c530000

static const struct {
	const char *name;
	u32 con1;	/* in apmixed */
	u32 ckdiv1;	/* in mcucfg */
} fire_plls[] = {
	{ "LL (A55)", 0x21c, 0xa2a0 },
	{ "L (A75)", 0x20c, 0xa2a4 },
	{ "CCI", 0x22c, 0xa2e0 },
};

/* ckdiv1 selector to a fraction of the PLL output */
static u32 fire_ckdiv(u32 khz, u32 sel)
{
	switch (sel) {
	case 9: return khz * 3 / 4;
	case 10: return khz / 2;
	case 11: return khz / 4;
	case 17: return khz * 4 / 5;
	case 18: return khz * 3 / 5;
	case 19: return khz * 2 / 5;
	case 20: return khz / 5;
	case 25: return khz * 5 / 6;
	case 26: return khz * 4 / 6;
	case 27: return khz / 2;
	case 28: return khz * 2 / 6;
	case 29: return khz / 6;
	default: return khz;
	}
}

static int __init fire_cpu_report(void)
{
	void __iomem *apmixed, *mcucfg;
	unsigned int i;

	apmixed = ioremap(FIRE_APMIXED_BASE, 0x1000);
	mcucfg = ioremap(FIRE_MCUCFG_BASE, 0xb000);
	if (!apmixed || !mcucfg)
		goto out;

	for (i = 0; i < ARRAY_SIZE(fire_plls); i++) {
		u32 con1 = readl(apmixed + fire_plls[i].con1);
		u32 div = readl(mcucfg + fire_plls[i].ckdiv1);
		u32 posdiv = (con1 >> 24) & 0x7;
		u32 khz = (((con1 & GENMASK(21, 0)) * 26) >> 14) * 1000;
		u32 sel = (div >> 17) & 0x1f;

		khz >>= min(posdiv, 4U);
		pr_err("fire-cpu %s: %u kHz (con1 %#x, ckdiv1 %u, mux %u)\n",
		       fire_plls[i].name, fire_ckdiv(khz, sel), con1, sel,
		       (div >> 9) & 0x3);
	}
out:
	if (mcucfg)
		iounmap(mcucfg);
	if (apmixed)
		iounmap(apmixed);
	return 0;
}
late_initcall(fire_cpu_report);
