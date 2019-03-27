obj-m += vmm.o
vmm-objs := main.o vmx.o

BUILD_PATH	:= /lib/modules/$(shell uname -r)/build

.PHONY: build
build:
	make -C $(BUILD_PATH) M=$(shell pwd) modules

.PHONY: install
install: build
	sudo insmod vmm.ko

.PHONY: uninstall
uninstall:
	sudo rmmod vmm

.PHONY:
clean:
	make -C $(BUILD_PATH) M=$(shell pwd) clean

