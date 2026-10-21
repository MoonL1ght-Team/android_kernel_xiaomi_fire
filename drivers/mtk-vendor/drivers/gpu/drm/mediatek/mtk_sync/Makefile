# SPDX-License-Identifier: GPL-2.0
ifneq ($(KERNELRELEASE),)

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps
ccflags-y += -I$(srctree)/drivers/gpu/drm

obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_sync.o

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
