# YJK/OS

## so.. what the hell is this?

This is my 32-bit OS project, mostly targetting mid to late-90s x86 based PCs. I don't have real machines from the era to actually test with, but I do test my OS on both QEMU and 86Box. So it will *probably* work.

It currently doesn't do very much. You can run `hello` command to get `Hello, world` message.

## kdoom

kdoom(Kernel DOOM) is a kernel's built-in DOOM that uses [PureDOOM](https://github.com/Daivuk/PureDOOM) project. Its purpose is mostly for integration test for various kernel components, including memory management, timer, disk I/O and filesystem driver.

This means that playability is not the primary focus, so it runs on the glorious original resolution of 320x200 without scaling. And **there's no keyboard input support**.

kdoom is also disabled by default, and to enable it, you need to provide:
- A copy of `PureDOOM.h`. It's not included to avoid potential licence conflicts.
- WAD file for DOOM (either commercial or shareware)

Copy `PureDOOM.h` to `kernel/kdoom` directory, and your DOOM WAD file to `customrootfs` directory. The latter will be copied to HDD image when you run `makehd` utility(See [Building](#building)).

Then add `KDOOM=1` to arguments when running `make`. If you've ran `make` before, you may have to remove old files using `make clean`.
Finally, run `kdoom` at the command line once the system is booted.

## System requirements
 - PCI 2.0 or later
 - PCI IDE controller
 - Intel Pentium(original) or later
 - 16MiB of RAM(I may decrease this down to 8 in the future)
 - VESA compabible graphics card (NOTE: 256-color or less is not supported yet!)
 - Legacy BIOS (though if this isn't the case your machine is probably too new anyway)
 - **Ability to boot from CD-ROM**.

## Building

### Dependencies
- GNU Make(gmake) and Python3. 
- Whatever needed to build GCC and Binutils (See [OSDev Wiki's GCC Cross Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) article if you need help). You also need `libzstd` with development files(e.g. libzstd-dev package on Debian systems), as it's required for LTO linking.
- xorriso (Needed by grub-mkrescue)
- qemu-system-i386 and/or 86Box if you want to run it
  - NOTE: If you are running under WSL2, bundled script will find and use Windows QEMU, instead of Linux QEMU. (Set `YJK_PENGUINQEMU` environment variable to 1 to disable this behavior)
- sfdisk and kpartx (Needed to create the HDD image)

### Building
Run
```
> gmake
```
You can add `-j[number of jobs]` to arguments to speed up build process, depending on how much processor and memory you have.
Also note that this will also build cross compiler, and it will respect the `-j` option you passed. Cross compiler build can take anywhere from less than 5 minutes to multiple hours depending on host system, so it's *highly recommeded* to use `-j`, especially for initial build.

See `docs/CrossCompiler.md` for more advanced info.

**NOTE:** You will have to do full rebuild if you change compiler flags, by running `gmake clean` first.

And you also need a HDD image (Root permissions are required):
```
> sudo support/tools/makehd.py
```
If you want to also copy your own files to the HDD image, copy them to `customrootfs` before running above command.

To run the OS under QEMU:
```
> gmake run
```

- This will also build the OS before running, which includes relinking the kernel at very minimum that can take some time. You can run `support/tools/run.py` directly if you want to avoid rebuilding the OS. 
- For more advanced options when launching, see `docs/Debug.md` (These still apply if you direcly launch via `run.py` script)
- For 86Box config that this was tested on, see `docs/86BoxConfig.md`
