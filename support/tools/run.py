#!/usr/bin/env python3
# Runs the OS in QEMU

import subprocess
import os
import re
import sys

def get_win_reg(key, name):
    result = subprocess.run(["reg.exe", "QUERY", key, "/v", name], capture_output=True)
    result.check_returncode()
    # Remember that Windows loves CRLF.
    for x in result.stdout.decode('utf-8').split('\r\n'):
        matchresult = re.match(f'^\s*{name}\s+REG_[A-Z_]+\s+(.*)', x)
        if matchresult:
            return matchresult[1]

def win_path_to_wsl(path):
    result = subprocess.run(["wslpath", "-a", path], capture_output=True)
    result.check_returncode()
    return result.stdout.decode('utf-8').splitlines()[0]

def wsl_path_to_win(path):
    result = subprocess.run(["wslpath", "-a", "-w", path], capture_output=True)
    result.check_returncode()
    return result.stdout.decode('utf-8').splitlines()[0]

uname_release = os.uname().release
sys.stderr.write(f" * Kernel Release: {uname_release}\n")
is_wsl2 = uname_release.find("WSL2") != -1

use_gdb_stub  = os.getenv('YJK_USE_GDBSTUB')  == '1' # Use GDB stub?
no_kvm        = os.getenv('YJK_NO_KVM')       == '1' # No KVM?
tfault_debug  = os.getenv('YJK_TFAULT_DEBUG') == '1' # Triple-fault debug?
penguin_qemu  = os.getenv('YJK_PENGUINQEMU')  == '1' # Use Linux QEMU under WSL2?
qemu_add_args = os.getenv('YJK_QEMUFLAGS')           # Additional QEMU flags

cdrompath = "out/i586/YJKOS_i586.iso"
hddpath   = "harddisk.img"
qemuexe   = "qemu-system-i386"

qemu_args = [
    qemuexe,
    "-m", "64M",
    "-serial", "mon:stdio",
    "-serial", "tcp:0.0.0.0:6001,server,nowait",
    "-net", "none",
]

if qemu_add_args != None:
    for arg in qemu_add_args.split(' '):
        qemu_args.append(arg)

if use_gdb_stub:
    qemu_args.append("-s")

qemu_debug_flags = "guest_errors"
if tfault_debug:
    qemu_debug_flags += ",int"
    no_kvm = True
    qemu_args.append("-no-reboot")
qemu_args.append("-d")
qemu_args.append(qemu_debug_flags)

if is_wsl2 and not penguin_qemu:
    # As of QEMU 8.1.94, Windows QEMU doesn't like loading CD-ROM images from WSL side:
    # ERROR:../../../block.c:1699:bdrv_open_driver: assertion failed: (is_power_of_2(bs->bl.request_alignment))
    cdrompath = wsl_path_to_win(cdrompath)
    hddpath   = wsl_path_to_win(hddpath)

    sys.stderr.write(" * WSL2 detected. Using Windows QEMU...\n")
    qemuBaseDirWin = get_win_reg("HKLM\\SOFTWARE\\QEMU", "Install_Dir")
    if qemuBaseDirWin == None:
        sys.stderr.write("WARNING: QEMU installation directory cannot be determined.\n")
        sys.stderr.write("WARNING: Assuming QEMU is in Windows PATH.\n")
        qemuBaseDir = ""
    else:
        sys.stderr.write(f" * Windows QEMU installation found: {qemuBaseDirWin}\n")
        qemuBaseDir = win_path_to_wsl(qemuBaseDirWin)
        sys.stderr.write(f" * UNIX-style path of the QEMU directory: {qemuBaseDir}\n")

    if use_gdb_stub:
        sys.stderr.write("NOTE: As of QEMU 8.1.94, WHPX accel doesn't support guest debugging\n")
        sys.stderr.write("NOTE: Disabling WHPX support\n")
        no_kvm = True
    
    if not no_kvm:
        qemu_args.append('-accel')
        # kernel-irqchip=off was added because otherwise it infinitely shows:
        # "whpx: injection failed, MSI (0, 0) delivery: 0, dest_mode: 0, trigger mode: 0, vector: 0, lost (c0350005)"
        #
        # This seems to happen around when setting up I/O APIC redirections, so it might be related to that.
        # (This only applies to modern 64-bit SMP platform, but that's what I was doing before switching to older 32-bit platform)
        qemu_args.append('whpx,kernel-irqchip=off')

    qemu_args[0] = f"{qemuBaseDir}/{qemuexe}.exe"
else:
    if not no_kvm:
        qemu_args.append('-enable-kvm')

qemu_args.append("-drive")
qemu_args.append(f"id=cd0,file={cdrompath},format=raw,if=none")
qemu_args.append("-device")
qemu_args.append("ide-cd,drive=cd0")

qemu_args.append("-drive")
qemu_args.append(f"id=dh0,file={hddpath},format=raw,if=none")
qemu_args.append("-device")
qemu_args.append("ide-hd,drive=dh0")

qemu_args.append("-boot")
qemu_args.append("d")

sys.stderr.write(f" * QEMU executable: {qemu_args[0]}\n")
sys.stderr.write(f" * CD-ROM image path: {cdrompath}\n")
sys.stderr.write(f" * QEMU flags: {qemu_args}\n")
subprocess.run(qemu_args).check_returncode()
