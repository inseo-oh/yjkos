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
#include <kernel/arch/thread.h>
#include <kernel/io/tty.h>
#include <kernel/lib/noreturn.h>
#include <kernel/kernel.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/panic.h>
#include <kernel/ticktime.h>
#include <kernel/trapmanager.h>
#include <kernel/types.h>
#include <stdint.h>

//------------------------------- Configuration -------------------------------

// Enable early VGA TTY? This should *ONLY* be enabled when debugging early
// boot process, system must boot into text mode. This may crash the system if
// it's booted into graphics mode.
// 
// Also note that serial debug takes precedence once it's initialized.
static bool const CONFIG_EARLY_VGATTY = false; 

// Enable serial debug?
static bool const CONFIG_SERIAL_DEBUG = true;

//-----------------------------------------------------------------------------

static struct archi586_serial s_serial0;
static bool s_serial0ready = false;

static void initserial(void) {
    int ret = archi586_serial_init(
        &s_serial0, 0x3f8, 115200, 4);
    if (ret < 0) {
        tty_printf(
            "failed to initialize serial0 (error %d)\n", ret);
        return;
    }
    ret = archi586_serial_config(&s_serial0, 115200);
    if (ret < 0) {
        tty_printf("failed to configure serial0 (error %d)\n", ret);
        return;
    }
    s_serial0.cr_to_crlf = true;
    tty_setdebugconsole(&s_serial0.stream);
    tty_printf("serial0 is ready\n");
    s_serial0ready = true;
}

NORETURN void archi586_kernelinit(uint32_t mbmagic, physptr mbinfoaddr) {
    if (CONFIG_EARLY_VGATTY) {
        archi586_vgatty_init_earlydebug();
    }
    if (CONFIG_SERIAL_DEBUG) {
        initserial();
    }

    archi586_mmu_init();
    archi586_mmu_writeprotect_kerneltext();
    // CR0.WP should've been enabled during early boot process, but if it isn't, the
    // CPU probably doesn't support the feature.
    if (!(archi586_readcr0() & (1 << 16))) {
        tty_printf(
            "warning: CR0.WP dowsn't seem to work. write-protect will not work in ring-0 Mode.\n");
    }

    archi586_gdt_init();
    archi586_idt_init();
    archi586_writeprotect_afterearlyinit();
    archi586_exceptions_init();
    archi586_gdt_load();
    archi586_gdt_reloadselectors();
    archi586_idt_load();
    // archi586_idt_test();

    if (mbmagic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("bad multiboot magic");
    }
    archi586_bootinfo_process(mbinfoaddr);
    archi586_pic_init();
    archi586_pit_init();

    tty_printf("enable interrupts...");
    arch_interrupts_enable();
    tty_printf("ok!\n");
    archi586_ps2ctrl_init();
    archi586_idebus_init();
    if (s_serial0ready) {
        archi586_serial_useirq(&s_serial0);
    }
    tty_printf("enter main kernel initialization\n");
    kernel_init();
}
