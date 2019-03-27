#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/ioctl.h>

#include <linux/kvm.h>

static void open_dev_kvm(void)
{
	int fd = open("/dev/kvm", O_RDWR);
	assert(fd >= 0 && "failed to open /dev/kvm");
	close(fd);
}

static void check_api_version(void)
{
	int fd = open("/dev/kvm", O_RDWR);
	assert(fd >= 0 && "failed to open /dev/kvm");

	int api_version = ioctl(fd, KVM_GET_API_VERSION, 0);
	assert(api_version == KVM_API_VERSION);

	close(fd);
}

static void create_vm(void)
{
	int fd = open("/dev/kvm", O_RDWR);
	assert(fd >= 0 && "failed to open /dev/kvm");

	int api_version = ioctl(fd, KVM_GET_API_VERSION, 0);
	assert(api_version == KVM_API_VERSION);

        int vm_fd = ioctl(fd, KVM_CREATE_VM, 0);
        assert(vm_fd >= 0);

        close(fd);
        close(vm_fd);
}

int main(int argc, char const *argv[])
{
        open_dev_kvm();
	check_api_version();
        create_vm();

	printf("no error detected\n");
	return 0;
}
