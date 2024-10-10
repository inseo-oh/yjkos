#!/bin/sh
# It's addr2line

if [ $# -lt 1 ]; then
    echo "Usage: addr2line.sh [address]"
    exit 1
fi

addr2line $1 -e out/x86/bootroot/boot/yjkernel-nostrip