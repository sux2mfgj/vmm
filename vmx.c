#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <asm/desc.h>

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
	//uint32_t data;
	char data[0];
};

struct test_vm {
	uintptr_t rip;
	uintptr_t stack;
	size_t size;
};

static DEFINE_PER_CPU(struct vmcs *, vmxon_region);

//static struct vmcs *vmxon_region = NULL;
static struct vmcs *vmcs_region = NULL;

static struct test_vm vm;

static inline uint64_t vmcs_read(enum vmcs_field_encoding encoding);
static inline int check_vmoperation_result(void);

static int vmxon(uint64_t address)
{
	uint64_t cr4, cr0;
	uint64_t msr, cpuid;
	uint8_t phys_bit_width;
	int r = 0;

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

	r = check_vmoperation_result();

	return r;
}

static int vmxoff(void)
{
    int r;

	asm volatile("vmxoff");

    r = check_vmoperation_result();

	return r;
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

	//page = alloc_page(GFP_KERNEL);
	page = __alloc_pages_node(node, GFP_KERNEL, 0);
	if (!page) {
		return NULL;
	}

	vmcs = page_address(page);
	memset(vmcs, 0, vmcs_size);

	vmcs->header.revision_id = vmx_msr_low;
	return vmcs;
}

static inline int check_vmoperation_result(void)
{
	uint64_t rflags;
	int r = 0;

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
		r = vmcs_read(VM_INSTRUCTIN_ERROR);
		printk("VMfailValid: error code %d\n", r);
		return r;
	}

	return 0;
}

static inline int vmcs_load(struct vmcs *vmcs)
{
	uintptr_t physical_addr = __pa(vmcs);
	int r = 0;

	asm volatile("vmptrld %0" ::"m"(physical_addr));

	r = check_vmoperation_result();

	return r;
}

static inline int vmcs_clear(struct vmcs *vmcs)
{
	uintptr_t physical_addr = __pa(vmcs);
	int r = 0;

	asm volatile("vmclear %0" ::"m"(physical_addr));

	r = check_vmoperation_result();

	return r;
}

static inline uint64_t vmcs_read(enum vmcs_field_encoding encoding)
{
	unsigned long value;
	unsigned long field = encoding;
	int r;

	asm volatile("vmread %1, %0" : "=r"(value) : "r"(field));

	r = check_vmoperation_result();

	return value;
}

static int vmcs_write(enum vmcs_field_encoding encoding, uint64_t value)
{
	unsigned long field = encoding;
	int r;

	asm volatile("vmwrite %1, %0" ::"r"(field), "r"(value));

	r = check_vmoperation_result();
	return r;
}

static int adjust_vmx_control(const uint32_t msr,
			       const enum vmcs_field_encoding dest,
			       uint64_t flags)
{
	uint32_t lower, upper;
    int r;

	rdmsr(msr, lower, upper);
	flags &= upper;
	flags |= lower;

	printk("0x%08x 0x%08x 0x%08llx\n", upper, lower, flags);

	r = vmcs_write(dest, flags);
    return r;
}

