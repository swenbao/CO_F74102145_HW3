#!/bin/bash


printf "[cache]\nSet = $((2**$1))\nWay = $((2**$2))\nBlockSize = $((2**$3))\nPolicy = \"$4\"" > config.conf

make "$4"
riscv64-unknown-elf-gcc -static -o qr ./benchmark/qrcode.c
spike --isa=RV64GC --dc="$1":"$2":"$3" /home/ubuntu/riscv/riscv64-unknown-elf/bin/pk qr
