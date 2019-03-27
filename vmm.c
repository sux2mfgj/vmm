#include <linux/module.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunsuke Mie <sux2mfgj@gmail.com>");

static int vmm_init(void)
{
        printk("vmm: Hello, world\n");

        return 0;
}

static void vmm_exit(void)
{
        printk("vmm: byebye\n");
}

module_init(vmm_init);
module_exit(vmm_exit);
