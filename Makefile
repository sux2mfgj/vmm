obj-m += vmm.o
vmm-objs := asm.o main.o vmx.o #vmx_asm.o

# the debug option causes a kernel panic. more details in issue #9.
ccflags-y += -g -O0

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

debug: debug.c vmm_debug.h
	$(CC) $< -o $@

.PHONY:
clean:
	make -C $(BUILD_PATH) M=$(shell pwd) clean
	rm -rf debug

