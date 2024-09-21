/*
Copyright (c) 2024 hexstd
SPDX-License-Identifier: BSD-3-Clause
*/

#include "hardware/gpio.h"
#include "pico/error.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/*
 *
Organization x8
    Memory cell array 4352 x 128K x 8
    Register 4352 x 8
    Page size 4352 bytes
    Block size (256K x 16K) bytes
*
*/

/// Per the documentation:
/// CLE = Command Latch Enable
///	 It's used to control loading of the operation mode cmd into internal cmd reg.
/// 	 Cmd is latched into the cmd reg from the IO port on the rising edge of the WE signal while CLE is high.
/// ALE = Address Latch Enable
///	 Address information is latched into the address reg from I/O port on rising edge of WE while ALE is high.
///
/// CE = Chip Enable (Active Low)
/// 	 Device goes into low power standby when CE goes HIGH (when in ready state, if busy this pin is ignored).
///
/// WE = Write Enable (Active Low)
/// 	 Used to control acquisition from the I/O port
///
/// RE = Read Enable (Active Low)
/// 	 Controls serial data output. The data is avaible tREA after falling edge of RE.
///    	 Internal column address counter is also incremented on falling edge.
///
/// IO = Input/Output
/// 	 Used for inputting address/command info and outputting data from the device
///
/// WP = Write Protect (Active Low)
/// 	 Used to protect the device from accidental programming.
///	 Internal regulator is reset when WP is low.
///
/// RY/BY = Ready/Busy
///	    Used to indicate operating condition of the device. Low means Busy. Requires a pull up.

/// Busy state can only accept 0x70 0x71 0xff

// Logic table page 29: serial data output:
// CLE = L
// ALE = L
// CE = L
// WE = H
// RE = "clocks" w/stuff happening on falling edge
// WP = Dont Care

// basic flow is
// sendCmd (Read/ReadID etc)
// sendAddr
// wait for ready pin
// readData
//   \___

typedef struct {
    int io_start; // io pin start offset

    int ale; // address latch enable (EN_HI)
    int cle; // command latch enable (EN_HI)

    int ce; // chip enable      (EN_LO)
    int re; // read enable      (EN_LO)
    int we; // write enable     (EN_LO)
    int wp; // write protect    (EN_LO)
    int ry; // read busy        (EN_HI if viewed as "ready", EN_LO if viewed as "busy")
} nand_pins_t;

typedef struct {
    uint8_t maker; /* 0x98 Kioxia/Toshiba */
    uint8_t device;
    uint8_t chip_n_type;
    uint8_t pgsz_bksz_iow;
    uint8_t districts;
} id_data_t;

typedef enum cmd_enum {
    CMD_READ_ID = 0,
    CMD_READ_PAGE = 1,
    CMD_RESET_PAGE_NO = 2,
    CMD_SET_PAGE_NO = 3,
    CMD_GET_DRIVE_STRENGTH = 4,
    CMD_GET_FLASH_INFO = 5,
    CMD_NONE
} cmd_enum_t;

typedef struct {
    cmd_enum_t cmd;
    uint32_t arg;
} cmd_t;

typedef struct {
    int sz;
    void* alloc;
} result_t;

void* malloc(size_t n); // silence IDE warnings
void* memset(void* data, int val, unsigned int n);
void free(void*);
int printf(const char*, ...);

char HELP_STR[] = "Commands: \n\
0: id - shows the ID/parameters of the connected NAND chip\n\
1: read - reads the contents of one page of the NAND chip and increments internal counter\n\
2: reset page - reset the page number to read\n\
3: set page - set the page number to specific offset\n\
4: get drive strength - get drive strength of pins\n\
else: help - Display this help string\n";

const uint32_t LED_PIN = 25;

void set_gpios(nand_pins_t* pins)
{

    pins->io_start = 0; // pico physical pins 1,2,4,5,6,7,9,10 (GP0-7). Setting this here makes math super easy

    pins->cle = 22;
    pins->ale = 21; // pico pin 11

    pins->ce = 20; // pico pin 14
    pins->re = 19;
    pins->we = 18;
    pins->wp = 17;

    pins->ry = 16; // pico pin 19
}

