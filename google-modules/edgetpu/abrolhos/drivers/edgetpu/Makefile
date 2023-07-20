# SPDX-License-Identifier: GPL-2.0
#
# Makefile for EdgeTPU framework and dependent drivers.
#

obj-$(CONFIG_ABROLHOS)		+= abrolhos.o

ccflags-y       += -DGIT_REPO_TAG=\"Not\ a\ git\ repository\"

edgetpu-objs	:= edgetpu-async.o edgetpu-dmabuf.o edgetpu-iremap-pool.o \
		   edgetpu-mailbox.o edgetpu-mapping.o \
		   edgetpu-sw-watchdog.o edgetpu-telemetry.o \
		   edgetpu-firmware-util.o edgetpu-firmware.o \
		   edgetpu-domain-pool.o

mobile-objs	:= edgetpu-kci.o

abrolhos-objs	:= abrolhos-core.o abrolhos-debug-dump.o \
		   abrolhos-device-group.o abrolhos-device.o \
		   abrolhos-firmware.o abrolhos-fs.o abrolhos-iommu.o \
		   abrolhos-platform.o abrolhos-pm.o abrolhos-thermal.o \
		   abrolhos-usage-stats.o abrolhos-wakelock.o \
		   $(mobile-objs) $(edgetpu-objs)


KBUILD_OPTIONS += CONFIG_ABROLHOS=m

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) $(@)
