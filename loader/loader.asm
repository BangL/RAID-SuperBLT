extern SBLT_PROXY_LOADER_FN_CXX : proc

.code
    ; Called by the generated assembly. There's the function args in their normal regs, plus
    ; some special arguments in two volatile registers:
    ; r10[31:0]  contains the index (in qwords) of the called function into the functions struct
    ; r10[63:32] has a number representing which DLL this belongs to.
    ; r11        has the function pointer to the original stub function to retry
    ; (See https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170 for more information)
    ;
    ; There is an equivalent GNU as version of this in loader_gas.S, keep them in sync.
    SBLT_PROXY_LOADER_FN proc

        ; Prologue for the 'function' part of this function, where we have a stack frame
        push rbp
        mov rbp, rsp

        ; The first 32 bytes are the shadow space of the call below, which the callee is free to overwrite. Our saved
        ; registers go above that. 32 + 104 bytes, rounded up.
        sub rsp, 144
        and rsp, -16 ; Align for movaps

        ; Save all the argument registers
        ; IDK if we're using SSE so save it anyway, but we're definitely not using AVX.
        mov [rsp+ 32], rcx
        mov [rsp+ 40], rdx
        mov [rsp+ 48], r8
        mov [rsp+ 56], r9
        movaps [rsp+ 64], xmm0
        movaps [rsp+ 80], xmm1
        movaps [rsp+ 96], xmm2
        movaps [rsp+112], xmm3
        mov [rsp+128], r11 ; We need this later, though we're not passing it to the final function.

        ; Call the C++ loader function
        mov rcx, r10 ; Function+DLL ID
        call SBLT_PROXY_LOADER_FN_CXX

        ; Restore the original args
        mov rcx, [rsp+ 32]
        mov rdx, [rsp+ 40]
        mov r8 , [rsp+ 48]
        mov r9 , [rsp+ 56]
        movaps xmm0, [rsp+ 64]
        movaps xmm1, [rsp+ 80]
        movaps xmm2, [rsp+ 96]
        movaps xmm3, [rsp+112]
        mov r11, [rsp+128]

        ; Epilogue
        mov rsp, rbp
        pop rbp

        ; Retry the call now the DLL is loaded
        jmp r11
    SBLT_PROXY_LOADER_FN endp
end
