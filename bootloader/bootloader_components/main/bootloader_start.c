/*
 * Lilka boot menu: 2nd stage bootloader that can switch between apps in flash
 * (Keira OS, Retro-Go) and flash .bin firmware from the SD card FIRMWARE directory.
 *
 * Hold START while powering on / resetting to open the menu, otherwise the app
 * selected in otadata boots as usual.
 */
#include <stdbool.h>
#include <string.h>

#include "bootloader_common.h"
#include "bootloader_flash_priv.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "esp_flash_partitions.h"
#include "esp_image_format.h"
#include "esp_rom_sys.h"
#include "hw.h"
#include "lcd.h"
#include "pff/pff.h"
#include "sdkconfig.h"

#define SECTOR_SIZE    0x1000
#define MAX_ITEMS      32
#define SCAN_DEPTH     3  // root + 2 levels of subdirectories
#define PATH_LEN       64
#define NAME_LEN       24 // fits one menu row
#define SLOT_LABEL     "app1" // where SD firmware gets written (same slot as Keira's multiboot)

#define COLOR_BG     RGB565(0, 0, 0)
#define COLOR_FG     RGB565(255, 255, 255)
#define COLOR_TITLE  RGB565(255, 200, 0)
#define COLOR_SEL_BG RGB565(0, 90, 200)
#define COLOR_DIM    RGB565(140, 140, 140)
#define COLOR_ERR    RGB565(255, 60, 60)

#define MENU_Y      30
#define MENU_ROW_H  20
#define MENU_ROWS   9
#define FOOTER_Y    (LCD_H - LCD_CHAR_H - 4)

typedef struct {
    char name[NAME_LEN];
    int ota_index;       // app to boot
    char path[PATH_LEN]; // SD file to flash into the slot first ("" for apps already in flash)
} menu_item_t;

typedef struct {
    const char *label;
    const char *title;
} known_app_t;

static const known_app_t known_apps[] = {
    {"app0", "Keira OS"},
    {"launcher", "Retro-Go"},
    {SLOT_LABEL, "Last SD firmware"},
};

typedef struct {
    menu_item_t items[MAX_ITEMS];
    FATFS fs;
    uint8_t io_buf[SECTOR_SIZE];
    uint8_t cmp_buf[512];
} scratch_t;

_Static_assert(sizeof(scratch_t) <= BOOT_SCRATCH_SIZE - 512, "scratch overlaps SD sector buffer");

#define SCRATCH ((scratch_t *)BOOT_SCRATCH_ADDR)
#define items   (SCRATCH->items)
#define fs      (SCRATCH->fs)
#define io_buf  (SCRATCH->io_buf)
#define cmp_buf (SCRATCH->cmp_buf)

static int item_count;
static int slot_index = -1;
static esp_partition_pos_t slot_pos;

static int select_partition_number(bootloader_state_t *bs);

/* ---------------------------------------------------------------- otadata */

// Same sequence scheme as esp_ota_set_boot_partition(): slot = (seq - 1) % ota_count
static void set_boot_slot(const bootloader_state_t *bs, int slot) {
    esp_ota_select_entry_t sel[2];
    if (bootloader_flash_read(bs->ota_info.offset, &sel[0], sizeof(sel[0]), false) != ESP_OK ||
        bootloader_flash_read(bs->ota_info.offset + SECTOR_SIZE, &sel[1], sizeof(sel[1]), false) != ESP_OK) {
        return;
    }
    int active = bootloader_common_get_active_otadata(sel);
    uint32_t seq = active >= 0 ? sel[active].ota_seq : 0;
    if (active >= 0 && (int)((seq - 1) % bs->app_count) == slot) {
        return; // already selected
    }
    do {
        seq++;
    } while ((int)((seq - 1) % bs->app_count) != slot);

    esp_ota_select_entry_t entry;
    memset(&entry, 0xFF, sizeof(entry));
    entry.ota_seq = seq;
    entry.crc = bootloader_common_ota_select_crc(&entry);

    uint32_t addr = bs->ota_info.offset + (active == 0 ? SECTOR_SIZE : 0);
    if (bootloader_flash_erase_sector(addr / SECTOR_SIZE) == ESP_OK) {
        bootloader_flash_write(addr, &entry, sizeof(entry), false);
    }
}

/* ---------------------------------------------------------------- menu items */

static void str_copy(char *dst, size_t size, const char *a, const char *b) {
    size_t n = 0;
    for (const char *s = a; s && *s && n + 1 < size; s++) dst[n++] = *s;
    for (const char *s = b; s && *s && n + 1 < size; s++) dst[n++] = *s;
    dst[n] = 0;
}

static void add_item(const char *name, int ota_index, const char *path) {
    if (item_count >= MAX_ITEMS) return;
    menu_item_t *it = &items[item_count++];
    str_copy(it->name, sizeof(it->name), name, NULL);
    it->ota_index = ota_index;
    str_copy(it->path, sizeof(it->path), path, NULL);
}

static bool image_valid(const esp_partition_pos_t *pos) {
    esp_image_metadata_t meta;
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, pos, &meta) == ESP_OK;
}

