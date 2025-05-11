#pragma once
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>
#include <stdint.h>

// PCI path consists of Bus, Device, and Function number.
typedef uint16_t PCIPATH;

PCIPATH pci_makepath(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pcipath_getbus(PCIPATH path);
uint8_t pcipath_getdev(PCIPATH path);
uint8_t pcipath_getfunc(PCIPATH path);

#define PCI_CMDFLAG_INTERRUPT_DISABLE               (1U << 10)
#define PCI_CMDFLAG_FAST_BACK_TO_BACK               (1U << 9)
#define PCI_CMDFLAG_ENABLE_SERR                     (1U << 8)
#define PCI_CMDFLAG_PARITY_ERROR_RESPONSE           (1U << 6)
#define PCI_CMDFLAG_VGA_PALETTE_SNOOP               (1U << 5)
#define PCI_CMDFLAG_ENABLE_MEMORY_WRITE_AND_ENABLE  (1U << 4)
#define PCI_CMDFLAG_SPECIAL_CYCLES                  (1U << 3)
#define PCI_CMDFLAG_BUS_MASTER                      (1U << 2)
#define PCI_CMDFLAG_MEMORY_SPACE                    (1U << 1)
#define PCI_CMDFLAG_IO_SPACE                        (1U << 0)

#define PCI_STATUSFLAG_INTERRUPT                 (1U << 3)
#define PCI_STATUSFLAG_CAPABILITIES_LIST         (1U << 4)
#define PCI_STATUSFLAG_66MHZ_CAPABLE             (1U << 5)
#define PCI_STATUSFLAG_FAST_BACK_TO_BACK_CAPABLE (1U << 7)
#define PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR  (1U << 8)
#define PCI_STATUSFLAG_DEVSELTIMING_MASK         (0x3U << 9)
#define PCI_STATUSFLAG_DEVSELTIMING_FAST         (0x0U << 9)
#define PCI_STATUSFLAG_DEVSELTIMING_MEDIUM       (0x1U << 9)
#define PCI_STATUSFLAG_DEVSELTIMING_SLOW         (0x2U << 9)
#define PCI_STATUSFLAG_SIGNALED_TARGET_ABORT     (1U << 11)
#define PCI_STATUSFLAG_RECEIVED_TARGET_ABORT     (1U << 12)
#define PCI_STATUSFLAG_RECEIVED_MASTER_ABORT     (1U << 13)
#define PCI_STATUSFLAG_SIGNALED_SYSTEM_ERROR     (1U << 14)
#define PCI_STATUSFLAG_DETECTED_PARITY_ERROR     (1U << 15)

void pci_probe_bus(
    void (*callback)(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data),
    void *data
);
void pci_readvendevid(uint16_t *venid_out, uint16_t *devid_out, PCIPATH path);
void pci_readclass(uint8_t *baseclass_out, uint8_t *subclass_out, PCIPATH path);
uint8_t pci_read_config_header_type(PCIPATH path);
uint8_t pci_readprogif(PCIPATH path);
void pci_writeprogif(PCIPATH path, uint8_t progif);
uint8_t pci_readinterruptline(PCIPATH path);
uint16_t pci_readcmdreg(PCIPATH path);
void pci_writecmdreg(PCIPATH path, uint16_t value);
uint16_t pci_readstatusreg(PCIPATH path);
// Note that writing 1 to status register bit clears that flag (If it's writable)
void pci_writestatusreg(PCIPATH path, uint16_t value);
void pci_printf(PCIPATH path, char const *fmt, ...);
// NOTE: Prefetchable is not applicable if it's I/O BAR.
NODISCARD int pci_readbar( uintptr_t *addr_out, bool *isiobar_out, bool *isprefetchable_out, PCIPATH path, uint8_t bar);
NODISCARD int pci_read_mem_bar(uintptr_t *addr_out, bool *isprefetchable_out, PCIPATH path, int bar);
NODISCARD int pci_read_io_bar(uintptr_t *addr_out, PCIPATH path, int bar);
void pci_printbus(void);
