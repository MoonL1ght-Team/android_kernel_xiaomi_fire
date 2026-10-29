// SPDX-License-Identifier: GPL-2.0
/*
 * fire: the EMI MPU region API of the vendor emimpu driver, enough for the
 * connectivity stack to open its shared EMI regions to the CONN domain.
 * The protection itself is set by ATF (MTK_SIP_EMIMPU_CONTROL); the values
 * below are the MT6768 emimpu DT node's (domain_cnt 16, addr_align 16,
 * region_cnt 32).
 */

#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <soc/mediatek/emi.h>

#define FIRE_EMIMPU_REGIONS	32
#define FIRE_EMIMPU_DOMAINS	16
#define FIRE_EMIMPU_ALIGN	16

int mtk_emimpu_init_region(struct emimpu_region_t *rg_info, unsigned int rg_num)
{
	unsigned int i;

	if (rg_num >= FIRE_EMIMPU_REGIONS)
		return -1;
	rg_info->start = 0;
	rg_info->end = 0;
	rg_info->rg_num = rg_num;
	rg_info->lock = false;
	rg_info->apc = kmalloc_array(FIRE_EMIMPU_DOMAINS, sizeof(unsigned int),
				     GFP_KERNEL);
	if (!rg_info->apc)
		return -1;
	for (i = 0; i < FIRE_EMIMPU_DOMAINS; i++)
		rg_info->apc[i] = MTK_EMIMPU_FORBIDDEN;
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_init_region);

int mtk_emimpu_free_region(struct emimpu_region_t *rg_info)
{
	kfree(rg_info->apc);
	rg_info->apc = NULL;
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_free_region);

int mtk_emimpu_set_addr(struct emimpu_region_t *rg_info,
			unsigned long long start, unsigned long long end)
{
	rg_info->start = start;
	rg_info->end = end;
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_set_addr);

int mtk_emimpu_set_apc(struct emimpu_region_t *rg_info, unsigned int d_num,
		       unsigned int apc)
{
	if (d_num >= FIRE_EMIMPU_DOMAINS)
		return -1;
	rg_info->apc[d_num] = apc & 0x7;
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_set_apc);

int mtk_emimpu_lock_region(struct emimpu_region_t *rg_info, bool lock)
{
	rg_info->lock = lock;
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_lock_region);

int mtk_emimpu_set_protection(struct emimpu_region_t *rg_info)
{
	struct arm_smccc_res res;
	unsigned int start, end, apc;
	int i, j;

	if (!rg_info->apc)
		return -1;
	start = (unsigned int)(rg_info->start >> FIRE_EMIMPU_ALIGN) |
		(rg_info->rg_num << 24);
	for (i = FIRE_EMIMPU_DOMAINS / 8 - 1; i >= 0; i--) {
		end = (unsigned int)(rg_info->end >> FIRE_EMIMPU_ALIGN) | (i << 24);
		for (apc = 0, j = 0; j < 8; j++)
			apc |= (rg_info->apc[i * 8 + j] & 0x7) << (3 * j);
		if (i == 0 && rg_info->lock)
			apc |= 0x80000000;
		arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_SET,
			      start, end, apc, 0, 0, 0, &res);
	}
	pr_info("fire-emimpu: region %u %#llx-%#llx set (%ld)\n",
		rg_info->rg_num, rg_info->start, rg_info->end, (long)res.a0);
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_set_protection);

int mtk_emimpu_clear_protection(struct emimpu_region_t *rg_info)
{
	struct arm_smccc_res res;

	if (rg_info->rg_num >= FIRE_EMIMPU_REGIONS)
		return -1;
	arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_CLEAR,
		      rg_info->rg_num, 0, 0, 0, 0, 0, &res);
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_clear_protection);

int emimpu_ap_region_init(void)
{
	return 0;
}
EXPORT_SYMBOL(emimpu_ap_region_init);

int mtk_emimpu_tmem_handler_register(emimpu_tmem_handler tmem_handler_func)
{
	return 0;
}
EXPORT_SYMBOL(mtk_emimpu_tmem_handler_register);
