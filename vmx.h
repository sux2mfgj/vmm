#ifndef __VMM_VMX_H
#define __VMM_VMX_H

#include <linux/kvm.h>

struct vm
{
        struct kvm_regs regs;
        struct kvm_sregs sregs;
};

int vmx_setup(void);
void vmx_tear_down(void);

long vmm_dev_ioctl_create_vm(unsigned long arg);

#endif