static int setup_vmcs(struct vmcs *vmcs)
{
	int r = 0;
    uint32_t cr0;
	uint64_t cr3, cr4, rflags;
	struct desc_ptr dt;
	uint64_t msr;
	struct page *page;
	uintptr_t host_rsp;
	int cpu;

	//preempt_disable();

	cr0 = read_cr0();
	printk("write cr0: %08llx\n", cr0);
	vmcs_write(HOST_CR0, cr0);
    printk("CR0 0x%08x\n", cr0);
    // CR0
    // | 0  | 1  | 2  | 3  | 4  | 5  |6 ~ 15| 16 | 17 | 18 | 19 ~ 28 | 29 | 30 | 31 |
    // | PE | MP | EM | TS | ET | NE | RE   | WP | RE | AM | RE      | NM | CD | PG |
	vmcs_write(GUEST_CR0, cr0);

	cr3 = __read_cr3();
	printk("write cr3: %08llx\n", cr3);
	vmcs_write(HOST_CR3, cr3);
	vmcs_write(GUEST_CR3, cr3);
    printk("CR3 0x%x\n", cr3);

	asm volatile("movq %%cr4, %0" : "=r"(cr4));
	printk("write cr4\n");
	vmcs_write(HOST_CR4, cr4);
	vmcs_write(GUEST_CR4, cr4);
    printk("CR4 0x%x\n", cr4);

	store_idt(&dt);
	printk("write idtr\n");
	vmcs_write(HOST_IDTR_BASE, dt.address);
	vmcs_write(GUEST_IDTR_BASE, dt.address);
	vmcs_write(GUEST_IDTR_LIMIT, dt.size);

	native_store_gdt(&dt);
	printk("write gdtr\n");
	vmcs_write(HOST_GDTR_BASE, dt.address);
	vmcs_write(GUEST_GDTR_BASE, dt.address);
	vmcs_write(GUEST_GDTR_LIMIT, dt.size);

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		return -1;
	}

	//host_rsp = (uintptr_t)page_address(page) + 0x1000 - 1;

	vmcs_write(HOST_RIP, (uintptr_t)vm_exit_guest);
	printk("write host rip\n");

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		return -1;
	}

	//vm.rip = (uintptr_t)test_guest_rip;
	vm.rip = (uintptr_t)NULL;
	vm.stack = (uintptr_t)page_address(page) + 0x1000 - 1;
	vmcs_write(GUEST_RIP, vm.rip);
	vmcs_write(GUEST_RSP, vm.stack);
    //vmcs_write(GUEST_RIP, NULL);
    //vmcs_write(GUEST_RSP, NULL);

	vmcs_write(VMCS_LINK_POINTER_FULL, -1ull);

	rdmsrl(MSR_IA32_SYSENTER_CS, msr);
	vmcs_write(HOST_IA32_SYSENTER_CS, msr);
	vmcs_write(GUEST_IA32_SYSENTER_CS, msr);

	rdmsrl(MSR_IA32_SYSENTER_EIP, msr);
	vmcs_write(HOST_IA32_SYSENTER_EIP, msr);
	vmcs_write(GUEST_IA32_SYSENTER_EIP, msr);
    printk("SYSENTER_EIP 0x%x\n", msr);

	rdmsrl(MSR_IA32_SYSENTER_ESP, msr);
	vmcs_write(HOST_IA32_SYSENTER_ESP, msr);
	vmcs_write(GUEST_IA32_SYSENTER_ESP, msr);
    printk("SYSENTER_ESP 0x%x\n", msr);

	vmcs_write(HOST_CS_SELECTOR, __KERNEL_CS & 0xf8);
	vmcs_write(GUEST_CS_SELECTOR, __KERNEL_CS & 0xf8);
	vmcs_write(HOST_DS_SELECTOR, 0);
	vmcs_write(GUEST_DS_SELECTOR, 0);
	vmcs_write(HOST_ES_SELECTOR, 0);
	vmcs_write(GUEST_ES_SELECTOR, 0);
    vmcs_write(HOST_FS_SELECTOR, 0);
    vmcs_write(GUEST_FS_SELECTOR, 0);
    vmcs_write(HOST_GS_SELECTOR, 0);
    vmcs_write(GUEST_GS_SELECTOR, 0);
	vmcs_write(HOST_SS_SELECTOR, __KERNEL_CS & 0xf8);

    rdmsrl(MSR_FS_BASE, msr);
    vmcs_write(HOST_FS_BASE, msr);
    vmcs_write(GUEST_FS_BASE, msr);
    rdmsrl(MSR_GS_BASE, msr);
    vmcs_write(HOST_GS_BASE, msr);
    vmcs_write(GUEST_GS_BASE, msr);

	cpu = get_cpu();
	vmcs_write(HOST_TR_BASE,
		   (unsigned long)&get_cpu_entry_area(cpu)->tss.x86_tss & 0xf8);

    vmcs_write(HOST_TR_SELECTOR, GDT_ENTRY_TSS * 8);
	vmcs_write(GUEST_TR_SELECTOR, GDT_ENTRY_TSS * 8);

	vmcs_write(GUEST_DR7, 0x400);

	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
	vmcs_write(GUEST_RFLAGS, rflags);

	vmcs_write(TSC_OFFSET_FULL, 0);

	vmcs_write(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write(PAGE_FAULT_ERROR_CODE_MATCH, 0);

	vmcs_write(VM_EXIT_MSR_STORE_COUNT, 0);
	vmcs_write(VM_EXIT_MSR_LOAD_COUNT, 0);

	vmcs_write(VM_ENTRY_MSR_LOAD_COUNT, 0);
	vmcs_write(VM_ENTRY_INTERRUPTION_INFO_FIELD, 0);

	rdmsrl(MSR_IA32_DEBUGCTLMSR, msr);
	vmcs_write(GUEST_IA32_DEBUGCTL_FULL, msr);

	printk("vw GUEST_INTERRUPTIBILITY_STATE\n");
	vmcs_write(GUEST_INTERRUPTIBILITY_STATE, 0);
	printk("vw GUEST_ACTIVITY_STATE\n");
	vmcs_write(GUEST_ACTIVITY_STATE, 0);
	printk("vw VM_ENTRY_CONTROLS\n");

	r = adjust_vmx_control(
            MSR_IA32_VMX_ENTRY_CTLS, VM_ENTRY_CONTROLS,
			   VM_ENTRY_CTRL_IA32e_MODE_GUEST);

	r |= adjust_vmx_control(
            MSR_IA32_VMX_EXIT_CTLS, VM_EXIT_CONTROLS, VM_EXIT_CTRL_HOST_ADDRESS_SPACE_SIZE | VM_EXIT_CTRL_ACK_INTERRUPT_ON_EXIT);

	r |= adjust_vmx_control(
            MSR_IA32_VMX_PROCBASED_CTLS,
			   PRIMARY_PROCESSOR_BASED_VM_EXEC_CTRLS,
			   CPU_BASED_EXEC_ACTIVE_SECONDARY_CTRLS | CPU_BASED_EXEC_HLT_EXIT);

    r |= adjust_vmx_control(MSR_IA32_VMX_PROCBASED_CTLS2, SECONDARY_PROCESSOR_BASED_VM_EXEC_CONTROL, 0);

	r |= adjust_vmx_control(
            MSR_IA32_VMX_PINBASED_CTLS,
			       PIN_BASED_VM_EXEC_CONTROLS, 0);

	//preempt_enable();
	return r;
}

