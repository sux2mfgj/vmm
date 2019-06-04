.globl get_cpuid_info

.text
get_cpuid_info:
    movl $0x80000008, %eax
    cpuid
    ret

.globl vm_exit_guest
vm_exit_guest:
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rbp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax

    movq %rsp, %rcx # a first argument of vm_exit_handler
    call vm_exit_handler

    ret

.globl test_guest_rip
test_guest_rip:
    hlt
