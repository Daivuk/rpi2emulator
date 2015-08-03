.section .init
.globl _start
_start:
	b main

.section .text
main:
	// Setup the stack
	mov sp,#0x8000

	// Main loop
	loop$:
		mov r0,#0xFF
		mov r1,r0
		b loop$

.section .end
endMarker:
