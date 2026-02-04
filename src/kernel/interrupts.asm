section .text
global isr0_wrapper
global isr1_wrapper
global isr12_wrapper
extern timer_handler
extern keyboard_handler
extern mouse_handler

; Helper to send EOI (End of Interrupt) to PIC
send_eoi:
    push rax
    mov al, 0x20
    out 0x20, al ; Master PIC
    ; If IRQ > 7, send to Slave too (Mouse is IRQ 12)
    ; We'll handle this in the specific wrappers or C code.
    ; Actually, simpler to do EOI in C or here.
    ; Let's just do it in C.
    pop rax
    ret

%macro ISR_NOERRCODE 1
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push rax
    push rbx
    push rbp
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    call %1
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop rbp
    pop rbx
    pop rax
    pop r9
    pop r8
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    iretq
%endmacro

isr0_wrapper:
    ISR_NOERRCODE timer_handler

isr1_wrapper:
    ISR_NOERRCODE keyboard_handler

isr12_wrapper:
    ISR_NOERRCODE mouse_handler
