#pragma once
#include <kernel/status.h>
#include <stdint.h>
#include <stdbool.h>

// PCI path consists of Bus, Device, and Function number.
typedef uint16_t pcipath;

pcipath pci_makepath(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pcipath_getbus(pcipath path);
uint8_t pcipath_getdev(pcipath path);
uint8_t pcipath_getfunc(pcipath path);

static uint16_t const PCI_CMDFLAG_INTERRUPT_DISABLE              = 1 << 10;
static uint16_t const PCI_CMDFLAG_FAST_BACK_TO_BACK              = 1 << 9;
static uint16_t const PCI_CMDFLAG_ENABLE_SERR                    = 1 << 8;
static uint16_t const PCI_CMDFLAG_PARITY_ERROR_RESPONSE          = 1 << 6;
static uint16_t const PCI_CMDFLAG_VGA_PALETTE_SNOOP              = 1 << 5;
static uint16_t const PCI_CMDFLAG_ENABLE_MEMORY_WRITE_AND_ENABLE = 1 << 4;
static uint16_t const PCI_CMDFLAG_SPECIAL_CYCLES                 = 1 << 3;
static uint16_t const PCI_CMDFLAG_BUS_MASTER                     = 1 << 2;
static uint16_t const PCI_CMDFLAG_MEMORY_SPACE                   = 1 << 1;
static uint16_t const PCI_CMDFLAG_IO_SPACE                       = 1 << 0;

static uint16_t const PCI_STATUSFLAG_INTERRUPT                 = 1 << 3;
static uint16_t const PCI_STATUSFLAG_CAPABILITIES_LIST         = 1 << 4;
static uint16_t const PCI_STATUSFLAG_66MHZ_CAPABLE             = 1 << 5;
static uint16_t const PCI_STATUSFLAG_FAST_BACK_TO_BACK_CAPABLE = 1 << 7;
static uint16_t const PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR  = 1 << 8;
static uint16_t const PCI_STATUSFLAG_DEVSELTIMING_MASK        = 0x3 << 9;
static uint16_t const PCI_STATUSFLAG_DEVSELTIMING_FAST        = 0x0 << 9;
static uint16_t const PCI_STATUSFLAG_DEVSELTIMING_MEDIUM      = 0x1 << 9;
static uint16_t const PCI_STATUSFLAG_DEVSELTIMING_SLOW        = 0x2 << 9;
static uint16_t const PCI_STATUSFLAG_SIGNALED_TARGET_ABORT     = 1 << 11;
static uint16_t const PCI_STATUSFLAG_RECEIVED_TARGET_ABORT     = 1 << 12;
static uint16_t const PCI_STATUSFLAG_RECEIVED_MASTER_ABORT     = 1 << 13;
static uint16_t const PCI_STATUSFLAG_SIGNALED_SYSTEM_ERROR     = 1 << 14;
static uint16_t const PCI_STATUSFLAG_DETECTED_PARITY_ERROR     = 1 << 15;

void pci_probebus(
    void (*callback)(pcipath path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data),
    void *data
);
void pci_readvendevid(uint16_t *venid_out, uint16_t *devid_out, pcipath path);
void pci_readclass(uint8_t *baseclass_out, uint8_t *subclass_out, pcipath path);
uint8_t pci_readconfigheadertype(pcipath path);
uint8_t pci_readprogif(pcipath path);
void pci_writeprogif(pcipath path, uint8_t progif);
uint8_t pci_readinterruptline(pcipath path);
uint16_t pci_readcmdreg(pcipath path);
void pci_writecmdreg(pcipath path, uint16_t value);
uint16_t pci_readstatusreg(pcipath path);
// Note that writing 1 to status register bit clears that flag(unless it's R/O flag) 
void pci_writestatusreg(pcipath path, uint16_t value);
void pci_printf(pcipath path, char const *fmt, ...);
// NOTE: Prefetchable is not applicable if it's I/O BAR.
FAILABLE_FUNCTION pci_readbar(uintptr_t *addr_out, bool *isiobar_out, bool *isprefetchable_out, pcipath path, uint8_t bar);
FAILABLE_FUNCTION pci_readmembar(uintptr_t *addr_out, bool *isprefetchable_out, pcipath path, uint8_t bar);
FAILABLE_FUNCTION pci_readiobar(uintptr_t *addr_out, pcipath path, uint8_t bar);
void pci_printbus(void);
