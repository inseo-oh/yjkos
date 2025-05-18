#include "fs/fsinit.h"
#include "shell/shell.h"
#include "windowd.h"
#include <kernel/dev/pci.h>
#include <kernel/dev/ps2.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/co.h>
#include <kernel/io/disk.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/tasks/sched.h>
#include <kernel/version.h>
#include <stdalign.h>
#include <stdint.h>

[[noreturn]] void Kernel_Init(void) {
    Co_Printf("\nYJK Operating System " YJKOS_RELEASE "-" YJKOS_VERSION "\n");
    Co_Printf("Copyright (c) 2025 YJK(Oh Inseo)\n\n");
    Co_Printf("%zu mibytes allocatable memory\n", Pmm_GetTotalMem() / (1024 * 1024));

    Heap_Expand();
    FsInit_InitAll();
    Shell_Init();
    Sched_InitBootThread();
    Co_Printf("\n:: system is now listing PCI devices...\n");
    Pci_PrintBus();
    Co_Printf("\n:: system is now initializing PS/2 devices\n");
    Ps2_InitDevices();
    Co_Printf("\n\n\n:: HOLD DOWN 1 KEY RIGHT NOW TO SELECT VGA CONSOLE!!!!!!\n\n\n");
    Co_Printf("\n:: system is now initializing logical disks\n");
    Ldisk_Discover();
    Co_Printf("\n:: system is now mounting the root filesystem\n");
    Vfs_MountRoot();

    Windowd_Start();
    Co_AskPrimaryConsole();

    Co_Printf("\n :: system is ready for use. Use keyboard to type commands.\n");
    while (1) {
        Shell_Repl();
    }
}
