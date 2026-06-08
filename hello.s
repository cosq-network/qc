; qc x64 assembly (Intel syntax, System V AMD64)
bits 64
default rel


section .text


global main
main:
        push rbp
        mov rbp, rsp
        sub rsp, 16
.L_main_entry:
        mov rax, 42
        jmp .L_main_epilogue
.L_main_epilogue:
        add rsp, 16
        pop rbp
        ret

