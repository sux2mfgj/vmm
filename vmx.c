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

struct test_vm {
	uintptr_t rip;
	uintptr_t stack;
	size_t size;
};

static struct vmcs *vmxon_region = NULL;
static struct vmcs *vmcs_region = NULL;

static struct test_vm vm;

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

	asm volatile("movq %%cr0, %0" : "=r"(cr0));
	printk("cr0 0x%llx\n", cr0);

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

static inline int vmcs_load(struct vmcs *vmcs)
{
	uintptr_t physical_addr = __pa(vmcs);
	int r = 0;
	uint64_t rflags;

	asm volatile("vmptrld %0" ::"m"(physical_addr));

	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));

	r = rflags & X86_EFLAGS_CF;
	if (r) {
		printk("VMfailnvalid\n");
		return r;
	}

	r = rflags & X86_EFLAGS_ZF;
	if (r) {
		printk("VMfailValid\n");
		// TODO read a error number. a detail of the error number is in Section 30.4 of SDM vol3.
		return r;
	}

	return r;
}

static inline int vmcs_clear(struct vmcs *vmcs)
{
	uintptr_t physical_addr = __pa(vmcs);
	int r = 0;
	uint64_t rflags;

	asm volatile("vmclear %0" ::"m"(physical_addr));

	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));

	r = rflags & X86_EFLAGS_CF;
	if (r) {
		printk("VMfailnvalid\n");
		return r;
	}

	r = rflags & X86_EFLAGS_ZF;
	if (r) {
		printk("VMfailValid\n");
		// TODO read a error number. a detail of the error number is in Section 30.4 of SDM vol3.
		return r;
	}

	return r;
}

static inline uint64_t vmcs_read(enum vmcs_field_encoding encoding)
{
	unsigned long value;
	unsigned long field = encoding;
	asm volatile("vmread %1, %0" : "=r"(value) : "r"(field));

    uint64_t rflags;
    asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
	printk("vr rflags 0x%llx\n", rflags);
	return value;
}

static void vmcs_write(enum vmcs_field_encoding encoding, uint64_t value)
{
	unsigned long field = encoding;
	asm volatile("vmwrite %1, %0" ::"r"(field), "r"(value));

	uint64_t rflags, result;
	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
	printk("rflags 0x%llx\n", rflags);
	result = vmcs_read(VM_INSTRUCTIN_ERROR);
	printk("vm instruction error %d\n", (uint32_t)value);
}

static int adjust_vmx_control(const uint32_t msr,
			      const enum vmcs_field_encoding dest,
			      uint64_t flags)
{
	uint32_t lower, upper;
	rdmsr(msr, lower, upper);
	flags &= upper;
	flags |= lower;
	printk("0x%08x 0x%08x 0x%08x\n", upper, lower, flags);
	vmcs_write(dest, flags);
}

