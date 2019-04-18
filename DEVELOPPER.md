simple docs for developpers
---

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
```

1. boot the guest os

```
$ virsh start dev_vmm_arch
```

1. install development tools
TBD

1. let's start hacking!
