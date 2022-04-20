/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "disasm.h"
#include "i8008.h"

#define container_of(ptr, type, member) (type*)((char*)(ptr)-offsetof(type, member))

static int trace = 0;

static uint8_t rom[2048];
static uint8_t ram[2048];

static uint8_t* mem_location(uint16_t addr, int write)
{
    static uint8_t dummy;
    if ((addr & 2048) == 0) {
        if (write)
            return &dummy;
        return rom + (addr & (2048 - 1));
    }
    return ram + (addr & (2048 - 1));
}

static uint8_t mem_read(uint16_t addr) { return *mem_location(addr, 0); }

static void mem_write(uint16_t addr, uint8_t value) { *mem_location(addr, 1) = value; }

struct platform {
    struct i8008_cpu cpu;
    uint8_t addr_low;
    uint8_t addr_high;
    uint8_t ctrl;

    int kickstarted;
    uint8_t stuffed_instructions[3];
    int stuffed_instructions_number;

    uint8_t (*mem_read)(uint16_t addr);
    void (*mem_write)(uint16_t addr, uint8_t value);

    int io_in_char;
    int int_enabled;

    uint8_t external_stack[8];
    int external_stack_ptr;
};

static void io_console_wait(struct platform* platform)
{
    struct pollfd in_fd = { .fd = 0, .events = POLLIN };

    if (platform->io_in_char == -1)
        poll(&in_fd, 1, -1);
}

static void io_console_poll(struct platform* platform)
{
    if (platform->io_in_char == -1) {
        char c;
        ssize_t rc = read(0, &c, 1);
        if (rc == 1)
            platform->io_in_char = c;
    }

    if (platform->io_in_char != -1 && platform->int_enabled)
        i8008_int_req(&platform->cpu, 1);
}

// INP: m=0   a0 <- int enabled   a1 <- console data available
// INP: m=1   console data
// OUT: m=0   a0 <- int enabled
// OUT: m=1   console data

// m=7: push/pop on external stack

static uint8_t io_inp(struct platform* platform, int m, uint8_t a)
{
    uint8_t result = 0;

    switch (m) {
    case 0:
        if (platform->int_enabled)
            result |= 1 << 0;
        if (platform->io_in_char != -1)
            result |= 1 << 1;
        break;
    case 1:
        result               = platform->io_in_char;
        platform->io_in_char = -1;
        break;
    case 7:
        result = platform->external_stack[--platform->external_stack_ptr];
        break;
    }
    return result;
}

static void io_out(struct platform* platform, int m, uint8_t a)
{
    switch (m) {
    case 0:
        platform->int_enabled = a;
        break;
    case 1:
        write(1, &a, 1);
        break;
    case 7:
        platform->external_stack[platform->external_stack_ptr++] = a;
        break;
    }
}

static uint8_t io_func(struct i8008_cpu* cpu, enum i8008_state state, uint8_t bus_out)
{
    struct platform* platform = container_of(cpu, struct platform, cpu);

    io_console_poll(platform);

    switch (state) {
    case I8008_STATE_T1I:
        i8008_int_req(cpu, 0); // acknowledge the interrupt
        platform->int_enabled                 = 0; // avoid reentrance
        platform->stuffed_instructions[0]     = 0x0D; // RST(1)
        platform->stuffed_instructions_number = 1;
    case I8008_STATE_T1:
        platform->addr_low = bus_out;
        break;
    case I8008_STATE_T2:
        platform->ctrl      = bus_out & I8008_T2_CTRL_MSK;
        platform->addr_high = bus_out & ~I8008_T2_CTRL_MSK;
        break;
    case I8008_STATE_T3: {
        uint16_t addr = platform->addr_high;
        addr          = (addr << 8) | platform->addr_low;
        switch (platform->ctrl) {
        case I8008_T2_CTRL_PCI: {
            uint8_t instr;
            if (platform->stuffed_instructions_number)
                return platform->stuffed_instructions[--platform->stuffed_instructions_number];

            instr = platform->mem_read(addr);
            if (instr == 0x1f) // RETI
                platform->int_enabled = 1;

            return instr;
        }
        case I8008_T2_CTRL_PCR:
            return platform->mem_read(addr);
        case I8008_T2_CTRL_PCC: {
            int r = (platform->addr_high >> 4) & 3;
            int m = (platform->addr_high >> 1) & 7;
            if (r == 0) {
                // INP
                return io_inp(platform, m, platform->addr_low);
            }
            break;
        }
        case I8008_T2_CTRL_PCW:
            platform->mem_write(addr, bus_out);
            break;
        }
        break;
    }
    case I8008_STATE_STOPPED:
        // Only an interrupt can make us return
        if (platform->kickstarted)
            io_console_wait(platform);
        else {
            // the CPU starts in STOPPED state, wake it
            platform->kickstarted = 1;
        }
        i8008_int_req(&platform->cpu, 1);
        break;
    case I8008_STATE_WAIT:
        if (platform->ctrl == I8008_T2_CTRL_PCC) {
            int r = (platform->addr_high >> 4) & 3;
            int m = (platform->addr_high >> 1) & 7;
            if (r != 0) {
                // OUT
                io_out(platform, m, platform->addr_low);
                return bus_out;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static void print_debug_info(struct platform* platform)
{
    uint16_t pc = platform->cpu.stack[platform->cpu.stack_idx];
    uint8_t op  = mem_read(pc);
    char disasm[16];

    switch (i8008_opcodes[op].size) {
    case 2:
        snprintf(disasm, sizeof(disasm), "%s 0x%02X", i8008_opcodes[op].mnemonic, (unsigned int)mem_read(pc + 1));
        break;
    case 3:
        snprintf(disasm, sizeof(disasm), "%s 0x%04X", i8008_opcodes[op].mnemonic,
                 (((unsigned int)mem_read(pc + 2)) << 8) + mem_read(pc + 1));
        break;
    default:
        snprintf(disasm, sizeof(disasm), "%s", i8008_opcodes[op].mnemonic);
        break;
    }

    fprintf(stderr, "PC=%02x op=%02x A=%02x H=%02x L=%02x   %s\n", pc, op, platform->cpu.regs[REG_A],
            platform->cpu.regs[REG_H], platform->cpu.regs[REG_L], disasm);
}

static void usage(const char* prg_name)
{
    printf("%s [-t] [<rom>]\n"
           "\t-t\ttrace instructions (stderr)\n"
           "\t<rom>\tload file as rom content\n",
           prg_name);
}

static int load_rom_content(const char* rom_file)
{
    int fd;
    int copied = 0;
    ssize_t rc;

    fd = open(rom_file, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    while ((rc = read(fd, rom + copied, sizeof(rom) - copied)) > 0) {
        copied += rc;
    }
    if (rc < 0) {
        perror("read");
        return 1;
    }
    close(fd);

    return 0;
}

static void setup(int argc, char** argv)
{
    int rc;

    while ((rc = getopt(argc, argv, "th")) != -1) {
        switch (rc) {
        case 't':
            trace = 1;
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
        default:
            usage(argv[0]);
            exit(1);
        }
    }
    if (optind < argc) {
        if (load_rom_content(argv[optind]))
            exit(1);
    }

    fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);
}

int main(int argc, char** argv)
{
    struct platform platform = { .io_in_char = -1, 0 };

    setup(argc, argv);

    platform.mem_read  = &mem_read;
    platform.mem_write = &mem_write;

    i8008_init(&platform.cpu, &io_func);

    while (1) {
        if (trace)
            print_debug_info(&platform);

        i8008_cycle(&platform.cpu);
    }

    return 0;
}
