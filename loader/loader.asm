extern SBLT_PROXY_LOADER_FN_CXX : proc

.code
    ; Called by the generated assembly. There's the function args in their normal regs, plus
    ; some special arguments in two volatile registers:
    ; r10[31:0]  contains the index (in qwords) of the called function into the functions struct
    ; r10[63:32] has a number representing which DLL this belongs to.
    ; r11        has the function pointer to the original stub function to retry
    ; (See https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170 for more information)
    SBLT_PROXY_LOADER_FN proc

        ; Prologue for the 'function' part of this function, where we have a stack frame
        push rbp
        mov rbp, rsp

        sub rsp, 104
        and rsp, -16 ; Align for movaps

        ; Save all the argument registers
        ; IDK if we're using SSE so save it anyway, but we're definitely not using AVX.
        mov [rsp+ 0], rcx
        mov [rsp+ 8], rdx
        mov [rsp+16], r8
        mov [rsp+24], r9
        movaps [rsp+32], xmm0
        movaps [rsp+48], xmm1
        movaps [rsp+64], xmm2
        movaps [rsp+80], xmm3
        mov [rsp+96], r11 ; We need this later, though we're not passing it to the final function.

        ; Call the C++ loader function
        mov rcx, r10 ; Function+DLL ID
        call SBLT_PROXY_LOADER_FN_CXX

        ; Restore the original args
        mov rcx, [rsp+ 0]
        mov rdx, [rsp+ 8]
        mov r8 , [rsp+16]
        mov r9 , [rsp+24]
        movaps xmm0, [rsp+32]
        movaps xmm1, [rsp+48]
        movaps xmm2, [rsp+64]
        movaps xmm3, [rsp+80]
        mov r11, [rsp+96]

        ; Epilogue
        mov rsp, rbp
        pop rbp

        ; Retry the call now the DLL is loaded
        jmp r11
    SBLT_PROXY_LOADER_FN endp
end
