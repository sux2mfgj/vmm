#ifndef __VMM_DEBUG_H
#define __VMM_DEBUG_H

#include <asm/kvm.h>
#include <linux/kvm.h>

#define VMM_DEBUG   _IO(KVMIO, 0x80)

#endif // __VMM_DEBUG_H

