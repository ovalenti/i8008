/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <assert.h>
#include <string.h>

#include "i8008.h"

#define MEM_PTR(cpu) (((uint16_t)(cpu)->regs[REG_H]) << 8 | (cpu)->regs[REG_L])
#define PC(cpu) ((cpu)->stack[(cpu)->stack_idx])
#define FIELD(value, left, right) (((value) >> (right)) & ((1 << ((left) + 1 - (right))) - 1))

enum i8008_flags {
    I8008_F_CARRY  = 0,
    I8008_F_ZERO   = 1,
    I8008_F_SIGN   = 2,
    I8008_F_PARITY = 3,
};

enum i8008_ual_op {
    I8008_OP_ADD  = 0,
    I8008_OP_ADDC = 1,
    I8008_OP_SUB  = 2,
    I8008_OP_SUBB = 3,
    I8008_OP_AND  = 4,
    I8008_OP_XOR  = 5,
    I8008_OP_OR   = 6,
    I8008_OP_CMP  = 7,
    I8008_OP_INC, // required to prevent carry modification
    I8008_OP_DEC, // idem
};

static uint8_t mem_fetch_byte(struct i8008_cpu* cpu, uint16_t addr, int is_instr, int is_inter)
{
    addr = addr & 0x3FFF;

    cpu->io(cpu, is_inter ? I8008_STATE_T1I : I8008_STATE_T1, addr);
    cpu->io(cpu, I8008_STATE_T2, addr >> 8 | (is_instr ? I8008_T2_CTRL_PCI : I8008_T2_CTRL_PCR));

    return cpu->io(cpu, I8008_STATE_T3, 0);
}

static void mem_write_byte(struct i8008_cpu* cpu, uint16_t addr, uint8_t value)
{
    addr = addr & 0x3FFF;

    cpu->io(cpu, I8008_STATE_T1, addr);
    cpu->io(cpu, I8008_STATE_T2, (addr >> 8) | I8008_T2_CTRL_PCW);

    cpu->io(cpu, I8008_STATE_T3, value);
}

static void inc_pc(struct i8008_cpu* cpu)
{
    if (!cpu->int_cycle)
        PC(cpu)++;
}

static int parity(uint8_t v)
{
    int p = 0;
    while (v) {
        p = !p;
        v &= v - 1;
    }
    return p;
}

static void update_flags(struct i8008_cpu* cpu, uint8_t v)
{
    cpu->flags &= I8008_F_CARRY;
    cpu->flags |=
        !v ? (1 << I8008_F_ZERO) : 0 | (v & 0x80) ? (1 << I8008_F_SIGN) : 0 | parity(v) ? (1 << I8008_F_PARITY) : 0;
}

static void update_carry(struct i8008_cpu* cpu, int c)
{
    if (!!c != !!(cpu->flags & I8008_F_CARRY))
        cpu->flags ^= I8008_F_CARRY;
}

static void instr_INVAL(struct i8008_cpu* cpu, uint8_t op_code)
{
    // TODO
}

static void instr_HALT(struct i8008_cpu* cpu, uint8_t op_code)
{
    cpu->io(cpu, I8008_STATE_STOPPED, 0);
    assert(cpu->int_req);
}

static void instr_LOAD(struct i8008_cpu* cpu, uint8_t op_code, int immediate)
{
    unsigned int dst, src;
    int t4_done_in_current_cycle = 0;
    uint8_t reg_b;

    // 1 1  D D D  S S S
    dst = FIELD(op_code, 5, 3);
    src = FIELD(op_code, 2, 0);

    if (dst == REG_MEM && src == REG_MEM) {
        instr_HALT(cpu, op_code);
        return;
    }

    // read source
    if (immediate) {
        reg_b = mem_fetch_byte(cpu, PC(cpu), 0, 0);
        inc_pc(cpu);
    } else {
        if (src == REG_MEM) {
            reg_b = mem_fetch_byte(cpu, MEM_PTR(cpu), 0, 0);
        } else {
            reg_b = cpu->regs[src];
            cpu->io(cpu, I8008_STATE_T4, reg_b);
            t4_done_in_current_cycle = 1;
        }
    }

    // write destination
    if (dst == REG_MEM) {
        mem_write_byte(cpu, MEM_PTR(cpu), reg_b);
    } else {
        if (!t4_done_in_current_cycle)
            cpu->io(cpu, I8008_STATE_T4, reg_b);
        cpu->regs[dst] = reg_b;
        cpu->io(cpu, I8008_STATE_T5, reg_b);
    }
}

