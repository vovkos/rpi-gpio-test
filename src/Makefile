obj-m += rpi-gpio-test.o

ifndef LINUX_BUILD_DIR
	LINUX_BUILD_DIR := /lib/modules/$(shell uname -r)/build/
endif

default:
	make -C $(LINUX_BUILD_DIR) M=$(PWD) modules

clean:
	make -C $(LINUX_BUILD_DIR) M=$(PWD) clean
