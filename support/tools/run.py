#!/usr/bin/env python3
# Runs the OS in QEMU

import subprocess
import os
import re
import sys

def getwindowsreg(key, name):
    result = subprocess.run(["reg.exe", "QUERY", key, "/v", name], capture_output=True)
    result.check_returncode()
    # Remember that Windows loves CRLF.
    for x in result.stdout.decode('utf-8').split('\r\n'):
        matchresult = re.match(f'^\s*{name}\s+REG_[A-Z_]+\s+(.*)', x)
        if matchresult:
            return matchresult[1]

def winpathtowsl(path):
    result = subprocess.run(["wslpath", "-a", path], capture_output=True)
    result.check_returncode()
    return result.stdout.decode('utf-8').splitlines()[0]

def wslpathtowin(path):
    result = subprocess.run(["wslpath", "-a", "-w", path], capture_output=True)
    result.check_returncode()
    return result.stdout.decode('utf-8').splitlines()[0]

unamerelease = os.uname().release
sys.stderr.write(f" * Kernel Release: {unamerelease}\n")
iswsl2 = unamerelease.find("WSL2") != -1

usegdbstub  = os.getenv('YJK_USE_GDBSTUB')  == '1' # Use GDB stub?
nokvm       = os.getenv('YJK_NO_KVM')       == '1' # No KVM?
tfaultdebug = os.getenv('YJK_TFAULT_DEBUG') == '1' # Triple-fault debug?
penguinqemu = os.getenv('YJK_PENGUINQEMU')  == '1' # Use Linux QEMU under WSL2?

cdrompath = "out/i586/YJKOS_i586.iso"
hddpath   = "harddisk.img"
qemuexe   = "qemu-system-i386"

qemuargs = [
    qemuexe,
    "-m", "64M",
    "-serial", "mon:stdio",
    # "-serial", "tcp:192.168.219.105:4000", # Don't worry it's a local IP address in network :D
    "-net", "none",
]

if usegdbstub:
    qemuargs.append("-s")

qemudebugflags = "guest_errors"
if tfaultdebug:
    qemudebugflags += ",int"
    nokvm = True
    qemuargs.append("-no-reboot")
qemuargs.append("-d")
qemuargs.append(qemudebugflags)

if iswsl2 and not penguinqemu:
    # As of QEMU 8.1.94, Windows QEMU doesn't like loading CD-ROM images from WSL side:
    # ERROR:../../../block.c:1699:bdrv_open_driver: assertion failed: (is_power_of_2(bs->bl.request_alignment))
    cdrompath = wslpathtowin(cdrompath)
    hddpath   = wslpathtowin(hddpath)

    sys.stderr.write(" * WSL2 detected. Using Windows QEMU...\n")
    qemuBaseDirWin = getwindowsreg("HKLM\\SOFTWARE\\QEMU", "Install_Dir")
    if qemuBaseDirWin == None:
        sys.stderr.write("WARNING: QEMU installation directory cannot be determined.\n")
        sys.stderr.write("WARNING: Assuming QEMU is in Windows PATH.\n")
        qemuBaseDir = ""
    else:
        sys.stderr.write(f" * Windows QEMU installation found: {qemuBaseDirWin}\n")
        qemuBaseDir = winpathtowsl(qemuBaseDirWin)
        sys.stderr.write(f" * UNIX-style path of the QEMU directory: {qemuBaseDir}\n")

    if usegdbstub:
        sys.stderr.write("NOTE: As of QEMU 8.1.94, WHPX accel doesn't support guest debugging\n")
        sys.stderr.write("NOTE: Disabling WHPX support\n")
        nokvm = True
    
    if not nokvm:
        qemuargs.append('-accel')
        # kernel-irqchip=off was added because otherwise it infinitely shows:
        # "whpx: injection failed, MSI (0, 0) delivery: 0, dest_mode: 0, trigger mode: 0, vector: 0, lost (c0350005)"
        #
        # This seems to happen around when setting up I/O APIC redirections, so it might be related to that.
        # (This only applies to modern 64-bit SMP platform, but that's what I was doing before switching to
        # older 32-bit platform)
        qemuargs.append('whpx,kernel-irqchip=off')

    qemuargs[0] = f"{qemuBaseDir}/{qemuexe}.exe"
else:
    if not nokvm:
        qemuargs.append('-enable-kvm')

qemuargs.append("-drive")
qemuargs.append(f"id=cd0,file={cdrompath},format=raw,if=none")
qemuargs.append("-device")
qemuargs.append("ide-cd,drive=cd0")

qemuargs.append("-drive")
qemuargs.append(f"id=dh0,file={hddpath},format=raw,if=none")
qemuargs.append("-device")
qemuargs.append("ide-hd,drive=dh0")

qemuargs.append("-boot")
qemuargs.append("d")

sys.stderr.write(f" * QEMU executable: {qemuargs[0]}\n")
sys.stderr.write(f" * CD-ROM image path: {cdrompath}\n")
sys.stderr.write(f" * QEMU flags: {qemuargs}\n")
subprocess.run(qemuargs).check_returncode()
