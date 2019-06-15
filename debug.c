#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "vmm_debug.h"

int main(int argc, char const* argv[])
{
    int fd;
    int result;

    fd = open("/dev/kvm", O_RDWR);
    if(fd < 0)
    {
        perror("failed to open the /dev/kvm");
        return 1;
    }

    result = ioctl(fd, VMM_DEBUG, 0);
    if(result)
    {
        perror("ioctl failed");
        goto close;
    }

close:
    result = close(fd);
    if(result)
    {
        perror("close");
        return 1;
    }

    return result;
}
