#!/bin/sh
set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <source.asm> <out>"
    exit 1
fi

nasm -f elf64 -g -F dwarf "$1" -o a.o
cc -o "$2" -g a.o -no-pie
# ld -g -o "$2" a.o -lc \
#    --dynamic-linker /lib64/ld-linux-x86-64.so.2 \
#    -no-pie

rm -f a.o