static int setup_vmcs(struct vmcs *vmcs)
{
	int r = 0;
	uint64_t cr0, cr3, cr4;
	struct desc_ptr dt;
	uint64_t msr;
	struct page *page;
	void *gdt;

	/*
	cr0 = read_cr0();
    printk("write cr0\n");
	vmcs_write(HOST_CR0, cr0);

	cr3 = __read_cr3();
    printk("write cr3\n");
	vmcs_write(HOST_CR3, cr0);

	asm volatile("movq %%cr4, %0" : "=r"(cr4));
    printk("write cr4\n");
	vmcs_write(HOST_CR4, cr4);

	store_idt(&dt);
    printk("write idtr\n");
	vmcs_write(HOST_IDTR_BASE, dt.address);

	gdt = get_current_gdt_ro();
	//store_gdt(&dt);
    printk("write gdtr\n");
	vmcs_write(HOST_GDTR_BASE, (uintptr_t)gdt);

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		return -1;
	}
	uintptr_t host_rsp = (uintptr_t)page_address(page) + 0x1000 - 1;

	vmcs_write(HOST_RIP, (uintptr_t)vm_exit_guest);
    printk("write host rip\n");
	vmcs_write(HOST_RSP, host_rsp);
    printk("write host rsp\n");

	rdmsrl(MSR_IA32_SYSENTER_CS, msr);
	vmcs_write(HOST_IA32_SYSENTER_CS, msr);
	vmcs_write(GUEST_IA32_SYSENTER_CS, msr);

	rdmsrl(MSR_IA32_SYSENTER_EIP, msr);
	vmcs_write(HOST_IA32_SYSENTER_EIP, msr);
	vmcs_write(GUEST_IA32_SYSENTER_EIP, msr);

	rdmsrl(MSR_IA32_SYSENTER_ESP, msr);
	vmcs_write(HOST_IA32_SYSENTER_ESP, msr);
	vmcs_write(GUEST_IA32_SYSENTER_ESP, msr);

	rdmsrl(MSR_IA32_SYSENTER_EIP, msr);
	vmcs_write(HOST_IA32_SYSENTER_EIP, msr);
	vmcs_write(GUEST_IA32_SYSENTER_EIP, msr);

	vmcs_write(HOST_CS_SELECTOR, 0);
	vmcs_write(HOST_DS_SELECTOR, __KERNEL_DS);
	vmcs_write(HOST_ES_SELECTOR, __KERNEL_DS);
	vmcs_write(HOST_SS_SELECTOR, __KERNEL_DS);
	vmcs_write(HOST_TR_SELECTOR, GDT_ENTRY_TSS * 8);

	vmcs_write(GUEST_CS_SELECTOR, 0);
	vmcs_write(GUEST_DS_SELECTOR, __KERNEL_DS);
	vmcs_write(GUEST_ES_SELECTOR, __KERNEL_DS);
	vmcs_write(GUEST_SS_SELECTOR, __KERNEL_DS);
	vmcs_write(GUEST_TR_SELECTOR, GDT_ENTRY_TSS * 8);

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		return -1;
	}

	vm.rip = (uintptr_t)test_guest_rip;
	vm.stack = (uintptr_t)page_address(page) + 0x1000;

	vmcs_write(GUEST_RIP, vm.rip);
	vmcs_write(GUEST_RSP, vm.stack);
	vmcs_write(GUEST_CR0, cr0);
	vmcs_write(GUEST_CR3, cr3);
	vmcs_write(GUEST_CR4, cr4);

	store_idt(&dt);
	vmcs_write(GUEST_IDTR_BASE, dt.address);

	vmcs_write(GUEST_IDTR_BASE, (uintptr_t)gdt);
    */

	/*
	//uint32_t msr32, msr32_1;
	//rdmsrl(MSR_IA32_VMX_TRUE_ENTRY_CTLS, msr32_1);
	//printk("TRUE 0x%x\n", msr32_1);
	uint32_t upper, lower, value = 0;;

	rdmsr(MSR_IA32_VMX_ENTRY_CTLS, lower, upper);
	//upper = msr >> 32; lower = msr;
	printk("0x%08x 0x%08x\n", lower & upper);
	vmcs_write(VM_ENTRY_CONTROLS,
		   (VM_ENTRY_CTRL_IA32e_MODE_GUEST | lower) & upper);

	rdmsr(MSR_IA32_VMX_EXIT_CTLS, lower, upper);
	//upper = msr >> 32; lower = msr;
	printk("0x%08x 0x%08x\n", lower & upper);
	vmcs_write(VM_EXIT_CONTROLS,
		   (VM_EXIT_CTRL_HOST_ADDRESS_SPACE_SIZE |
		    VM_EXIT_CTRL_ACK_INTERRUPT_ON_EXIT | lower) &
			   upper);

	rdmsr(MSR_IA32_VMX_PROCBASED_CTLS, lower, upper);
	//upper = msr >> 32; lower = msr;
	printk("0x%08x 0x%08x\n", lower & upper);
	vmcs_write(PRIMARY_PROCESSOR_BASED_VM_EXEC_CTRLS,
		   (CPU_BASED_EXEC_HLT_EXIT |
		    CPU_BASED_EXEC_ACTIVE_SECONDARY_CTRLS | lower) &
			   upper);

	rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2, lower, upper);
	//upper = msr >> 32; lower = msr;
	printk("0x%08x 0x%08x\n", lower & upper);
	vmcs_write(SECONDARY_PROCESSOR_BASED_VM_EXEC_CONTROL,
		   (SECOND_EXEC_ENABLE_RDTSCP | lower) & upper);

	rdmsr(MSR_IA32_VMX_PINBASED_CTLS, lower, upper);
	//upper = msr >> 32; lower = msr;
	printk("0x%08x 0x%08x\n", upper, lower);
	vmcs_write(PIN_BASED_VM_EXEC_CONTROLS, lower & upper);
    */

	printk("vw GUEST_INTERRUPTIBILITY_STATE\n");
	vmcs_write(GUEST_INTERRUPTIBILITY_STATE, 0);
	printk("vw GUEST_ACTIVITY_STATE\n");
	vmcs_write(GUEST_ACTIVITY_STATE, 0);
	printk("vw VM_ENTRY_CONTROLS\n");
	adjust_vmx_control(MSR_IA32_VMX_ENTRY_CTLS, VM_ENTRY_CONTROLS,
			   0); //VM_ENTRY_CTRL_IA32e_MODE_GUEST);
    printk("vw VM_EXIT_CONTROLS\n");
	adjust_vmx_control(MSR_IA32_VMX_EXIT_CTLS, VM_EXIT_CONTROLS, 0);
    printk("vw PRIMARY_PROCESSOR_BASED_VM_EXEC_CTRLS\n");
	adjust_vmx_control(MSR_IA32_VMX_PROCBASED_CTLS,
			   PRIMARY_PROCESSOR_BASED_VM_EXEC_CTRLS,
			   CPU_BASED_EXEC_HLT_EXIT);
    printk("vw PIN_BASED\n");
	adjust_vmx_control(MSR_IA32_VMX_PINBASED_CTLS,
			   PIN_BASED_VM_EXEC_CONTROLS, 0);

	return r;
}

