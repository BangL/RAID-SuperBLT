/* GNU as (Intel syntax) version of loader.asm, which is for MSVC. Keep them in sync.
 *
 * Called by the generated assembly. There's the function args in their normal regs, plus
 * some special arguments in two volatile registers:
 * r10[31:0]  contains the index (in qwords) of the called function into the functions struct
 * r10[63:32] has a number representing which DLL this belongs to.
 * r11        has the function pointer to the original stub function to retry
 */

.intel_syntax noprefix
.text

.globl SBLT_PROXY_LOADER_FN
SBLT_PROXY_LOADER_FN:
    /* Prologue for the 'function' part of this function, where we have a stack frame */
    push rbp
    mov rbp, rsp

    /* The first 32 bytes are the shadow space of the call below, which the callee is free to overwrite. Our saved
     * registers go above that. 32 + 104 bytes, rounded up. */
    sub rsp, 144
    and rsp, -16 /* Align for movaps */

    /* Save all the argument registers */
    mov qword ptr [rsp+ 32], rcx
    mov qword ptr [rsp+ 40], rdx
    mov qword ptr [rsp+ 48], r8
    mov qword ptr [rsp+ 56], r9
    movaps xmmword ptr [rsp+ 64], xmm0
    movaps xmmword ptr [rsp+ 80], xmm1
    movaps xmmword ptr [rsp+ 96], xmm2
    movaps xmmword ptr [rsp+112], xmm3
    mov qword ptr [rsp+128], r11 /* We need this later, though we're not passing it to the final function. */

    /* Call the C++ loader function */
    mov rcx, r10 /* Function+DLL ID */
    call SBLT_PROXY_LOADER_FN_CXX

    /* Restore the original args */
    mov rcx, qword ptr [rsp+ 32]
    mov rdx, qword ptr [rsp+ 40]
    mov r8 , qword ptr [rsp+ 48]
    mov r9 , qword ptr [rsp+ 56]
    movaps xmm0, xmmword ptr [rsp+ 64]
    movaps xmm1, xmmword ptr [rsp+ 80]
    movaps xmm2, xmmword ptr [rsp+ 96]
    movaps xmm3, xmmword ptr [rsp+112]
    mov r11, qword ptr [rsp+128]

    /* Epilogue */
    mov rsp, rbp
    pop rbp

    /* Retry the call now the DLL is loaded */
    jmp r11
