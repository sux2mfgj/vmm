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

static int vmxon_cpu = -1;
static struct vmcs *vmxon_region;
static struct vmcs *vmcs;

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

static struct vmcs *alloc_vmcs_region(int cpu)
{
	struct vmcs *vmcs;
	struct page *page;
	uint32_t vmx_msr_low, vmx_msr_high;
	size_t vmcs_size;

	int node = cpu_to_node(cpu);

	rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);
	vmcs_size = vmx_msr_high & 0x1ffff;

	page = __alloc_pages_node(node, GFP_KERNEL, 0);
	if (!page) {
		return NULL;
	}

	vmcs = page_address(page);
	memset(vmcs, 0, vmcs_size);

	return vmcs;
}

static int vmxon(u64 address)
{
	u8 error;
	asm volatile("vmxon %1; setna %0"
		     : "=q"(error)
		     : "m"(address)
		     : "memory", "cc");

	return error;
}

static int vmclear(u64 address)
{
	u8 error;
	asm volatile("vmclear %1; setna %0" : "=qm"(error) : "m"(address));

	return error;
}

static int vmptrld(u64 address)
{
	u8 error;
	asm volatile("vmptrld %1; setna %0" : "=qm"(error) : "m"(address));

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

static void adjust_ctrl_value(u32 msr, u32 *val)
{
	u64 tmp;
	rdmsrl(msr, tmp);
	*val &= (u32)(tmp >> 32);
	*val |= (u32)tmp;
}

static int vmwrite(u64 field, u64 value)
{
	u8 error;
	asm volatile("vmwrite %2, %1; setna %0"
		     : "=qm"(error)
		     : "r"(field), "r"(value));
	return error;
}

int vmx_run(void)
{
	u64 msr_vmx_basic;
	u64 pa_vmx, pa_vmcs;
	u64 cr0, cr4, msr_tmp;
	int r;
	int cpu = smp_processor_id();

	rdmsrl(MSR_IA32_VMX_BASIC, msr_vmx_basic);
	vmxon_region = alloc_vmcs_region(cpu);

	vmxon_region->revision_id = (u32)msr_vmx_basic;

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

	pa_vmx = __pa(vmxon_region);

	printk("%p %ld\n", vmxon_region, (uintptr_t)pa_vmx);
	r = vmxon(pa_vmx);
	if (r) {
		printk(KERN_ERR "vmm: failed to vmxon [%d]\n", r);
		return r;
	}
	vmxon_cpu = cpu;

	vmcs = alloc_vmcs_region(cpu);
	vmcs->revision_id = (u32)msr_vmx_basic;
	pa_vmcs = __pa(vmcs);

	r = vmclear(pa_vmcs);
	if (r) {
		printk(KERN_ERR "vmm: vmclear failed [%d]\n", r);
		return r;
	}

	r = vmptrld(pa_vmcs);
	if (r) {
		printk(KERN_ERR "vmm: vmptrld failed [%d]\n", r);
		return r;
	}

	u32 msr_off = 0;
	if (msr_vmx_basic & VMX_BASIC_TRUE_CTLS) {
		msr_off = 0xc;
	}

	printk(KERN_DEBUG "vmm: msr_off %d\n", msr_off);

	u32 vm_entry_ctrl = VM_ENTRY_IA32E_MODE;
	adjust_ctrl_value(MSR_IA32_VMX_ENTRY_CTLS + msr_off, &vm_entry_ctrl);

	u32 vm_exit_ctrl =
		VM_EXIT_ASK_INTR_ON_EXIT | VM_EXIT_HOST_ADDR_SPACE_SIZE;
	adjust_ctrl_value(MSR_IA32_VMX_EXIT_CTLS + msr_off, &vm_exit_ctrl);

	u32 vm_pin_ctrl = 0;

	u32 vm_cpu_ctrl = CPU_BASED_HLT_EXITING;
	adjust_ctrl_value(MSR_IA32_VMX_PROCBASED_CTLS + msr_off, &vm_cpu_ctrl);

	u32 vm_2nd_ctrl = 0;

	r |= vmwrite(VM_ENTRY_CONTROLS, vm_entry_ctrl);
	r |= vmwrite(VM_EXIT_CONTROLS, vm_exit_ctrl);
	r |= vmwrite(PIN_BASED_VM_EXEC_CONTROL, vm_pin_ctrl);
	r |= vmwrite(CPU_BASED_VM_EXEC_CONTROL, vm_cpu_ctrl);
	r |= vmwrite(SECONDARY_VM_EXEC_CONTROL, vm_2nd_ctrl);

	if (r) {
		printk(KERN_ERR
		       "vmm: error occured at VM CONTROL setups [%d]\n",
		       r);
		return r;
	}

	r |= vmwrite(VM_EXIT_MSR_STORE_COUNT, 0);
	r |= vmwrite(VM_EXIT_MSR_STORE_ADDR, 0);
	r |= vmwrite(VM_EXIT_MSR_LOAD_COUNT, 0);
	r |= vmwrite(VM_EXIT_MSR_LOAD_ADDR, 0);
	r |= vmwrite(VM_ENTRY_MSR_LOAD_COUNT, 0);
	r |= vmwrite(VM_ENTRY_INTR_INFO_FIELD, 0);

	if (r) {
		printk(KERN_ERR
		       "vmm: error occured at msr count configurations. [%d]\n",
		       r);
		return r;
	}

	r |= vmwrite(PAGE_FAULT_ERROR_CODE_MASK, 0);
	r |= vmwrite(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	r |= vmwrite(CR3_TARGET_COUNT, 0);
	r |= vmwrite(VMCS_LINK_POINTER, -1ULL);

	if (r) {
		printk(KERN_ERR
		       "vmm: error occured at configuration of control field[%d]\n",
		       r);
        return r;
	}

    // configuration of guest state areas.

	return 0;
}

void vmxoff(void *junk)
{
	int cpu = smp_processor_id();
	if (vmxon_cpu == cpu) {
		u8 error;
		asm volatile("vmxoff; setna %0" : "=q"(error));
		if (error) {
			printk(KERN_ERR "vmm: failed to vmxoff\n");
			return;
		}

		printk("vmm: vmxoff success\n");
	}
}

int vmx_deinit(void)
{
	on_each_cpu(vmxoff, NULL, 1);

	return 0;
}
