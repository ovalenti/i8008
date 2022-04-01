/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef ASM_BLER_H_
#define ASM_BLER_H_

#include <inttypes.h>

struct asm_ctx {
    int pc;
    int current_line_number;

    uint8_t* output;
    int output_alloc;

    int dot_org;

    struct symbol {
        char* name;
        int addr;
        struct symbol* next;
    } * symbols;

    struct reference {
        char* name;
        int addr;
        enum {
            REF_MOD_L = 1 << 0,
            REF_MOD_H = 1 << 1,
        } mod;
        int line_number;
        struct reference* next;
    } * references;

    enum asm_status {
        ASM_ST_OK = 0,
        ASM_ST_ERR_SYM,
        ASM_ST_ERR_INSTR,
    } status;
    union {
        struct reference* err_sym;
        char err_instr[8];
    } status_detail;
};

void asm_ble(struct asm_ctx* ctx, int (*nextc)(void*), void* arg);

void asm_free(struct asm_ctx* ctx);

#endif /* ASM_BLER_H_ */