int vmx_setup(void)
{
	int r = 0;
	uint64_t rflags;
	uint64_t value;

	if (vmxon_region != NULL) {
		printk("why??\n");
		return -1;
	}
	vmxon_region = alloc_vmcs_region();
	if (vmxon_region == NULL) {
		return -1;
	}

	r = vmxon(__pa(vmxon_region));
	if (r) {
		printk("faild to vmxon\n");
		return -1;
	}

	vmcs_region = alloc_vmcs_region();
	if (vmcs_region == NULL) {
		printk("failed allocate a vmcs region\n");
		return -1;
	}

	r = vmcs_clear(vmcs_region);
	if (r) {
		printk("failed to execute the vmclear");
		return -1;
	}

	r = vmcs_load(vmcs_region);
	if (r) {
		printk("failed to execute the vmptrld");
		return -1;
	}

	r = setup_vmcs(vmcs_region);
	if (r) {
		printk("failed to setup the vmcs region");
		return -1;
	}

	printk("vmlaunch\n");
	asm volatile("vmlaunch");

	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
	printk("rflags 0x%llx\n", rflags);
	value = vmcs_read(VM_INSTRUCTIN_ERROR);
	printk("vm instruction error %d\n", (uint32_t)value);
	printk("failed to execute the vmlaunch instruction\n");

	return r;
}

void vmx_tear_down(void)
{
	if (vmxon_region != NULL) {
		vmxoff();
		__free_page(virt_to_page(vmxon_region));
		printk("vmxoff and free the vmxon_region\n");
	}
	if (vmcs_region != NULL) {
		//vmcs_clear(vmcs_region);
		__free_page(virt_to_page(vmcs_region));
		printk("vmclar and free the vmcs_region\n");
	}
	printk("bye\n");
}

void vm_exit_handler(uintptr_t guest_regs_ptr)
{
	panic("handling the vm exit\n");
	return;
}
