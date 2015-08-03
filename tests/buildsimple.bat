arm-none-eabi-gcc -DRPI2 -O2 -mfpu=neon-vfpv4 -mfloat-abi=hard -mcpu=cortex-a7 -march=armv7-a -mtune=cortex-a7 -nostartfiles -nostdlib -ffreestanding -g ./source/simple.s -o ./kernel.elf -T kernel.ld

arm-none-eabi-objcopy ./kernel.elf -O binary ./build/simple7.img
