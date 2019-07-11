#include <asm/types.h>
#include <asm/msr.h>
#include <asm/processor.h>

#include <linux/mm.h>
#include <linux/percpu-defs.h>

#include "vmx.h"
#include "x86.h"

struct vmcs {
	u32 revision_id;
	u32 abort;
	u32 data[1];
};

//static int vmxon_cpu = -1;
static DEFINE_PER_CPU(struct vmcs*, vmxon_region);
static struct vmcs *vmcs;

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

static int vmxoff(void)
{
	u8 error;
	asm volatile("vmxoff; setna %0" : "=q"(error));
	if (error) {
		printk(KERN_ERR "vmm: failed to vmxoff\n");
		return error;
	}

	return 0;
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

static int setup_vmcs_guest_field(void)
{
    int r = 0;

	u16 es = read_es();
	u16 cs = read_cs();
	u16 ss = read_ss();
	u16 ds = read_ds();
	u16 fs = read_fs();
	u16 gs = read_gs();
	u16 ldtr = read_ldt();
	u16 tr = read_tr();

	// configuration of guest state areas.
	r |= vmwrite(GUEST_ES_SELECTOR, es);
	r |= vmwrite(GUEST_CS_SELECTOR, cs);
	r |= vmwrite(GUEST_SS_SELECTOR, ss);
	r |= vmwrite(GUEST_DS_SELECTOR, ds);
	r |= vmwrite(GUEST_FS_SELECTOR, fs);
	r |= vmwrite(GUEST_GS_SELECTOR, gs);
	r |= vmwrite(GUEST_LDTR_SELECTOR, ldtr);
	r |= vmwrite(GUEST_TR_SELECTOR, tr);

	printk(KERN_DEBUG "vmm: es 0x%x, cs 0x%x, ss 0x%x\n", es, cs, ss);
	printk(KERN_DEBUG "vmm: ds 0x%x, fs 0x%x, ldtr 0x%x, tr 0x%x\n", ds, fs,
	       ldtr, tr);

	if (r) {
		printk(KERN_ERR
		       "vmm: failed to configurate the segment selectors[%d]\n",
		       r);
		return r;
	}

	r |= vmwrite(GUEST_ES_LIMIT, load_segment_limit(es));
	r |= vmwrite(GUEST_CS_LIMIT, load_segment_limit(cs));
	r |= vmwrite(GUEST_SS_LIMIT, load_segment_limit(ss));
	r |= vmwrite(GUEST_DS_LIMIT, load_segment_limit(ds));
	r |= vmwrite(GUEST_FS_LIMIT, load_segment_limit(fs));
	r |= vmwrite(GUEST_GS_LIMIT, load_segment_limit(gs));
	r |= vmwrite(GUEST_LDTR_LIMIT, load_segment_limit(ldtr));
	r |= vmwrite(GUEST_TR_LIMIT, load_segment_limit(tr));

	if (r) {
		printk(KERN_ERR
		       "vmm: failed to configurate the segment limits. [%d]\n", r);
		return r;
	}

    r |= vmwrite(GUEST_ES_AR_BYTES, load_access_right(es));
    r |= vmwrite(GUEST_CS_AR_BYTES, load_access_right(cs));
    r |= vmwrite(GUEST_SS_AR_BYTES, load_access_right(ss));
    r |= vmwrite(GUEST_DS_AR_BYTES, load_access_right(ds));
    r |= vmwrite(GUEST_FS_AR_BYTES, load_access_right(fs));
    r |= vmwrite(GUEST_GS_AR_BYTES, load_access_right(gs));
    r |= vmwrite(GUEST_LDTR_AR_BYTES, load_access_right(ldtr));
    r |= vmwrite(GUEST_TR_AR_BYTES, load_access_right(tr));

    if(r) {
        printk(KERN_ERR "vmm: failed to configurate the access right[%d]\n", r);
    }

    r |= vmwrite(GUEST_INTERRUPTIBILITY_INFO, 0);
    r |= vmwrite(GUEST_ACTIVITY_STATE, GUEST_ACTIVITY_ACTIVE);
    u64 msr_debug_ctl;
    rdmsrl(MSR_IA32_DEBUGCTLMSR, msr_debug_ctl);
    r |= vmwrite(GUEST_IA32_DEBUGCTL, msr_debug_ctl);

    return r;
}

static int setup_vmcs(u32 msr_off)
{
    int r = 0;

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
		       "vmm: failed to configurate the control field[%d]\n",
		       r);
		return r;
	}

    r = setup_vmcs_guest_field();
    if(r)
    {
        printk(KERN_ERR "vmm: failed to guest filed of vmcs. [%d]", r);
        return r;
    }

    return r;
}

