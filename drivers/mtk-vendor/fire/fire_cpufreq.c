// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire bring-up: MT6768 CPU DVFS done from the AP
 *
 * The stock kernel hands CPU DVFS to the SSPM coprocessor, which is not
 * ported. This drives the hardware directly, like the AP-side path of
 * the vendor cpufreq_v2 plat_k68 driver: ARMPLL DDS/post divider for the
 * LL (cpu0-5) and L (cpu6-7) clusters and the CCI, and the MT6358 VPROC
 * bucks (VPROC12 feeds LL and CCI, VPROC11 feeds L). OPP tables and the
 * efuse to table mapping come from mtk_cpufreq_opp_table.h.
 */

#include <linux/bitfield.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/mt6358/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define FIRE_APMIXED_BASE	0x1000c000
#define FIRE_EFUSE_BASE		0x11c10000
#define FIRE_EFUSE_SEGMENT	0x78
#define FIRE_EFUSE_PTPOD	0xc8

#define PLL_CON1_CHG		BIT(31)
#define PLL_CON1_POSDIV		GENMASK(26, 24)
#define PLL_CON1_PCW		GENMASK(21, 0)
#define PLL_SETTLE_US		20

/* Voltages are in 10 uV, as in the vendor tables. */
#define VPROC_MAX		112000
#define VSRAM_MIN		85000
#define VSRAM_MAX		112000
#define VSRAM_VPROC_GAP		10000
#define VSRAM_VPROC_MAX_GAP	26250
#define VOLT_STEP		625
#define VOLT_BASE		50000
#define VOLT_SLEW		750	/* per us, the slower (falling) edge */

#define FIRE_NR_OPP		16

struct fire_opp {
	u32 khz;
	u32 volt;
	u32 posdiv;
};

enum { FIRE_LL, FIRE_L, FIRE_CCI, FIRE_NR_PLL };

struct fire_seg {
	const char *name;
	struct fire_opp opp[FIRE_NR_PLL][FIRE_NR_OPP];
};

