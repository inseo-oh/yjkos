#!/bin/sh
# Connects to GDB stub running on QEMU

IP=localhost
if [ ! -z $1 ]; then
    IP=$1
fi
echo "Using IP address: "$IP
KBINARY=out/i586/bootroot/boot/yjkernel-nostrip
GDB=toolchain/bin/i586-elf-gdb
$GDB -ex "target remote $IP:1234" $KBINARY