// {src|imm} op dst -> dst
static void instr_ALU(struct i8008_cpu* cpu, enum i8008_ual_op op, enum i8008_regs src, enum i8008_regs dst,
                      int immediate)
{
    uint8_t reg_b;
    uint16_t result;

    // read source
    if (op == I8008_OP_INC || op == I8008_OP_DEC) {
        reg_b = 1;
    } else if (immediate) {
        reg_b = mem_fetch_byte(cpu, PC(cpu), 0, 0);
        inc_pc(cpu);
    } else {
        if (src == REG_MEM) {
            reg_b = mem_fetch_byte(cpu, MEM_PTR(cpu), 0, 0);
        } else {
            reg_b = cpu->regs[src];
        }
        cpu->io(cpu, I8008_STATE_T4, reg_b);
    }

    result = cpu->regs[dst];

    // operation
    switch (op) {
    case I8008_OP_ADDC:
        if (cpu->flags & I8008_F_CARRY)
            result++;
    case I8008_OP_ADD:
    case I8008_OP_INC:
        result += reg_b;
        break;
    case I8008_OP_SUBB:
        if (cpu->flags & I8008_F_CARRY)
            result--;
    case I8008_OP_SUB:
    case I8008_OP_DEC:
    case I8008_OP_CMP:
        result -= reg_b;
        break;
    case I8008_OP_AND:
        result &= reg_b;
        break;
    case I8008_OP_XOR:
        result ^= reg_b;
        break;
    case I8008_OP_OR:
        result |= reg_b;
        break;
    }

    // store result
    if (op != I8008_OP_CMP)
        cpu->regs[dst] = result;

    update_flags(cpu, result);

    if (op != I8008_OP_INC && op != I8008_OP_DEC)
        update_carry(cpu, result & 0x100);
}

static void instr_INCDEC(struct i8008_cpu* cpu, uint8_t op_code)
{
    unsigned int dst;

    // 0 0  D D D  0 0 I/D
    dst = FIELD(op_code, 5, 3);

    if (dst == REG_A) {
        instr_HALT(cpu, op_code);
        return;
    }

    if (dst == REG_MEM) {
        instr_INVAL(cpu, op_code);
        return;
    }

    instr_ALU(cpu, op_code & 1 ? I8008_OP_DEC : I8008_OP_INC, 0, dst, 0);
}

static void instr_ROT(struct i8008_cpu* cpu, uint8_t op_code)
{
    uint8_t* a = &cpu->regs[REG_A];
    int a7     = (*a & 0x80) >> 7;
    int a0     = (*a & 0x01);
    int carry  = (cpu->flags & I8008_F_CARRY) ? 1 : 0;

    switch (op_code >> 3) {
    case 0: // RLC
        *a <<= 1;
        *a |= a7;
        carry = a7;
        break;
    case 1: // RRC
        *a >>= 1;
        *a |= (a0 << 7);
        carry = a0;
        break;
    case 2: // RAL
        *a <<= 1;
        *a |= carry;
        carry = a7;
        break;
    case 3: // RAR
        *a >>= 1;
        *a |= (carry << 7);
        carry = a0;
        break;
    }
    update_carry(cpu, carry);
}

static void instr_JMPCALL(struct i8008_cpu* cpu, uint8_t op_code)
{
    int do_jump = 0;
    int is_a_call;

    // JMP 0 1  X X X  1 0 0
    // JFc 0 1  0 C C  0 0 0
    // JFc 0 1  1 C C  0 0 0
    // CAL 0 1  X X X  1 1 0
    // CFc 0 1  0 C C  0 1 0
    // CTc 0 1  1 C C  0 1 0

    is_a_call = op_code & 0x2;

    // determine what to do
    if (op_code & 0x4) {
        // JMP
        do_jump = 1;
    } else {
        // JFc, JTc
        int flag_idx = FIELD(op_code, 4, 3);
        int flag_val = cpu->flags & (1 << flag_idx);

        if (op_code & 0x20)
            do_jump = flag_val; // JTc / CTc
        else
            do_jump = !flag_val; // JFc / CFc
    }

    // actual jump
    if (do_jump) {
        uint8_t reg_b, reg_a;
        reg_b = mem_fetch_byte(cpu, PC(cpu), 0, 0);
        inc_pc(cpu);
        reg_a = mem_fetch_byte(cpu, PC(cpu), 0, 0);
        inc_pc(cpu);
        cpu->io(cpu, I8008_STATE_T4, reg_a);
        cpu->io(cpu, I8008_STATE_T5, reg_b);

        if (is_a_call)
            cpu->stack_idx = (cpu->stack_idx + 1) % 8;

        PC(cpu) = FIELD(reg_a, 5, 0);
        PC(cpu) <<= 8;
        PC(cpu) |= reg_b;
    } else {
        // skip the address
        inc_pc(cpu);
        inc_pc(cpu);
    }
}