void set_drive_strengths(nand_pins_t* pins, enum gpio_drive_strength strength)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_drive_strength(pins->io_start + i, strength);
    }
    gpio_set_drive_strength(pins->ale, strength);
    gpio_set_drive_strength(pins->cle, strength);

    gpio_set_drive_strength(pins->ce, strength);
    gpio_set_drive_strength(pins->re, strength);
    gpio_set_drive_strength(pins->we, strength);
    gpio_set_drive_strength(pins->wp, strength);
}

void init_gpios(nand_pins_t* pins, enum gpio_drive_strength init_strength)
{
    for (int i = 0; i < 8; i++) {
        gpio_init(pins->io_start + i);
    }
    gpio_init(pins->ale);
    gpio_init(pins->cle);

    gpio_init(pins->ce);
    gpio_init(pins->re);
    gpio_init(pins->we);
    gpio_init(pins->wp);
    gpio_init(pins->ry);
    gpio_init(LED_PIN);
    gpio_set_pulls(pins->ry, true, false);

    gpio_set_dir_out_masked(0xFF);
    gpio_set_dir(pins->ale, true);
    gpio_set_dir(pins->cle, true);

    gpio_set_dir(pins->ce, true);
    gpio_set_dir(pins->re, true);
    gpio_set_dir(pins->we, true);
    gpio_set_dir(pins->wp, true);
    gpio_set_dir(pins->ry, false);

    gpio_set_dir(LED_PIN, true);

    gpio_put(pins->ale, false);
    gpio_put(pins->cle, false);
    gpio_put(pins->ce, true); // start with chip disabled
    gpio_put(pins->we, true);
    gpio_put(pins->re, true);
    gpio_put(pins->wp, true); // no write protect anymore
    gpio_put(LED_PIN, true);

    set_drive_strengths(pins, init_strength);
}

void set_io_dir(nand_pins_t* pins, bool to_output)
{
    if (to_output) {
        gpio_set_dir_out_masked(0x000000FF);
    } else {
        gpio_set_dir_in_masked(0x000000FF);
    }
}

void set_io_val(nand_pins_t* pins, uint8_t io_val)
{
    gpio_put_masked(0x000000FF, (uint32_t)io_val);
}

uint8_t get_io_val(nand_pins_t* pins)
{
    return gpio_get_all() & 0xFF;
}

void write_cmd(nand_pins_t* pins, uint8_t cmd)
{
    gpio_put(pins->re, true);
    gpio_put(pins->we, true);
    gpio_put(pins->ale, false);
    set_io_dir(pins, true);
    set_io_val(pins, cmd);
    gpio_put(pins->cle, true);
    gpio_put(pins->ce, false);
    busy_wait_at_least_cycles(5); // at least 20ns from CE low to WE high

    gpio_put(pins->we, false);
    busy_wait_at_least_cycles(3);
    gpio_put(pins->we, true);
    busy_wait_at_least_cycles(3); // at least 5ns until cle deassert and io_val change
    gpio_put(pins->cle, false);
}

void reset_nand(nand_pins_t* pins)
{
    write_cmd(pins, 0xFF);
    gpio_put(pins->ce, true);
    sleep_us(600); // a little more than max time to reset
}

void write_addr_1(nand_pins_t* pins, uint8_t addr)
{
    gpio_put(pins->ce, false); // buffer of 5ns
    gpio_put(pins->re, true);
    gpio_put(pins->we, true);
    gpio_put(pins->cle, false);
    set_io_dir(pins, true);
    gpio_put(pins->ale, true);
    busy_wait_at_least_cycles(4);

    set_io_val(pins, addr);
    busy_wait_at_least_cycles(3);
    gpio_put(pins->we, false);
    busy_wait_at_least_cycles(3);
    gpio_put(pins->we, true);
    busy_wait_at_least_cycles(3);
    gpio_put(pins->ale, false);
}

