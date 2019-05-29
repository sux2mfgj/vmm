#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "vmx.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunsuke Mie <sux2mfgj@gmail.com>");

struct vmcs_header {
	uint32_t revision_id : 31;
	uint32_t shadow_vmcs_indicator : 1;
};

struct vmcs {
	struct vmcs_header header;
	uint32_t vmx_abort_indicator;
	uint32_t data;
};

static struct vmcs *vmxon_region = NULL;

static int vmxon(uint64_t address)
{
	uint64_t cr4, cr0;
	uint64_t rflags, is_error;
    uint64_t msr, cpuid;
    uint8_t phys_bit_width;

    cpuid = get_cpuid_info();
    printk("cpuid 0x%llx\n", cpuid);

    phys_bit_width = (uint8_t)cpuid;
    printk("phys bit-width %d\n", phys_bit_width);

    rdmsrl(MSR_IA32_VMX_CR4_FIXED0, msr);
    printk("msr: vmx_cr4_fixed0 0x%llx\n", msr);

    rdmsrl(MSR_IA32_VMX_CR4_FIXED1, msr);
    printk("msr: vmx_cr4_fixed1 0x%llx\n", msr);

	// enable virtual machine extension
	asm volatile("movq %%cr4, %0" : "=r"(cr4));
	cr4 |= X86_CR4_VMXE;

    //cr4 |= (uint32_t)msr;

	asm volatile("movq %0, %%cr4" : : "r"(cr4));
    printk("cr4 0x%llx\n", cr4);

    // update MSR IA32_FEATURE_CONTROL
    rdmsrl(MSR_IA32_FEATURE_CONTROL, msr);
    printk("msr: feature_control 0x%llx\n", msr);
    //wrmsrl(MSR_IA32_FEATURE_CONTROL, msr);

    rdmsrl(MSR_IA32_FEATURE_CONTROL, msr);
    printk("msr: feature_control 0x%llx\n", msr);

    rdmsrl(MSR_IA32_VMX_CR0_FIXED0, msr);
    printk("msr: vmx_cr0_fixed0 0x%llx\n", msr);

    rdmsrl(MSR_IA32_VMX_CR0_FIXED1, msr);
    printk("msr: vmx_cr0_fixed1 0x%llx\n", msr);

    asm volatile("movq %%cr0, %0": "=r"(cr0));
    printk("cr0 0x%llx\n", cr0);

	// check the result
	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
    printk("rflags 0x%llx\n", rflags);

    printk("vmxon 0x%llx", address);
	// vmxon
	asm volatile("vmxon %0" : : "m"(address));

	// check the result
	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
    printk("rflags 0x%llx\n", rflags);
	is_error = rflags & X86_EFLAGS_CF;

	return is_error;
}

static int vmxoff(void)
{
	asm volatile("vmxoff");
	// TODO check a result of the vmxoff
	// CF == 0 and ZF == 0
	return 0;
}

static struct vmcs *alloc_vmcs_region(void)
{
	struct vmcs *vmcs;
	struct page *page;
	uint32_t vmx_msr_low, vmx_msr_high;
	size_t vmcs_size;

	rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);
	vmcs_size = vmx_msr_high & 0x1ffff;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		return NULL;
	}

	vmcs = page_address(page);
	memset(vmcs, 0, vmcs_size);

	vmcs->header.revision_id = vmx_msr_low;
	return vmcs;
}

int vmx_setup(void)
{

    int r = 0;
    if(vmxon_region != NULL)
    {
        printk("why??\n");
        return -1;
    }
    vmxon_region = alloc_vmcs_region();
    if(vmxon_region == NULL)
    {
        return -1;
    }

    r = vmxon(__pa(vmxon_region));
    if(r)
    {
        printk("faild to vmxon\n");
        return -1;
    }

    return r;
}

void vmx_tear_down(void)
{

    if(vmxon_region != NULL)
    {
        vmxoff();
        __free_page(virt_to_page(vmxon_region));
        printk("free and vmxoff\n");
    }
    printk("bye\n");
}
