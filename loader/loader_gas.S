.intel_syntax noprefix
.text
.globl jumpToPA
jumpToPA:
	jmp qword ptr [rip + PA]
