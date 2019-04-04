#ifndef __VMM_VMX_H
#define __VMM_VMX_H

#include <linux/kvm.h>
#include <linux/mm.h>

#include "vmx.h"
#include "config.h"

struct vcpu {
	unsigned int vpid;
	unsigned int id;
        struct kvm_run* run;
};

struct vm {
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	//        struct mm_struct *mm;
	struct vcpu *vcpus[VCPU_MAX];
};

int vmx_setup(void);
void vmx_tear_down(void);

long vmm_dev_ioctl_create_vm(unsigned long arg);

#endif
