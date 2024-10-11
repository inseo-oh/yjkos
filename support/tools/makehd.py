#!/usr/bin/env python3
# Creates hard drive image

import subprocess
import sys
import os
import shutil

def Which(program):
    result = shutil.which(program)
    if result == None:
        raise Exception(f"{program} doesn't appear to be installed\n")
    return result

HD_SECTOR_SIZE  = 512
HD_SIZE_IN_MIB  = 2048
HD_PATH         = "harddisk.img"
HD_SECTOR_COUNT = (HD_SIZE_IN_MIB * 1024 * 1024) // HD_SECTOR_SIZE

MOUNT_DIR       = "support/rootfsmount"

try:
    stat = os.stat(HD_PATH)
    if stat.st_size != HD_SECTOR_COUNT * 512:
        sys.stderr.write(f"WARNING: Hard disk image {HD_PATH} exists and its size is different from HD size we are going to create:\n")
        sys.stderr.write(f" - Old size: {stat.st_size}\n")
        sys.stderr.write(f" - New size: {HD_SECTOR_COUNT * HD_SECTOR_SIZE}\n")
        sys.stderr.write(f"Certain emulators save disk parameters along with disk image path, and may cause problems if disk size suddenly changes\n")
        sys.stderr.write(f"Type y and press enter to continue, or Ctrl+C to cancel: ")
        sys.stderr.flush()
        while sys.stdin.read(1) != 'y':
            pass
except FileNotFoundError:
    pass
except KeyboardInterrupt:
    sys.stderr.write("\nCanceled\n")
    sys.exit(1)

sys.stderr.write("[makehd] Create empty disk\n")
subprocess.run(["dd", "if=/dev/zero", f"of={HD_PATH}", "bs=1M", f"count={HD_SIZE_IN_MIB}", "status=progress"]).check_returncode()

sys.stderr.write("[makehd] Prepare loopback disk\n")
losetupResult = subprocess.run([Which("losetup"), "-f", "--show", HD_PATH], stdout=subprocess.PIPE)
losetupResult.check_returncode()
loopDevice = losetupResult.stdout.decode('utf-8').split('\n')[0]

sys.stderr.write(f"[makehd] Loopback device {loopDevice}\n")
try:
    sys.stderr.write("[makehd] Partition the disk\n")
    sfdisk = subprocess.Popen([Which("sfdisk"), loopDevice], stdin=subprocess.PIPE, encoding="utf-8")
    sfdisk.communicate(f"""
    label: dos
    1 {HD_SECTOR_COUNT - 1} L -
    """)
    sys.stderr.write("[makehd] Add partition mappings\n")
    kpartxResult = subprocess.run([Which("kpartx"), "-l", loopDevice], stdout=subprocess.PIPE)
    kpartxResult.check_returncode()
    partitionDevices = []
    for line in kpartxResult.stdout.decode('utf-8').split('\n'):
        if line.strip() == "":
            continue
        partitionDevices.append(f"/dev/mapper/{line.split(':')[0].strip()}")
    print(partitionDevices)
    subprocess.run([Which("kpartx"), "-as", loopDevice], stdout=subprocess.PIPE).check_returncode()

    partitionDevice = partitionDevices[0]
    sys.stderr.write("[makehd] Format the partition\n")
    subprocess.run([Which("mkfs.ext2"), partitionDevice]).check_returncode()
    sys.stderr.write("[makehd] Make mount directory\n")
    os.makedirs(MOUNT_DIR, exist_ok=True)
    sys.stderr.write("[makehd] Print blkid\n")
    subprocess.run([Which("blkid"), partitionDevice]).check_returncode()
    sys.stderr.write("[makehd] Mount the disk\n")
    subprocess.run([Which("mount"), "-t", "ext2", partitionDevice, MOUNT_DIR]).check_returncode()
    customrootfsexist = False
    try:
        os.listdir('customrootfs')
        customrootfsexist = True
    except FileNotFoundError:
        pass
    if customrootfsexist:
        sys.stderr.write("[makehd] Copy customrootfs\n")
        subprocess.run([Which("rsync"), "-arv", f"customrootfs/", MOUNT_DIR]).check_returncode()
    sys.stderr.write("[makehd] Print df\n")
    subprocess.run([Which("df"), "-h", partitionDevice]).check_returncode()
finally:
    sys.stderr.write("[makehd] Unmount the disk\n")
    subprocess.run([Which("umount"), MOUNT_DIR])
    sys.stderr.write("[makehd] Remove partition mappings\n")
    subprocess.run([Which("kpartx"), "-d", loopDevice])
    sys.stderr.write("[makehd] Detach loopback disk\n")
    subprocess.run(["losetup", "-d", loopDevice])
sys.stderr.write("[makehd] Done\n")