static void collect_flash_apps(void) {
    struct {
        int known;
        int index;
        esp_partition_pos_t pos;
    } found[sizeof(known_apps) / sizeof(known_apps[0])];
    int found_count = 0;

    // Only one flash mapping may be active, so pick the partitions first and verify them afterwards
    const esp_partition_info_t *table = bootloader_mmap(ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
    if (!table) return;
    for (size_t k = 0; k < sizeof(known_apps) / sizeof(known_apps[0]); k++) {
        for (size_t i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES && table[i].magic == ESP_PARTITION_MAGIC; i++) {
            const esp_partition_info_t *p = &table[i];
            if (p->type != PART_TYPE_APP || (p->subtype & ~PART_SUBTYPE_OTA_MASK) != PART_SUBTYPE_OTA_FLAG) continue;
            if (strncmp((const char *)p->label, known_apps[k].label, sizeof(p->label)) != 0) continue;
            found[found_count].known = k;
            found[found_count].index = p->subtype & PART_SUBTYPE_OTA_MASK;
            found[found_count].pos = p->pos;
            found_count++;
            break;
        }
    }
    bootloader_munmap(table);

    for (int i = 0; i < found_count; i++) {
        const known_app_t *app = &known_apps[found[i].known];
        if (strcmp(app->label, SLOT_LABEL) == 0) {
            slot_index = found[i].index;
            slot_pos = found[i].pos;
        }
        if (image_valid(&found[i].pos)) {
            add_item(app->title, found[i].index, NULL);
        }
    }
}

static bool has_bin_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && strcmp(dot, ".BIN") == 0;
}

// Only ESP32-S3 app images count: plenty of other files are called *.bin too
static bool is_firmware(const char *path, DWORD size) {
    esp_image_header_t hdr;
    UINT got = 0;
    return size > sizeof(hdr) && size <= slot_pos.size && pf_open(path) == FR_OK &&
           pf_read(&hdr, sizeof(hdr), &got) == FR_OK && got == sizeof(hdr) && hdr.magic == ESP_IMAGE_HEADER_MAGIC &&
           hdr.chip_id == ESP_CHIP_ID_ESP32S3;
}

// Menu name: the tail of the path that fits one row, e.g. "SD: REGRAV~1/FIRMWARE.BIN"
static void path_to_name(char *name, const char *path) {
    static const char prefix[] = "SD: ";
    size_t room = NAME_LEN - sizeof(prefix);
    size_t len = strlen(path);
    const char *tail = path + 1;
    while (strlen(tail) > room) {
        const char *slash = strchr(tail, '/');
        tail = slash ? slash + 1 : path + len - room;
    }
    str_copy(name, NAME_LEN, prefix, tail);
}

static void scan_dir(char *path, int depth) {
    DIR dir;
    FILINFO fno;
    if (pf_opendir(&dir, path[0] ? path : "/") != FR_OK) return;

    size_t base = strlen(path);
    while (item_count < MAX_ITEMS && pf_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & (AM_HID | AM_SYS) || fno.fname[0] == '.') continue;
        if (base + 1 + strlen(fno.fname) >= PATH_LEN) continue;
        path[base] = '/';
        str_copy(path + base + 1, PATH_LEN - base - 1, fno.fname, NULL);

        if (fno.fattrib & AM_DIR) {
            if (depth + 1 < SCAN_DEPTH) {
                scan_dir(path, depth + 1);
            }
        } else if (has_bin_ext(fno.fname) && is_firmware(path, fno.fsize)) {
            char name[NAME_LEN];
            path_to_name(name, path);
            add_item(name, slot_index, path);
        }
        path[base] = 0;
    }
}

static void collect_sd_files(void) {
    if (slot_index < 0 || pf_mount(&fs) != FR_OK) return;
    char path[PATH_LEN] = "";
    scan_dir(path, 0);
}

/* ---------------------------------------------------------------- UI */

static void draw_status(const char *msg, uint16_t color) {
    lcd_fill(0, FOOTER_Y, LCD_W, LCD_CHAR_H, COLOR_BG);
    lcd_text(4, FOOTER_Y, msg, color, COLOR_BG);
}

static void draw_menu(int selected, int top) {
    for (int row = 0; row < MENU_ROWS; row++) {
        int i = top + row;
        int y = MENU_Y + row * MENU_ROW_H;
        uint16_t bg = (i == selected) ? COLOR_SEL_BG : COLOR_BG;
        lcd_fill(0, y, LCD_W, MENU_ROW_H, bg);
        if (i < item_count) {
            lcd_text(8, y + (MENU_ROW_H - LCD_CHAR_H) / 2, items[i].name, COLOR_FG, bg);
        }
    }
}

static int wait_button(void) {
    static const int buttons[] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_A, PIN_BTN_B, PIN_BTN_START};
    for (;;) {
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
            if (hw_btn(buttons[i])) {
                hw_delay_ms(20);
                while (hw_btn(buttons[i])) {
                    hw_delay_ms(5);
                }
                return buttons[i];
            }
        }
        hw_delay_ms(5);
    }
}