static const struct fire_seg fire_segs[] = {
	{ "6768", {
	{ { 1700000, 96250, 2 }, { 1625000, 93125, 2 }, { 1500000, 89375, 2 }, { 1450000, 88125, 2 }, { 1375000, 85625, 2 }, { 1325000, 84375, 2 }, { 1275000, 83125, 2 }, { 1175000, 80000, 2 }, { 1100000, 77500, 2 }, { 1050000, 76250, 2 }, { 999000, 74375, 2 }, { 950000, 73125, 2 }, { 900000, 71875, 2 }, { 850000, 70000, 4 }, { 774000, 67500, 4 }, { 500000, 65000, 4 } },
	{ { 2000000, 108750, 1 }, { 1950000, 107500, 1 }, { 1900000, 106250, 1 }, { 1850000, 105000, 1 }, { 1800000, 103125, 1 }, { 1710000, 100000, 1 }, { 1621000, 96875, 1 }, { 1532000, 93125, 2 }, { 1443000, 90000, 2 }, { 1354000, 86875, 2 }, { 1295000, 84375, 2 }, { 1176000, 80000, 2 }, { 1087000, 76875, 2 }, { 998000, 73125, 2 }, { 909000, 70000, 2 }, { 850000, 67500, 2 } },
	{ { 1187000, 94375, 2 }, { 1120000, 90000, 2 }, { 1049000, 87500, 2 }, { 1014000, 86250, 2 }, { 961000, 83750, 2 }, { 909000, 81875, 2 }, { 856000, 80000, 2 }, { 821000, 78125, 2 }, { 768000, 76250, 2 }, { 733000, 75000, 4 }, { 698000, 73125, 4 }, { 663000, 71875, 4 }, { 628000, 70625, 4 }, { 593000, 69375, 4 }, { 558000, 67500, 4 }, { 500000, 65000, 4 } },
	} },
	{ "6768_v7", {
	{ { 1700000, 92500, 2 }, { 1625000, 88125, 2 }, { 1500000, 85625, 2 }, { 1450000, 84375, 2 }, { 1375000, 82500, 2 }, { 1325000, 81250, 2 }, { 1275000, 80000, 2 }, { 1175000, 77500, 2 }, { 1100000, 75625, 2 }, { 1050000, 74375, 2 }, { 999000, 73125, 2 }, { 950000, 71875, 2 }, { 900000, 70625, 2 }, { 850000, 69375, 4 }, { 774000, 67500, 4 }, { 500000, 65000, 4 } },
	{ { 2000000, 107500, 1 }, { 1950000, 106250, 1 }, { 1900000, 105000, 1 }, { 1850000, 103750, 1 }, { 1800000, 102500, 1 }, { 1710000, 99375, 1 }, { 1621000, 96250, 1 }, { 1532000, 93125, 2 }, { 1443000, 89375, 2 }, { 1354000, 86250, 2 }, { 1295000, 84375, 2 }, { 1176000, 80000, 2 }, { 1087000, 76250, 2 }, { 998000, 73125, 2 }, { 909000, 70000, 2 }, { 850000, 67500, 2 } },
	{ { 1187000, 95000, 2 }, { 1120000, 90625, 2 }, { 1049000, 88125, 2 }, { 1014000, 86875, 2 }, { 961000, 84375, 2 }, { 909000, 82500, 2 }, { 856000, 80000, 2 }, { 821000, 78750, 2 }, { 768000, 76250, 2 }, { 733000, 75000, 4 }, { 698000, 73750, 4 }, { 663000, 71875, 4 }, { 628000, 70625, 4 }, { 593000, 69375, 4 }, { 558000, 67500, 4 }, { 500000, 65000, 4 } },
	} },
	{ "G75", {
	{ { 1800000, 100625, 1 }, { 1625000, 93125, 2 }, { 1500000, 89375, 2 }, { 1450000, 88125, 2 }, { 1375000, 85625, 2 }, { 1325000, 84375, 2 }, { 1275000, 83125, 2 }, { 1175000, 80000, 2 }, { 1100000, 77500, 2 }, { 1050000, 76250, 2 }, { 999000, 74375, 2 }, { 950000, 73125, 2 }, { 900000, 71875, 2 }, { 850000, 70000, 4 }, { 774000, 67500, 4 }, { 500000, 65000, 4 } },
	{ { 2000000, 108750, 1 }, { 1950000, 107500, 1 }, { 1900000, 106250, 1 }, { 1850000, 105000, 1 }, { 1800000, 103125, 1 }, { 1710000, 100000, 1 }, { 1621000, 96875, 1 }, { 1532000, 93125, 2 }, { 1443000, 90000, 2 }, { 1354000, 86875, 2 }, { 1295000, 84375, 2 }, { 1176000, 80000, 2 }, { 1087000, 76875, 2 }, { 998000, 73125, 2 }, { 909000, 70000, 2 }, { 850000, 67500, 2 } },
	{ { 1277000, 100625, 2 }, { 1120000, 90000, 2 }, { 1049000, 87500, 2 }, { 1014000, 86250, 2 }, { 961000, 83750, 2 }, { 909000, 81875, 2 }, { 856000, 80000, 2 }, { 821000, 78125, 2 }, { 768000, 76250, 2 }, { 733000, 75000, 4 }, { 698000, 73125, 4 }, { 663000, 71875, 4 }, { 628000, 70625, 4 }, { 593000, 69375, 4 }, { 558000, 67500, 4 }, { 500000, 65000, 4 } },
	} },
};

/* CON1 offsets in apmixed */
static const u32 fire_pll_con1[FIRE_NR_PLL] = { 0x21c, 0x20c, 0x22c };

struct fire_buck {
	const char *name;
	u32 vsel, da_vsel;		/* VPROC */
	u32 sram_vsel, sram_da_vsel;	/* VSRAM_PROC, value in [14:8] */
	u32 track;
};

/* LL and CCI on VPROC12, L on VPROC11 */
static const struct fire_buck fire_bucks[2] = {
	{ "vproc12", MT6358_BUCK_VPROC12_ELR0, MT6358_BUCK_VPROC12_DBG0,
	  MT6358_LDO_VSRAM_CON1, MT6358_LDO_VSRAM_PROC12_DBG0,
	  MT6358_LDO_VSRAM_PROC12_TRACKING_CON0 },
	{ "vproc11", MT6358_BUCK_VPROC11_ELR0, MT6358_BUCK_VPROC11_DBG0,
	  MT6358_LDO_VSRAM_CON0, MT6358_LDO_VSRAM_PROC11_DBG0,
	  MT6358_LDO_VSRAM_PROC11_TRACKING_CON0 },
};

static struct {
	void __iomem *apmixed;
	struct regmap *pmic;
	const struct fire_seg *seg;
	bool sram_track[2];
	int idx[FIRE_NR_PLL];
	struct cpufreq_frequency_table table[2][FIRE_NR_OPP + 1];
	struct mutex lock;
} fire;

/* PLL */

static u32 fire_pll_get(int pll)
{
	u32 con1 = readl(fire.apmixed + fire_pll_con1[pll]);
	u64 vco = (u64)(con1 & PLL_CON1_PCW) * 26000 >> 14;

	/* the tables are in whole MHz of VCO; undo the PCW truncation */
	vco = DIV_ROUND_CLOSEST_ULL(vco, 1000) * 1000;
	return vco >> min_t(u32, FIELD_GET(PLL_CON1_POSDIV, con1), 4);
}

static void fire_pll_posdiv(int pll, u32 posdiv)
{
	void __iomem *reg = fire.apmixed + fire_pll_con1[pll];
	u32 val = readl(reg) & ~PLL_CON1_POSDIV;

	writel(val | FIELD_PREP(PLL_CON1_POSDIV, ilog2(posdiv)), reg);
	udelay(1);
}

/* Never runs the cluster above the old or the new rate on the way. */
static void fire_pll_set(int pll, const struct fire_opp *opp)
{
	void __iomem *reg = fire.apmixed + fire_pll_con1[pll];
	u32 cur_posdiv = 1 << FIELD_GET(PLL_CON1_POSDIV, readl(reg));
	u32 pcw = div_u64((u64)(opp->khz / 1000 * opp->posdiv) << 14, 26);
	u32 val;

	if (cur_posdiv < opp->posdiv)
		fire_pll_posdiv(pll, opp->posdiv);

	val = readl(reg) & ~PLL_CON1_PCW;
	writel(val | (pcw & PLL_CON1_PCW) | PLL_CON1_CHG, reg);
	udelay(PLL_SETTLE_US);

	if (cur_posdiv > opp->posdiv)
		fire_pll_posdiv(pll, opp->posdiv);
}

/* PMIC */

static int fire_volt_read(u32 reg, u32 shift)
{
	unsigned int val;
	int ret = regmap_read(fire.pmic, reg, &val);

	if (ret)
		return ret;
	return ((val >> shift) & 0x7f) * VOLT_STEP + VOLT_BASE;
}

static int fire_volt_write(u32 reg, u32 da_reg, u32 da_shift, u32 volt)
{
	u32 sel = DIV_ROUND_UP(volt - VOLT_BASE, VOLT_STEP);
	int old = fire_volt_read(da_reg, da_shift);
	int ret, i;

	if (old < 0)
		return old;
	ret = regmap_update_bits(fire.pmic, reg, 0x7f, sel);
	if (ret)
		return ret;
	udelay(abs((int)volt - old) / VOLT_SLEW + 5);

	/* the buck must follow, or the clock must not go up */
	for (i = 0; i < 10; i++) {
		if (fire_volt_read(da_reg, da_shift) == sel * VOLT_STEP + VOLT_BASE)
			return 0;
		udelay(10);
	}
	pr_err("fire-cpufreq: %#x stuck at %d, wanted %u\n", da_reg,
	       fire_volt_read(da_reg, da_shift), volt);
	return -EIO;
}

static u32 fire_sram_for(u32 vproc)
{
	return clamp_t(u32, vproc + VSRAM_VPROC_GAP, VSRAM_MIN, VSRAM_MAX);
}

