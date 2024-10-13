#include <kernel/dev/atadisk.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/io/disk.h>
#include <kernel/io/iodev.h>
#include <kernel/io/tty.h>
#include <kernel/ticktime.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

enum {
    TIMEOUT     = 5000,
    MAX_RETRIES = 3,
};

static WARN_UNUSED_RESULT int waitirq(struct atadisk *disk) {
    enum {
        STATUS_POLL_PERIOD = 10,
    };
    int ret = 0;
    ticktime starttime = g_ticktime;
    ticktime lastchecktime = 0;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        if (STATUS_POLL_PERIOD <= (lastchecktime - g_ticktime)) {
            uint8_t diskstatus = disk->ops->readstatus(disk);
            if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
                ret = -EIO;
                goto fail;
            }
            lastchecktime = g_ticktime;
        }
        if (disk->ops->getirqflag(disk)) {
            disk->ops->clearirqflag(disk);
            ok = true;
            break;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int waitbusyclear(struct atadisk *disk) {
    enum {
        STATUS_POLL_PERIOD = 10,
    };
    int ret = 0;
    ticktime starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint8_t diskstatus = disk->ops->readstatus(disk);
        if (!(diskstatus & ATA_STATUSFLAG_BSY)) {
            ok = true;
            break;
        }
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            ret = -EIO;
            goto fail;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int waitbusyclear_irq(struct atadisk *disk) {
    int ret = 0;
    while(1) {
        ret = waitirq(disk);
        if (ret < 0) {
            goto fail;
        }
        if (!(disk->ops->readstatus(disk) & ATA_STATUSFLAG_BSY)) {
            break;
        }
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int waitdrqset(struct atadisk *disk) {
    int ret = 0;
    ticktime starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint8_t diskstatus = disk->ops->readstatus(disk);
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            ret = -EIO;
            goto fail;
        }
        if (diskstatus & ATA_STATUSFLAG_DRQ) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static void extractstring_from_identifydata(char *dest, struct ata_databuf *rawdata, size_t startoffset, size_t endoffset) {
    for (size_t i = startoffset; i <= endoffset; i++) {
        dest[(i - startoffset) * 2] = rawdata->data[i] >> 8;
        dest[(i - startoffset) * 2 + 1] = rawdata->data[i];
    }
}

struct identifyresult {
    char serial[41];
    char firmware[9];
    char modelnum[41];
};

static WARN_UNUSED_RESULT int identifydevice(
    struct identifyresult *out, struct atadisk *disk)
{
    int ret = 0;
    disk->ops->selectdisk(disk);
    disk->ops->setlbaparam(disk, 0);
    disk->ops->issuecmd(disk, ATA_CMD_IDENTIFYDEVICE);
    uint8_t diskstatus = disk->ops->readstatus(disk);
    if (diskstatus == 0) {
        // Disk is not there
        ret = -ENODEV;
        goto fail;
    }
    ret = waitbusyclear_irq(disk);
    if (ret < 0) {
        goto fail;
    }

    /*
     * Wait for DRQ. waitdrqset() isn't used, because we also check LBA outputs
     * while waiting.
     */
    ticktime starttime = g_ticktime;
    bool ok = false;
    while ((g_ticktime - starttime) < TIMEOUT) {
        uint32_t lba = disk->ops->getlbaoutput(disk);
        if ((lba & 0xffff00) != 0) {
            // Not an ATA device
            ret = -ENODEV;
            goto fail;
        }
        uint8_t diskstatus = disk->ops->readstatus(disk);
        if (diskstatus & (ATA_STATUSFLAG_ERR | ATA_STATUSFLAG_DF)) {
            ret = -EIO;
            goto fail;
        }
        if (diskstatus & ATA_STATUSFLAG_DRQ) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        ret = -EIO;
        goto fail;
    }
    struct ata_databuf buffer;
    disk->ops->readdata(&buffer, disk);
    memset(out, 0, sizeof(*out));
    extractstring_from_identifydata(out->serial, &buffer, 10, 19);
    extractstring_from_identifydata(out->firmware, &buffer, 23, 26);
    extractstring_from_identifydata(out->modelnum, &buffer, 27, 46);
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int flushcache(struct atadisk *disk) {
    int ret = 0;
    disk->ops->selectdisk(disk);
    disk->ops->issuecmd(disk, ATA_CMD_FLUSHCACHE);
    ret = waitbusyclear(disk);
    if (ret < 0) {
        goto fail;
    }
    goto out;
fail:
out:
    return ret;
}

static WARN_UNUSED_RESULT int writesectors(struct atadisk *disk, uint32_t lba, size_t sectorcount, void const *buf) {
    int ret = 0;
    disk->ops->lock(disk);
    bool candma = disk->ops->dma_beginsession(disk);
    size_t remainingsectorcount = sectorcount;
    uint32_t currentlba = lba;
    uint8_t const *srcbuf = buf;

    while(remainingsectorcount > 0) {
        for (int try = 0;; try++) {
            bool iscrcerror = false;
            bool dmainitialized = false;
            bool dmarunning = false;
            size_t currentsectorcount = remainingsectorcount;
            if (ATA_MAX_SECTORS_PER_TRANSFER < remainingsectorcount) {
                currentsectorcount = ATA_MAX_SECTORS_PER_TRANSFER;
            }
            if (candma) {
                ret = disk->ops->dma_inittransfer(disk, (uint8_t *)srcbuf, currentsectorcount * ATA_SECTOR_SIZE, false);
                if (ret < 0) {
                    iodev_printf(
                        &disk->physdisk.iodev,
                        "failed to initialize DMA transfer\n");
                    goto tryfailed;
                }
                dmainitialized = true;
            }
            disk->ops->selectdisk(disk);
            disk->ops->setfeaturesparam(disk, 0);
            if (currentsectorcount == ATA_MAX_SECTORS_PER_TRANSFER) {
                disk->ops->setcountparam(disk, 0);
            } else {
                disk->ops->setcountparam(disk, currentsectorcount);
            }
            disk->ops->setlbaparam(disk, currentlba);
            if (candma) {
                // Use DMA
                disk->ops->issuecmd(disk, ATA_CMD_WRITEDMA);
                ret = disk->ops->dma_begintransfer(disk);
                if (ret < 0) {
                    iodev_printf(&disk->physdisk.iodev, "failed to start DMA transfer\n");
                    goto tryfailed;
                }
                dmarunning = true;
                while (dmarunning) {
                    ret = waitirq(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                    enum ata_dmastatus dmastatus =
                        disk->ops->dma_checktransfer(disk);
                    switch (dmastatus) {
                        case ATA_DMASTATUS_BUSY:
                            continue;
                        case ATA_DMASTATUS_SUCCESS:
                            dmarunning = false;
                            break;
                        case ATA_DMASTATUS_FAIL_UDMA_CRC:
                            ret = -EIO;
                            iscrcerror = true;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_OTHER_IO:
                            ret = -EIO;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_NOMEM:
                            ret = -ENOMEM;
                            goto tryfailed;
                    }
                }
                disk->ops->dma_endtransfer(disk, true);
                disk->ops->dma_deinittransfer(disk);
                srcbuf += currentsectorcount * ATA_SECTOR_SIZE;
            } else {
                // Use PIO
                disk->ops->issuecmd(disk, ATA_CMD_WRITESECTORS);
                for (size_t sector = 0; sector < currentsectorcount; sector++) {
                    struct ata_databuf buffer;
                    ret = waitbusyclear(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                    ret = waitdrqset(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                    for (size_t i = 0; i < 256; i++) {
                        buffer.data[i] = ((uint16_t)srcbuf[1] << 8) | srcbuf[0];
                        srcbuf += 2;
                    }
                    disk->ops->writedata(disk, &buffer);
                    ret = waitbusyclear_irq(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                }
            }
            remainingsectorcount -= currentsectorcount;
            currentlba += currentsectorcount;
            ret = waitbusyclear(disk);
            if (ret < 0) {
                iodev_printf(
                    &disk->physdisk.iodev,
                    "failed to wait for write to finish\n");
                goto tryfailed;
            }
            ret = flushcache(disk);
            if (ret < 0) {
                iodev_printf(&disk->physdisk.iodev, "disk flush failed after writing\n");
                goto tryfailed;
            }

            break;
        tryfailed:
            if (dmarunning) {
                disk->ops->dma_endtransfer(disk, false);
            }
            if (dmainitialized) {
                disk->ops->dma_deinittransfer(disk);
            }
            if (try == MAX_RETRIES) {
                goto fail;
            }
            iodev_printf(
                &disk->physdisk.iodev,
                "error %d occured (try %u/%u)\n", ret, try + 1, MAX_RETRIES);
            if (!iscrcerror && candma) {
                // If we are using DMA, reset the disk to take it out of DMA mode, to be safe.
                iodev_printf(&disk->physdisk.iodev, "resetting disk\n");
                disk->ops->softreset(disk);
            }
            continue;
        }
    }
    goto out;
fail:
out:
    if (candma) {
        disk->ops->dma_endsession(disk);
    }
    disk->ops->unlock(disk);
    return ret;
}

static WARN_UNUSED_RESULT int readsectors(
    struct atadisk *disk, uint32_t lba, size_t sectorcount, void *buf)
{
    int ret = 0;
    disk->ops->lock(disk);
    bool candma = disk->ops->dma_beginsession(disk);
    size_t remainingsectorcount = sectorcount;
    uint32_t currentlba = lba;
    uint8_t *destbuf = buf;

    while(remainingsectorcount > 0) {
        for (int try = 0;; try++) {
            bool iscrcerror = false;
            bool dmainitialized = false;
            bool dmarunning = false;
            size_t currentsectorcount = remainingsectorcount;
            if (ATA_MAX_SECTORS_PER_TRANSFER < remainingsectorcount) {
                currentsectorcount = ATA_MAX_SECTORS_PER_TRANSFER;
            }
            if (candma) {
                ret = disk->ops->dma_inittransfer(
                    disk, destbuf, currentsectorcount * ATA_SECTOR_SIZE, true);
                if (ret < 0) {
                    iodev_printf(
                        &disk->physdisk.iodev,
                        "failed to initialize DMA transfer\n");
                    goto tryfailed;
                }
                dmainitialized = true;
            }
            disk->ops->selectdisk(disk);
            disk->ops->setfeaturesparam(disk, 0);
            if (currentsectorcount == ATA_MAX_SECTORS_PER_TRANSFER) {
                disk->ops->setcountparam(disk, 0);
            } else {
                disk->ops->setcountparam(disk, currentsectorcount);
            }
            disk->ops->setlbaparam(disk, currentlba);
            if (candma) {
                // Use DMA
                disk->ops->issuecmd(disk, ATA_CMD_READDMA);
                ret = disk->ops->dma_begintransfer(disk);
                if (ret < 0) {
                    iodev_printf(
                        &disk->physdisk.iodev,
                        "failed to start DMA transfer\n");
                    goto tryfailed;
                }
                dmarunning = true;
                while (dmarunning) {
                    ret = waitirq(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                    enum ata_dmastatus dmastatus =
                        disk->ops->dma_checktransfer(disk);
                    switch (dmastatus) {
                        case ATA_DMASTATUS_BUSY:
                            continue;
                        case ATA_DMASTATUS_SUCCESS:
                            dmarunning = false;
                            break;
                        case ATA_DMASTATUS_FAIL_UDMA_CRC:
                            ret = -EIO;
                            iscrcerror = true;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_OTHER_IO:
                            ret = -EIO;
                            goto tryfailed;
                        case ATA_DMASTATUS_FAIL_NOMEM:
                            ret = -ENOMEM;
                            goto tryfailed;
                    }
                }
                disk->ops->dma_endtransfer(disk, true);
                disk->ops->dma_deinittransfer(disk);
                destbuf += currentsectorcount * ATA_SECTOR_SIZE;
            } else {
                // Use PIO
                disk->ops->issuecmd(disk, ATA_CMD_READSECTORS);
                for (size_t sector = 0; sector < currentsectorcount; sector++) {
                    struct ata_databuf buffer;
                    ret = waitbusyclear_irq(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                    ret = waitdrqset(disk);
                    if (ret < 0) {
                        goto tryfailed;
                    }
                    disk->ops->readdata(&buffer, disk);
                    for (size_t i = 0; i < 256; i++) {
                        *(destbuf++) = buffer.data[i];
                        *(destbuf++) = buffer.data[i] >> 8;
                    }
                }
            }
            remainingsectorcount -= currentsectorcount;
            currentlba += currentsectorcount;
            break;
        tryfailed:
            if (dmarunning) {
                disk->ops->dma_endtransfer(disk, false);
            }
            if (dmainitialized) {
                disk->ops->dma_deinittransfer(disk);
            }
            if (try == MAX_RETRIES) {
                goto fail;
            }
            iodev_printf(
                &disk->physdisk.iodev,
                "error %d occured (try %u/%u)\n", ret, try + 1, MAX_RETRIES);
            if (!iscrcerror && candma) {
                // If we are using DMA, reset the disk to take it out of DMA mode, to be safe.
                iodev_printf(
                    &disk->physdisk.iodev, "resetting disk\n");
                disk->ops->softreset(disk);
            }
            continue;
        }
    }
    goto out;
fail:
out:
    if (candma) {
        disk->ops->dma_endsession(disk);
    }
    disk->ops->unlock(disk);
    return ret;
}

static WARN_UNUSED_RESULT int op_read(struct pdisk *self, void *buf, size_t blockaddr, size_t blockcount) {
    struct atadisk *disk = self->data;
    return readsectors(disk, blockaddr, blockcount, buf);
}
static WARN_UNUSED_RESULT int op_write(struct pdisk *self, void const *buf, size_t blockaddr, size_t blockcount) {
    struct atadisk *disk = self->data;
    return writesectors(disk, blockaddr, blockcount, buf);
}

static struct pdisk_ops const OPS = {
    .read = op_read,
    .write = op_write,
};

WARN_UNUSED_RESULT int atadisk_register(struct atadisk *disk_out, struct atadisk_ops const *ops, void *data) {
    int ret = 0;
    memset(disk_out, 0, sizeof(*disk_out));
    disk_out->ops = ops;
    disk_out->data = data;
    struct identifyresult result;
    ret = identifydevice(&result, disk_out);
    if (ret < 0) {
        goto fail;
    }
    ret = pdisk_register(
        &disk_out->physdisk, ATA_SECTOR_SIZE, &OPS,
        disk_out);
    if (ret < 0) {
        goto fail;
    }
    iodev_printf(
        &disk_out->physdisk.iodev,
        "   model: %s\n", result.modelnum);
    iodev_printf(
        &disk_out->physdisk.iodev,
        "firmware: %s\n", result.firmware);
    iodev_printf(
        &disk_out->physdisk.iodev,
        "  serial: %s\n", result.serial);
    goto out;
fail:
out:
    return ret;
}
