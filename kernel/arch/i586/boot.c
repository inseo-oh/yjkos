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
#include <kernel/panic.h>
#include <kernel/types.h>
#include <stdint.h>

/******************************** Configuration *******************************/

/*
 * Enable early VGA TTY? This should *ONLY* be enabled when debugging early boot process, system must boot into text mode.
 * This may crash the system if it's booted into graphics mode.
 *
 * Also note that serial debug takes precedence once it's initialized.
 */
static bool const CONFIG_EARLY_VGATTY = false;

/* Enable serial debug? */
static bool const CONFIG_SERIAL_DEBUG = true;

/******************************************************************************/

static struct ArchI586_Serial s_serial0, s_serial1;
static bool s_serial0_ready = false;

static void init_serial0(void) {
    int ret = ArchI586_Serial_Init(&s_serial0, 0x3f8, 115200, 4);
    if (ret < 0) {
        Co_Printf("failed to initialize serial0 (error %d)\n", ret);
        return;
    }
    ret = ArchI586_Serial_Config(&s_serial0, 115200);
    if (ret < 0) {
        Co_Printf("failed to configure serial0 (error %d)\n", ret);
        return;
    }
    s_serial0.cr_to_crlf = true;
    Co_SetDebugConsole(&s_serial0.tty.stream);
    Co_Printf("serial0 is ready\n");
    s_serial0_ready = true;
}
static void init_serial1(void) {
    int ret = ArchI586_Serial_Init(&s_serial1, 0x2f8, 115200, 3);
    if (ret < 0) {
        Co_Printf("failed to initialize serial1 (error %d)\n", ret);
        return;
    }
    ret = ArchI586_Serial_Config(&s_serial1, 115200);
    if (ret < 0) {
        Co_Printf("failed to configure serial1 (error %d)\n", ret);
        return;
    }
    s_serial1.cr_to_crlf = false;
    ArchI586_Serial_UseIrq(&s_serial1);
    ret = ArchI586_Serial_InitIoDev(&s_serial1);
    if (ret < 0) {
        Co_Printf("failed to register serial1 (error %d)\n", ret);
        return;
    }
    Co_Printf("serial1 is ready\n");
}

[[noreturn]] void ArchI586_Init(uint32_t mb_magic, PHYSPTR mb_info_addr) {
    if (CONFIG_EARLY_VGATTY) {
        ArchI586_VgaTty_InitEarlyDebug();
    }
    if (CONFIG_SERIAL_DEBUG) {
        init_serial0();
    }
    Co_Printf("TO USE VGA CONSOLE SMASH 1 RIGHT NOW\n");

    ArchI586_Mmu_Init();
    ArchI586_Mmu_WriteProtectKernelText();
    /* CR0.WP should've been enabled during early boot process, but if it isn't, the CPU probably doesn't support the feature. */
    if (!(ArchI586_ReadCr0() & (1U << 16))) {
        Co_Printf("warning: CR0.WP dowsn't seem to work. write-protect will not work in ring-0 Mode.\n");
    }

    ArchI586_Gdt_Init();
    ArchI586_Idt_Init();
    ArchI586_Mmu_WriteProtectAfterEarlyInit();
    ArchI586_Exceptions_Init();
    ArchI586_Gdt_Load();
    ArchI586_Gdt_ReloadSelectors();
    ArchI586_Idt_Load();
    /* ArchI586_Idt_Test(); */

    if (mb_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        Panic("bad multiboot magic");
    }
    ArchI586_BootInfo_Process(mb_info_addr);
    ArchI586_Pic_Init();
    ArchI586_Pit_Init();

    Arch_Irq_Enable();
    ArchI586_Ps2Ctrl_Init();
    ArchI586_IdeBus_Init();
    if (s_serial0_ready) {
        ArchI586_Serial_UseIrq(&s_serial0);
    }
    init_serial1();
    Co_Printf("enter main kernel initialization\n");

    Kernel_Init();
}
