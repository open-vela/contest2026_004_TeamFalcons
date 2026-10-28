# 量 NSH 控制台线程跑一条命令时的栈高水位。
#
# 做法：把栈里未使用的部分填成 0xDEADBEEF，命令跑完再回读，最低被写坏的位置就是高水位。
#
# 注意：OpenOCD 的连接配置带 connect_assert_srst，每次 attach 都会复位板子，
# 所以填充和回读必须放在同一个会话里，用 nsh_command 断点当"停下"的时机。
#
# 用法（从本仓根目录）：
#   arm-none-eabi-gdb --batch -x scripts/gdb/measure-console-stack.cmd
#   然后从 COM3 依次发 5 条命令：uname -a / echo AAA / echo BBB / mv / cat
#   （第 4 条入口处填充，第 5 条入口处回读；换成别的命令也可以，只要条数对得上）
#
# nsh_command 的地址随固件变化，先用 nm 核对：
#   arm-none-eabi-nm .debug/nuttx.elf | grep ' nsh_command$'

set pagination off
set confirm off
set width 0
set height 0

file .debug/nuttx.elf

target extended-remote localhost:3333
printf "=== attached ===\n"

hbreak nsh_command
printf "=== waiting for commands ===\n"

continue
printf "\n[stop 1: uname]\n"
continue
printf "[stop 2: echo AAA]\n"
continue
printf "[stop 3: echo BBB]\n"
continue
printf "[stop 4: mv - filling the stack now]\n"

set $rtcb = *(unsigned int *)0x24000264
printf "console tcb = %p\n", $rtcb
set $sz = *(unsigned int *)($rtcb + 0x6c)
set $sa = *(unsigned int *)($rtcb + 0x70)
printf "stack alloc = [%p, %p)  adj_stack_size = %d\n", $sa, $sa + $sz + 24, $sz
printf "tls_info_s->tl_task before fill = "
x/1xw $sa

set $a = $sa + 0x18
set $end = $sp - 0x100
while $a < $end
  set {unsigned int}$a = 0xdeadbeef
  set $a = $a + 4
end
printf "filled %d bytes below sp\n", $end - ($sa + 0x18)

printf "\n=== letting the command run ===\n"
continue
printf "\n[stop 5: after the measured command]\n"

set $lo = $sa + 0x18
set $hi = $sa + 0x18 + $sz
set $a = $lo
set $hw = 0
while $a < $hi
  if *(unsigned int *)$a != 0xdeadbeef
    set $hw = $a
    loop_break
  end
  set $a = $a + 4
end

if $hw == 0
  printf "no pattern left: stack fully consumed\n"
else
  printf "high-water mark = %p\n", $hw
  printf "deepest use     = %d bytes of %d available\n", $hi - $hw, $sz
  printf "headroom left   = %d bytes\n", $hw - $lo
end

printf "tls_info_s->tl_task after      = "
x/1xw $sa

printf "\n=== resetting target ===\n"
monitor reset run
detach
quit
