# 在 arm_hardfault 入口下硬件断点，等板上复现时停住，抓故障现场。
#
# 用法（从本仓根目录）：
#   arm-none-eabi-gdb --batch -x scripts/gdb/catch-hardfault.cmd
#   另开一个进程通过 COM3 发复现命令。
#
# 断点设在函数第一条指令，此时 r0 = irq、r1 = context（NuttX 保存的寄存器区指针）。
# arm_hardfault 的地址随固件变化，先用 nm 核对：
#   arm-none-eabi-nm .debug/nuttx.elf | grep ' arm_hardfault$'

set pagination off
set confirm off
set width 0
set height 0

file .debug/nuttx.elf

target extended-remote localhost:3333
printf "=== CONNECTED ===\n"
info registers pc sp

hbreak arm_hardfault
printf "=== hardware breakpoint installed ===\n"
info breakpoints

printf "=== running, waiting for the board to fault ===\n"
continue

printf "\n\n############ HARD FAULT CAUGHT ############\n"
printf "\n--- cpu regs at handler entry ---\n"
info registers

printf "\n--- SCB fault status ---\n"
printf "CFSR  @0xE000ED28: "
x/1xw 0xE000ED28
printf "HFSR  @0xE000ED2C: "
x/1xw 0xE000ED2C
printf "MMFAR @0xE000ED34: "
x/1xw 0xE000ED34
printf "BFAR  @0xE000ED38: "
x/1xw 0xE000ED38

printf "\n--- NuttX context frame (r1 = context) ---\n"
set $ctx = $r1
p/x $ctx
x/16xw $ctx

printf "\n--- g_readytorun @0x24000264 ---\n"
x/4xw 0x24000264

printf "\n--- backtrace ---\n"
bt

printf "\n--- 32 words of handler stack at sp ---\n"
x/32xw $sp

printf "\n############ CAPTURE DONE ############\n"

monitor reset run
detach
quit
