ARCH     ?= x86
OUTDIR    = out/$(ARCH)
BOOTROOT  = $(OUTDIR)/bootroot
ISO_NAME  = $(OUTDIR)/YJKOS_$(ARCH).iso
FONTDIR   = $(OUTDIR)/kernelfont
FONT      = ter-112n.psf

all: iso

prepare:
	@mkdir -p $(BOOTROOT) $(BOOTROOT)/boot $(BOOTROOT)/boot/grub
	@cp support/res/grub.cfg $(BOOTROOT)/boot/grub/

kernelfont: prepare
	@cd support/thirdparty/terminus-font-4.49.1 && ./configure --psfdir=$(CURDIR)/$(FONTDIR)
	@$(MAKE) -C support/thirdparty/terminus-font-4.49.1 psf
	@$(MAKE) -C support/thirdparty/terminus-font-4.49.1 install-psf
	gunzip -c $(FONTDIR)/$(FONT).gz > $(FONTDIR)/kernelfont.psf

kernel: prepare kernelfont
	@$(MAKE) -C kernel ARCH=$(ARCH) KDOOM=$(KDOOM)

iso: prepare kernel
	$(info [Target ISO]     $(ISO_NAME))
	@grub-mkrescue -o $(ISO_NAME) $(BOOTROOT)

clean:
	make -C support/thirdparty/terminus-font-4.49.1 clean
	@$(MAKE) -C kernel ARCH=$(ARCH) clean
	@rm -rf out

run: all
	@support/tools/run.py

.PHONY: all clean kernel iso