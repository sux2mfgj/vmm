#include <asm/types.h>
#include <asm/msr.h>
#include <asm/processor.h>

#include <linux/mm.h>

#include "vmx.h"

struct vmcs {
	u32 revision_id;
	u32 abort;
	u32 data[1];
};

static struct vmcs vmxon_region;
//static struct vmcs vmcs;

static inline int check_vmoperation_result(void)
{
	uint64_t rflags;
	int r = 0;

	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));

	r = rflags & X86_EFLAGS_CF;
    printk("vmm: rflags %llx\n", rflags);
	if (r) {
		printk(KERN_DEBUG "vmm: VMfailnvalid\n");
		return r;
	}

	r = rflags & X86_EFLAGS_ZF;
	if (r) {
		//r = vmcs_read(VM_INSTRUCTIN_ERROR);
		printk(KERN_DEBUG "vmm: VMfailValid: error code %d\n", r);
		return r;
	}

	return 0;
}

static int vmxon(u64 address)
{
    u8 error;
	asm volatile("vmxon %1; setna %0" : "=q"(error): "m"(address) : "memory", "cc");

    return error;
}

static u64 read_cr4(void)
{
    u64 cr4;
	asm volatile("movq %%cr4, %0" : "=r"(cr4));

    return cr4;
}

static void write_cr4(u64 cr4)
{
	asm volatile("movq %0, %%cr4" : : "r"(cr4));
}

int vmx_run(void)
{
	u64 msr_vmx_basic;
    u64 pa_vmx;
    u64 cr0, cr4, msr_tmp;
    int r;

	rdmsrl(MSR_IA32_VMX_BASIC, msr_vmx_basic);

	vmxon_region.revision_id = (u32)msr_vmx_basic;

    cr0 = read_cr0();
    rdmsrl(MSR_IA32_VMX_CR0_FIXED1, msr_tmp);
    cr0 &= msr_tmp;
    rdmsrl(MSR_IA32_VMX_CR0_FIXED0, msr_tmp);
    cr0 |= msr_tmp;
    write_cr0(cr0);

    cr4 = read_cr4();
    rdmsrl(MSR_IA32_VMX_CR4_FIXED1, msr_tmp);
    cr4 &= msr_tmp;
    rdmsrl(MSR_IA32_VMX_CR4_FIXED0, msr_tmp);
    cr4 |= msr_tmp;
    write_cr4(cr4);

	pa_vmx = __pa(&vmxon_region);

    printk("%p %ld\n", &vmxon_region, (uintptr_t)pa_vmx);
    r = vmxon(pa_vmx);
    if(r)
    {
        printk(KERN_ERR "vmm: failed to vmxon [%d]\n", r);
        return r;
    }

	return 0;
}
