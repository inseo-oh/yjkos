#include "fs/fsinit.h"
#include "shell/shell.h"
#include <kernel/arch/tsc.h>
#include <kernel/dev/pci.h>
#include <kernel/dev/ps2.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/co.h>
#include <kernel/lib/noreturn.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/raster/fb.h>
#include <kernel/tasks/sched.h>
#include <kernel/ticktime.h>
#include <kernel/version.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

NORETURN void kernel_init(void) {
    co_printf(
        "\nYJK Operating System " YJKOS_RELEASE "-" YJKOS_VERSION "\n");
    co_printf("Copyright (C) 2024 YJK(Oh Inseo)\n");
    co_printf(
        "%zu mibytes allocatable memory\n",
        pmm_get_totalmem() / (1024 * 1024));
    
    heap_expand();
    fsinit_init_all();
    shell_init();
    sched_initbootthread();
    co_printf("\n:: system is now listing PCI devices...\n");
    pci_printbus();
    co_printf("\n:: system is now initializing PS/2 devices\n");
    ps2_initdevices();
    co_printf("\n:: system is now initializing logical disks\n");
    ldisk_discover();
    co_printf("\n:: system is now mounting the root filesystem\n");
    vfs_mountroot();

    co_printf(
        "\n :: system is ready for use. Use keyboard to type commands.\n");
    while(1) {
        shell_repl();
    }
}

