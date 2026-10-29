// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: bind the /interconnect (mediatek,mt6768-mmqos) node.
 *
 * The display OVL/RDMA nodes list it in "interconnects", so fw_devlink
 * holds them, and with them mediatek-drm, until the 10 s deferred-probe
 * timeout. We build without the MTK interconnect extension
 * (of_mtk_icc_get() returns NULL), so nothing needs a real MMQoS
 * provider: an empty driver is enough to release the consumers.
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

static int fire_mmqos_probe(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id fire_mmqos_of_match[] = {
	{ .compatible = "mediatek,mt6768-mmqos" },
	{ }
};

static struct platform_driver fire_mmqos_driver = {
	.probe = fire_mmqos_probe,
	.driver = {
		.name = "fire-mmqos",
		.of_match_table = fire_mmqos_of_match,
	},
};
builtin_platform_driver(fire_mmqos_driver);
