// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: MTCMOS power domains as clocks for the stock device tree
 *
 * The stock DT has gpufreq power the GPU through "mtcmos_mfg*" clocks of
 * a "mediatek,scpsys-clk" provider, which the vendor 6.6 kernel
 * implemented in clk-mt6768-pg.c. The same domains are already exposed
 * as genpds by the vendor scpsys driver, so back each clock with a
 * device attached to the matching domain and switch it with runtime PM.
 * That keeps genpd's parent ordering (MFG_ASYNC -> MFG -> MFG_COREx).
 *
 * The SCP_SYS_* clock IDs equal the MT6768_POWER_DOMAIN_* IDs.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/power/mt6768-power.h>

struct fire_mtcmos {
	struct clk_hw hw;
	struct platform_device *pdev;
	bool on;
};

#define to_fire_mtcmos(_hw) container_of(_hw, struct fire_mtcmos, hw)

static int fire_mtcmos_prepare(struct clk_hw *hw)
{
	struct fire_mtcmos *m = to_fire_mtcmos(hw);
	int ret;

	ret = pm_runtime_resume_and_get(&m->pdev->dev);
	if (!ret)
		m->on = true;
	return ret;
}

static void fire_mtcmos_unprepare(struct clk_hw *hw)
{
	struct fire_mtcmos *m = to_fire_mtcmos(hw);

	m->on = false;
	pm_runtime_put_sync(&m->pdev->dev);
}

static int fire_mtcmos_is_prepared(struct clk_hw *hw)
{
	return to_fire_mtcmos(hw)->on;
}

static const struct clk_ops fire_mtcmos_ops = {
	.prepare = fire_mtcmos_prepare,
	.unprepare = fire_mtcmos_unprepare,
	.is_prepared = fire_mtcmos_is_prepared,
};

/* Only the GPU needs these; other domains are handled by their drivers. */
static const struct {
	unsigned int id;
	const char *name;
} fire_mtcmos_domains[] = {
	{ MT6768_POWER_DOMAIN_MFG, "mtcmos_mfg" },
	{ MT6768_POWER_DOMAIN_MFG_CORE0, "mtcmos_mfg_core0" },
	{ MT6768_POWER_DOMAIN_MFG_CORE1, "mtcmos_mfg_core1" },
	{ MT6768_POWER_DOMAIN_MFG_ASYNC, "mtcmos_mfg_async" },
};

static void fire_mtcmos_release(void *data)
{
	struct platform_device *pdev = data;

	pm_runtime_disable(&pdev->dev);
	if (pdev->dev.pm_domain)
		pm_genpd_remove_device(&pdev->dev);
	platform_device_unregister(pdev);
}

static int fire_mtcmos_add(struct device *dev, struct device_node *scpsys,
			   unsigned int id, const char *name,
			   struct clk_hw **hwp)
{
	struct of_phandle_args spec = {
		.np = scpsys,
		.args_count = 1,
		.args = { id },
	};
	struct clk_init_data init = {
		.name = name,
		.ops = &fire_mtcmos_ops,
	};
	struct fire_mtcmos *m;
	int ret;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->pdev = platform_device_register_data(dev, "fire-mtcmos", id,
						NULL, 0);
	if (IS_ERR(m->pdev))
		return PTR_ERR(m->pdev);
	ret = devm_add_action_or_reset(dev, fire_mtcmos_release, m->pdev);
	if (ret)
		return ret;

	ret = of_genpd_add_device(&spec, &m->pdev->dev);
	if (ret)
		/* scpsys not registered yet */
		return ret == -ENOENT ? -EPROBE_DEFER : ret;
	pm_runtime_enable(&m->pdev->dev);

	m->hw.init = &init;
	ret = devm_clk_hw_register(dev, &m->hw);
	if (ret)
		return ret;

	*hwp = &m->hw;
	return 0;
}

static int fire_scpsys_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *data;
	struct device_node *scpsys;
	unsigned int i;
	int ret = 0;

	scpsys = of_find_compatible_node(NULL, NULL, "mediatek,mt6768-scpsys");
	if (!scpsys)
		return -ENODEV;

	data = devm_kzalloc(dev, struct_size(data, hws, MT6768_POWER_DOMAIN_NR),
			    GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto out;
	}
	data->num = MT6768_POWER_DOMAIN_NR;
	for (i = 0; i < data->num; i++)
		data->hws[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < ARRAY_SIZE(fire_mtcmos_domains); i++) {
		ret = fire_mtcmos_add(dev, scpsys, fire_mtcmos_domains[i].id,
				      fire_mtcmos_domains[i].name,
				      &data->hws[fire_mtcmos_domains[i].id]);
		if (ret)
			goto out;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
out:
	of_node_put(scpsys);
	return ret;
}

static const struct of_device_id fire_scpsys_clk_match[] = {
	{ .compatible = "mediatek,scpsys-clk" },
	{ }
};

static struct platform_driver fire_scpsys_clk_driver = {
	.probe = fire_scpsys_clk_probe,
	.driver = {
		.name = "fire-scpsys-clk",
		.of_match_table = fire_scpsys_clk_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(fire_scpsys_clk_driver);
