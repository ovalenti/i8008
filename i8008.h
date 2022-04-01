/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef I8008_H_INCLUDED
#define I8008_H_INCLUDED

#include <stdint.h>

struct i8008_cpu;

enum i8008_state {
    I8008_STATE_T1      = 2,
    I8008_STATE_T1I     = 6,
    I8008_STATE_T2      = 4,
    I8008_STATE_WAIT    = 0,
    I8008_STATE_T3      = 1,
    I8008_STATE_STOPPED = 3,
    I8008_STATE_T4      = 7,
    I8008_STATE_T5      = 5,
};

enum i8008_regs {
    REG_A   = 0,
    REG_B   = 1,
    REG_C   = 2,
    REG_D   = 3,
    REG_E   = 4,
    REG_H   = 5,
    REG_L   = 6,
    REG_MEM = 7,
};

enum i8008_t2_ctrl {
    I8008_T2_CTRL_PCI = 0 << 6, // read instruction
    I8008_T2_CTRL_PCR = 2 << 6, // read data
    I8008_T2_CTRL_PCC = 1 << 6, // command I/O
    I8008_T2_CTRL_PCW = 3 << 6, // data write
    I8008_T2_CTRL_MSK = 3 << 6,
};

typedef uint8_t(i8008_io_func)(struct i8008_cpu* cpu, enum i8008_state state, uint8_t bus_out);

struct i8008_cpu {
    i8008_io_func* io;

    uint8_t regs[7];
    uint8_t flags;

    int stack_idx;
    uint16_t stack[8];

    int int_req;
    int int_cycle;
};

void i8008_init(struct i8008_cpu* cpu, i8008_io_func* io_func);
void i8008_cycle(struct i8008_cpu* cpu);
void i8008_int_req(struct i8008_cpu* cpu, int int_req);

#endif // 8008_H_INCLUDED