void write_addr_5(nand_pins_t* pins, uint32_t page_addr, uint32_t col_addr)
{

    const int timing_multiplier = 10;

    uint8_t col_addr_0 = col_addr & 0xFF;
    uint8_t col_addr_1 = (col_addr >> 8) & 0x1F;

    uint8_t page_addr_0 = page_addr & 0xFF;
    uint8_t page_addr_1 = (page_addr >> 8) & 0xFF;
    uint8_t page_addr_2 = (page_addr >> 16) & 0x01;

    uint8_t addr_bytes[5] = { col_addr_0, col_addr_1, page_addr_0, page_addr_1, page_addr_2 };

    gpio_put(pins->ce, false); // buffer of 5ns
    gpio_put(pins->re, true);
    gpio_put(pins->we, true);
    gpio_put(pins->cle, false);
    set_io_dir(pins, true);
    gpio_put(pins->ale, true);
    busy_wait_at_least_cycles(5 * timing_multiplier);

    for (int i = 0; i < 5; i++) {
        set_io_val(pins, addr_bytes[i]);
        busy_wait_at_least_cycles(4 * timing_multiplier);
        gpio_put(pins->we, false);
        busy_wait_at_least_cycles(4 * timing_multiplier);
        gpio_put(pins->we, true);
        busy_wait_at_least_cycles(4 * timing_multiplier);
    }
    busy_wait_at_least_cycles(3 * timing_multiplier);
    gpio_put(pins->ale, false);
}

void read_bytes(nand_pins_t* pins, uint8_t* dst, uint32_t num_bytes)
{
    const int timing_multiplier = 2;

    set_io_dir(pins, false); // set to input for read
    gpio_put(pins->ce, false);
    gpio_put(pins->cle, false);
    gpio_put(pins->ale, false);
    gpio_put(pins->we, true);
    gpio_put(pins->re, true);

    busy_wait_at_least_cycles(20 * timing_multiplier); // 100ns max before ready signal goes low

    while (!gpio_get(pins->ry)) {
        busy_wait_at_least_cycles(20 * timing_multiplier);
    }
    busy_wait_at_least_cycles(5 * timing_multiplier);

    for (int i = 0; i < num_bytes; i++) {
        gpio_put(pins->re, false);
        busy_wait_at_least_cycles(5 * timing_multiplier); // 20ns required before data can be read
        *(dst + i) = get_io_val(pins);
        busy_wait_at_least_cycles(3 * timing_multiplier);
        gpio_put(pins->re, true);
        busy_wait_at_least_cycles(3 * timing_multiplier);
    }
}

/*

ID[0] = 98 ==> Toshiba / Kioxia
ID[1] = dc device code
ID[2] = 90 9 reserved, 0 == 2-level
ID[3] = 26 x8, 256Kb block size, 4Kb page size
ID[4] = 76 2 districts

*/

void read_id(nand_pins_t* pins, id_data_t* id_bytes)
{
    write_cmd(pins, 0x90);
    write_addr_1(pins, 0x00);
    read_bytes(pins, (unsigned char*)id_bytes, sizeof(id_data_t));
}

typedef enum maker_enum {
    TOSHIBA_KIOXIA = 0x98,
} maker_code;

void explain_id(id_data_t* id_bytes)
{
    printf("Maker: ");
    switch (id_bytes->maker) {
    case TOSHIBA_KIOXIA:
        printf("Toshiba/Kioxia");
        break;
    default:
        printf("Unknown (%x)", id_bytes->maker);
    }
    printf("\n");

    printf("Device Code: %x\n", id_bytes->device);
    printf("Internal Chip Number: %d\n", 1 << (id_bytes->chip_n_type & 0x03));
    printf("Number of Cell Levels: %d\n", 1 << (((id_bytes->chip_n_type & 0x0B) >> 2) + 1));

    printf("Page Size (without redundant area): %d KB\n", 1 << (id_bytes->pgsz_bksz_iow & 0x03));
    printf("Block Size: %d KB\n", (1 << ((id_bytes->pgsz_bksz_iow & 0x30) >> 4)) * 64);
    printf("I/O Width: x%d\n", (1 << ((id_bytes->pgsz_bksz_iow & 0x40) >> 6)) * 8);

    printf("Number of Districts: %d\n", (1 << ((id_bytes->pgsz_bksz_iow & 0x0B) >> 6) + 1));
}

typedef struct _pg_sz_struct {
    uint16_t page_size_bytes;
    uint16_t oob_size_bytes;
    uint64_t flash_size_bytes;
} flash_info_struct;

