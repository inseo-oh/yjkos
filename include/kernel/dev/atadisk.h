#pragma once
#include <kernel/io/disk.h>
#include <kernel/status.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ACS-3 6.2 Status field
typedef uint8_t ata_status_t;

static uint8_t const ATA_STATUSFLAG_ERR = 1 << 0;
static uint8_t const ATA_STATUSFLAG_DRQ = 1 << 3;
static uint8_t const ATA_STATUSFLAG_DF  = 1 << 5;
static uint8_t const ATA_STATUSFLAG_RDY = 1 << 6;
static uint8_t const ATA_STATUSFLAG_BSY = 1 << 7;

// ACS-3 6.3 ERROR field
typedef uint8_t ata_error_t;

typedef enum {
    ATA_CMD_FLUSHCACHE     = 0xe7, // ACS-3 7.10
    ATA_CMD_IDENTIFYDEVICE = 0xec, // ACS-3 7.12
    ATA_CMD_READDMA        = 0xc8, // ACS-3 7.21
    ATA_CMD_READSECTORS    = 0x20, // ACS-3 7.28
    ATA_CMD_WRITEDMA       = 0xca, // ACS-3 7.58
    ATA_CMD_WRITESECTORS   = 0x30, // ACS-3 7.67
} atacmd_t;

enum {
    ATA_MAX_SECTORS_PER_TRANSFER = 256, // Maximum sector count for a 28-bit transfer command.
    ATA_SECTOR_SIZE              = 512,
};

typedef struct ata_databuf ata_databuf_t;
struct ata_databuf {
    uint16_t data[256];
};

typedef enum {
    ATA_DMASTATUS_FAIL_UDMA_CRC,
    ATA_DMASTATUS_FAIL_OTHER_IO,
    ATA_DMASTATUS_FAIL_NOMEM,
    ATA_DMASTATUS_SUCCESS,
    ATA_DMASTATUS_BUSY,
} ata_dmastatus_t;

typedef struct atadisk atadisk_t;
typedef struct atadisk_ops atadisk_ops_t;
struct atadisk_ops {
    bool (*dma_beginsession)(atadisk_t *self);
    void (*dma_endsession)(atadisk_t *self);
    void (*lock)(atadisk_t *self);
    void (*unlock)(atadisk_t *self);
    ata_status_t (*readstatus)(atadisk_t *self);
    FAILABLE_FUNCTION (*selectdisk)(atadisk_t *self);
    void (*setfeaturesparam)(atadisk_t *self, uint16_t data);
    void (*setcountparam)(atadisk_t *self, uint16_t data);
    void (*setlbaparam)(atadisk_t *self, uint32_t data);
    void (*setdeviceparam)(atadisk_t *self, uint8_t data);
    uint32_t (*getlbaoutput)(atadisk_t *self);
    void (*issuecmd)(atadisk_t *self, atacmd_t cmd);
    bool (*getirqflag)(atadisk_t *self);
    void (*clearirqflag)(atadisk_t *self);
    void (*readdata)(ata_databuf_t *out, atadisk_t *self);
    void (*writedata)(atadisk_t *self, ata_databuf_t *buffer);
    void (*softreset)(atadisk_t *self);

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
    FAILABLE_FUNCTION (*dma_inittransfer)(atadisk_t *self, void *buffer, size_t len, bool isread);
    FAILABLE_FUNCTION (*dma_begintransfer)(atadisk_t *self);
    ata_dmastatus_t (*dma_checktransfer)(atadisk_t *self);
    void (*dma_endtransfer)(atadisk_t *self, bool wasSuccess);
    void (*dma_deinittransfer)(atadisk_t *self);

};
struct atadisk {
    pdisk_t physdisk;
    atadisk_ops_t const *ops;
    void *data;
};

FAILABLE_FUNCTION atadisk_register(atadisk_t *disk_out, atadisk_ops_t const *ops, void *data);
