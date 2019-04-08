#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/anon_inodes.h>

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

static void vmx_get_desc_table(struct kvm_dtable* dtable,
                enum vmcs_field_encoding base,
                enum vmcs_field_encoding limit)
{
        dtable->base = vmcs_read(base);
        dtable->limit = vmcs_read(limit);
}

static int vcpu_get_kvm_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
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
//         sregs->cr2 = ;
        sregs->cr3 = vmcs_read(GUEST_CR3);
        sregs->cr4 = vmcs_read(GUEST_CR4);
//         sregs->cr8 = ;
        //TODO

	return r;
}

static long vmm_vcpu_ioctl(struct file *filp, unsigned int ioctl,
			   unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	struct kvm_sregs kvm_sregs;
	int r = -EFAULT;

	switch (ioctl) {
	case KVM_GET_SREGS: {
		r = vcpu_get_kvm_sregs(vcpu, &kvm_sregs);
		break;
	}
	default:
		break;
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

	vcpu = kvmalloc(sizeof(struct vcpu), GFP_KERNEL);
	// TODO use virtual processor identifire to increase speed.
	vcpu->vpid = 0;
	vcpu->id = id;

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
