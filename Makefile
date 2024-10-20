ARCH            ?= i586
OUTDIR           = out/$(ARCH)
BOOTROOT         = $(OUTDIR)/bootroot
ISO_NAME         = $(OUTDIR)/YJKOS_$(ARCH).iso
FONTDIR          = $(OUTDIR)/kernelfont
FONT             = ter-114b.psf
TOOLCHAIN_BINDIR = toolchain/bin

all: iso

prepare:
	@MAKEJOBS=$(MAKEJOBS) support/tools/buildtoolchain.sh $(ARCH)
	@mkdir -p $(BOOTROOT) $(BOOTROOT)/boot $(BOOTROOT)/boot/grub
	@cp support/res/grub.cfg $(BOOTROOT)/boot/grub/
	@./support/tools/genversionfile.sh > include/kernel/version.h

# NOTE: We reconfigure the terminus-font after building, to prevent 
#       accidentally commiting with someone's home folder path left in Makefile.
kernelfont: prepare
	$(info [Target Font]     $(FONT))
	@cd support/thirdparty/terminus-font-4.49.1 && ./configure --psfdir=$(CURDIR)/$(FONTDIR)
	@$(MAKE) -C support/thirdparty/terminus-font-4.49.1 psf
	@$(MAKE) -C support/thirdparty/terminus-font-4.49.1 install-psf
	@cd support/thirdparty/terminus-font-4.49.1 && ./configure
	@gunzip -c $(FONTDIR)/$(FONT).gz > $(FONTDIR)/kernelfont.psf

kernel: prepare kernelfont
	@$(MAKE) -C kernel ARCH=$(ARCH) KDOOM=$(KDOOM)

iso: prepare kernel
	$(info [Target ISO]     $(ISO_NAME))
	$(TOOLCHAIN_BINDIR)/grub-mkrescue -o $(ISO_NAME) $(BOOTROOT)

clean:
	@$(MAKE) -C support/thirdparty/terminus-font-4.49.1 clean
	@$(MAKE) -C kernel ARCH=$(ARCH) clean
	@rm -rf out

run: all
	@support/tools/run.py

.PHONY: all clean kernel kernelfont iso