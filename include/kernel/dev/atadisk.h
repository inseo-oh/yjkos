#pragma once
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

/* ACS-3 6.2 Status field */
#define ATA_STATUSFLAG_ERR (1U << 0)
#define ATA_STATUSFLAG_DRQ (1U << 3)
#define ATA_STATUSFLAG_DF (1U << 5)
#define ATA_STATUSFLAG_RDY (1U << 6)
#define ATA_STATUSFLAG_BSY (1U << 7)

/* ACS-3 6.3 ERROR field */
typedef enum {
    ATA_CMD_FLUSH_CACHE = 0xe7,     /* ACS-3 7.10 */
    ATA_CMD_IDENTIFY_DEVICE = 0xec, /* ACS-3 7.12 */
    ATA_CMD_READ_DMA = 0xc8,        /* ACS-3 7.21 */
    ATA_CMD_READ_SECTORS = 0x20,    /* ACS-3 7.28 */
    ATA_CMD_WRITE_DMA = 0xca,       /* ACS-3 7.58 */
    ATA_CMD_WRITE_SECTORS = 0x30,   /* ACS-3 7.67 */
} ATA_CMD;

/* Maximum sector count for a 28-bit transfer command. */
#define ATA_MAX_SECTORS_PER_TRANSFER 256
#define ATA_SECTOR_SIZE 512

struct Ata_DataBuf {
    uint16_t data[256];
};

typedef enum {
    ATA_DMASTATUS_FAIL_UDMA_CRC,
    ATA_DMASTATUS_FAIL_OTHER_IO,
    ATA_DMASTATUS_FAIL_NOMEM,
    ATA_DMASTATUS_SUCCESS,
    ATA_DMASTATUS_BUSY,
} ATA_DMASTATUS;

struct Ata_Disk;
struct Ata_DiskOps {
    bool (*Dma_BeginSession)(struct Ata_Disk *self);
    void (*Dma_EndSession)(struct Ata_Disk *self);
    void (*Lock)(struct Ata_Disk *self);
    void (*Unlock)(struct Ata_Disk *self);
    uint8_t (*ReadStatus)(struct Ata_Disk *self);
    void (*SelectDisk)(struct Ata_Disk *self);
    void (*SetFeaturesParam)(struct Ata_Disk *self, uint16_t data);
    void (*SetCountParam)(struct Ata_Disk *self, uint16_t data);
    void (*SetLbaParam)(struct Ata_Disk *self, uint32_t data);
    void (*SetDeviceParam)(struct Ata_Disk *self, uint8_t data);
    uint32_t (*GetLbaOutput)(struct Ata_Disk *self);
    void (*IssueCommand)(struct Ata_Disk *self, ATA_CMD cmd);
    bool (*GetIrqFlag)(struct Ata_Disk *self);
    void (*ClearIrqFlag)(struct Ata_Disk *self);
    void (*ReadData)(struct Ata_DataBuf *out, struct Ata_Disk *self);
    void (*WriteData)(struct Ata_Disk *self, struct Ata_DataBuf *buffer);
    void (*SoftReset)(struct Ata_Disk *self);

    /*
     * DMA API
     * The order of operation is:
     * 1. [DMA] Initialize DMA Transfer
     * 2. [ATA] Issue the corresponding ATA command
     * 3. [DMA] Begin DMA Transfer
     * 4. [ATA] Wait. When IRQ is received, check DMA status, and stop waiting when finished
     * 5. [DMA] End DMA transfer
     * 6. [DMA] Deinitialize DMA transfer
     * (Step 5 and 6 are separate, so that DMA can be deinitialized safely if something fails between
     * Step 1 and Step 3)
     */
    int (*Dma_InitTransfer)(struct Ata_Disk *self, void *buffer, size_t len, bool is_read);
    int (*Dma_BeginTransfer)(struct Ata_Disk *self);
    ATA_DMASTATUS (*Dma_CheckTransfer)(struct Ata_Disk *self);
    void (*Dma_EndTransfer)(struct Ata_Disk *self, bool was_success);
    void (*Dma_DeinitTransfer)(struct Ata_Disk *self);
};
struct Ata_Disk {
    struct PDisk physdisk;
    struct Ata_DiskOps const *ops;
    void *data;
};

[[nodiscard]] int AtaDisk_Register(struct Ata_Disk *disk_out, struct Ata_DiskOps const *ops, void *data);
