#pragma once

#include <linux/kvm.h>

int vm_enter_guest(struct kvm_regs* regs, int is_launch);
void vm_exit_guest(struct kvm_regs* regs);
