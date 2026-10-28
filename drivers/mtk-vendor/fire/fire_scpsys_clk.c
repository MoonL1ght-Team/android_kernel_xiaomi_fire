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
#include <linux/io.h>
#include <linux/iopoll.h>
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
	/*
	 * Only the clock (that is gpufreq) decides when the MFG domains are
	 * on. Left to system sleep, genpd powers them off in suspend_noirq
	 * and back on in resume_noirq before gpufreq has turned VGPU back
	 * on: the MTCMOS ack times out, the GPU is dead after s2idle and
	 * the screen stays black.
	 */
	dev_pm_syscore_device(&m->pdev->dev, true);
	pm_runtime_enable(&m->pdev->dev);

	m->hw.init = &init;
	ret = devm_clk_hw_register(dev, &m->hw);
	if (ret)
		return ret;

	*hwp = &m->hw;
	return 0;
}

/*
 * MD1 (SCP_SYS_MD1, used by the modem driver as "scp-sys-md1-main"):
 * the generic scpsys MD ops release reset first and leave the domain
 * isolated with its clock disabled, so the modem's registers read 0.
 * Use the stock clk-mt6768-pg sequence (spm_mtcmos_ctrl_md1_pwr and
 * spm_mtcmos_ctrl_md1_bus_prot) instead.
 */
#define FIRE_SPM_BASE		0x10006000
#define FIRE_INFRACFG_BASE	0x10001000
#define SPM_POWERON_CONFIG_EN	0x000
#define  SPM_PROJECT_CODE	0xb16
#define SPM_PWR_STATUS		0x180
#define SPM_PWR_STATUS_2ND	0x184
#define SPM_MD1_PWR_CON		0x320
#define SPM_MD_SRAM_ISO_CON	0x394
#define SPM_MD_EXTRA_PWR_CON	0x398
#define  PWR_RST_B		BIT(0)
#define  PWR_ISO		BIT(1)
#define  PWR_ON			BIT(2)
#define  PWR_ON_2ND		BIT(3)
#define  PWR_CLK_DIS		BIT(4)
#define  MD1_SRAM_PDN		BIT(8)
#define  MD1_PWR_STA		BIT(0)
#define INFRA_PROTECTEN_SET	0x2a0
#define INFRA_PROTECTEN_CLR	0x2a4
#define INFRA_PROTECTEN_STA1	0x228
#define INFRA_PROTECTEN_1_SET	0x2a8
#define INFRA_PROTECTEN_1_CLR	0x2ac
#define INFRA_PROTECTEN_1_STA1	0x258
#define MD1_PROT_STEP1		BIT(7)
#define MD1_PROT_STEP2_0	(BIT(3) | BIT(4))
#define MD1_PROT_STEP2_1	BIT(6)

struct fire_md1 {
	struct clk_hw hw;
	void __iomem *spm, *infra;
	bool on;
};

#define to_fire_md1(_hw) container_of(_hw, struct fire_md1, hw)

static void fire_md1_rmw(void __iomem *reg, u32 clr, u32 set)
{
	writel((readl(reg) & ~clr) | set, reg);
}

