#include <errno.h>
#include <kernel/dev/atadisk.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/ticktime.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define TIMEOUT 5000
#define MAX_RETRIES 3

NODISCARD static int wait_irq(struct atadisk *disk) {
    enum {
        STATUS_POLL_PERIOD = 10,
    };
    int ret = 0;
    TICKTIME starttime = g_ticktime;
    TICKTIME lastchecktime = 0;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        if (STATUS_POLL_PERIOD <= (lastchecktime - g_ticktime)) {
            uint8_t diskstatus = disk->ops->read_status(disk);
            if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
                ret = -EIO;
                goto out;
            }
            lastchecktime = g_ticktime;
        }
        if (disk->ops->get_irq_flag(disk)) {
            disk->ops->clear_irq_flag(disk);
            ok = true;
            break;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto out;
    }
out:
    return ret;
}

NODISCARD static int wait_busy_clear(struct atadisk *disk) {
    int ret = 0;
    TICKTIME starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint8_t diskstatus = disk->ops->read_status(disk);
        if (!(diskstatus & ATA_STATUSFLAG_BSY)) {
            ok = true;
            break;
        }
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            ret = -EIO;
            goto out;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto out;
    }
    goto out;
out:
    return ret;
}

NODISCARD static int wait_busy_clear_irq(struct atadisk *disk) {
    int ret = 0;
    while (1) {
        ret = wait_irq(disk);
        if (ret < 0) {
            goto out;
        }
        if (!(disk->ops->read_status(disk) & ATA_STATUSFLAG_BSY)) {
            break;
        }
    }
    goto out;
out:
    return ret;
}

NODISCARD static int wait_drq_set(struct atadisk *disk) {
    int ret = 0;
    TICKTIME starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint8_t diskstatus = disk->ops->read_status(disk);
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            ret = -EIO;
            goto out;
        }
        if (diskstatus & ATA_STATUSFLAG_DRQ) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto out;
    }
    goto out;
out:
    return ret;
}

static void extract_string_from_identify_data(uint8_t *dest, struct ata_databuf *raw_data, size_t start_offset, size_t endoffset) {
    for (size_t i = start_offset; i <= endoffset; i++) {
        dest[(i - start_offset) * 2] = raw_data->data[i] >> 8;
        dest[(i - start_offset) * 2 + 1] = raw_data->data[i];
    }
}

struct identify_result {
    uint8_t serial[41];
    uint8_t firmware[9];
    uint8_t modelnum[41];
};

NODISCARD static int identify_device(struct identify_result *out, struct atadisk *disk) {
    int ret = 0;
    disk->ops->select_disk(disk);
    disk->ops->set_lba_param(disk, 0);
    disk->ops->issue_cmd(disk, ATA_CMD_IDENTIFY_DEVICE);
    uint8_t diskstatus = disk->ops->read_status(disk);
    if (diskstatus == 0) {
        // Disk is not there
        ret = -ENODEV;
        goto out;
    }
    ret = wait_busy_clear_irq(disk);
    if (ret < 0) {
        goto out;
    }

    /*
     * Wait for DRQ. waitdrqset() isn't used, because we also check LBA outputs
     * while waiting.
     */
    TICKTIME starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint32_t lba = disk->ops->get_lba_output(disk);
        if ((lba & 0xffff00U) != 0) {
            // Not an ATA device
            ret = -ENODEV;
            goto out;
        }
        uint8_t diskstatus = disk->ops->read_status(disk);
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            ret = -EIO;
            goto out;
        }
        if (diskstatus & ATA_STATUSFLAG_DRQ) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto out;
    }
    struct ata_databuf buffer;
    disk->ops->read_data(&buffer, disk);
    memset(out, 0, sizeof(*out));
    extract_string_from_identify_data(out->serial, &buffer, 10, 19);
    extract_string_from_identify_data(out->firmware, &buffer, 23, 26);
    extract_string_from_identify_data(out->modelnum, &buffer, 27, 46);
    goto out;
out:
    return ret;
}

NODISCARD static int flush_cache(struct atadisk *disk) {
    int ret = 0;
    disk->ops->select_disk(disk);
    disk->ops->issue_cmd(disk, ATA_CMD_FLUSH_CACHE);
    ret = wait_busy_clear(disk);
    if (ret < 0) {
        goto out;
    }
    goto out;
out:
    return ret;
}

