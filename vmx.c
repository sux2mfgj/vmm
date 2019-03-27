#include <linux/module.h>
#include <linux/mm.h>

#include "vmx.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunsuke Mie <sux2mfgj@gmail.com>");

struct vmcs_header
{
        uint32_t revision_id: 31;
        uint32_t shadow_vmcs_indicator: 1;
};

struct vmcs
{
        struct vmcs_header header;
        uint32_t vmx_abort_indicator;
        uint32_t data;
};

static struct vmcs* vmxon_region;

static struct vmcs* alloc_vmcs_region(void)
{
        struct vmcs* vmcs;
        struct page* page;
        uint32_t vmx_msr_low, vmx_msr_high;
        size_t vmcs_size;

        rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);
        vmcs_size = vmx_msr_high & 0x1ffff;

        page = alloc_page(GFP_KERNEL);
        if(!page)
        {
                return NULL;
        }

        vmcs = page_address(page);
        memset(vmcs, 0, vmcs_size);

        vmcs->header.revision_id = vmx_msr_low;

        return vmcs;
}

static int vmxon(uint64_t address)
{
        uint32_t cr4;
        uint64_t rflags, is_error;

        // enable virtual machine extension
        asm volatile ("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= X86_CR4_VMXE;
        asm volatile ("mov %0, %%cr4" : : "r"(cr4));

        // vmxon
        asm volatile ("vmxon %0" : : "m"(address));

        // check the result
        asm volatile ("pushfq\n\t"
                      "pop %0" : "=g"(rflags));
        is_error = rflags & X86_EFLAGS_CF;

        return is_error;
}

static int vmxoff(void)
{
        asm volatile ("vmxoff");
        return 0;
}

int vmx_setup(void)
{
        int r = 0;
        uint64_t vmxon_region_pa;

        vmxon_region = alloc_vmcs_region();
        if(!vmxon_region)
        {
                return -ENOMEM;
        }
        vmxon_region_pa = __pa(vmxon_region);

        printk("0x%llx 0x%llx\n", (uint64_t)vmxon_region, vmxon_region_pa);
        r = vmxon(vmxon_region_pa);

        return r;
}

void vmx_tear_down(void)
{
        vmxoff();
        free_page((unsigned long)vmxon_region);
}
