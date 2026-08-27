/*--------------------------------------------------------------------------------
	This Source Code Form is subject to the terms of the Mozilla Public License,
	v. 2.0. If a copy of the MPL was not distributed with this file, You can
	obtain one at https://mozilla.org/MPL/2.0/.

	Based on devkitarm-crtls 3dsx_crt0.s. This New 3DS-only 3DSX profile gives
	GZDoom's VM and zone allocator a 64 MiB conventional heap and reserves
	32 MiB of linear memory for Citro3D/NovaGL texture and geometry staging.
	Native/CIA builds use the separate automatic/32 MiB contract in memory.cpp.
--------------------------------------------------------------------------------*/

	.cpu mpcore

	.section ".crt0","ax"
	.global _start, __service_ptr, __apt_appid, __heap_size, __linear_heap_size, __system_arglist, __system_runflags
	.align 2
	.arm

_start:
	b startup
	.ascii "_prm"
__service_ptr:
	.word 0
__apt_appid:
	.word 0x300
__heap_size:
	.word 64*1024*1024
__linear_heap_size:
	.word 32*1024*1024
__system_arglist:
	.word 0
__system_runflags:
	.word 0
startup:
	mov r4, lr
	ldr r0, =__bss_start__
	ldr r1, =__bss_end__
	sub r1, r1, r0
	bl ClearMem
	mov r0, r4
	bl initSystem
	ldr r0, =__system_argc
	ldr r1, =__system_argv
	ldr r0, [r0]
	ldr r1, [r1]
	ldr r3, =main
	ldr lr, =exit
	bx r3

ClearMem:
	mov r2, #3
	add r1, r1, r2
	bics r1, r1, r2
	bxeq lr
	mov r2, #0
ClrLoop:
	stmia r0!, {r2}
	subs r1, r1, #4
	bne ClrLoop
	bx lr
