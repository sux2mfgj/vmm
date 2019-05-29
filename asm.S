.globl get_cpuid_info

.text
get_cpuid_info:
    movl $0x80000008, %eax
    cpuid
    ret