int vmx_setup(void)
{
	int r = 0;
	int cpu;

	for_each_possible_cpu (cpu) {
		struct vmcs *vmcs;

		vmcs = alloc_vmcs_region(cpu);
		if (vmcs == NULL) {
			goto fail;
		}
		per_cpu(vmxon_region, cpu) = vmcs;
	}

	return r;
fail:
	vmx_tear_down();
	return -1;
}

static bool is_vmxon = false;
static int vmxon_cpu = -1;

int vmx_run(void)
{
	int r = 0;
	int cpu;
	uintptr_t vmxon_region_pa, vmxon_region_va;
    uint64_t rflags;

	if (is_vmxon) {
		r = -1;
		goto fail;
	}

	cpu = raw_smp_processor_id();
	vmxon_region_va = (uintptr_t)per_cpu(vmxon_region, cpu);
	if ((struct vmcs *)vmxon_region_va == NULL) {
		printk("why??\n");
		r = -2;
		goto fail;
	}

	vmxon_region_pa = (uintptr_t)__pa(vmxon_region_va);
	if ((struct vmcs *)vmxon_region_pa == NULL) {
		r = -1;
		goto fail;
	}

	r = vmxon((uint64_t)vmxon_region_pa);
	if (r) {
		printk("faild to vmxon\n");
		goto fail;
	}
	is_vmxon = true;
	vmxon_cpu = cpu;
	printk("vmxon_cpu: %d\n", vmxon_cpu);
	printk("success to execute the vmxon\n");

	vmcs_region = alloc_vmcs_region(cpu);
	if (vmcs_region == NULL) {
		printk("failed allocate a vmcs region\n");
		return -1;
	}

	r = vmcs_clear(vmcs_region);
	if (r) {
		printk("failed to execute the vmclear");
		return -1;
	}
	printk("success to execute the vmclear\n");

	r = vmcs_load(vmcs_region);
	if (r) {
		printk("failed to execute the vmptrld");
		return -1;
	}
	printk("success to execute the vmptrload\n");

    preempt_disable();
	r = setup_vmcs(vmcs_region);
	if (r) {
		printk("failed to setup the vmcs region");
		goto fail;
	}

    uint64_t rsp;
    asm volatile (
            "movq %%rsp, %0"
            :"=g"(rsp));
	vmcs_write(HOST_RSP, rsp);
	printk("write host rsp\n");
    printk("rsp: 0x%x\n", rsp);

	printk("vmlaunch\n");
    asm volatile(
            //"vmlaunch\n\t"
            "pushfq\n\t"
            "pop %0\n\t"
            ".globl vm_exit_guest\n\t"
            "vm_exit_guest:\n\t"
            : "=g"(rflags));

    /*
	asm volatile("vmlaunch");

	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
             */

	r = rflags & X86_EFLAGS_CF;
	if (r) {
		printk("VMfailnvalid\n");
		return r;
	}

	r = rflags & X86_EFLAGS_ZF;
	if (r) {
		r = vmcs_read(VM_INSTRUCTIN_ERROR);
		printk("VMfailValid: error code %d\n", r);
		return r;
	}

    // normally, the vmlaunch jump to guest rip as non-root operation.
	//r = check_vmoperation_result();
    printk("is it collect?\n");
    preempt_enable();
    //on_each_cpu(vmx_launch, NULL, 1);
fail:
	return r;
}

static void check_and_vmxoff(void *babble)
{
	int cpu = raw_smp_processor_id();
	if (vmxon_cpu == cpu) {
		if (vmcs_region != NULL) {
			printk("vmclar and free the vmcs_region\n");
			vmcs_clear(vmcs_region);
			__free_page(virt_to_page(vmcs_region));
			vmcs_region = NULL;
		}

		printk("cpu %d: vmxoff\n", cpu);
		vmxoff();
		is_vmxon = false;
	}
}

void vmx_tear_down(void)
{
	int cpu;
	struct vmcs *vmcs;

	on_each_cpu(check_and_vmxoff, NULL, 1);

	for_each_possible_cpu (cpu) {
		printk("cpu: %d\n", cpu);

		vmcs = per_cpu(vmxon_region, cpu);
		if (vmcs != NULL) {
			printk("free the vmxon_region\n");
			__free_page(virt_to_page(vmcs));
			vmcs = NULL;
		}
	}

	printk("bye\n");
}

void vm_exit_handler(uintptr_t guest_regs_ptr)
{
	printk("handling the vm exit\n");
	while (true) {
	}
	return;
}
