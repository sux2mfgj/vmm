obj-m += vmm.o
vmm-objs := main.o vmx.o vmx_asm.o
VMM_CFLAGS += -g3 -DDEBUG
ccflags-y += ${VMM_CFLAGS}
CC += ${VMM_CFLAGS}

BUILD_PATH	:= /lib/modules/$(shell uname -r)/build

.PHONY: build
build:
	make -C $(BUILD_PATH) M=$(shell pwd) modules

.PHONY: install
install: build
	./check_and_uninstall.sh
	sudo insmod vmm.ko

.PHONY: uninstall
uninstall:
	sudo rmmod vmm

.PHONY:
clean:
	make -C $(BUILD_PATH) M=$(shell pwd) clean

