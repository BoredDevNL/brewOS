#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void idt_set_gate(uint8_t vector, void *isr, uint16_t cs, uint8_t flags);
void idt_register_interrupts(void);
void idt_load(void);

// ISR wrappers defined in assembly
extern void isr0_wrapper(void);  // Timer
extern void isr1_wrapper(void);  // Keyboard
extern void isr12_wrapper(void); // Mouse

#endif
