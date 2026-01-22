    global READINPUT
READINPUT:
    sub rsp, 8
    ; mov rbp, rsp
    mov r12, rdi                ; input mode
.searching: ;; find next digit (abort if not found)
    mov rdi, [rel fp]
    call fgetc
    movsxd rcx, eax
    cmp rcx, '-'
    je .foundstart
    cmp rcx, -1
    je .eof
    cmp rcx, '0'
    jl .searching
    cmp rcx, '9'
    jg .searching
    jmp .foundstart
.eof:
    ;; mov rdi, strtmp
    ;; mov rsi, readfail
    ;; call printf
    mov rax, 60          ; sys_exit
    mov rdi, 1           ; fail
    syscall
.foundstart:
    mov r14, 0                  ; length of pushed bytes
    sub rsp, 1
    mov dl, 0                   ; null terminator
    mov [rsp], dl
.loading:
    inc r14
    sub rsp, 1
    mov [rsp], cl
    mov rdi, [rel fp]
    call fgetc
    movsxd rcx, eax
    cmp rcx, '.'
    je .loading
    cmp rcx, '0'
    jl .loaded
    cmp rcx, '9'
    jg .loaded
    jmp .loading
.loaded:
    mov rcx, rsp                ; bottom pointer
    lea rbx, [rsp + r14 - 1]  ; top pointer (after null)
    mov r15, r14                ; how many moves to make
    shr r15, 1
.reversing:
    ;; swap top with bottom
    test r15, r15
    jz .reversed
    mov dl, [rcx]
    mov sil, [rbx]
    mov [rcx], sil
    mov [rbx], dl
    inc rcx
    dec rbx
    dec r15
    jmp .reversing
.reversed:
    test r12, r12
    jnz .floatcall
    mov rdi, rsp
    call atol
    add rsp, r14
    inc rsp
    ; mov rsp, rbp
    add rsp, 8
    ret
.floatcall:
    mov rdi, rsp
    call atof
    add rsp, r14
    inc rsp
    ; mov rsp, rbp
    add rsp, 8
    ret
