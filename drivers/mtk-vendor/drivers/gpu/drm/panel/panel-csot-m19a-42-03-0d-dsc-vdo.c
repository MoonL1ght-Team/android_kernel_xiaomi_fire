// SPDX-License-Identifier: GPL-2.0
/*
 * CSOT FT8720 1080x2460 DSC video mode panel of Xiaomi Redmi 12 (fire),
 * "dsi_panel_m19a_42_03_0d_dsc_vdo", driven through the MT6382 bridge.
 *
 * Timings, DSC and mtk_panel_params values match the stock 6.6 vendor
 * module; the command sequences match it and the 4.19 LCM driver.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include "../mediatek/mediatek_v2/mtk_panel_ext.h"

#define FRAME_WIDTH		1080
#define FRAME_HEIGHT		2460
#define PHYSICAL_WIDTH		69336
#define PHYSICAL_HEIGHT		157932

#define HFP			172
#define HSA			16
#define HBP			22
#define VSA			10
#define VBP			20

#define DATA_RATE		770

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *biasp_gpio, *biasn_gpio;

	bool prepared;
	bool enabled;

	int error;
};

#define lcm_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	const u8 *cmd = data;
	ssize_t ret;

	if (ctx->error < 0)
		return;

	if (*cmd < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %*ph\n",
			ret, (int)len, data);
		ctx->error = ret;
	}
}

/* Enable the FocalTech command 2 page. */
static void lcm_cmd2_enable(struct lcm *ctx)
{
	lcm_dcs_write_seq_static(ctx, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x87, 0x25, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x80);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x87, 0x25);
}

static void lcm_cmd2_disable(struct lcm *ctx)
{
	lcm_dcs_write_seq_static(ctx, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0xFF, 0xFF, 0xFF);
}

static void lcm_panel_init(struct lcm *ctx)
{
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(3000, 3100);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(1000, 1100);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(12000, 12100);

	lcm_cmd2_enable(ctx);

	lcm_dcs_write_seq_static(ctx, 0x00, 0xB0);
	lcm_dcs_write_seq_static(ctx, 0xB4, 0x00, 0x14, 0x02, 0x00, 0x01, 0xE8,
				 0x00, 0x07, 0x05, 0x0E, 0x05, 0x16, 0x10, 0xF0);
	/* 0x51 bit depth and PWM frequency */
	lcm_dcs_write_seq_static(ctx, 0x00, 0xB0);
	lcm_dcs_write_seq_static(ctx, 0xCA, 0x03, 0x03, 0x0B, 0x0F, 0xFF, 0x08);
	/* CABC: UI 90%, still 70%, moving 40% */
	lcm_dcs_write_seq_static(ctx, 0x00, 0x80);
	lcm_dcs_write_seq_static(ctx, 0xCA, 0xE6, 0xD1, 0xC0, 0xB4, 0xAA, 0xA1,
				 0x9A, 0x94, 0x8F, 0x8A, 0x86, 0x82);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x90);
	lcm_dcs_write_seq_static(ctx, 0xCA, 0xFE, 0xFF, 0x66, 0xF5, 0xFF, 0xAD,
				 0xFA, 0xFF, 0xD6);
	lcm_dcs_write_seq_static(ctx, 0x00, 0xA0);
	lcm_dcs_write_seq_static(ctx, 0xCA, 0x06, 0x06, 0x07);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x82);
	lcm_dcs_write_seq_static(ctx, 0xCE, 0x27, 0x27);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x83);
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x18, 0x00, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x93);
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x18, 0x00, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x00, 0xA3);
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x18, 0x00, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x73);
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x18, 0x00, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x00, 0xA4);
	lcm_dcs_write_seq_static(ctx, 0xC1, 0x6C);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x85);
	lcm_dcs_write_seq_static(ctx, 0xCE, 0xE0);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x9A);
	lcm_dcs_write_seq_static(ctx, 0xCE, 0x0C, 0x1C, 0x03);
	lcm_dcs_write_seq_static(ctx, 0x00, 0xB2);
	lcm_dcs_write_seq_static(ctx, 0xCF, 0xD9, 0xDD);
	lcm_dcs_write_seq_static(ctx, 0x00, 0xB7);
	lcm_dcs_write_seq_static(ctx, 0xCF, 0x4D, 0x51);
	lcm_dcs_write_seq_static(ctx, 0x00, 0xE4);
	lcm_dcs_write_seq_static(ctx, 0xCF, 0x0A, 0x18, 0x0A, 0x17, 0x0A, 0x17,
				 0x0A, 0x17, 0x0A, 0x17, 0x0A, 0x17);
	lcm_dcs_write_seq_static(ctx, 0x00, 0x0E);
	lcm_dcs_write_seq_static(ctx, 0xF3, 0x80, 0xFF);

	lcm_cmd2_disable(ctx);

	lcm_dcs_write_seq_static(ctx, 0x51, 0xFF, 0x07);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x2C);
	lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x11, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x11, 0x00);
	msleep(95);
	lcm_dcs_write_seq_static(ctx, 0x29, 0x00);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = BACKLIGHT_POWER_OFF;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_err("fire-panel unprepare (prepared=%d)\n", ctx->prepared);

	if (!ctx->prepared)
		return 0;

	lcm_cmd2_enable(ctx);
	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(35);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(150);
	/* deep standby */
	lcm_dcs_write_seq_static(ctx, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xF7, 0x5A, 0xA5, 0x95, 0x27);
	lcm_cmd2_disable(ctx);
	msleep(10);

	gpiod_set_value(ctx->biasn_gpio, 0);
	gpiod_set_value(ctx->biasp_gpio, 0);
	usleep_range(5000, 5100);

	ctx->error = 0;
	ctx->prepared = false;
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_err("fire-panel prepare (prepared=%d)\n", ctx->prepared);
	if (ctx->prepared)
		return 0;

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(3000, 3100);
	gpiod_set_value(ctx->biasp_gpio, 1);
	usleep_range(2000, 2100);
	gpiod_set_value(ctx->biasn_gpio, 1);
	usleep_range(2000, 2100);

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;
	mtk_panel_tch_rst(panel);

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = BACKLIGHT_POWER_ON;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;
	return 0;
}

