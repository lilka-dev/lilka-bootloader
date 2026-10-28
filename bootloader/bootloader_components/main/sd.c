// Minimal SD card driver (SPI mode, read-only) backing Petit FatFs.
#include <string.h>

#include "esp_rom_sys.h"
#include "hw.h"
#include "pff/diskio.h"

#define CMD0   0
#define CMD8   8
#define CMD16  16
#define CMD17  17
#define CMD55  55
#define CMD58  58
#define ACMD41 41

static bool sd_ready;
static bool sd_block_addr; // SDHC/SDXC: sector numbers instead of byte offsets
static DWORD cached_sector = 0xFFFFFFFF;

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg) {
    uint8_t crc = 0x01;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;

    spi_xfer(0xFF);
    uint8_t frame[6] = {
        (uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, crc,
    };
    spi_write(frame, sizeof(frame));

    uint8_t r1 = 0xFF;
    for (int i = 0; i < 10 && (r1 & 0x80); i++) {
        r1 = spi_xfer(0xFF);
    }
    return r1;
}

static uint8_t sd_acmd(uint8_t cmd, uint32_t arg) {
    sd_cmd(CMD55, 0);
    return sd_cmd(cmd, arg);
}

static void sd_release(void) {
    spi_deselect(PIN_SD_CS);
    spi_xfer(0xFF); // give the card a clock edge to release MISO
}

DSTATUS disk_initialize(void) {
    sd_ready = false;
    cached_sector = 0xFFFFFFFF;

    spi_set_slow(true);
    // Display CS stays high: the LCD only ever sees mode 3 transactions (lcd-tap compatibility)
    spi_deselect(PIN_SD_CS);
    for (int i = 0; i < 10; i++) {
        spi_xfer(0xFF); // >= 74 clocks with CS high
    }

    spi_select(PIN_SD_CS, 0);
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 10 && r1 != 0x01; i++) {
        r1 = sd_cmd(CMD0, 0);
    }
    if (r1 != 0x01) {
        goto fail;
    }

    bool v2 = false;
    if (sd_cmd(CMD8, 0x1AA) == 0x01) {
        uint8_t r7[4];
        spi_read(r7, 4);
        if (r7[2] != 0x01 || r7[3] != 0xAA) {
            goto fail;
        }
        v2 = true;
    }

    // ACMD41 until the card leaves idle state (up to ~1 s)
    for (int i = 0; i < 1000; i++) {
        r1 = sd_acmd(ACMD41, v2 ? 0x40000000 : 0);
        if (r1 == 0x00) break;
        esp_rom_delay_us(1000);
    }
    if (r1 != 0x00) {
        goto fail;
    }

    sd_block_addr = false;
    if (v2 && sd_cmd(CMD58, 0) == 0x00) {
        uint8_t ocr[4];
        spi_read(ocr, 4);
        sd_block_addr = (ocr[0] & 0x40) != 0;
    }
    if (!sd_block_addr && sd_cmd(CMD16, 512) != 0x00) {
        goto fail;
    }

    sd_release();
    spi_set_slow(false);
    sd_ready = true;
    return 0;

fail:
    sd_release();
    spi_set_slow(false);
    return STA_NOINIT;
}

static bool sd_read_sector(DWORD sector, uint8_t *buf) {
    spi_select(PIN_SD_CS, 0);
    bool ok = false;
    if (sd_cmd(CMD17, sd_block_addr ? sector : sector * 512) == 0x00) {
        uint8_t token = 0xFF;
        for (int i = 0; i < 100000 && token == 0xFF; i++) {
            token = spi_xfer(0xFF);
        }
        if (token == 0xFE) {
            spi_read(buf, 512);
            spi_xfer(0xFF); // CRC
            spi_xfer(0xFF);
            ok = true;
        }
    }
    sd_release();
    return ok;
}

DRESULT disk_readp(BYTE *buff, DWORD sector, UINT offset, UINT count) {
    if (!sd_ready) {
        return RES_NOTRDY;
    }
    if (offset == 0 && count == 512 && buff) {
        return sd_read_sector(sector, buff) ? RES_OK : RES_ERROR;
    }
    if (sector != cached_sector) {
        if (!sd_read_sector(sector, SD_SECTOR_BUF)) {
            cached_sector = 0xFFFFFFFF;
            return RES_ERROR;
        }
        cached_sector = sector;
    }
    if (buff) {
        memcpy(buff, SD_SECTOR_BUF + offset, count);
    }
    return RES_OK;
}
