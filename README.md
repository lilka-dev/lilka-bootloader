# lilka_boot

Boot menu for Lilka v2 (ESP32-S3, 16 MB) that keeps **Keira OS** and **Retro-Go** on one flash
and can flash firmware from the SD card.

The board is flashed once over USB with the boot menu, the shared partition table and Keira.
Retro-Go is installed later from Keira: open `retro-go_lilka.img` in the file manager (or get it
from the catalog). Keira's installer copies every app of the image into the partition with the
same label and leaves the bootloader, the partition table and Keira alone.

It is a custom ESP-IDF 2nd stage bootloader (`bootloader_override`, IDF v4.4.7) with a
bit-banged SPI driver for the shared display/SD bus, a read-only SD card driver,
[Petit FatFs](http://elm-chan.org/fsw/ff/00index_p.html) and an ST7789 text UI. All of that
fits in the 32 KB the bootloader has before the partition table at `0x8000`.

## Usage

- **Normal boot**: the app selected in `otadata` starts, exactly like the stock bootloader.
- **Hold START while resetting / powering on**: the boot menu opens.
  - `Keira OS`, `Retro-Go`: become the default app and boot.
  - `Last SD firmware`: boots what's in `app1` once (like Keira's multiboot).
  - `SD: ...`: ESP32-S3 firmware (`*.bin` with a valid S3 image header, up to 3 MB) found on the
    SD card, up to 2 subdirectories deep. It is written to `app1`, only sectors that differ, and
    booted once.
  - UP/DOWN select, A or START boots, B leaves the menu and boots normally.
- **Keira → Retro-Go**: *Applications → Retro-Go* in Keira (shown when the `launcher` partition exists).
- **Retro-Go → Keira**: *About Retro-Go → Debug → Reboot to firmware*, or *Reboot to factory* in the recovery menu (hold D at start), or the boot menu.

SD cards: FAT16/FAT32 (also without a partition table), no exFAT. Names are 8.3 (`REGRAV~1/FIRMWARE.BIN`).

## Flash layout

See [partitions.csv](partitions.csv): Keira in `app0` (4 MB), multiboot slot `app1` (3 MB),
the Retro-Go apps in `ota_2..ota_8` under the labels Retro-Go looks for, SPIFFS (3.2 MB).

The partition table stays at `0x8000` because Keira's prebuilt Arduino libraries and Retro-Go
are built with that offset.

## Build and flash

Needs Docker (`espressif/idf:v4.4.7`) and PlatformIO (only for `esptool.py`).

```bash
git submodule update --init
make bootloader      # boot menu + partition table
make spiffs          # Keira's data/ as a SPIFFS image for this layout
make image           # build/lilka_boot.img: boot menu + partition table + Keira (KEIRA_BIN=... to override)
make flash PORT=/dev/cu.usbmodemXXXX
make retro-go        # build/retro-go_lilka.img: copy to the SD card / publish in the catalog
```

Keira itself is built from its repo (`pio run -e v2`); the Makefile picks up
`../0014_keira/.pio/build/v2/firmware.bin` by default.

## Notes

- Retro-Go is [ab4o1on/retro-go](https://github.com/ab4o1on/retro-go/tree/lilka-dev) (`lilka` target)
  with `patches/retro-go-lilka-keira.patch`: factory app = `app0`, built-in updater off, and the
  boot is confirmed on start (Keira switches with rollback enabled).
- Rollback is enabled in the bootloader, as in Keira's Arduino build: Keira's multiboot relies on it.
- The bootloader header must stay DIO (`--flash_mode keep`); the ROM can't read a QIO header
  before the bootloader enables QIO.
- Keira's own OTA (`esp_https_ota`) always writes the *next* OTA partition; with this layout
  Keira refuses to update from `app1` instead of overwriting Retro-Go's launcher.
