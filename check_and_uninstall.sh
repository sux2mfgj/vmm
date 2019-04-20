#!/bin/bash

is_installed_kvm=$(lsmod | grep kvm_intel)

if [ -n "${is_installed_kvm}" ]; then
    sudo modprobe -r kvm_intel
fi

is_installed_vmm=$(lsmod | grep vmm)
if [ -n "${is_installed_vmm}" ]; then
    sudo rmmod vmm
fi