static void vmx_enable(void *junk)
{
	int r;

	u64 msr_vmx_basic;
	u64 pa_vmx;
	u64 cr0, cr4, msr_tmp;

    int cpu = raw_smp_processor_id();

    rdmsrl(MSR_IA32_VMX_BASIC, msr_vmx_basic);
	struct vmcs* vmxon_vmcs = per_cpu(vmxon_region, cpu);

	vmxon_vmcs->revision_id = (u32)msr_vmx_basic;

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

	pa_vmx = __pa(vmxon_vmcs);

	printk("%p %08lx\n", vmxon_vmcs, (uintptr_t)pa_vmx);

	r = vmxon(pa_vmx);
	if (r) {
		printk(KERN_ERR "vmm: failed to vmxon [%d]\n", r);
	}

    /*
	struct vmcs* vmcs = alloc_vmcs_region(cpu);
	vmcs->revision_id = (u32)msr_vmx_basic;
	u64 pa_vmcs = __pa(vmcs);

	r = vmclear(pa_vmcs);
	if (r) {
		printk(KERN_ERR "vmm: vmclear failed [%d]\n", r);
	}

	r = vmptrld(pa_vmcs);
	if (r) {
		printk(KERN_ERR "vmm: vmptrld failed [%d]\n", r);
	}

	u32 msr_off = 0;
	if (msr_vmx_basic & VMX_BASIC_TRUE_CTLS) {
		msr_off = 0xc;
	}
	printk(KERN_DEBUG "vmm: msr_off %d\n", msr_off);
    */

    //r = setup_vmcs(msr_off);
    //if(r)
    //{
    //    printk(KERN_ERR "vmm: failed to setup the vmcs fileds[%d]\n", r);
    //}

	// TODO

}

int vmx_run(void)
{
    on_each_cpu(vmx_enable, NULL, 1);

	return 0;
}

static void vmx_closing(void *junk)
{
	int r;

	int cpu = raw_smp_processor_id();;

    //TODO: fix it.
    //it is not vmxon_region. it should be cleared the vmcs for guest os.
    //struct vmcs* vmcs = per_cpu(vmxon_region, cpu);
    //u64 pa_vmcs = __pa(vmcs);

    //r = vmclear(pa_vmcs);
    //if (r) {
    //    printk(KERN_ERR "vmm: vmclear failed[%d]\n", r);
    //    return;
    //}

    r = vmxoff();
    if (r) {
        printk(KERN_ERR "vmm: vmxoff failed[%d]\n", r);
        return;
    }

    //__free_page(virt_to_page(vmcs));
    __free_page(virt_to_page(per_cpu(vmxon_region, cpu)));

    vmcs = NULL;
    vmxon_region = NULL;

    printk("vmm: vmxoff success [cpu %d]\n", cpu);
}

int vmx_init(void)
{
    int cpu;
    for_each_possible_cpu(cpu) {
        struct vmcs *vmcs;

        vmcs = alloc_vmcs_region(cpu);
        if(!vmcs)
        {
            return -ENOMEM;
        }

        per_cpu(vmxon_region, cpu) = vmcs;
    }

	return 0;
}

int vmx_deinit(void)
{
	on_each_cpu(vmx_closing, NULL, 1);

	return 0;
}
