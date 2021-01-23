#!/bin/sh

SPATH=$(dirname $(readlink -f "$0"))

set -e

cd $SPATH/..
export LEMONDIR=$(pwd)

qemu-img create -f vpc Disks/Lemon.vhd 1G
sudo sh -c "qemu-nbd -c /dev/nbd0 Disks/Lemon.vhd
sfdisk /dev/nbd0 < Scripts/partitions.sfdisk
mkfs.ext2 -b 4096 /dev/nbd0p2
mkfs.vfat -F 32 /dev/nbd0p3
mkdir -p /mnt/Lemon
mkdir -p /mnt/LemonEFI
mount /dev/nbd0p2 /mnt/Lemon
mount /dev/nbd0p3 /mnt/LemonEFI
mkdir -p /mnt/Lemon/lemon/boot
grub-install --target=x86_64-efi --boot-directory=/mnt/Lemon/lemon/boot --efi-directory=/mnt/LemonEFI /dev/nbd0 --removable
grub-install --target=i386-pc --boot-directory=/mnt/Lemon/lemon/boot /dev/nbd0
umount /mnt/Lemon
umount /mnt/LemonEFI
rmdir /mnt/Lemon
rmdir /mnt/LemonEFI
qemu-nbd -d /dev/nbd0"