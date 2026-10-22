// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi fire: the MediaTek dma-buf heap names gralloc asks for
 *
 * The stock Mali gralloc allocates display and GPU buffers from the
 * "mtk_mm" and "mtk_mm-uncached" heaps of the vendor system heap module
 * (mtk_system_heap.c). Provide those names on top of the ACK system
 * heaps; the display and GPU map the pages through their own IOMMUs, so
 * physically scattered buffers are fine.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>

static const struct {
	const char *name;
	const char *backing;
} fire_mtk_heaps[] = {
	{ "mtk_mm", "system" },
	{ "mtk_mm-uncached", "system-uncached" },
};

static struct dma_buf *fire_mtk_heap_allocate(struct dma_heap *heap,
					      unsigned long len, u32 fd_flags,
					      u64 heap_flags)
{
	struct dma_heap *backing = dma_heap_get_drvdata(heap);

	return dma_heap_buffer_alloc(backing, len, fd_flags, heap_flags);
}

static const struct dma_heap_ops fire_mtk_heap_ops = {
	.allocate = fire_mtk_heap_allocate,
};

static int __init fire_mtk_heaps_init(void)
{
	struct dma_heap_export_info exp_info = {
		.ops = &fire_mtk_heap_ops,
	};
	struct dma_heap *heap, *backing;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fire_mtk_heaps); i++) {
		backing = dma_heap_find(fire_mtk_heaps[i].backing);
		/* system-uncached is not registered on every config */
		if (!backing)
			backing = dma_heap_find("system");
		if (!backing) {
			pr_err("fire-mtk-heaps: no system heap\n");
			return -ENODEV;
		}

		exp_info.name = fire_mtk_heaps[i].name;
		exp_info.priv = backing;
		heap = dma_heap_add(&exp_info);
		if (IS_ERR(heap)) {
			dma_heap_put(backing);
			pr_err("fire-mtk-heaps: cannot add %s: %ld\n",
			       exp_info.name, PTR_ERR(heap));
		}
	}
	return 0;
}
/* After the system heap's module_init */
late_initcall(fire_mtk_heaps_init);
