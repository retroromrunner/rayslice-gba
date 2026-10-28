#!/bin/bash
set -e
cd "$(dirname "$0")"
export PATH="$HOME/workspace/toolchain/usr/bin:$PATH"
arm-none-eabi-gcc -mcpu=arm7tdmi -marm -O2 -ffreestanding -nostdlib -Wall -T gba.ld -o rayslice.elf src/crt0.s src/main.c -lgcc
arm-none-eabi-objcopy -O binary rayslice.elf rayslice.gba
python3 fix_header.py rayslice.gba
python3 -c "d=open('rayslice.gba','rb').read(); open('rayslice.gba','wb').write(d + b'\x00' * (32768 - len(d)) if len(d) < 32768 else d)"
mkdir -p releases
cp rayslice.gba releases/raycaster-gba-slice.gba
ls -l releases/raycaster-gba-slice.gba
