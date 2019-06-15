#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kvm.h>

#include "vmx.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunsuke Mie <sux2mfgj@gmail.com>");

#define DEVICE_NAME "kvm"
#define CLASS_NAME "vmm"

#define VMM_DEBUG   _IO(KVMIO, 0x80)


static int majorNumber;
static struct class *vmm_class = NULL;
static struct device *vmm_device = NULL;

static long vmm_dev_ioctl(struct file* filep, unsigned int ioctl, unsigned long arg)
{
    long r = -EINVAL;
    switch(ioctl)
    {
        case VMM_DEBUG:
            r = 0;
            break;

        default:
            printk("not yet implemented\n");
            break;
    }

    return r;
}

static struct file_operations vmm_fops = {
	.unlocked_ioctl = vmm_dev_ioctl
};

static int register_device(void)
{
	int r = 0;
	majorNumber = register_chrdev(0, DEVICE_NAME, &vmm_fops);
	if (majorNumber < 0) {
		printk(KERN_ALERT "vmm: failed to register a major number\n");
		r = majorNumber;
		goto failed;
	}

	vmm_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(vmm_class)) {
		printk(KERN_ALERT "vmm: failed to create a class\n");
		r = PTR_ERR(vmm_class);
		goto failed_class_create;
	}

	vmm_device = device_create(vmm_class, NULL, MKDEV(majorNumber, 0), NULL,
				   DEVICE_NAME);
	if (IS_ERR(vmm_device)) {
		printk(KERN_ALERT "vmm: failed to create a device\n");
		r = PTR_ERR(vmm_device);
		goto failed_create_device;
	}

	return 0;

failed_create_device:
	class_unregister(vmm_class);
	class_destroy(vmm_class);
failed_class_create:
	unregister_chrdev(majorNumber, DEVICE_NAME);
failed:
	return r;
}

static void unregister_device(void)
{
	device_destroy(vmm_class, MKDEV(majorNumber, 0));
	class_unregister(vmm_class);
	class_destroy(vmm_class);
	unregister_chrdev(majorNumber, DEVICE_NAME);
}

static int vmm_init(void)
{
	int r = 0;
    r = register_device();
    if(r)
    {
        printk("failed register a device\n");
        goto failed;
    }

	r = vmx_setup();
	if (r) {
        printk("failed to setup the intel VT-x\n");
        goto failed_unreg;
	}

    printk("ok\n");
	return 0;

failed_unreg:
    unregister_device();
failed:
    return r;
}

static void vmm_exit(void)
{
	vmx_tear_down();
    unregister_device();
    printk("bye\n");
}

module_init(vmm_init);
module_exit(vmm_exit);