/* Move VPROC, keeping VSRAM above it and within the allowed gap. */
static int fire_buck_set(int b, u32 target)
{
	const struct fire_buck *bk = &fire_bucks[b];
	int vproc, vsram, ret;

	target = min_t(u32, target, VPROC_MAX);
	if (fire.sram_track[b])
		return fire_volt_write(bk->vsel, bk->da_vsel, 0, target);

	for (;;) {
		vproc = fire_volt_read(bk->da_vsel, 0);
		vsram = fire_volt_read(bk->sram_da_vsel, 8);
		if (vproc < 0 || vsram < 0)
			return -EIO;

		if (vproc < target) {
			u32 s = min_t(u32, fire_sram_for(target),
				      vproc + VSRAM_VPROC_MAX_GAP);
			u32 p = min_t(u32, target, s);

			if (s > vsram) {
				ret = fire_volt_write(bk->sram_vsel,
						      bk->sram_da_vsel, 8, s);
				if (ret)
					return ret;
			}
			ret = fire_volt_write(bk->vsel, bk->da_vsel, 0, p);
		} else if (vproc > target) {
			u32 p = max_t(u32, target, vsram - VSRAM_VPROC_MAX_GAP);

			ret = fire_volt_write(bk->vsel, bk->da_vsel, 0, p);
			if (ret)
				return ret;
			ret = fire_volt_write(bk->sram_vsel, bk->sram_da_vsel, 8,
					      max_t(u32, fire_sram_for(target), p));
		} else {
			if (vsram != fire_sram_for(target) &&
			    fire_sram_for(target) >= vproc)
				return fire_volt_write(bk->sram_vsel,
						       bk->sram_da_vsel, 8,
						       fire_sram_for(target));
			return 0;
		}
		if (ret)
			return ret;
	}
}

/* DVFS */

static const struct fire_opp *fire_opp(int pll, int idx)
{
	return &fire.seg->opp[pll][idx];
}

/* Highest CCI step at or under the voltage the clusters already need. */
static int fire_cci_idx(u32 volt)
{
	int i;

	for (i = 0; i < FIRE_NR_OPP - 1; i++)
		if (fire_opp(FIRE_CCI, i)->volt <= volt)
			break;
	return i;
}

static int fire_dvfs(int pll, int idx)
{
	int idx_new[FIRE_NR_PLL];
	u32 v_l, v_12;
	int i, ret, cur;

	memcpy(idx_new, fire.idx, sizeof(idx_new));
	idx_new[pll] = idx;

	v_l = fire_opp(FIRE_L, idx_new[FIRE_L])->volt;
	v_12 = max(fire_opp(FIRE_LL, idx_new[FIRE_LL])->volt, v_l);
	idx_new[FIRE_CCI] = fire_cci_idx(v_12);
	v_12 = max(v_12, fire_opp(FIRE_CCI, idx_new[FIRE_CCI])->volt);

	/* up: voltage first */
	cur = fire_volt_read(fire_bucks[0].da_vsel, 0);
	if (cur < 0)
		return cur;
	if (v_12 > cur) {
		ret = fire_buck_set(0, v_12);
		if (ret)
			return ret;
	}
	cur = fire_volt_read(fire_bucks[1].da_vsel, 0);
	if (cur < 0)
		return cur;
	if (v_l > cur) {
		ret = fire_buck_set(1, v_l);
		if (ret)
			return ret;
	}

	for (i = 0; i < FIRE_NR_PLL; i++) {
		if (idx_new[i] == fire.idx[i])
			continue;
		fire_pll_set(i, fire_opp(i, idx_new[i]));
		fire.idx[i] = idx_new[i];
	}

	/* down: voltage last; a failure here only costs power */
	if ((int)v_12 < fire_volt_read(fire_bucks[0].da_vsel, 0))
		fire_buck_set(0, v_12);
	if ((int)v_l < fire_volt_read(fire_bucks[1].da_vsel, 0))
		fire_buck_set(1, v_l);

	return 0;
}

static int fire_target_index(struct cpufreq_policy *policy, unsigned int idx)
{
	int pll = (long)policy->driver_data;
	int ret;

	mutex_lock(&fire.lock);
	ret = fire_dvfs(pll, idx);
	mutex_unlock(&fire.lock);
	return ret;
}

static unsigned int fire_get(unsigned int cpu)
{
	return fire_pll_get(cpu < 6 ? FIRE_LL : FIRE_L);
}

static int fire_init(struct cpufreq_policy *policy)
{
	int pll = policy->cpu < 6 ? FIRE_LL : FIRE_L;
	int cpu;

	for (cpu = pll == FIRE_LL ? 0 : 6; cpu < (pll == FIRE_LL ? 6 : 8); cpu++)
		cpumask_set_cpu(cpu, policy->cpus);
	policy->driver_data = (void *)(long)pll;
	policy->freq_table = fire.table[pll];
	policy->cpuinfo.transition_latency = 200 * NSEC_PER_USEC;
	policy->transition_delay_us = 2000;
	return 0;
}

