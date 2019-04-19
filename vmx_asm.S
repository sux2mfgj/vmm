
#define SAVE_HOST_REGS \
    pushq %r15; \
    pushq %r14; \
    pushq %r13; \
    pushq %r12; \
    pushq %rbp; \
    pushq %rbx;

#define LOAD_HOST_REGS \
    popq %rbx; \
    popq %rbp; \
    popq %r12; \
    popq %r13; \
    popq %r14; \
    popq %r15;

#define SAVE_GUEST_REGS \
    movq %rax, 0 * 8(%rdi); \
    movq %rbx, 1 * 8(%rdi); \
    movq %rcx, 2 * 8(%rdi); \
    movq %rdx, 3 * 8(%rdi); \
    movq %rsi, 4 * 8(%rdi); \
    movq %rdi, 5 * 8(%rdi); \
    movq %rsp, 6 * 8(%rdi); \
    movq %rbp, 7 * 8(%rdi); \
    movq %r8, 7 * 8(%rdi); \
    movq %r9, 9 * 8(%rdi); \
    movq %r10, 10 * 8(%rdi); \
    movq %r11, 11 * 8(%rdi); \
    movq %r12, 12 * 8(%rdi); \
    movq %r13, 13 * 8(%rdi); \
    movq %r14, 14 * 8(%rdi); \
    movq %r15, 15 * 8(%rdi); \
    pushq %rdi


#define LOAD_GUEST_REGS \
    popq %rdi; \
    movq 0 * 8(%rdi), %rax; \
    movq 1 * 8(%rdi), %rbx; \
    movq 2 * 8(%rdi), %rcx; \
    movq 3 * 8(%rdi), %rdx; \
    movq 4 * 8(%rdi), %rsi; \
    movq 6 * 8(%rdi), %rsp; \
    movq 7 * 8(%rdi), %rbp; \
    movq 8 * 8(%rdi), %r8; \
    movq 9 * 8(%rdi), %r9; \
    movq 10 * 8(%rdi), %r10; \
    movq 11 * 8(%rdi), %r11; \
    movq 12 * 8(%rdi), %r12; \
    movq 13 * 8(%rdi), %r13; \
    movq 14 * 8(%rdi), %r14; \
    movq 15 * 8(%rdi), %r15; \
    movq 5 * 8(%rdi), %rdi

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

    movl (%esi), %r8d
    cmpl $1, %r8d
    je exec_vmresume
    // disable the is_launch
    movl $0, (%esi)
    LOAD_GUEST_REGS
    vmlaunch
    // when some error occured at the vmlaunch, the ret is executed.
    movl $1, %eax
    ret

exec_vmresume:
    LOAD_GUEST_REGS
    // when some error occured at the vmresume, the ret is executed.
    vmresume
    movl $2, %eax
    ret
/*
 * void vm_exit_guest(void); // TODO
 */
.globl vm_exit_guest
vm_exit_guest:
    SAVE_GUEST_REGS
    LOAD_HOST_REGS
    //TODO
    ret
