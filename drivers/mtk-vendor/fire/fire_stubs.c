// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: stand-ins for vendor providers mediatek_v2 links against
 * but MT6768 does not use for display: MML (newer SoCs' media path),
 * the frequency meter, DRAMC info, MMDVFS debug, and the SMI and IOMMU
 * debug helpers (smi-dbg, iommu_debug), the GIC IRQ dump and the SLC
 * group id of dma-bufs (MT6768 has no system level cache).
 */

#include <linux/dma-buf.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include "mtk-mml-drm-adaptor.h"
#include "clk-fmeter.h"
#include "mtk-mmdvfs-debug.h"
#include <soc/mediatek/dramc.h>
#include <soc/mediatek/smi.h>
#include "iommu_debug.h"

struct mml_drm_ctx *mml_drm_get_context(struct platform_device *pdev,
					struct mml_drm_param *disp)
{
	return ERR_PTR(-ENODEV);
}

void mml_drm_put_context(struct mml_drm_ctx *dctx) { }
bool mml_drm_ctx_idle(struct mml_drm_ctx *dctx) { return true; }
void mml_drm_kick_done(struct mml_drm_ctx *dctx) { }

int mml_drm_get_hw_caps(u32 *mode_caps, u32 *pq_caps)
{
	*mode_caps = 0;
	*pq_caps = 0;
	return 0;
}

enum mml_mode mml_drm_query_cap(struct mml_drm_ctx *dctx,
				struct mml_frame_info *info)
{
	return MML_MODE_NOT_SUPPORT;
}

bool mml_drm_query_hw_support(const struct mml_frame_info *info)
{
	return false;
}

int mml_drm_query_multi_layer(struct mml_drm_ctx *dctx,
			      struct mml_frame_info *infos, u32 cnt,
			      u32 duration_us)
{
	return -ENODEV;
}

s32 mml_drm_racing_config_sync(struct mml_drm_ctx *dctx, struct cmdq_pkt *pkt)
{
	return -ENODEV;
}

s32 mml_drm_racing_stop_sync(struct mml_drm_ctx *dctx, struct cmdq_pkt *pkt)
{
	return -ENODEV;
}

void mml_drm_set_panel_pixel(struct mml_drm_ctx *dctx, u32 panel_width,
			     u32 panel_height) { }
void mml_drm_split_info(struct mml_submit *submit,
			struct mml_submit *submit_pq) { }

s32 mml_drm_stop(struct mml_drm_ctx *dctx, struct mml_submit *submit,
		 bool force)
{
	return -ENODEV;
}

s32 mml_drm_submit(struct mml_drm_ctx *dctx, struct mml_submit *submit,
		   void *cb_param)
{
	return -ENODEV;
}

void mml_drm_submit_timeout(void) { }

struct platform_device *mml_get_plat_device(struct platform_device *pdev)
{
	return NULL;
}

unsigned int mt_get_fmeter_freq(unsigned int id, enum FMETER_TYPE type)
{
	return 0;
}

unsigned int mtk_dramc_get_ddr_type(void)
{
	return 0;
}

void mmdvfs_debug_status_dump(struct seq_file *file) { }

void mtk_smi_dbg_dump_for_disp(void) { }

s32 mtk_smi_dbg_hang_detect(char *user)
{
	return 0;
}

int mtk_smi_dbg_register_notifier(struct notifier_block *nb)
{
	return 0;
}

int mtk_iommu_register_fault_callback(int port, mtk_iommu_fault_callback_t fn,
				      void *cb_data, bool is_vpu)
{
	return 0;
}

void mt_irq_dump_status(unsigned int irq) { }

int dma_buf_get_gid(struct dma_buf *dmabuf)
{
	return -EOPNOTSUPP;
}
