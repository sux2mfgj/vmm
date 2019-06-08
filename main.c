#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kvm.h>

#include "vmx.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunsuke Mie <sux2mfgj@gmail.com>");

static int vmm_init(void)
{
	int r = 0;
	r = vmx_setup();
    if(r)
    {
        return -1;
    }

	return 0;
}

static void vmm_exit(void)
{
	vmx_tear_down();
}

module_init(vmm_init);
module_exit(vmm_exit);