#define LCM_MODE(_clock, _vfp) {				\
	.clock = _clock,					\
	.hdisplay = FRAME_WIDTH,				\
	.hsync_start = FRAME_WIDTH + HFP,			\
	.hsync_end = FRAME_WIDTH + HFP + HSA,			\
	.htotal = FRAME_WIDTH + HFP + HSA + HBP,		\
	.vdisplay = FRAME_HEIGHT,				\
	.vsync_start = FRAME_HEIGHT + (_vfp),			\
	.vsync_end = FRAME_HEIGHT + (_vfp) + VSA,		\
	.vtotal = FRAME_HEIGHT + (_vfp) + VSA + VBP,		\
}

/* The single mode is the preferred one. */
static const struct drm_display_mode lcm_modes[] = {
	LCM_MODE(291178, 18),		/* 90 Hz */
	/*
	 * Only 90 Hz for now: on every refresh rate switch the hwcomposer
	 * asks the MDP blitter, which is not ported, and the frame flickers;
	 * at 48/36 Hz the FT8725 touch, scanning in step with the panel,
	 * also stops reporting. The stock 60/48/36 Hz modes can come back
	 * with MDP.
	 */
};

#define LCM_EXT_PARAMS(_vfp_low_power) {			\
	.vfp_low_power = _vfp_low_power,			\
	.cust_esd_check = 1,					\
	.esd_check_enable = 1,					\
	.lcm_esd_check_table[0] = {				\
		.cmd = 0x0a,					\
		.count = 1,					\
		.para_list[0] = 0x9c,				\
	},							\
	.physical_width_um = PHYSICAL_WIDTH,			\
	.physical_height_um = PHYSICAL_HEIGHT,			\
	.dsc_params = {						\
		.bdg_dsc_enable = 1,				\
		.ver = 17,					\
		.slice_mode = 1,				\
		.rgb_swap = 0,					\
		.dsc_cfg = 34,					\
		.rct_on = 1,					\
		.bit_per_channel = 8,				\
		.dsc_line_buf_depth = 9,			\
		.bp_enable = 1,					\
		.bit_per_pixel = 128,				\
		.pic_height = FRAME_HEIGHT,			\
		.pic_width = FRAME_WIDTH,			\
		.slice_height = 20,				\
		.slice_width = 540,				\
		.chunk_size = 540,				\
		.xmit_delay = 512,				\
		.dec_delay = 526,				\
		.scale_value = 32,				\
		.increment_interval = 488,			\
		.decrement_interval = 7,			\
		.line_bpg_offset = 12,				\
		.nfl_bpg_offset = 1294,				\
		.slice_bpg_offset = 1302,			\
		.initial_offset = 6144,				\
		.final_offset = 4336,				\
		.flatness_minqp = 3,				\
		.flatness_maxqp = 12,				\
		.rc_model_size = 8192,				\
		.rc_edge_factor = 6,				\
		.rc_quant_incr_limit0 = 11,			\
		.rc_quant_incr_limit1 = 11,			\
		.rc_tgt_offset_hi = 3,				\
		.rc_tgt_offset_lo = 3,				\
	},							\
	.data_rate = DATA_RATE,					\
	.dyn_fps = {						\
		.switch_en = 1,					\
		.vact_timing_fps = 90,				\
	},							\
	.dyn = {						\
		.data_rate = 750,				\
		.hbp = 14,					\
		.vfp_lp_dyn = 2526,				\
	},							\
	.lcm_index = 4,						\
}

