ENTRY(archx86_kernelentry)

VIRTUAL_BASE     = 0xc0000000;

SECTIONS
{
    . = 1M;
    __ARCHX86_KERNEL_PHYSICAL_ADDRESS_BEGIN__ = .;

    .text.init : ALIGN(4096) {
        __init_text_begin__ = .;
        *(.multiboot)
        *(.text.init)
        __init_text_end__ = .;
    }
    .bss.init : ALIGN(4096) {
        *(.bss.init)
    }
    LOAD_ADDR_OFFSET = .;

    . = VIRTUAL_BASE;
    __ARCH_KERNEL_VIRTUAL_ADDRESS_BEGIN__ = .;
    __physaddr_kernel_begin__ = (. - VIRTUAL_BASE + LOAD_ADDR_OFFSET);

    .text : AT(ADDR(.text) - VIRTUAL_BASE + LOAD_ADDR_OFFSET) ALIGN(4096) {
        __ARCHX86_KERNEL_SHELLTEXT_BEGIN__ = .;
        *(.text.shell)
        __ARCHX86_KERNEL_SHELLTEXT_END__ = .;
        . = ALIGN(4096);
        __ARCHX86_KERNEL_TEXT_BEGIN__ = .;
        *(.text)
        *(.text.*)
        . = ALIGN(4096);
        __ARCHX86_KERNEL_TEXT_END__ = .;
    }
    .ctors : AT(ADDR(.ctors) - VIRTUAL_BASE + LOAD_ADDR_OFFSET) ALIGN(4096) {
        __initarray_begin__ = .;
        KEEP(*(.ctors))
        *(.ctors)
        KEEP(*(.init_array*))
        *(.init_array*)
        __initarray_end__ = .;
    }
    .data : AT(ADDR(.data) - VIRTUAL_BASE + LOAD_ADDR_OFFSET) ALIGN(4096) {
        __ARCHX86_KERNEL_SHELLDATA_BEGIN__ = .;
        *(.data.shell)
        __ARCHX86_KERNEL_SHELLDATA_END__ = .;
        . = ALIGN(4096);
        __ARCHX86_KERNEL_RO_AFTER_EARLY_INIT_BEGIN__ = .;
        *(.data.ro_after_early_init)
        . = ALIGN(4096);
        __ARCHX86_KERNEL_RO_AFTER_EARLY_INIT_END__ = .;
        *(.data)
        *(.data.*)
    }
    .rodata : AT(ADDR(.rodata) - VIRTUAL_BASE + LOAD_ADDR_OFFSET) ALIGN(4096) {
        __ARCHX86_KERNEL_SHELLRODATA_BEGIN__ = .;
        *(.rodata.shell)
        __ARCHX86_KERNEL_SHELLRODATA_END__ = .;
        *(.rodata)
        *(.rodata.*)
    }
    .bss : AT(ADDR(.bss) - VIRTUAL_BASE + LOAD_ADDR_OFFSET) ALIGN(4096) {
        __ARCHX86_KERNEL_SHELLBSS_BEGIN__ = .;
        *(.bss.shell)
        __ARCHX86_KERNEL_SHELLBSS_END__ = .;
        . = ALIGN(4096);
        *(.bss)
        *(.bss.*)
    }
    . = ALIGN(4096);
    __ARCH_KERNEL_VIRTUAL_ADDRESS_END__ = .;
    __ARCHX86_KERNEL_PHYSICAL_ADDRESS_END__ = (. - VIRTUAL_BASE + LOAD_ADDR_OFFSET);
}
