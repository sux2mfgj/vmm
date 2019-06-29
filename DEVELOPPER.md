simple docs for developpers
---
I'm developping vmm as nested vmm. A below figure shows that.
```
+--------------------+
|+------------------+|
||some code as guest||
||    Linux + vmm   ||
|+------------------+|
|       Qemu         |
|  Host Linux + KVM  |
+--------------------+
```

1. install qemu-kvm and setup user
```
$ apt install libvirt-bin bridge-utils virt-manager ovmf
$ adduser `whoami` libvirt
$ adduser `whoami` libvirt-qemu
$ reboot
$ virt-manager # to check a connectivity with libvirtd
```

1. create a install disk image as a file.(change the file name and the image size by your-self)
```
$ qemu-img create -f raw arch_linux_disk.img 8G
```

1. download a installer image (in this case, arch linux is used, and from a cache in japan.)
```
$ wget "http://ftp.jaist.ac.jp/pub/Linux/ArchLinux/iso/2019.02.01/archlinux-2019.02.01-x86_64.iso"
```

1. install guest os to the disk image file
```
$ virt-install \
        --name dev_vmm_arch \
        --boot uefi \
        --connect=qemu:///system \
        --network bridge=virbr0 \
        --hvm \
        --vcpu=1 \
        --memory=4096 \
        --cdrom=./archlinux-2019.02.01-x86_64.iso \
        --graphics spice,listen=0.0.0.0 \
        --os-type-linux
$ # follow the installer and shutdown
$ # Maybe, you have to change a configuration to use nested vmm of the qemu-kvm
```

1. boot the guest os

```
$ virsh start dev_vmm_arch
```

1. install development tools
TBD

1. let's start hacking!

#### Notice
Please uninstall a kvm_intel module from guest linux. like,
```
$ modprove -r kvm_instal
```

#### Debuging using kdb

1. prepare a KDB enabled kernel
```
$ # download linux kernel
$ cd <the linux kernel directory>
$ # make distclean mrproper
$ # make localconfig
$ make menuconfig
Kernel Hacking -> KGDB -> KDB
$ # build and install
$ # don't forget to change the configuration of boot loader (grub, systemd-boot, etc).
```
1. boot with the prepared kernel
1. install the vmm kernel module and set breakpoint
```
$ # login as user
$ cd <a directory of vmm>
$ make install
```
```
$ # login as root
$ echo kbd > /sys/module/kgdboc/parameters/kgdboc
$ echo g > /proc/sysrq-trigger # or push the magicrq
kdb> ? # you can see all of commands.
kdb> bp vmx_run
kdb> go
```
```
$ # login as user
$ cd <vmm directory>
$ make debug
$ sudo ./debug
$ dmesg
```
