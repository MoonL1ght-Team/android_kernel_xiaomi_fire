// SPDX-License-Identifier: GPL-2.0
/*
 * Standalone provider wrapper for MediaTek sync symbols.
 *
 * Keep the implementation shared with mediatek_v2, but build it outside the
 * DRM bundle so MML can depend on mtk_sync.ko without creating MML <-> DRM.
 */
#include "../mediatek_v2/mtk_sync.c"
