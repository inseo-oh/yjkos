ARCH_CC         = i586-elf-gcc
ARCH_STRIP      = i586-elf-strip
ARCH_OBJCOPY    = i586-elf-objcopy

ARCH_CFLAGS     = -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 
ARCH_CFLAGS    += -I$(PROJECT_DIR)/toolchain/$(ARCH)/include
ARCH_CFLAGS    += -DYJKERNEL_ARCH_I586
ARCH_CFLAGS    += -DYJKERNEL_ARCH_TRAP_COUNT=256

ARCH_CFLAGS    += -m80387

ARCH_LDSCRIPT   = link.ld
