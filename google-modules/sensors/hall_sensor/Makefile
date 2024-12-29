obj-$(CONFIG_SENSORS_HALL)		+= hall_sensor.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

KBUILD_OPTIONS += CONFIG_SENSORS_HALL=m

all:
        $(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) modules

modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) $(@)