static int try_write_dma(bool *is_crc_error_out, struct atadisk *disk) {
    int ret;
    bool is_crc_error = false;
    bool dma_running = false;
    // Use DMA
    disk->ops->issue_cmd(disk, ATA_CMD_WRITE_DMA);
    ret = disk->ops->dma_begin_transfer(disk);
    if (ret < 0) {
        iodev_printf(&disk->physdisk.iodev, "failed to start DMA transfer\n");
        goto out;
    }
    dma_running = true;
    while (dma_running) {
        ret = wait_irq(disk);
        if (ret < 0) {
            goto out;
        }
        ATA_DMASTATUS dmastatus = disk->ops->dma_check_transfer(disk);
        switch (dmastatus) {
        case ATA_DMASTATUS_BUSY:
            continue;
        case ATA_DMASTATUS_SUCCESS:
            dma_running = false;
            break;
        case ATA_DMASTATUS_FAIL_UDMA_CRC:
            ret = -EIO;
            is_crc_error = true;
            goto out;
        case ATA_DMASTATUS_FAIL_OTHER_IO:
            ret = -EIO;
            goto out;
        case ATA_DMASTATUS_FAIL_NOMEM:
            ret = -ENOMEM;
            goto out;
        }
    }
    disk->ops->dma_end_transfer(disk, true);
    disk->ops->dma_deinit_transfer(disk);
out:
    if (dma_running) {
        disk->ops->dma_end_transfer(disk, false);
    }
    *is_crc_error_out = is_crc_error;
    return ret;
}

static int try_write_pio(struct atadisk *disk, uint8_t const *srcbuf, size_t sector_count) {
    int ret = 0;
    disk->ops->issue_cmd(disk, ATA_CMD_WRITE_SECTORS);
    for (size_t sector = 0; sector < sector_count; sector++) {
        struct ata_databuf buffer;
        ret = wait_busy_clear(disk);
        if (ret < 0) {
            goto out;
        }
        ret = wait_drq_set(disk);
        if (ret < 0) {
            goto out;
        }
        for (size_t i = 0; i < 256; i++) {
            buffer.data[i] = ((uint32_t)srcbuf[1] << 8) | srcbuf[0];
            srcbuf += 2;
        }
        disk->ops->write_data(disk, &buffer);
        ret = wait_busy_clear_irq(disk);
        if (ret < 0) {
            goto out;
        }
    }
out:
    return ret;
}

static int try_write(bool *is_crc_error_out, struct atadisk *disk, uint8_t const *srcbuf, bool can_dma, uint32_t lba, size_t sector_count) {
    int ret;
    bool dma_initialized = false;
    if (can_dma) {
        ret = disk->ops->dma_init_transfer(disk, (uint8_t *)srcbuf, sector_count * ATA_SECTOR_SIZE, false);
        if (ret < 0) {
            iodev_printf(&disk->physdisk.iodev, "failed to initialize DMA transfer\n");
            goto out;
        }
        dma_initialized = true;
    }
    disk->ops->select_disk(disk);
    disk->ops->set_features_param(disk, 0);
    if (sector_count == ATA_MAX_SECTORS_PER_TRANSFER) {
        disk->ops->set_count_param(disk, 0);
    } else {
        disk->ops->set_count_param(disk, sector_count);
    }
    disk->ops->set_lba_param(disk, lba);
    if (can_dma) {
        ret = try_write_dma(is_crc_error_out, disk);
    } else {
        ret = try_write_pio(disk, srcbuf, sector_count);
    }
    if (ret < 0) {
        goto out;
    }
    ret = wait_busy_clear(disk);
    if (ret < 0) {
        iodev_printf(&disk->physdisk.iodev, "failed to wait for write to finish\n");
        goto out;
    }
    ret = flush_cache(disk);
    if (ret < 0) {
        iodev_printf(&disk->physdisk.iodev, "disk flush failed after writing\n");
        goto out;
    }
out:
    if (dma_initialized) {
        disk->ops->dma_deinit_transfer(disk);
    }
    return ret;
}

