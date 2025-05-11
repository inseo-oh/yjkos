#pragma once
#include <kernel/io/disk.h>
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ACS-3 6.2 Status field
#define ATA_STATUSFLAG_ERR (1U << 0)
#define ATA_STATUSFLAG_DRQ (1U << 3)
#define ATA_STATUSFLAG_DF (1U << 5)
#define ATA_STATUSFLAG_RDY (1U << 6)
#define ATA_STATUSFLAG_BSY (1U << 7)

// ACS-3 6.3 ERROR field
typedef enum {
    ATA_CMD_FLUSH_CACHE = 0xe7,     // ACS-3 7.10
    ATA_CMD_IDENTIFY_DEVICE = 0xec, // ACS-3 7.12
    ATA_CMD_READ_DMA = 0xc8,        // ACS-3 7.21
    ATA_CMD_READ_SECTORS = 0x20,    // ACS-3 7.28
    ATA_CMD_WRITE_DMA = 0xca,       // ACS-3 7.58
    ATA_CMD_WRITE_SECTORS = 0x30,   // ACS-3 7.67
} ATA_CMD;

// Maximum sector count for a 28-bit transfer command.
#define ATA_MAX_SECTORS_PER_TRANSFER 256
#define ATA_SECTOR_SIZE 512

struct ata_databuf {
    uint16_t data[256];
};

typedef enum {
    ATA_DMASTATUS_FAIL_UDMA_CRC,
    ATA_DMASTATUS_FAIL_OTHER_IO,
    ATA_DMASTATUS_FAIL_NOMEM,
    ATA_DMASTATUS_SUCCESS,
    ATA_DMASTATUS_BUSY,
} ATA_DMASTATUS;

struct atadisk;
struct atadisk_ops {
    bool (*dma_beginsession)(struct atadisk *self);
    void (*dma_endsession)(struct atadisk *self);
    void (*lock)(struct atadisk *self);
    void (*unlock)(struct atadisk *self);
    uint8_t (*read_status)(struct atadisk *self);
    void (*select_disk)(struct atadisk *self);
    void (*set_features_param)(struct atadisk *self, uint16_t data);
    void (*set_count_param)(struct atadisk *self, uint16_t data);
    void (*set_lba_param)(struct atadisk *self, uint32_t data);
    void (*set_device_param)(struct atadisk *self, uint8_t data);
    uint32_t (*get_lba_output)(struct atadisk *self);
    void (*issue_cmd)(struct atadisk *self, ATA_CMD cmd);
    bool (*get_irq_flag)(struct atadisk *self);
    void (*clear_irq_flag)(struct atadisk *self);
    void (*read_data)(struct ata_databuf *out, struct atadisk *self);
    void (*write_data)(struct atadisk *self, struct ata_databuf *buffer);
    void (*soft_reset)(struct atadisk *self);

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
    NODISCARD int (*dma_init_transfer)(struct atadisk *self, void *buffer, size_t len, bool isread);
    NODISCARD int (*dma_begin_transfer)(struct atadisk *self);
    NODISCARD ATA_DMASTATUS (*dma_check_transfer)(struct atadisk *self);
    void (*dma_end_transfer)(struct atadisk *self, bool wasSuccess);
    void (*dma_deinit_transfer)(struct atadisk *self);
};
struct atadisk {
    struct pdisk physdisk;
    struct atadisk_ops const *ops;
    void *data;
};

NODISCARD int atadisk_register(struct atadisk *disk_out, struct atadisk_ops const *ops, void *data);
