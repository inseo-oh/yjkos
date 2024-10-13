#!/bin/sh
# Connects to GDB stub running on QEMU

IP=localhost
if [ ! -z $1 ]; then
    IP=$1
fi
echo "Using IP address: "$IP
KBINARY=out/x86/bootroot/boot/yjkernel-nostrip
i586-elf-gdb -ex "target remote $IP:1234" $KBINARY
