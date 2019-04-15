
#define SAVE_HOST_REGS \
    movq %r15, 15 * 8 (%rdi)

#define LOAD_HOST_REGS \
    movq 15*8(%rdi), %r15

.text
/*
 * int vm_enter_guest(struct kvm_regs* regs, int is_launch);
 * - args
 *  struct kvm_regs* regs   : %rdi
 *  int is_launch           : %rsi
 */
.globl vm_enter_guest
vm_enter_guest:
    SAVE_HOST_REGS

    movl %esi, %r8d
    cmpl $0, %r8d
    je exec_vmresume
    vmlaunch

    // when some error occured at the vmlaunch, the ret is executed.
    movl $1, %eax
    ret

exec_vmresume:
    // when some error occured at the vmresume, the ret is executed.
    vmresume
    ret
/*
 * void vm_exit_guest(?);
 */
.globl vm_exit_guest
vm_exit_guest:
    LOAD_HOST_REGS
