#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kvm.h>

#include "vmx.h"
#include "vmm_debug.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunsuke Mie <sux2mfgj@gmail.com>");

#define DEVICE_NAME "kvm"
#define CLASS_NAME "vmm"

static int majorNumber;
static struct class *vmm_class = NULL;
static struct device *vmm_device = NULL;

static long vmm_dev_ioctl(struct file *filep, unsigned int ioctl,
			  unsigned long arg)
{
	long r = -EINVAL;
	switch (ioctl) {
	case VMM_DEBUG:
		r = vmx_run();
		break;

	default:
		printk(KERN_ERR "vmm: not yet implemented\n");
		break;
	}

	return r;
}

static struct file_operations vmm_fops = { .unlocked_ioctl = vmm_dev_ioctl };

static int register_device(void)
{
	int r = 0;
	majorNumber = register_chrdev(0, DEVICE_NAME, &vmm_fops);
	if (majorNumber < 0) {
		printk(KERN_ERR "vmm: failed to register a major number\n");
		r = majorNumber;
		goto failed;
	}

	vmm_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(vmm_class)) {
		printk(KERN_ERR "vmm: failed to create a class\n");
		r = PTR_ERR(vmm_class);
		goto failed_class_create;
	}

	vmm_device = device_create(vmm_class, NULL, MKDEV(majorNumber, 0), NULL,
				   DEVICE_NAME);
	if (IS_ERR(vmm_device)) {
		printk(KERN_ERR "vmm: failed to create a device\n");
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

    printk(KERN_DEBUG "vmm: hello\n");

	r = register_device();
	if (r) {
		printk(KERN_ERR "vmm: failed register a device\n");
		goto failed;
	}

    r = vmx_init();

    return r;

//failed_unreg:
	unregister_device();
failed:
	return r;
}

static void vmm_exit(void)
{
    vmx_deinit();
	unregister_device();
	printk(KERN_DEBUG "vmm: bye\n");
}

module_init(vmm_init);
module_exit(vmm_exit);