/* ---------------------------------------------------------------- SD -> flash */

// Writes only the sectors that differ, so re-selecting the same file is quick.
static bool flash_file(const char *path) {
    if (pf_open(path) != FR_OK) {
        draw_status("Can't open file", COLOR_ERR);
        return false;
    }
    uint32_t size = fs.fsize;
    if (size == 0 || size > slot_pos.size) {
        draw_status("File doesn't fit slot", COLOR_ERR);
        return false;
    }

    int last_pct = -1;
    for (uint32_t off = 0; off < size; off += SECTOR_SIZE) {
        UINT got = 0;
        if (pf_read(io_buf, SECTOR_SIZE, &got) != FR_OK) {
            draw_status("SD read error", COLOR_ERR);
            return false;
        }
        if (got < SECTOR_SIZE) {
            memset(io_buf + got, 0xFF, SECTOR_SIZE - got);
        }

        uint32_t addr = slot_pos.offset + off;
        bool same = true;
        for (uint32_t i = 0; i < SECTOR_SIZE && same; i += sizeof(cmp_buf)) {
            same = bootloader_flash_read(addr + i, cmp_buf, sizeof(cmp_buf), false) == ESP_OK &&
                   memcmp(cmp_buf, io_buf + i, sizeof(cmp_buf)) == 0;
        }
        if (!same) {
            if (bootloader_flash_erase_sector(addr / SECTOR_SIZE) != ESP_OK ||
                bootloader_flash_write(addr, io_buf, SECTOR_SIZE, false) != ESP_OK) {
                draw_status("Flash write error", COLOR_ERR);
                return false;
            }
        }

        int pct = (int)((uint64_t)(off + SECTOR_SIZE) * 100 / size);
        if (pct > 100) pct = 100;
        if (pct / 5 != last_pct / 5) {
            last_pct = pct;
            lcd_fill(0, FOOTER_Y - 10, LCD_W * pct / 100, 6, COLOR_SEL_BG);
        }
    }
    lcd_fill(0, FOOTER_Y - 10, LCD_W, 6, COLOR_BG);

    if (!image_valid(&slot_pos)) {
        draw_status("Not a valid app image", COLOR_ERR);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- menu */

// Returns the OTA index to boot, or INVALID_INDEX to continue with the normal selection.
static int run_menu(bootloader_state_t *bs) {
    lcd_init();
    lcd_text(4, 6, "LILKA BOOT", COLOR_TITLE, COLOR_BG);
    draw_status("Reading SD card...", COLOR_DIM);

    collect_flash_apps();
    collect_sd_files();

    // Wait for START (used to enter the menu) to be released
    while (hw_btn(PIN_BTN_START)) {
        hw_delay_ms(5);
    }

    int selected = 0, top = 0;
    for (;;) {
        if (selected < top) top = selected;
        if (selected >= top + MENU_ROWS) top = selected - MENU_ROWS + 1;
        draw_menu(selected, top);
        draw_status(item_count ? "A: boot   B: exit" : "No apps found. B: exit", COLOR_DIM);

        int btn = wait_button();
        if (btn == PIN_BTN_UP && item_count) {
            selected = (selected + item_count - 1) % item_count;
        } else if (btn == PIN_BTN_DOWN && item_count) {
            selected = (selected + 1) % item_count;
        } else if (btn == PIN_BTN_B) {
            return INVALID_INDEX;
        } else if ((btn == PIN_BTN_A || btn == PIN_BTN_START) && item_count) {
            menu_item_t *it = &items[selected];
            if (it->path[0]) {
                draw_status("Flashing...", COLOR_FG);
                if (!flash_file(it->path)) {
                    wait_button();
                    continue;
                }
            }
            // The multiboot slot runs once (otadata untouched, next reset returns to the previous
            // app), like Keira's multiboot. Apps installed in flash become the default.
            if (it->ota_index != slot_index) {
                set_boot_slot(bs, it->ota_index);
            }
            draw_status("Booting...", COLOR_FG);
            return it->ota_index;
        }
    }
}

/* ---------------------------------------------------------------- entry */

void __attribute__((noreturn)) call_start_cpu0(void) {
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    bootloader_utility_load_boot_image_from_deep_sleep();
#endif

    bootloader_state_t bs = {0};
    int boot_index = select_partition_number(&bs);

    hw_init();
    if (hw_btn(PIN_BTN_START) || boot_index == INVALID_INDEX) {
        int chosen = run_menu(&bs);
        if (chosen != INVALID_INDEX) {
            boot_index = chosen;
        }
    }
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    bootloader_utility_load_boot_image(&bs, boot_index);
}

static int select_partition_number(bootloader_state_t *bs) {
    if (!bootloader_utility_load_partition_table(bs)) {
        esp_rom_printf("lilka_boot: can't load partition table\n");
        return INVALID_INDEX;
    }
    return bootloader_utility_get_selected_boot_partition(bs);
}

// Return global reent struct if any newlib functions are linked to bootloader
struct _reent *__getreent(void) {
    return _GLOBAL_REENT;
}
