#include "fs/fsinit.h"
#include "kernel/kobject.h"
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

struct kobject_ops test_ops;

[[noreturn]] void kernel_init(void) {
    co_printf("\nYJK Operating System " YJKOS_RELEASE "-" YJKOS_VERSION "\n");
    co_printf("Copyright (c) 2025 YJK(Oh Inseo)\n\n");
    co_printf("%zu mibytes allocatable memory\n", pmm_get_total_mem_size() / (1024 * 1024));

    heap_expand();
    fsinit_init_all();
    shell_init();
    sched_init_boot_thread();
    co_printf("\n:: system is now listing PCI devices...\n");
    pci_print_bus();
    co_printf("\n:: system is now initializing PS/2 devices\n");
    ps2_init_devices();
    co_printf("\n\n\n:: HOLD DOWN 1 KEY RIGHT NOW TO SELECT VGA CONSOLE!!!!!!\n\n\n");
    co_printf("\n:: system is now initializing logical disks\n");
    ldisk_discover();
    co_printf("\n:: system is now mounting the root filesystem\n");
    vfs_mount_root();

    windowd_start();
    co_ask_primary_console();

    [[maybe_unused]] int ret_unused;
    struct kobject *root, *new1, *new2, *new3_auto;
    ret_unused = kobject_create(&root, "root_object", 0, &test_ops);
    co_printf("object created [%s]\n", kobject_get_id(root));
    ret_unused = kobject_create(&new1, "new_object_1", 0, &test_ops);
    ret_unused = kobject_create(&new2, "new_object_2", 0, &test_ops);
    ret_unused = kobject_set_parent(new1, root);
    ret_unused = kobject_set_parent(new2, root);
    /* Add objects with auto-generated IDs */
    for (int i = 0; i < 100; i++) {
        ret_unused = kobject_create(&new3_auto, NULL, 0, &test_ops);
        ret_unused = kobject_set_parent(new3_auto, new2);
    }

    kobject_print_tree(root);

    co_printf("\n :: system is ready for use. Use keyboard to type commands.\n");
    while (1) {
        shell_repl();
    }
}
