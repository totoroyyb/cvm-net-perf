# obj-m += khires.o
# CFLAGS_khires.o += -DSUPPRESS_CUSTOMIZED_IPI_HANDLE

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
# BUILD_DIR ?= $(PWD)/build
# BUILD_DIR_MAKEFILE ?= $(PWD)/build/Makefile

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f Module.symvers modules.order *.o *.ko *.mod.c *.mod.o *.d

install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

load: default
	sudo insmod ./khires.ko

unload:
	sudo rmmod khires || true # Ignore error if not loaded

rebuild: clean default

reload: unload load

# Target to view kernel messages
logs:
	dmesg | tail -n 20

.PHONY: default clean install load unload rebuild reload logs