static void instr_RET(struct i8008_cpu* cpu, uint8_t op_code)
{
    // RET 0 0  X X X  1 1 1
    // RFc 0 0  0 C C  0 1 1
    // RTc 0 0  1 C C  0 1 1

    int do_return;

    // determine what to do
    if (op_code & 0x4) {
        // RET
        do_return = 1;
    } else {
        // RFc, RTc
        int flag_idx = FIELD(op_code, 4, 3);
        int flag_val = cpu->flags & (1 << flag_idx);

        if (op_code & 0x20)
            do_return = flag_val; // RTc / RTc
        else
            do_return = !flag_val; // RFc / RFc
    }

    if (do_return) {
        cpu->stack_idx = (cpu->stack_idx + 7) % 8;
        cpu->io(cpu, I8008_STATE_T4, 0);
        cpu->io(cpu, I8008_STATE_T5, 0);
    }
}

static void instr_RST(struct i8008_cpu* cpu, uint8_t op_code)
{
    // 0 0  A A A  1 0 1

    // return address
    cpu->stack_idx = (cpu->stack_idx + 1) % 8;

    PC(cpu) = op_code & 0x38;

    cpu->io(cpu, I8008_STATE_T4, 0);
    cpu->io(cpu, I8008_STATE_T5, PC(cpu));
}

static void instr_IO(struct i8008_cpu* cpu, uint8_t op_code)
{
    // INP 0 1  0 0 M  M M 1
    // OUT 0 1  R R M  M M 1
    uint8_t reg_b;
    int r = FIELD(op_code, 5, 4);

    cpu->io(cpu, I8008_STATE_T1, cpu->regs[REG_A]);
    cpu->io(cpu, I8008_STATE_T2, op_code); // opcode prefix matches PCC cycle bits

    if (r == 0) {
        reg_b = cpu->io(cpu, I8008_STATE_T3, 0);
        cpu->io(cpu, I8008_STATE_T4, cpu->flags);
        cpu->regs[REG_A] = reg_b;
        cpu->io(cpu, I8008_STATE_T5, reg_b);
    } else {
        cpu->io(cpu, I8008_STATE_WAIT, 0);
    }
}

void i8008_init(struct i8008_cpu* cpu, i8008_io_func* io_func)
{
    memset(cpu, 0, sizeof(*cpu));

    cpu->io = io_func;

    instr_HALT(cpu, 0); // boot in STOPPED state
}

void i8008_cycle(struct i8008_cpu* cpu)
{
    uint8_t op_code;

    if (cpu->int_req)
        cpu->int_cycle = 1;

    op_code = mem_fetch_byte(cpu, PC(cpu), 1, cpu->int_cycle);
    inc_pc(cpu);

    switch (FIELD(op_code, 7, 6)) {
    case 0: // 0 0  X X X  X X X
        switch (FIELD(op_code, 2, 0)) {
        case 0: // 0 0  X X X  0 0 X
        case 1:
            instr_INCDEC(cpu, op_code);
            break;
        case 2: // 0 0  X X X  0 1 0
            instr_ROT(cpu, op_code);
            break;
        case 3: // 0 0  X X X  X 1 1
        case 7:
            instr_RET(cpu, op_code);
            break;
        case 4: // 0 0  X X X  1 0 0
            instr_ALU(cpu, (op_code >> 3) & 0x7, 0, REG_A, 1);
            break;
        case 5: // 0 0  X X X  1 0 1
            instr_RST(cpu, op_code);
            break;
        case 6: // 0 0  X X X  1 1 0
            instr_LOAD(cpu, op_code, 1);
            break;
        }
        break;
    case 1:
        if (op_code & 1) // 0 1  X X X  X X 1
            instr_IO(cpu, op_code);
        else // 0 1  X X X  X X 0
            instr_JMPCALL(cpu, op_code);
        break;
    case 2:
        instr_ALU(cpu, FIELD(op_code, 5, 3), FIELD(op_code, 2, 0), REG_A, 0);
        break;
    case 3:
        instr_LOAD(cpu, op_code, 0);
        break;
    }

    cpu->int_cycle = 0;
}

void i8008_int_req(struct i8008_cpu* cpu, int int_req) { cpu->int_req = int_req; }
