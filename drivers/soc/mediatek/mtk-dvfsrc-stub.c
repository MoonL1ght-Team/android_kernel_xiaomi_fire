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
 * Bind to the DVFSRC node so its consumers can probe. The hardware is
 * left as configured by the bootloader.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static int mtk_dvfsrc_stub_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "using bootloader DVFSRC configuration\n");
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
