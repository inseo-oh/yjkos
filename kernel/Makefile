ifndef ARCH
$(error ARCH is not set)
endif

PROJECTDIR       := $(CURDIR)/..
TOOLCHAIN_BINDIR  = $(PROJECTDIR)/toolchain/bin
OUTDIR            = $(PROJECTDIR)/out/$(ARCH)
FONTDIR           = $(OUTDIR)/kernelfont
FONTNAME          = kernelfont.psf
FONTOBJ           = kernelfont.o

# We have to to remove './' from the result, because without it $(OBJDIR)/kdoom/kdoom.o rule doesn't work due to that ./
C_SRCS        := $(shell find . -name '*.c' | sed 's/^.\///')
C_OBJ_NAMES    = $(patsubst %.c, %.o, $(C_SRCS))
ASM_SRCS      := $(shell find . -name '*.S' | sed 's/^.\///')
ASM_OBJ_NAMES  = $(patsubst %.S, %.o, $(ASM_SRCS))
OBJ_NAMES      = $(C_OBJ_NAMES) $(ASM_OBJ_NAMES) $(FONTOBJ)

TARGET         = $(OUTDIR)/bootroot/boot/yjkernel
TARGET_NOSTRIP = $(TARGET)-nostrip
OBJDIR         = $(OUTDIR)/obj


ARCHDIR = arch/$(ARCH)
include $(ARCHDIR)/config.mk
CC            = $(TOOLCHAIN_BINDIR)/$(ARCH_CC)
STRIP         = $(TOOLCHAIN_BINDIR)/$(ARCH_STRIP)
OBJCOPY       = $(TOOLCHAIN_BINDIR)/$(ARCH_OBJCOPY)

OBJS      = $(addprefix $(OBJDIR)/, $(OBJ_NAMES))
DEPS      = $(patsubst %.o, %.d, $(OBJS))
OBJDIRS   = $(sort $(dir $(OBJS)))

CFLAGS  = -std=c2x
CFLAGS += -Wall -Wextra -Werror -pedantic
CFLAGS += -I$(PROJECTDIR)/include
CFLAGS += -I$(PROJECTDIR)/include/kernel/lib/libc
CFLAGS += -ffreestanding -fstack-protector-all
CFLAGS += -Wno-error=unused-function -Wno-error=unused-const-variable -g 
CFLAGS += -fsanitize=undefined
CFLAGS += -DYJKERNEL_SOURCE
CFLAGS += -O3 -fno-omit-frame-pointer

ifeq ($(KDOOM),1)
CFLAGS       += -DYJKERNEL_ENABLE_KDOOM
CFLAGS_KDOOM  = -DYJKERNEL_ENABLE_KDOOM -std=c11
else
CFLAGS_KDOOM = 
endif

CFLAGS += $(ARCH_CFLAGS)

# -Wno-error=maybe-uninitialized is mainly for DOOM code. 
LINK_EXTRAFLAGS  = -Wno-error=maybe-uninitialized -Wno-error=stringop-overflow
LINK_EXTRAFLAGS += -nostdlib -T $(ARCHDIR)/$(ARCH_LDSCRIPT) -lgcc

all: $(TARGET)

clean:
	@rm -f $(OBJS) $(DEPS) $(TARGET)

cleandep:
	@rm -f $(DEPS)

prepare:
	@mkdir -p $(OBJDIRS)

# We need to override many diagnostic flags for DOOM, because it's using C code from late 90s.
# Most of them are declared inside KDOOM itself to only apply those to PureDOOM header file, but we still need few command-line flags.
# In addition to disabling warnings, it also disables UBSanitizer.
$(OBJDIR)/shell/kdoom/doom.o: shell/kdoom/doom.c
	$(info [Target C(DOOM)] $@)
	@$(CC) -o $@ -c -MMD $< $(CFLAGS_KDOOM)

$(OBJDIR)/$(FONTOBJ): $(FONTDIR)/$(FONTNAME)
	$(info [Target Font O]  $@)
	@cd $(FONTDIR) && \
		$(OBJCOPY) -O elf32-i386 -B i386 -I binary $(FONTNAME) $@

$(OBJDIR)/%.o: %.c
	$(info [Target C]       $@)
	@$(CC) -o $@ -c -MMD $< $(CFLAGS)

$(OBJDIR)/%.o: %.S
	$(info [Target ASM]     $@)
	@$(CC) -o $@ -c -MMD $< $(CFLAGS) -DYJKERNEL_ASMFILE

$(TARGET_NOSTRIP): prepare $(OBJS)
	$(info [Target Kernel]  $(TARGET_NOSTRIP))
	@$(CC) -o $(TARGET_NOSTRIP) $(OBJS) $(CFLAGS) $(LINK_EXTRAFLAGS)
	@$(TOOLCHAIN_BINDIR)/grub-file --is-x86-multiboot $(TARGET_NOSTRIP)

$(TARGET): $(TARGET_NOSTRIP)
	$(info [Target Strip]   $(TARGET))
	@cp $(TARGET_NOSTRIP) $(TARGET)
	@$(STRIP) $(TARGET)

-include $(DEPS)

.PHONY: all prepare clean cleandep
