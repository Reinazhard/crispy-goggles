# SPDX-License-Identifier: GPL-2.0

# Set KERNEL_SRC to google-modules so that the include paths are set correctly
# for in-tree compilation, since the modules assume that `google-modules` exists
# in the same directory as the kernel tree (one level up), whereas we've got
# `google-modules` inside the kernel tree. This way, when the modules include
# with `-I$(KERNEL_SRC)/../google-modules/SOME_MODULE`, inclusion works as-is.
export KERNEL_SRC := $(srctree)/google-modules

obj-y += amplifiers/audiometrics/
obj-y += amplifiers/cs35l41/
obj-y += amplifiers/cs40l25/
obj-y += amplifiers/drv2624/
obj-y += aoc/
obj-y += aoc/alsa/
obj-y += aoc/usb/
obj-y += bluetooth/broadcom/
obj-y += bms/
obj-y += display/samsung/
obj-y += edgetpu/abrolhos/drivers/edgetpu/
obj-y += gpu/mali_kbase/
obj-y += gpu/mali_pixel/
obj-y += lwis/
obj-y += nfc/
obj-y += power/reset/
obj-y += touch/common/
obj-y += touch/fts/ftm5/
obj-y += uwb/qorvo/dw3000/kernel/
obj-y += video/gchips/
obj-y += wlan/bcmdhd4389/