bool get_flash_info(id_data_t* id_bytes, flash_info_struct* flash_info)
{

    uint32_t pg_size_kb = (1 << (id_bytes->pgsz_bksz_iow & 0x03));

    if (id_bytes->maker == TOSHIBA_KIOXIA) {
        switch (pg_size_kb) {
        case 4:
            flash_info->page_size_bytes = 4096;
            flash_info->oob_size_bytes = 256;
            break;
        case 2:
            flash_info->page_size_bytes = 2048;
            flash_info->oob_size_bytes = 128; // TODO: is this just the formula page_size / 16?
            break;
        default:
            // printf("bad pg_size_kb = %d\n", pg_size_kb);
            return false;
        }

        // So far, this is tracks, but it may not apply to all Toshiba chips
        uint32_t total_pg_size = (uint32_t)flash_info->page_size_bytes + (uint32_t)flash_info->oob_size_bytes;
        flash_info->flash_size_bytes = 64 * 2048 * total_pg_size;

        return true;
    }
    // printf("unknown maker = %x\n", id_bytes->maker);
    return false;
}

bool check_supported_io_width(id_data_t* id_bytes)
{
    uint32_t io_width = (1 << ((id_bytes->pgsz_bksz_iow & 0x40) >> 6)) * 8;

    return io_width == 8; // currently this dumper only supports x8 chips
}

bool read_onfi_id(nand_pins_t* pins)
{
    char onfi_bytes[] = { 'O', 'N', 'F', 'I' };
    char onfi_resp[6] = { 0 };
    write_cmd(pins, 0x90);
    write_addr_1(pins, 0x20);
    read_bytes(pins, (unsigned char*)onfi_resp, sizeof(onfi_bytes));
}

void read_page(nand_pins_t* pins, uint32_t page_num, uint8_t* page_buff, uint32_t page_size)
{
    reset_nand(pins);
    write_cmd(pins, 0x00);
    write_addr_5(pins, page_num, 0); // column address 0
    write_cmd(pins, 0x30);
    sleep_us(1);
    read_bytes(pins, page_buff, page_size);
}

void display_page(uint8_t* page_buff, uint32_t page_size)
{
    for (int i = 0; i < page_size; i++) {
        printf("%02X", page_buff[i]);
    }
}

// Shared State between cores
queue_t cmd_queue = { 0 };
queue_t results_queue = { 0 };
nand_pins_t pins_glob = { 0 };
uint8_t shared_buffer[16384] = { 0 };
flash_info_struct flash_info_glob = { 0 };

void core1_main()
{

    cmd_t cmd_arg = { 0, 5 };
    result_t result = { 0 };
    int page_num = 0;

    while (1) {

        queue_remove_blocking(&cmd_queue, &cmd_arg);

        switch (cmd_arg.cmd) {
        case CMD_READ_ID:
            result.sz = sizeof(id_data_t);
            read_id(&pins_glob, (id_data_t*)shared_buffer);
            break;

        case CMD_READ_PAGE:
            result.sz = flash_info_glob.page_size_bytes + flash_info_glob.oob_size_bytes; // TODO adjust this for other page sizes
            memset(shared_buffer, 0, sizeof(shared_buffer));
            read_page(&pins_glob, page_num, shared_buffer, result.sz);
            page_num += 1;
            break;

        case CMD_RESET_PAGE_NO:
            result.sz = 1; // TODO make a proper return value
            result.alloc = 0;
            page_num = 0;

            break;

        case CMD_SET_PAGE_NO:
            result.sz = 1;
            result.alloc = 0;
            page_num = (uint32_t)cmd_arg.arg;

            break;
        default:
            break;
        }

        queue_add_blocking(&results_queue, &result);
    }
}

