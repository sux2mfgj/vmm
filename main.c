#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kvm.h>

//#include "vmx.h"

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

static int vmm_init(void)
{
    if(vmxon_region != NULL)
    {
        vmxon_region = alloc_vmcs_region();
    }

    return 0;
}

static void vmm_exit(void)
{
    if(vmxon_region != NULL)
    {
        __free_page(virt_to_page(vmxon_region));
    }
}

module_init(vmm_init);
module_exit(vmm_exit);
