#include "x86.h"

u16 read_es(void)
{
	u16 es;
	asm volatile("mov %%es, %0" : "=r"(es));
	return es;
}

u16 read_cs(void)
{
	u16 cs;
	asm volatile("mov %%cs, %0" : "=r"(cs));
	return cs;
}

u16 read_ss(void)
{
	u16 ss;
	asm volatile("mov %%ss, %0" : "=r"(ss));
	return ss;
}

u16 read_ds(void)
{
	u16 ds;
	asm volatile("mov %%ds, %0" : "=r"(ds));
	return ds;
}

u16 read_fs(void)
{
	u16 fs;
	asm volatile("mov %%fs, %0" : "=r"(fs));
	return fs;
}

u16 read_gs(void)
{
	u16 gs;
	asm volatile("mov %%gs, %0" : "=r"(gs));
	return gs;
}

u16 read_ldt(void)
{
	u16 ldt;
	asm volatile("sldt %0" : "=r"(ldt));
	return ldt;
}

u16 read_tr(void)
{
	u16 tr;
	asm volatile("str %0" : "=r"(tr));
	return tr;
}

u64 load_segment_limit(u64 selector)
{
	u64 limit;
	asm volatile("lsl %1, %0" : "=r"(limit) : "r"(selector));
	return limit;
}

u64 load_access_right(u64 selector)
{
	u64 access_right;
	asm volatile("lar %1, %0" : "=r"(access_right) : "r"(selector));
    return access_right;
}