NODISCARD static int write_sectors(struct atadisk *disk, uint32_t lba, size_t sector_count, void const *buf) {
    int ret = 0;
    disk->ops->lock(disk);
    bool can_dma = disk->ops->dma_beginsession(disk);
    size_t remaining_sector_count = sector_count;
    uint32_t current_lba = lba;
    uint8_t const *srcbuf = buf;

    while (remaining_sector_count > 0) {
        for (int try = 0;; try++) {
            bool is_crc_error = false;
            size_t current_sector_count = remaining_sector_count;
            if (ATA_MAX_SECTORS_PER_TRANSFER < remaining_sector_count) {
                current_sector_count = ATA_MAX_SECTORS_PER_TRANSFER;
            }
            ret = try_write(&is_crc_error, disk, srcbuf, can_dma, current_lba, current_sector_count);
            if (ret < 0) {
                goto tryfailed;
            }
            remaining_sector_count -= current_sector_count;
            current_lba += current_sector_count;
            srcbuf += current_sector_count * ATA_SECTOR_SIZE;
            break;
        tryfailed:
            if (try == MAX_RETRIES) {
                goto out;
            }
            iodev_printf(&disk->physdisk.iodev, "error %d occured (try %u/%u)\n", ret, try + 1, MAX_RETRIES);
            if (!is_crc_error && can_dma) {
                // If we are using DMA, reset the disk to take it out of DMA mode, to be safe.
                iodev_printf(&disk->physdisk.iodev, "resetting disk\n");
                disk->ops->soft_reset(disk);
            }
        }
    }
    goto out;
out:
    if (can_dma) {
        disk->ops->dma_endsession(disk);
    }
    disk->ops->unlock(disk);
    return ret;
}

static int try_read_dma(bool *is_crc_error_out, struct atadisk *disk) {
    int ret = 0;
    bool dma_running = false;
    bool is_crc_error = false;
    // Use DMA
    disk->ops->issue_cmd(disk, ATA_CMD_READ_DMA);
    ret = disk->ops->dma_begin_transfer(disk);
    if (ret < 0) {
        iodev_printf(&disk->physdisk.iodev, "failed to start DMA transfer\n");
        goto tryfailed;
    }
    dma_running = true;
    while (dma_running) {
        ret = wait_irq(disk);
        if (ret < 0) {
            goto tryfailed;
        }
        ATA_DMASTATUS dmastatus = disk->ops->dma_check_transfer(disk);
        switch (dmastatus) {
        case ATA_DMASTATUS_BUSY:
            continue;
        case ATA_DMASTATUS_SUCCESS:
            dma_running = false;
            break;
        case ATA_DMASTATUS_FAIL_UDMA_CRC:
            ret = -EIO;
            is_crc_error = true;
            goto tryfailed;
        case ATA_DMASTATUS_FAIL_OTHER_IO:
            ret = -EIO;
            goto tryfailed;
        case ATA_DMASTATUS_FAIL_NOMEM:
            ret = -ENOMEM;
            goto tryfailed;
        }
    }
    disk->ops->dma_end_transfer(disk, true);
    disk->ops->dma_deinit_transfer(disk);
tryfailed:
    if (dma_running) {
        disk->ops->dma_end_transfer(disk, false);
    }
    *is_crc_error_out = is_crc_error;
    return ret;
}

static int try_read_pio(struct atadisk *disk, uint8_t *destbuf, size_t sector_count) {
    int ret = 0;
    disk->ops->issue_cmd(disk, ATA_CMD_READ_SECTORS);
    for (size_t sector = 0; sector < sector_count; sector++) {
        struct ata_databuf buffer;
        ret = wait_busy_clear_irq(disk);
        if (ret < 0) {
            goto out;
        }
        ret = wait_drq_set(disk);
        if (ret < 0) {
            goto out;
        }
        disk->ops->read_data(&buffer, disk);
        for (size_t i = 0; i < 256; i++) {
            *(destbuf++) = buffer.data[i];
            *(destbuf++) = buffer.data[i] >> 8;
        }
    }
out:
    return ret;
}

