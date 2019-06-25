#include <linux/linkage.h>
#include <asm/asm.h>

.text

//.global vm_exit_handler
//;extern printk

ENTRY(get_cpuid_info)
    movl $0x80000008, %eax
    cpuid
    ret
ENDPROC(get_cpuid_info)

//ENTRY(vm_exit_guest)
//    mov $1, %edi
//    jmp vm_exit_handler
//    ret
//ENDPROC(vm_exit_guest)

ENTRY(test_guest_rip)
    hlt
    hlt
    hlt
    hlt
    hlt
    hlt
    hlt
    hlt
ENDPROC(test_guest_rip)
