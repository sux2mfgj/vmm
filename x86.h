#pragma once
#include <asm/types.h>

u16 read_es(void);
u16 read_cs(void);
u16 read_ss(void);
u16 read_ds(void);
u16 read_fs(void);
u16 read_gs(void);
u16 read_ldt(void);
u16 read_tr(void);

u64 load_segment_limit(u64 selector);
u64 load_access_right(u64 selector);