static struct mtk_panel_params ext_params[] = {
	LCM_EXT_PARAMS(2200),
};

static_assert(ARRAY_SIZE(ext_params) == ARRAY_SIZE(lcm_modes));

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
				 void *handle, unsigned int level)
{
	/* 11-bit brightness */
	char bl_tb[] = { 0x51, level >> 3, level & 0x7 };

	if (!cb)
		return -EINVAL;

	cb(dsi, handle, bl_tb, ARRAY_SIZE(bl_tb));
	return 0;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
				   struct drm_connector *connector,
				   unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	struct drm_display_mode *m;
	unsigned int i = 0;
	int vrefresh;

	list_for_each_entry(m, &connector->modes, head)
		if (i++ == mode)
			break;
	if (list_entry_is_head(m, &connector->modes, head))
		return -EINVAL;

	vrefresh = drm_mode_vrefresh(m);
	for (i = 0; i < ARRAY_SIZE(lcm_modes); i++) {
		if (drm_mode_vrefresh(&lcm_modes[i]) == vrefresh) {
			ext->params = &ext_params[i];
			return 0;
		}
	}
	return 1;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	gpiod_set_value(ctx->reset_gpio, on);
	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	return 1;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
};

static int lcm_get_modes(struct drm_panel *panel,
			 struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(lcm_modes); i++) {
		mode = drm_mode_duplicate(connector->dev, &lcm_modes[i]);
		if (!mode)
			return -ENOMEM;
		drm_mode_set_name(mode);
		mode->type = DRM_MODE_TYPE_DRIVER;
		if (!i)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = PHYSICAL_WIDTH / 1000;
	connector->display_info.height_mm = PHYSICAL_HEIGHT / 1000;

	return i;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

/*
 * The DT lists every panel fire ships with; LK points the DSI endpoint
 * at the one it detected.
 */
static bool lcm_is_connected(struct device *dev)
{
	struct device_node *dsi_node, *endpoint, *remote = NULL;

	dsi_node = of_get_parent(dev->of_node);
	if (!dsi_node)
		return false;
	endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
	if (endpoint) {
		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
	}
	of_node_put(dsi_node);
	of_node_put(remote);

	return remote == dev->of_node;
}

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *backlight;
	struct lcm *ctx;
	int ret;

	if (!lcm_is_connected(dev))
		return -ENODEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	/* LK leaves the panel running: keep the lines high. */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "cannot get reset gpio\n");
	ctx->biasp_gpio = devm_gpiod_get(dev, "biasp", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->biasp_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->biasp_gpio),
				     "cannot get biasp gpio\n");
	ctx->biasn_gpio = devm_gpiod_get(dev, "biasn", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->biasn_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->biasn_gpio),
				     "cannot get biasn gpio\n");

	/* LK left the panel running; mediatek_v2 skips the first prepare. */
	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params[0], &ext_funcs, &ctx->panel);
	if (ret < 0) {
		mipi_dsi_detach(dsi);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "panel probed\n");
	return 0;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "csot,dsi_panel_m19a_42_03_0d_dsc_vdo", },
	{ }
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "dsi_panel_m19a_42_03_0d_dsc_vdo",
		.of_match_table = lcm_of_match,
	},
};
module_mipi_dsi_driver(lcm_driver);

MODULE_DESCRIPTION("CSOT m19a 42-03-0d DSC video mode panel (Xiaomi fire)");
MODULE_LICENSE("GPL");
