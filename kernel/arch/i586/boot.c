#include "asm/i586.h"
#include "bootinfo.h"
#include "dev/idebus.h"
#include "dev/ps2ctrl.h"
#include "exceptions.h"
#include "gdt.h"
#include "idt.h"
#include "mmu_ext.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"
#include "thirdparty/multiboot.h"
#include "vgatty.h"
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/kernel.h>
#include <kernel/lib/noreturn.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <stdint.h>

//------------------------------- Configuration -------------------------------

// Enable early VGA TTY? This should *ONLY* be enabled when debugging early boot process, system must boot into text mode.
// This may crash the system if it's booted into graphics mode.
//
// Also note that serial debug takes precedence once it's initialized.
static bool const CONFIG_EARLY_VGATTY = false;

// Enable serial debug?
static bool const CONFIG_SERIAL_DEBUG = true;

//-----------------------------------------------------------------------------

static struct archi586_serial s_serial0, s_serial1;
static bool s_serial0_ready = false;

static void init_serial0(void) {
    int ret = archi586_serial_init(&s_serial0, 0x3f8, 115200, 4);
    if (ret < 0) {
        co_printf("failed to initialize serial0 (error %d)\n", ret);
        return;
    }
    ret = archi586_serial_config(&s_serial0, 115200);
    if (ret < 0) {
        co_printf("failed to configure serial0 (error %d)\n", ret);
        return;
    }
    s_serial0.cr_to_crlf = true;
    co_setdebug(&s_serial0.tty.stream);
    co_printf("serial0 is ready\n");
    s_serial0_ready = true;
}
static void init_serial1(void) {
    int ret = archi586_serial_init(&s_serial1, 0x2f8, 115200, 3);
    if (ret < 0) {
        co_printf("failed to initialize serial1 (error %d)\n", ret);
        return;
    }
    ret = archi586_serial_config(&s_serial1, 115200);
    if (ret < 0) {
        co_printf("failed to configure serial1 (error %d)\n", ret);
        return;
    }
    s_serial1.cr_to_crlf = false;
    archi586_serial_useirq(&s_serial1);
    ret = archi586_serial_initiodev(&s_serial1);
    if (ret < 0) {
        co_printf("failed to register serial1 (error %d)\n", ret);
        return;
    }
    co_printf("serial1 is ready\n");
}

NORETURN void archi586_kernelinit(uint32_t mb_magic, PHYSPTR mb_info_addr) {
    if (CONFIG_EARLY_VGATTY) {
        archi586_vgatty_init_earlydebug();
    }
    if (CONFIG_SERIAL_DEBUG) {
        init_serial0();
    }

    archi586_mmu_init();
    archi586_mmu_write_protect_kernel_text();
    // CR0.WP should've been enabled during early boot process, but if it isn't, the CPU probably doesn't support the feature.
    if (!(archi586_read_cr0() & (1U << 16))) {
        co_printf("warning: CR0.WP dowsn't seem to work. write-protect will not work in ring-0 Mode.\n");
    }

    archi586_gdt_init();
    archi586_idt_init();
    archi586_mmu_write_protect_after_early_init();
    archi586_exceptions_init();
    archi586_gdt_load();
    archi586_gdt_reload_selectors();
    archi586_idt_load();
    // archi586_idt_test();

    if (mb_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("bad multiboot magic");
    }
    archi586_bootinfo_process(mb_info_addr);
    archi586_pic_init();
    archi586_pit_init();

    arch_interrupts_enable();
    archi586_ps2ctrl_init();
    archi586_idebus_init();
    if (s_serial0_ready) {
        archi586_serial_useirq(&s_serial0);
    }
    init_serial1();
    co_printf("enter main kernel initialization\n");
    kernel_init();
}
