#pragma once
#include <kernel/io/disk.h>
#include <kernel/status.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ACS-3 6.2 Status field
static uint8_t const ATA_STATUSFLAG_ERR = 1 << 0;
static uint8_t const ATA_STATUSFLAG_DRQ = 1 << 3;
static uint8_t const ATA_STATUSFLAG_DF  = 1 << 5;
static uint8_t const ATA_STATUSFLAG_RDY = 1 << 6;
static uint8_t const ATA_STATUSFLAG_BSY = 1 << 7;

// ACS-3 6.3 ERROR field
enum ata_cmd {
    ATA_CMD_FLUSHCACHE     = 0xe7, // ACS-3 7.10
    ATA_CMD_IDENTIFYDEVICE = 0xec, // ACS-3 7.12
    ATA_CMD_READDMA        = 0xc8, // ACS-3 7.21
    ATA_CMD_READSECTORS    = 0x20, // ACS-3 7.28
    ATA_CMD_WRITEDMA       = 0xca, // ACS-3 7.58
    ATA_CMD_WRITESECTORS   = 0x30, // ACS-3 7.67
};

enum {
    ATA_MAX_SECTORS_PER_TRANSFER = 256, // Maximum sector count for a 28-bit transfer command.
    ATA_SECTOR_SIZE              = 512,
};

struct ata_databuf {
    uint16_t data[256];
};

enum ata_dmastatus {
    ATA_DMASTATUS_FAIL_UDMA_CRC,
    ATA_DMASTATUS_FAIL_OTHER_IO,
    ATA_DMASTATUS_FAIL_NOMEM,
    ATA_DMASTATUS_SUCCESS,
    ATA_DMASTATUS_BUSY,
};

struct atadisk;
struct atadisk_ops {
    bool (*dma_beginsession)(struct atadisk *self);
    void (*dma_endsession)(struct atadisk *self);
    void (*lock)(struct atadisk *self);
    void (*unlock)(struct atadisk *self);
    uint8_t (*readstatus)(struct atadisk *self);
    FAILABLE_FUNCTION (*selectdisk)(struct atadisk *self);
    void (*setfeaturesparam)(struct atadisk *self, uint16_t data);
    void (*setcountparam)(struct atadisk *self, uint16_t data);
    void (*setlbaparam)(struct atadisk *self, uint32_t data);
    void (*setdeviceparam)(struct atadisk *self, uint8_t data);
    uint32_t (*getlbaoutput)(struct atadisk *self);
    void (*issuecmd)(struct atadisk *self, enum ata_cmd cmd);
    bool (*getirqflag)(struct atadisk *self);
    void (*clearirqflag)(struct atadisk *self);
    void (*readdata)(struct ata_databuf *out, struct atadisk *self);
    void (*writedata)(struct atadisk *self, struct ata_databuf *buffer);
    void (*softreset)(struct atadisk *self);

    // DMA API
    // The order of operation is:
    // 1. [DMA] Initialize DMA Transfer
    // 2. [ATA] Issue the corresponding ATA command
    // 3. [DMA] Begin DMA Transfer
    // 4. [ATA] Wait. When IRQ is received, check DMA status, and stop waiting when finished
    // 5. [DMA] End DMA transfer
    // 6. [DMA] Deinitialize DMA transfer
    // (Step 5 and 6 are separate, so that DMA can be deinitialized safely if something fails between
    // Step 1 and Step 3)
    FAILABLE_FUNCTION (*dma_inittransfer)(struct atadisk *self, void *buffer, size_t len, bool isread);
    FAILABLE_FUNCTION (*dma_begintransfer)(struct atadisk *self);
    enum ata_dmastatus (*dma_checktransfer)(struct atadisk *self);
    void (*dma_endtransfer)(struct atadisk *self, bool wasSuccess);
    void (*dma_deinittransfer)(struct atadisk *self);
};
struct atadisk {
    struct pdisk physdisk;
    struct atadisk_ops const *ops;
    void *data;
};

FAILABLE_FUNCTION atadisk_register(struct atadisk *disk_out, struct atadisk_ops const *ops, void *data);
