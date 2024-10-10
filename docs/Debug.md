## Two kernel binaries

Building YJK/OS will produce two kernel binaries: `yjkernel` and `yjkernel-nostrip`:
- The former is intended for use with actual system, with debug info stripped out. This reduces size of the kernel binary.
- Latter is kernel binary before it was stripped, so that it can be used with debugging tools.

## `make run` flags 

There are few environment variables that can be set(by setting them to 1) when launching, including:
- `YJK_PENGUINQEMU`: Runs Linux QEMU even under WSL2.
- `YJK_NO_KVM`: Disables KVM(Linux)/WHPX(Windows) acceleration
- `YJK_USE_GDBSTUB`: Enable QEMU's built-in GDB stub. **This will also disable WHPX on Windows**, as QEMU doesn't seem to support it last time I tested.
- `YJK_TFAULT_DEBUG`: This flag is designed to help debugging triple-fault bugs:
  - Disables KVM
  - Disables rebooting, to stop QEMU from rebooting on triple-fault.
  - Adds `int` debug flag to QEMU, which prints *every* exceptions and interrupts.

To connect GDB to the QEMU when launched with `YJK_USE_GDBSTUB`, run:
```
> ./support/tools/rungdb.sh
```
or to specify remote address:
```
> ./support/tools/rungdb.sh <remote address>
```

 - **NOTE:** Make sure toolchain's `bin` directory is in `$PATH`.
 -  **If you are on WSL2:** You **will** have to specify your Windows IP address as remote address, unless you are using Linux native QEMU. Under WSL2, `localhost` is only for the Linux side, not Windows.

## `support/tools/addr2line.sh`

It's wrapper for `addr2line` with executable name set to kernel binary. If you don't know what `addr2line` is, `man addr2line` will probably explain it to you better than what I could :D
