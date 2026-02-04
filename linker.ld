/* Tell the linker that we want an x86_64 ELF64 output file */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

/* We want the symbol _start to be our entry point */
ENTRY(_start)

/* Define the memory layout for the kernel */
SECTIONS
{
    /* We want to be loaded in the upper half of memory */
    . = 0xffffffff80000000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(COMMON)
        *(.bss*)
    }

    /* Limine requests section */
    .requests : {
        KEEP(*(.requests_start))
        KEEP(*(.requests))
        KEEP(*(.requests_end))
    }

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}