NODISCARD static int try_read(bool *is_crc_error_out, struct atadisk *disk, uint8_t *destbuf, bool can_dma, uint32_t lba, size_t sector_count) {
    int ret = 0;
    bool is_crc_error = false;
    bool dma_initialized = false;
    if (can_dma) {
        ret = disk->ops->dma_init_transfer(disk, destbuf, sector_count * ATA_SECTOR_SIZE, true);
        if (ret < 0) {
            iodev_printf(&disk->physdisk.iodev, "failed to initialize DMA transfer\n");
            goto tryfailed;
        }
        dma_initialized = true;
    }
    disk->ops->select_disk(disk);
    disk->ops->set_features_param(disk, 0);
    if (sector_count == ATA_MAX_SECTORS_PER_TRANSFER) {
        disk->ops->set_count_param(disk, 0);
    } else {
        disk->ops->set_count_param(disk, sector_count);
    }
    disk->ops->set_lba_param(disk, lba);
    if (can_dma) {
        ret = try_read_dma(is_crc_error_out, disk);
    } else {
        ret = try_read_pio(disk, destbuf, sector_count);
    }
tryfailed:
    if (dma_initialized) {
        disk->ops->dma_deinit_transfer(disk);
    }
    *is_crc_error_out = is_crc_error;
    return ret;
}

NODISCARD static int read_sectors(struct atadisk *disk, uint32_t lba, size_t sector_count, void *buf) {
    int ret = 0;
    disk->ops->lock(disk);
    bool can_dma = disk->ops->dma_beginsession(disk);
    size_t remaining_sector_count = sector_count;
    uint32_t current_lba = lba;
    uint8_t *destbuf = buf;

    while (remaining_sector_count > 0) {
        for (int try = 0;; try++) {
            bool is_crc_error = false;
            size_t current_sector_count = remaining_sector_count;
            if (ATA_MAX_SECTORS_PER_TRANSFER < remaining_sector_count) {
                current_sector_count = ATA_MAX_SECTORS_PER_TRANSFER;
            }
            ret = try_read(&is_crc_error, disk, destbuf, can_dma, current_lba, current_sector_count);
            if (ret < 0) {
                goto tryfailed;
            }
            remaining_sector_count -= current_sector_count;
            current_lba += current_sector_count;
            destbuf += current_sector_count * ATA_SECTOR_SIZE;
            break;
        tryfailed:
            if (try == MAX_RETRIES) {
                goto out;
            }
            iodev_printf(&disk->physdisk.iodev, "error %d occured (try %u/%u)\n", ret, try + 1, MAX_RETRIES);
            if (!is_crc_error && can_dma) {
                // If we are using DMA, reset the disk to take it out of DMA mode, to be safe.
                iodev_printf(&disk->physdisk.iodev, "resetting disk\n");
                disk->ops->soft_reset(disk);
            }
        }
    }
    goto out;
out:
    if (can_dma) {
        disk->ops->dma_endsession(disk);
    }
    disk->ops->unlock(disk);
    return ret;
}

NODISCARD static int op_read(struct pdisk *self, void *buf, size_t block_addr, size_t block_count) {
    struct atadisk *disk = self->data;
    return read_sectors(disk, block_addr, block_count, buf);
}

NODISCARD static int op_write(struct pdisk *self, void const *buf, size_t block_addr, size_t block_count) {
    struct atadisk *disk = self->data;
    return write_sectors(disk, block_addr, block_count, buf);
}

static struct pdisk_ops const OPS = {
    .read = op_read,
    .write = op_write,
};

NODISCARD int atadisk_register(struct atadisk *disk_out, struct atadisk_ops const *ops, void *data) {
    int ret = 0;
    memset(disk_out, 0, sizeof(*disk_out));
    disk_out->ops = ops;
    disk_out->data = data;
    struct identify_result result;
    ret = identify_device(&result, disk_out);
    if (ret < 0) {
        goto fail;
    }
    ret = pdisk_register(&disk_out->physdisk, ATA_SECTOR_SIZE, &OPS, disk_out);
    if (ret < 0) {
        goto fail;
    }
    iodev_printf(&disk_out->physdisk.iodev, "   model: %s\n", result.modelnum);
    iodev_printf(&disk_out->physdisk.iodev, "firmware: %s\n", result.firmware);
    iodev_printf(&disk_out->physdisk.iodev, "  serial: %s\n", result.serial);
    goto out;
fail:
out:
    return ret;
}
