; brew-os/src/kernel/boot.asm
; 64-bit Entry Point for BrewOS

section .text
global _start
extern kmain

bits 64

_start:
    ; Ensure interrupts are disabled
    cli
    
    ; Setup stack is handled by Limine, but we can re-align if paranoid
    ; (Limine guarantees 16-byte alignment)

    ; Call the C kernel entry point
    call kmain

    ; Halt if kmain returns
    hlt
.loop:
    jmp .loop
