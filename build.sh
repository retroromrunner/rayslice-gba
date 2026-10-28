#!/bin/bash
set -e
cd "$(dirname "$0")"
export PATH="$HOME/workspace/toolchain/usr/bin:$PATH"
arm-none-eabi-gcc -mcpu=arm7tdmi -marm -O2 -ffreestanding -nostdlib -Wall -T gba.ld -o raycaster.elf src/crt0.s src/main.c -lgcc
arm-none-eabi-objcopy -O binary raycaster.elf raycaster.gba
python3 fix_header.py raycaster.gba
python3 -c "import sys; d=open('raycaster.gba','rb').read(); open('raycaster.gba','wb').write(d + b'\x00' * (32768 - len(d)) if len(d) < 32768 else d)"
mkdir -p releases
cp raycaster.gba releases/raycaster-gba-instant.gba
ls -l releases/raycaster-gba-instant.gba
