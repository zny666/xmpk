// Minimal M-mode RV64 startup for the public kernel examples.
.section .text.init
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  la sp, stack_top
  la t0, trap
  csrw mtvec, t0
  li t0, 0x6600
  csrs mstatus, t0
  la t0, __bss_start
  la t1, __bss_end
1:
  bgeu t0, t1, 2f
  sd zero, 0(t0)
  addi t0, t0, 8
  j 1b
2:
  call main
  li a0, 0x3fffffff
  j tohost_exit
trap:
  li a0, 0x3fffffff
.globl tohost_exit
tohost_exit:
  slli a0, a0, 1
  ori a0, a0, 1
  la t0, tohost
  sd a0, 0(t0)
  fence rw,rw
3:
  j 3b
.section .tohost,"aw",@progbits
.balign 64
.globl tohost
tohost: .dword 0
.globl fromhost
fromhost: .dword 0
.section .bss
.balign 16
stack_bottom: .skip 1048576
stack_top:
