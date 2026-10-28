// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6768 DVFSRC placeholder
 *
 * The vendor device tree points the MSDC hosts at the DVFSRC vcore
 * regulator via "dvfsrc-vcore-supply". mtk-sd never requests that
 * supply, but fw_devlink still treats the DVFSRC node as a mandatory
 * supplier, so without a bound driver both MMC hosts defer forever and
 * first stage init cannot find its partitions.
 *
 * Bind to the DVFSRC node so its consumers can probe, and start the
 * DVFSRC the way the stock dvfsrc-opp-mt6768, mtk-dvfsrc and
 * mtk-dvfsrc-start modules do: ATF loads the SPM firmware for the DRAM
 * type, then initialises and starts the DVFSRC. The modem votes for
 * vcore/DRAM levels through it and waits for the acks while booting.
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define MTK_SIP_SPM_DVFSRC_INIT		0xc200022a
#define MTK_SIP_VCOREFS_CONTROL		0xc2000506
#define  VCOREFS_SMC_CMD_INIT		0
#define  VCOREFS_SMC_CMD_START		1
#define  VCOREFS_SMC_CMD_DRAM_FREQ	0x10

/* DVFSRC_SW_REQ (stock mt6768_regs): DRAM level 1:0, vcore level 3:2 */
#define DVFSRC_SW_REQ			0x4
#define  DVFSRC_SW_REQ_MAX		0xe

/* SPM firmware index: 1 is pcm_allinone_lp4_3600 (LPDDR4X 3600) */
static int spmfw_idx = 1;
module_param(spmfw_idx, int, 0444);
static bool start = true;
module_param(start, bool, 0444);

static void mtk_dvfsrc_stub_dump(struct device *dev, void __iomem *base,
				 const char *when)
{
	if (base)
		dev_info(dev, "%s: %*ph\n", when, 0x40, base);
}

static int mtk_dvfsrc_stub_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct arm_smccc_res res;
	struct device_node *np;
	void __iomem *base;
	int cnt, i;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		base = NULL;
	mtk_dvfsrc_stub_dump(dev, base, "before");
	if (!start) {
		dev_info(dev, "using bootloader DVFSRC configuration\n");
		return 0;
	}

	/*
	 * As dvfsrc-opp-mt6768: give ATF the DRAM rate of each DFS step
	 * (freq_step, filled into the dramc node by LK) before the SPM
	 * firmware init. The modem's DSP votes for these levels.
	 */
	np = of_find_compatible_node(NULL, NULL, "mediatek,common-dramc");
	cnt = np ? of_property_count_u32_elems(np, "freq_step") : -ENODEV;
	for (i = 0; i < cnt && i < 8; i++) {
		u32 freq;

		if (of_property_read_u32_index(np, "freq_step", i, &freq))
			break;
		arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, VCOREFS_SMC_CMD_DRAM_FREQ,
			      i, freq, 0, 0, 0, 0, &res);
		dev_info(dev, "dram step %d: %u (%lx)\n", i, freq, res.a0);
	}
	if (cnt <= 0)
		dev_info(dev, "no freq_step in the dramc node (%d)\n", cnt);
	of_node_put(np);

	arm_smccc_smc(MTK_SIP_SPM_DVFSRC_INIT, 0, spmfw_idx, 0, 0, 0, 0, 0,
		      &res);
	dev_info(dev, "spmfw init %d: %lx\n", spmfw_idx, res.a0);
	arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, VCOREFS_SMC_CMD_INIT, 0, 0,
		      0, 0, 0, 0, &res);
	dev_info(dev, "init: %lx\n", res.a0);
	arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, VCOREFS_SMC_CMD_START, 0, 0,
		      0, 0, 0, 0, &res);
	dev_info(dev, "start: %lx\n", res.a0);

	/*
	 * Nothing votes for DRAM bandwidth here (the stock display and
	 * mmqos drivers do), so once the DVFSRC runs it drops DRAM and
	 * vcore when idle and the display underruns to black. Hold the top
	 * level, which is what the bootloader left running.
	 */
	if (base)
		writel(readl(base + DVFSRC_SW_REQ) | DVFSRC_SW_REQ_MAX,
		       base + DVFSRC_SW_REQ);
	mtk_dvfsrc_stub_dump(dev, base, "after");
	return 0;
}

static const struct of_device_id mtk_dvfsrc_stub_of_match[] = {
	{ .compatible = "mediatek,mt6768-dvfsrc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_dvfsrc_stub_of_match);

static struct platform_driver mtk_dvfsrc_stub_driver = {
	.probe = mtk_dvfsrc_stub_probe,
	.driver = {
		.name = "mtk-dvfsrc-stub",
		.of_match_table = mtk_dvfsrc_stub_of_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(mtk_dvfsrc_stub_driver);

MODULE_DESCRIPTION("MediaTek MT6768 DVFSRC placeholder");
MODULE_LICENSE("GPL");
