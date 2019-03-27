#ifndef __VMM_VMX_H
#define __VMM_VMX_H

struct vm
{

};

int vmx_setup(void);
void vmx_tear_down(void);

long vmm_dev_ioctl_create_vm(unsigned long arg);

#endif