static struct cpufreq_driver fire_cpufreq_driver = {
	.name		= "fire-cpufreq",
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK |
			  CPUFREQ_HAVE_GOVERNOR_PER_POLICY,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= fire_target_index,
	.get		= fire_get,
	.init		= fire_init,
	.attr		= cpufreq_generic_attr,
};

/* Same mapping as _mt_cpufreq_get_cpu_level() */
static const struct fire_seg *fire_pick_seg(u32 seg, u32 ptp)
{
	switch (seg) {
	case 0x80: case 0x01: case 0x40: case 0x02:
	case 0xe0: case 0x07: case 0x10: case 0x08:
		return &fire_segs[ptp <= 0x10 ? 1 : 0];
	case 0x90: case 0x09: case 0x50: case 0x0a:
	case 0xa0: case 0x05: case 0x60: case 0x06:
		return &fire_segs[2];
	case 0:
		return &fire_segs[0];	/* unfused: CPU_LEVEL_0 */
	default:
		return NULL;		/* 6767 bins, not in the table */
	}
}

/* Index of the table step the PLL runs at now (or the next one up). */
static int fire_cur_idx(int pll)
{
	u32 khz = fire_pll_get(pll);
	int i;

	for (i = FIRE_NR_OPP - 1; i > 0; i--)
		if (fire_opp(pll, i)->khz >= khz)
			break;
	return i;
}

static int fire_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct platform_device *pmic;
	struct mt6397_chip *chip;
	void __iomem *efuse;
	u32 seg, ptp;
	int i, j;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mt6358");
	pmic = np ? of_find_device_by_node(np) : NULL;
	of_node_put(np);
	chip = pmic ? platform_get_drvdata(pmic) : NULL;
	if (pmic)
		put_device(&pmic->dev);
	if (!chip || !chip->regmap)
		return -EPROBE_DEFER;
	fire.pmic = chip->regmap;

	efuse = ioremap(FIRE_EFUSE_BASE, 0x1000);
	if (!efuse)
		return -ENOMEM;
	seg = readl(efuse + FIRE_EFUSE_SEGMENT) & 0xff;
	ptp = readl(efuse + FIRE_EFUSE_PTPOD) & 0xf0;
	iounmap(efuse);

	fire.seg = fire_pick_seg(seg, ptp);
	pr_err("fire-cpufreq: efuse segment %#x ptpod %#x -> %s\n", seg, ptp,
	       fire.seg ? fire.seg->name : "unknown");
	if (!fire.seg)
		return -ENODEV;

	fire.apmixed = devm_ioremap(&pdev->dev, FIRE_APMIXED_BASE, 0x1000);
	if (!fire.apmixed)
		return -ENOMEM;

	for (i = 0; i < 2; i++) {
		unsigned int track = 0;

		regmap_read(fire.pmic, fire_bucks[i].track, &track);
		/* RG_LDO_VSRAM_PROCx_TRACK_ON_CTRL */
		fire.sram_track[i] = track & BIT(1);
		pr_err("fire-cpufreq: %s %d, vsram %d, tracking %#x\n",
		       fire_bucks[i].name,
		       fire_volt_read(fire_bucks[i].da_vsel, 0),
		       fire_volt_read(fire_bucks[i].sram_da_vsel, 8), track);
	}

	for (i = 0; i < FIRE_NR_PLL; i++) {
		fire.idx[i] = fire_cur_idx(i);
		pr_err("fire-cpufreq: pll %d at %u kHz, step %d\n", i,
		       fire_pll_get(i), fire.idx[i]);
	}

	for (i = 0; i < 2; i++) {
		for (j = 0; j < FIRE_NR_OPP; j++)
			fire.table[i][j].frequency = fire_opp(i, j)->khz;
		fire.table[i][j].frequency = CPUFREQ_TABLE_END;
	}

	mutex_init(&fire.lock);
	return cpufreq_register_driver(&fire_cpufreq_driver);
}

static const struct of_device_id fire_cpufreq_of_match[] = {
	{ .compatible = "mediatek,mt-cpufreq" },
	{ }
};

static struct platform_driver fire_cpufreq_pdrv = {
	.probe = fire_cpufreq_probe,
	.driver = {
		.name = "fire-cpufreq",
		.of_match_table = fire_cpufreq_of_match,
	},
};
builtin_platform_driver(fire_cpufreq_pdrv);