int main()
{

    // Queue of Results
    queue_init(&results_queue, sizeof(result_t), 20);

    // Command Queue to Core 1 (secondary core)
    queue_init(&cmd_queue, sizeof(cmd_t), 20);

    // Get the chip into a good state
    set_gpios(&pins_glob);
    init_gpios(&pins_glob, GPIO_DRIVE_STRENGTH_2MA);
    reset_nand(&pins_glob);

    // read some of the config of the chip
    id_data_t id_data = { 0 };
    read_id(&pins_glob, &id_data);

    if (!check_supported_io_width(&id_data)) {
        printf("Unsupported I/O width!\n");
        while (true) {
            tight_loop_contents();
        }
    }

    if (!get_flash_info(&id_data, &flash_info_glob)) {
        printf("Unrecognized NAND flash ID bytes!\n");
        while (true) {
            tight_loop_contents();
        }
    }

    sleep_ms(500);

    multicore_launch_core1(core1_main);

    stdio_init_all();

    bool val = true;
    uint32_t curr_page = 0;

    while (1) {
        val = (time_us_32() >> 17) & 0x1; // blink about every .262 secs
        gpio_put(LED_PIN, val);
        char c = getchar_timeout_us(0);
        cmd_t cmd_arg;
        cmd_arg.cmd = CMD_NONE;
        cmd_arg.arg = 0;

        if (c >= 0x30 && c <= 0x39) {
            result_t res = { 0 };
            gpio_put(LED_PIN, true);
            switch (c - 0x30) {
            case CMD_READ_ID: // read id
                cmd_arg.cmd = CMD_READ_ID;

                queue_add_blocking(&cmd_queue, &cmd_arg);
                queue_remove_blocking(&results_queue, &res);

                if (res.sz <= 0 || res.sz > sizeof(id_data_t)) {
                    printf("Error return: %d %p\n", res.sz, shared_buffer);
                    break;
                }
                printf("ID: ");
                for (int i = 0; i < res.sz; i++) {
                    printf("%02x ", ((uint8_t*)shared_buffer)[i]);
                }
                printf("\n");

                explain_id((id_data_t*)shared_buffer);
                break;

            case CMD_READ_PAGE: // read_page
                cmd_arg.cmd = CMD_READ_PAGE; // no buffer passed to reduce copying

                queue_add_blocking(&cmd_queue, &cmd_arg);
                queue_remove_blocking(&results_queue, &res);

                if (res.sz <= 0 || res.sz > flash_info_glob.page_size_bytes + flash_info_glob.oob_size_bytes) {
                    printf("Error reading page: %d\n", res.sz);
                    break;
                }

                display_page(shared_buffer, res.sz);
                curr_page += 1;
                break;

            case CMD_RESET_PAGE_NO: // reset
                cmd_arg.cmd = CMD_RESET_PAGE_NO;
                queue_add_blocking(&cmd_queue, &cmd_arg);
                queue_remove_blocking(&results_queue, &res); // just pop the result anyway
                if (res.sz != 1) {
                    printf("Error resetting page %d\n", res.sz);
                }
                break;

            case CMD_SET_PAGE_NO: // dump_rom

                cmd_arg.cmd = CMD_SET_PAGE_NO;

                int p1 = getchar_timeout_us(2000000);
                int p2 = getchar_timeout_us(2000000);
                int p3 = getchar_timeout_us(2000000);
                if (PICO_ERROR_TIMEOUT == p1 || PICO_ERROR_TIMEOUT == p2 || PICO_ERROR_TIMEOUT == p3) {
                    printf("Timed out reading page number\n");
                    break;
                }
                uint32_t page_no = (p1 & 0xff) | ((p2 & 0xff) << 8) | ((p3 & 0x1) << 16);

                cmd_arg.arg = page_no;
                queue_add_blocking(&cmd_queue, &cmd_arg);
                queue_remove_blocking(&results_queue, &res); // just pop the result anyway

                if (res.sz != 1) {
                    printf("Error setting page %d\n", res.sz);
                }

                break;

            case CMD_GET_DRIVE_STRENGTH: {

                enum gpio_drive_strength str = gpio_get_drive_strength(1);
                printf("Drive strength is %u\n", str);
            } break;

            case CMD_GET_FLASH_INFO:
                printf("%d,%d,%lld\n", flash_info_glob.page_size_bytes,
                    flash_info_glob.oob_size_bytes,
                    flash_info_glob.flash_size_bytes);
                break;
            default:
                printf("%s", HELP_STR);
            }
        }
    }
    return 0;
}