static int fire_md1_prepare(struct clk_hw *hw)
{
	struct fire_md1 *m = to_fire_md1(hw);
	void __iomem *con = m->spm + SPM_MD1_PWR_CON;
	u32 a, b;
	int ret;

	writel(SPM_PROJECT_CODE << 16 | BIT(0), m->spm + SPM_POWERON_CONFIG_EN);
	fire_md1_rmw(con, PWR_RST_B, 0);
	fire_md1_rmw(con, 0, PWR_ON);
	fire_md1_rmw(con, 0, PWR_ON_2ND);
	ret = readl_poll_timeout(m->spm + SPM_PWR_STATUS, a,
				 (a & MD1_PWR_STA) &&
				 (readl(m->spm + SPM_PWR_STATUS_2ND) & MD1_PWR_STA),
				 10, 100000);
	b = readl(m->spm + SPM_PWR_STATUS_2ND);
	if (ret) {
		pr_err("fire-md1: power ack timeout (%#x %#x)\n", a, b);
		return ret;
	}
	fire_md1_rmw(con, MD1_SRAM_PDN, 0);
	fire_md1_rmw(m->spm + SPM_MD_SRAM_ISO_CON, 0, BIT(0));
	fire_md1_rmw(con, PWR_ISO, 0);
	fire_md1_rmw(con, PWR_CLK_DIS, 0);
	fire_md1_rmw(con, 0, PWR_RST_B);
	fire_md1_rmw(m->spm + SPM_MD_EXTRA_PWR_CON, BIT(0), 0);

	/* release the bus protection (acks are not checked, as in stock) */
	writel(MD1_PROT_STEP2_0, m->infra + INFRA_PROTECTEN_CLR);
	writel(MD1_PROT_STEP2_1, m->infra + INFRA_PROTECTEN_1_CLR);
	writel(MD1_PROT_STEP1, m->infra + INFRA_PROTECTEN_CLR);

	pr_info("fire-md1: on, PWR_CON %#x\n", readl(con));
	m->on = true;
	return 0;
}

static void fire_md1_unprepare(struct clk_hw *hw)
{
	struct fire_md1 *m = to_fire_md1(hw);
	void __iomem *con = m->spm + SPM_MD1_PWR_CON;
	u32 v;

	m->on = false;
	writel(MD1_PROT_STEP1, m->infra + INFRA_PROTECTEN_SET);
	readl_poll_timeout(m->infra + INFRA_PROTECTEN_STA1, v,
			   (v & MD1_PROT_STEP1) == MD1_PROT_STEP1, 10, 10000);
	writel(MD1_PROT_STEP2_0, m->infra + INFRA_PROTECTEN_SET);
	readl_poll_timeout(m->infra + INFRA_PROTECTEN_STA1, v,
			   (v & MD1_PROT_STEP2_0) == MD1_PROT_STEP2_0, 10, 10000);
	writel(MD1_PROT_STEP2_1, m->infra + INFRA_PROTECTEN_1_SET);
	readl_poll_timeout(m->infra + INFRA_PROTECTEN_1_STA1, v,
			   (v & MD1_PROT_STEP2_1) == MD1_PROT_STEP2_1, 10, 10000);

	fire_md1_rmw(m->spm + SPM_MD_EXTRA_PWR_CON, 0, BIT(0));
	fire_md1_rmw(con, 0, PWR_CLK_DIS);
	fire_md1_rmw(con, 0, PWR_ISO);
	fire_md1_rmw(m->spm + SPM_MD_SRAM_ISO_CON, BIT(0), 0);
	fire_md1_rmw(con, 0, MD1_SRAM_PDN);
	fire_md1_rmw(con, PWR_ON, 0);
	fire_md1_rmw(con, PWR_ON_2ND, 0);
}

static int fire_md1_is_prepared(struct clk_hw *hw)
{
	return to_fire_md1(hw)->on;
}

static const struct clk_ops fire_md1_ops = {
	.prepare = fire_md1_prepare,
	.unprepare = fire_md1_unprepare,
	.is_prepared = fire_md1_is_prepared,
};

static int fire_md1_add(struct device *dev, struct clk_hw **hwp)
{
	struct clk_init_data init = {
		.name = "mtcmos_md1",
		.ops = &fire_md1_ops,
	};
	struct fire_md1 *m;
	int ret;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->spm = devm_ioremap(dev, FIRE_SPM_BASE, 0x1000);
	m->infra = devm_ioremap(dev, FIRE_INFRACFG_BASE, 0x1000);
	if (!m->spm || !m->infra)
		return -ENOMEM;
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

	ret = fire_md1_add(dev, &data->hws[MT6768_POWER_DOMAIN_MD]);
	if (ret)
		goto out;

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
