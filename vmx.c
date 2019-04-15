#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>

#include "vmx.h"
#include "config.h"

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

static struct vmcs *vmxon_region;
static struct vmcs *vmcs;

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

static int vmxon(uint64_t address)
{
	uint32_t cr4;
	uint64_t rflags, is_error;

	// enable virtual machine extension
	asm volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= X86_CR4_VMXE;
	asm volatile("mov %0, %%cr4" : : "r"(cr4));

	// vmxon
	asm volatile("vmxon %0" : : "m"(address));

	// check the result
	asm volatile("pushfq\n\t"
		     "pop %0"
		     : "=g"(rflags));
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

static inline void vmcs_load(struct vmcs *vmcs)
{
	uintptr_t physical_addr = __pa(vmcs);
	asm volatile("vmptrld %0" ::"m"(physical_addr));
}

static inline void vmcs_clear(struct vmcs *vmcs)
{
	uintptr_t physical_addr = __pa(vmcs);
	asm volatile("vmclear %0" ::"m"(physical_addr));
}

static inline unsigned long vmcs_read(enum vmcs_field_encoding encoding)
{
	unsigned long value;
	unsigned long field = encoding;
	asm volatile("vmread %1, %0" : "=r"(value) : "r"(field));
	return value;
}

static void vmcs_write(enum vmcs_field_encoding encoding, unsigned long value)
{
	unsigned long field = encoding;
	asm volatile("vmwrite %0, %1" ::"r"(field), "r"(value));
}

int vmx_setup(void)
{
	int r = 0;
	uint64_t vmxon_region_pa;

	//TODO check the CPUID, VMX capability, and so on.

	vmxon_region = alloc_vmcs_region();
	if (!vmxon_region) {
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

static void vmx_get_segment(struct kvm_segment *segment,
			    enum vmcs_field_encoding base,
			    enum vmcs_field_encoding limit,
			    enum vmcs_field_encoding selector,
			    enum vmcs_field_encoding access_right)
{
	u32 access_right_tmp;
	segment->base = vmcs_read(base);
	segment->limit = vmcs_read(limit);
	segment->selector = vmcs_read(selector);

	access_right_tmp = (u32)vmcs_read(access_right);

	segment->unusable = (access_right_tmp >> 16) & 1;
	segment->type = access_right_tmp & 15;
	segment->s = (access_right_tmp >> 4) & 1;
	segment->dpl = (access_right_tmp >> 5) & 3;
	segment->present = !segment->unusable;
	segment->avl = (access_right_tmp >> 12) & 1;
	segment->l = (access_right_tmp >> 13) & 1;
	segment->db = (access_right_tmp >> 14) & 1;
	segment->g = (access_right_tmp >> 15) & 1;
}

static void vmx_set_segment(struct kvm_segment *segment,
			    enum vmcs_field_encoding base,
			    enum vmcs_field_encoding limit,
			    enum vmcs_field_encoding selector,
			    enum vmcs_field_encoding access_right)
{
	u32 access_right_tmp = 0;

	vmcs_write(base, segment->base);
	vmcs_write(limit, segment->limit);
	vmcs_write(selector, segment->selector);
	if (segment->unusable || !segment->present) {
		access_right_tmp = 1 << 16;

	} else {
		access_right_tmp = segment->type & 15;
		access_right_tmp |= (segment->s & 1) << 4;
		access_right_tmp |= (segment->dpl & 3) << 5;
		access_right_tmp |= (segment->present & 1) << 7;
		access_right_tmp |= (segment->avl & 1) << 12;
		access_right_tmp |= (segment->l & 1) << 13;
		access_right_tmp |= (segment->db & 1) << 14;
		access_right_tmp |= (segment->g & 1) << 15;
	}
	vmcs_write(access_right, access_right_tmp);
}

static void vmx_get_desc_table(struct kvm_dtable *dtable,
			       enum vmcs_field_encoding base,
			       enum vmcs_field_encoding limit)
{
	dtable->base = vmcs_read(base);
	dtable->limit = vmcs_read(limit);
}

static void vmx_set_desc_table(struct kvm_dtable *dtable,
			       enum vmcs_field_encoding base,
			       enum vmcs_field_encoding limit)
{
	vmcs_write(base, dtable->base);
	vmcs_write(limit, dtable->limit);
}

static int vcpu_get_kvm_sregs(struct vcpu *vcpu, struct kvm_sregs *sregs)
{
	int r = -EFAULT;

	vmx_get_segment(&sregs->cs, GUEST_CS_BASE, GUEST_CS_LIMIT,
			GUEST_CS_SELECTOR, GUEST_CS_ACCESS_RIGHTS);
	vmx_get_segment(&sregs->ds, GUEST_DS_BASE, GUEST_DS_LIMIT,
			GUEST_DS_SELECTOR, GUEST_DS_ACCESS_RIGHTS);
	vmx_get_segment(&sregs->es, GUEST_ES_BASE, GUEST_ES_LIMIT,
			GUEST_ES_SELECTOR, GUEST_ES_ACCESS_RIGHTS);
	vmx_get_segment(&sregs->fs, GUEST_FS_BASE, GUEST_FS_LIMIT,
			GUEST_FS_SELECTOR, GUEST_FS_ACCESS_RIGHTS);
	vmx_get_segment(&sregs->gs, GUEST_GS_BASE, GUEST_GS_LIMIT,
			GUEST_GS_SELECTOR, GUEST_GS_ACCESS_RIGHTS);
	vmx_get_segment(&sregs->ss, GUEST_SS_BASE, GUEST_SS_LIMIT,
			GUEST_SS_SELECTOR, GUEST_SS_ACCESS_RIGHTS);

	vmx_get_segment(&sregs->tr, GUEST_TR_BASE, GUEST_TR_LIMIT,
			GUEST_TR_SELECTOR, GUEST_TR_ACCESS_RIGHTS);
	vmx_get_segment(&sregs->ldt, GUEST_LDTR_BASE, GUEST_LDTR_LIMIT,
			GUEST_LDTR_SELECTOR, GUEST_LDTR_ACCESS_RIGHTS);

	vmx_get_desc_table(&sregs->idt, GUEST_IDTR_BASE, GUEST_IDTR_LIMIT);
	vmx_get_desc_table(&sregs->gdt, GUEST_GDTR_BASE, GUEST_GDTR_LIMIT);

	sregs->cr0 = vmcs_read(GUEST_CR0);
	sregs->cr2 = vcpu->sregs.cr2;
	sregs->cr3 = vmcs_read(GUEST_CR3);
	sregs->cr4 = vmcs_read(GUEST_CR4);
	sregs->cr8 = vcpu->sregs.cr8;

	//TODO
	//sregs->efer = vcpu->arch.efer;
	//sregs->apic_base = vcpu->arch.apic_base;

	memset(sregs->interrupt_bitmap, 0, sizeof(sregs->interrupt_bitmap));

	//TODO: do i need to setup the interrupt bitmap?
	// like,
	// set_bit(vcpu->arch.interrupt.nr,
	//              (unsigned long *)sregs->interrupt_bitmap);

	r = 0;
	return r;
}

static int vcpu_set_kvm_sregs(struct vcpu *vcpu, struct kvm_sregs *sregs)
{
	memcpy(&vcpu->sregs, sregs, sizeof(struct kvm_sregs));
	return 0;
}

static int vcpu_set_kvm_regs(struct vcpu *vcpu, struct kvm_regs *regs)
{
	memcpy(&vcpu->regs, regs, sizeof(struct kvm_regs));
	return 0;
}

static int update_vmcs_guest_state_area(struct vcpu *vcpu)
{
	uint32_t msr_low, msr_high;

	vmcs_write(GUEST_CR0, vcpu->sregs.cr0);
	vmcs_write(GUEST_CR3, vcpu->sregs.cr3);
	vmcs_write(GUEST_CR4, vcpu->sregs.cr4);

	vmcs_write(GUEST_DR7, vcpu->debug_regs.dr7);
	vmcs_write(GUEST_RIP, vcpu->regs.rip);
	vmcs_write(GUEST_RSP, vcpu->regs.rsp);
	vmcs_write(GUEST_RFLAGS, vcpu->regs.rflags);

	vmx_set_segment(&vcpu->sregs.cs, GUEST_CS_BASE, GUEST_CS_LIMIT,
			GUEST_CS_SELECTOR, GUEST_CS_ACCESS_RIGHTS);
	vmx_set_segment(&vcpu->sregs.ss, GUEST_SS_BASE, GUEST_SS_LIMIT,
			GUEST_SS_SELECTOR, GUEST_SS_ACCESS_RIGHTS);
	vmx_set_segment(&vcpu->sregs.ds, GUEST_DS_BASE, GUEST_DS_LIMIT,
			GUEST_DS_SELECTOR, GUEST_DS_ACCESS_RIGHTS);
	vmx_set_segment(&vcpu->sregs.es, GUEST_ES_BASE, GUEST_ES_LIMIT,
			GUEST_ES_SELECTOR, GUEST_ES_ACCESS_RIGHTS);
	vmx_set_segment(&vcpu->sregs.fs, GUEST_FS_BASE, GUEST_FS_LIMIT,
			GUEST_FS_SELECTOR, GUEST_FS_ACCESS_RIGHTS);
	vmx_set_segment(&vcpu->sregs.gs, GUEST_GS_BASE, GUEST_GS_LIMIT,
			GUEST_GS_SELECTOR, GUEST_GS_ACCESS_RIGHTS);

	vmx_set_segment(&vcpu->sregs.tr, GUEST_TR_BASE, GUEST_TR_LIMIT,
			GUEST_TR_SELECTOR, GUEST_TR_ACCESS_RIGHTS);
	vmx_set_segment(&vcpu->sregs.ldt, GUEST_LDTR_BASE, GUEST_LDTR_LIMIT,
			GUEST_LDTR_SELECTOR, GUEST_LDTR_ACCESS_RIGHTS);

	vmx_set_desc_table(&vcpu->sregs.idt, GUEST_IDTR_BASE, GUEST_IDTR_LIMIT);
	vmx_set_desc_table(&vcpu->sregs.gdt, GUEST_GDTR_BASE, GUEST_GDTR_LIMIT);

	//TODO is it correct?

	rdmsr(MSR_IA32_DEBUGCTLMSR, msr_low, msr_high);
	vmcs_write(GUEST_IA32_DEBUGCTL_FULL,
		   (uint64_t)msr_high << 32 | msr_low);

	vmcs_write(TSC_OFFSET_FULL, 0);
	vmcs_write(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write(PAGE_FAULT_ERROR_CODE_MATCH, 0);

	vmcs_write(VM_EXIT_MSR_STORE_COUNT, 0);
	vmcs_write(VM_EXIT_MSR_LOAD_COUNT, 0);

	vmcs_write(VM_ENTRY_MSR_LOAD_COUNT, 0);
	vmcs_write(VM_ENTRY_INTERRUPTION_INFO_FIELD, 0);

	vmcs_write(GUEST_INTERRUPTIBILITY_STATE, 0);
	vmcs_write(GUEST_ACTIVITY_STATE, 0);

	//     vmcs_write(PRIMARY_PROCESSOR_BASED_VM_EXEC_CTRLS, );
	//     vmcs_write(PIN_BASED_VM_EXEC_CONTROLS, );
	//     vmcs_write(SECONDARY_PROCESSOR_BASED_VM_EXEC_CONTROL, );

	//     vmcs_write(VM_EXIT_CONTROLS, );
	//     vmcs_write(VM_ENTRY_CONTROLS, );

	vmcs_write(CR3_TARGET_COUNT, 0);
	vmcs_write(CR3_TARGET_VALUE_0, 0);
	vmcs_write(CR3_TARGET_VALUE_1, 0);
	vmcs_write(CR3_TARGET_VALUE_2, 0);
	vmcs_write(CR3_TARGET_VALUE_3, 0);

	//     vmcs_write(MSR_BITMAPS_FULL, );

	//TODO setup MSRs

	return -EINVAL;
}

static int update_vmcs_host_state_area(struct vcpu *vcpu)
{
	struct desc_ptr dt;
	uint32_t msr_tmp_low, msr_tmp_high;
	void *gdt;

	vmcs_write(HOST_CR0, read_cr0());
	vmcs_write(HOST_CR3, __read_cr3());
	vmcs_write(HOST_CR4, __read_cr4());

	vmcs_write(HOST_CS_SELECTOR, __KERNEL_CS);
	vmcs_write(HOST_DS_SELECTOR, 0);
	vmcs_write(HOST_ES_SELECTOR, 0);

	vmcs_write(HOST_SS_SELECTOR, __KERNEL_DS);
	vmcs_write(HOST_TR_SELECTOR, GDT_ENTRY_TSS * 8);

	store_idt(&dt);
	vmcs_write(HOST_IDTR_BASE, dt.address);

	// TODO
	//vmcs_write(HOST_RIP, );

	rdmsr(MSR_IA32_SYSENTER_CS, msr_tmp_low, msr_tmp_high);
	vmcs_write(HOST_IA32_SYSENTER_CS,
		   (uint64_t)msr_tmp_high << 32 | msr_tmp_low);

	rdmsr(MSR_IA32_SYSENTER_EIP, msr_tmp_low, msr_tmp_high);
	vmcs_write(HOST_IA32_SYSENTER_EIP,
		   (uint64_t)msr_tmp_high << 32 | msr_tmp_low);

	rdmsr(MSR_IA32_SYSENTER_ESP, msr_tmp_low, msr_tmp_high);
	vmcs_write(HOST_IA32_SYSENTER_ESP,
		   (uint64_t)msr_tmp_high << 32 | msr_tmp_low);

	gdt = get_current_gdt_ro();
	vmcs_write(HOST_GDTR_BASE, (unsigned long)gdt);

	rdmsr(MSR_EFER, msr_tmp_low, msr_tmp_high);
	vmcs_write(HOST_IA32_EFER_FULL,
		   (uint64_t)msr_tmp_high << 32 | msr_tmp_low);

	return -EINVAL;
}

static int vcpu_enter_guest(struct vcpu *vcpu)
{
	int r = -EINVAL;
	;
	r = update_vmcs_guest_state_area(vcpu);
	if (r) {
		// TODO
	}

	r = update_vmcs_host_state_area(vcpu);
	if (r) {
		// TODO
	}

	// TODO
	return r;
}

static int vcpu_kvm_run(struct vcpu *vcpu)
{
	int r = -EINVAL;
	while (true) {
		if (vcpu->mp_state == MP_RUNNABLE) {
			r = vcpu_enter_guest(vcpu);
		} else {
			return -EINVAL;
		}
	}

	return r;
}

static long vmm_vcpu_ioctl(struct file *filp, unsigned int ioctl,
			   unsigned long arg)
{
	struct vcpu *vcpu = filp->private_data;
	struct kvm_sregs kvm_sregs;
	struct kvm_regs kvm_regs;

	void __user *argp = (void __user *)arg;

	int r = -EFAULT;

	switch (ioctl) {
	case KVM_GET_SREGS: {
		printk("vmm: KVM_GET_SREGS\n");
		r = vcpu_get_kvm_sregs(vcpu, &kvm_sregs);
		if (r) {
			printk("vmm: failed vcpu_get_kvm_sregs\n");
			r = -EINVAL;
			break;
		}
		r = copy_to_user(argp, &kvm_sregs, sizeof(struct kvm_sregs));
		if (r) {
			printk("vmm: failed copy_to_user\n");
			r = -EINVAL;
			break;
		}
		break;
	}
	case KVM_SET_SREGS: {
		printk("vmm: KVM_SET_SREGS\n");
		r = copy_from_user(&kvm_sregs, argp, sizeof(struct kvm_sregs));
		if (r) {
			printk("vmm: failed copy kvm_sregs from user\n");
			r = -EINVAL;
			break;
		}
		r = vcpu_set_kvm_sregs(vcpu, &kvm_sregs);
		if (r) {
			printk("vmm: failed vcpu_set_kvm_sregs\n");
			r = -EINVAL;
			break;
		}
		break;
	}
	case KVM_SET_REGS: {
		printk("vmm: KVM_SET_RES\n");

		r = copy_from_user(&kvm_regs, argp, sizeof(struct kvm_regs));
		if (r) {
			printk("vmm: failed copy the kvm_regs from userspace\n");
			r = EINVAL;
			break;
		}
		r = vcpu_set_kvm_regs(vcpu, &kvm_regs);
		if (r) {
			printk("vmm: failed set kvm_regs to vmcs area\n");
			r = EINVAL;
			break;
		}
		break;
	}
	case KVM_RUN: {
		printk("vmm: kvm_run\n");
		r = vcpu_kvm_run(vcpu);
		if (r) {
			printk("vmm: failed run the vcpu(%d)\n", vcpu->id);
			break;
		}

		break;
	}
	default: {
		printk("vmm: unknwown ioctl command for vcpu. [ %d ]\n", ioctl);
		break;
	}
	}

	return r;
}

static vm_fault_t vmm_vcpu_page_fault(struct vm_fault *vmf)
{
	struct vcpu *vcpu = vmf->vma->vm_file->private_data;
	struct page *page;

	page = virt_to_page(vcpu->run);

	//XXX: why the get_page is necessary?
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct vmm_vcpu_vm_ops = {
	.fault = vmm_vcpu_page_fault,
};

static int vmm_vcpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &vmm_vcpu_vm_ops;
	return 0;
}

static struct file_operations vmm_vcpu_fops = {
	.unlocked_ioctl = vmm_vcpu_ioctl,
	.mmap = vmm_vcpu_mmap,
};

static int create_vcpu_fd(struct vcpu *vcpu)
{
	//strlen("vmm-vcpu:00") == 11
	char name[11];

	snprintf(name, sizeof(name), "vmm-vcpu:%d", vcpu->id);
	return anon_inode_getfd(name, &vmm_vcpu_fops, vcpu, O_RDWR | O_CLOEXEC);
}

static long vmm_vm_ioctl_create_vcpu(struct vm *vm, unsigned int id)
{
	struct vcpu *vcpu;
	struct page *page;
	int r;

	if (id > VCPU_MAX) {
		return -EFAULT;
	}

	// all of registers and states of the processor are zero.
	vcpu = kvmalloc(sizeof(struct vcpu), GFP_KERNEL | __GFP_ZERO);
	// TODO use the virtual processor identifire (VPID) to increase speed.
	vcpu->vpid = 0;
	vcpu->id = id;

	if (id == 0) // this vcpu is BSP (bootstrap processor).
	{
		vcpu->mp_state = MP_RUNNABLE;
	} else // others are AP.
	{
		vcpu->mp_state = MP_UNINITIALIZED;
	}

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto failed_create_vcpu;
	}
	vcpu->run = page_address(page);

	vm->vcpus[id] = vcpu;

	r = create_vcpu_fd(vcpu);
	if (r < 0) {
		r = -EFAULT;
		goto failed_create_vcpu;
	}

	return r;

failed_create_vcpu:
	kvfree(vcpu);
	return -r;
}

static long vmm_vm_ioctl(struct file *filep, unsigned int ioctl,
			 unsigned long arg)
{
	struct vm *vm = filep->private_data;
	long r = -EFAULT;

	switch (ioctl) {
	case KVM_SET_TSS_ADDR: {
		//TODO
		r = 0;
		break;
	}
	case KVM_SET_USER_MEMORY_REGION: {
		//TODO
		r = 0;
		break;
	}
	case KVM_CREATE_VCPU: {
		r = vmm_vm_ioctl_create_vcpu(vm, arg);

	} break;
	default:
		break;
	}

	return r;
}

static struct file_operations vmm_vm_fops = { .unlocked_ioctl = vmm_vm_ioctl };

long vmm_dev_ioctl_create_vm(unsigned long arg)
{
	struct vm *vm;
	struct file *file;
	long r = -EINVAL;

	vm = vzalloc(sizeof(struct vm));
	if (vm == NULL) {
		return -ENOMEM;
	}

	vmcs = alloc_vmcs_region();
	if (vmcs == NULL) {
		return -ENOMEM;
	}
	vmcs_load(vmcs);
	vmcs_clear(vmcs);

	r = get_unused_fd_flags(O_CLOEXEC);
	if (r < 0) {
		goto failed_get_fd;
	}

	file = anon_inode_getfile("vmm-vm", &vmm_vm_fops, vm, O_RDWR);
	if (IS_ERR(file)) {
		r = PTR_ERR(file);
		goto failed_get_fd;
	}

	fd_install(r, file);

	return r;

failed_get_fd:
	kvfree(vm);

	return r;
}
