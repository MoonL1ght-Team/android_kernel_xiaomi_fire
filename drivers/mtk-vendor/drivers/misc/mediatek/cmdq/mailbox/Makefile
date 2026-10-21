# SPDX-License-Identifier: GPL-2.0
ifneq ($(KERNELRELEASE),)

ifneq ($(DEVICE_MODULES_PATH),)
LINUXINCLUDE := -I$(DEVICE_MODULES_PATH)/include $(LINUXINCLUDE)
endif

#
# Copyright (c) 2021 MediaTek Inc.
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mach/$(MTK_PLATFORM)/include \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mdp/$(MTK_PLATFORM) \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmp/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/smi/ \
             -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3 \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cmdq/virtio

ifeq ($(CONFIG_VHOST_CMDQ),m)
ccflags-y += -I$(KERNEL_SRC)/include/
endif

ifneq ($(CONFIG_VIRTIO_CMDQ),m)
ccflags-$(CONFIG_DEBUG_FS) += -DCONFIG_CMDQ_MMPROFILE_SUPPORT
endif

ifneq ($(COMMON_OUT_DIR),)
DEVICE_MODULES_SYMVERS := $(COMMON_OUT_DIR)/$(notdir $(DEVICE_MODULES_PATH))
KBUILD_EXTRA_SYMBOLS += $(wildcard $(DEVICE_MODULES_SYMVERS)/Module.symvers)
endif

ifneq ($(filter y m,$(CONFIG_MICROTRUST_TEE_SUPPORT)),)
include $(DEVICE_MODULES_PATH)/drivers/tee/teei/Makefile.include
endif

ifneq ($(filter y m,$(CONFIG_TRUSTONIC_TEE_SUPPORT)),)
include $(DEVICE_MODULES_PATH)/drivers/tee/gud/Makefile.include
endif

ifneq ($(filter y m,$(CONFIG_MTK_GZ_TZ_SYSTEM)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/geniezone/public
ccflags-y += -DCMDQ_SECURE_MTEE_SUPPORT

ccflags-y += -DCMDQ_SECURE_SUPPORT
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += cmdq-sec-drv.o
cmdq-sec-drv-y := cmdq_sec_mtee.o cmdq_sec_pkvm.o cmdq-sec-mailbox.o cmdq-sec-helper.o

ccflags-y += -DCMDQ_SECURE_CPR_SUPPORT
endif

ccflags-y += -DCMDQ_DCACHE_INVAL

obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += mtk-cmdq-drv-ext.o
mtk-cmdq-drv-ext-y := mtk-cmdq-mailbox-ext.o mtk-cmdq-helper-ext.o cmdq-util.o

obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += cmdq-test.o

cmdq-platform := $(strip $(if $(MTK_PLATFORM),$(MTK_PLATFORM),$(subst ",,$(CONFIG_MTK_LEGACY_PLATFORM))))
cmdq-platform-candidates := mt6833 mt6873 mt6853 mt6885 mt6893 mt6983 mt6879 mt6895 \
                            mt6855 mt6985 mt6886 mt6897 mt6989 mt6878 mt6768 mt6761 \
                            mt6991 mt6877 mt6765 mt6781 mt6899

ifeq ($(CONFIG_MTK_CMDQ_MBOX_EXT), y)
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT_MT6768) += cmdq-platform-mt6768.o
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT_MT6761) += cmdq-platform-mt6761.o
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT_MT6765) += cmdq-platform-mt6765.o
else
ifneq ($(cmdq-platform),)
ifneq ($(wildcard $(srctree)/$(src)/cmdq-platform-$(cmdq-platform).c),)
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += cmdq-platform-$(cmdq-platform).o
endif
else
cmdq-platform-existing := $(foreach p,$(cmdq-platform-candidates),$(if $(wildcard $(srctree)/$(src)/cmdq-platform-$(p).c),cmdq-platform-$(p).o))
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += $(cmdq-platform-existing)
endif
endif

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
