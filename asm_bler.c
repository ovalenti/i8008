/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "asm_bler.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void append_byte(struct asm_ctx* ctx, uint8_t v)
{
    if (ctx->output_alloc <= ctx->pc) {
        ctx->output_alloc = ((ctx->pc / 1024) + 1) * 1024;
        ctx->output       = (uint8_t*)realloc(ctx->output, ctx->output_alloc);
    }
    ctx->output[ctx->pc++] = v;
}

static void trim(char** str)
{
    char* end;
    while (isblank(**str))
        (*str)++;

    end = (*str) + strlen(*str) - 1;
    while (end > *str && isblank(*end))
        *(end--) = '\0';
}

static void declare_symbol(struct asm_ctx* ctx, const char* sym_name)
{
    struct symbol* sym = (struct symbol*)malloc(sizeof(struct symbol));
    sym->name          = strdup(sym_name);
    sym->addr          = ctx->pc;

    sym->next    = ctx->symbols;
    ctx->symbols = sym;
}

static void declare_reference(struct asm_ctx* ctx, char* ref_name)
{
    struct reference* ref;
    char* mod;

    mod = strchr(ref_name, '/');
    if (mod)
        *(mod++) = '\0';
    else
        mod = "W";

    ref              = (struct reference*)malloc(sizeof(struct reference));
    ref->name        = strdup(ref_name);
    ref->addr        = ctx->pc;
    ref->line_number = ctx->current_line_number;

    switch (*mod) {
    case 'L':
        ref->mod = REF_MOD_L;
        break;
    case 'H':
        ref->mod = REF_MOD_H;
        break;
    case 'W':
    default:
        ref->mod = REF_MOD_L | REF_MOD_H;
    }

    ref->next       = ctx->references;
    ctx->references = ref;

    // reserve room for the address
    if (ref->mod & REF_MOD_L)
        append_byte(ctx, 0);
    if (ref->mod & REF_MOD_H)
        append_byte(ctx, 0);
}

static int link(struct asm_ctx* ctx)
{
    struct reference* ref = ctx->references;

    while (ref) {
        struct symbol* sym = ctx->symbols;
        int target_addr    = ref->addr;

        while (sym) {
            if (0 == strcmp(sym->name, ref->name))
                break;
            sym = sym->next;
        }

        if (!sym) {
            ctx->status                = ASM_ST_ERR_SYM;
            ctx->status_detail.err_sym = ref;
            return 1;
        }
        if (ref->mod & REF_MOD_L)
            ctx->output[target_addr++] = sym->addr;
        if (ref->mod & REF_MOD_H)
            ctx->output[target_addr] = sym->addr >> 8;

        ref = ref->next;
    }

    return 0;
}

static void parse_label(struct asm_ctx* ctx, char** line)
{
    char* colon;

    colon = strchr(*line, ':');
    if (colon) {
        char* label = *line;
        *colon      = '\0';
        trim(&label);
        declare_symbol(ctx, label);
        *line = colon + 1;
    }
}

static int letter2register(char l, uint8_t* r)
{
    switch (l) {
    case 'A':
        *r = 0;
        break;
    case 'B':
        *r = 1;
        break;
    case 'C':
        *r = 2;
        break;
    case 'D':
        *r = 3;
        break;
    case 'E':
        *r = 4;
        break;
    case 'H':
        *r = 5;
        break;
    case 'L':
        *r = 6;
        break;
    case 'M':
        *r = 7;
        break;
    default:
        return 1;
    }
    return 0;
}

static struct {
    const char* name;
    uint8_t code;
} alu_op_list[] = { { .name = "AD", .code = 0 }, { .name = "AC", .code = 1 }, { .name = "SU", .code = 2 },
                    { .name = "SB", .code = 3 }, { .name = "ND", .code = 4 }, { .name = "XR", .code = 5 },
                    { .name = "OR", .code = 6 }, { .name = "_P", .code = 7 }, { 0 } };

static int alu_op(const char* instr, uint8_t* op)
{
    int i = 0;
    while (alu_op_list[i].name) {
        if (0 == strncmp(alu_op_list[i].name, instr, 2)) {
            *op = alu_op_list[i].code;
            return 0;
        }
        i++;
    }
    return 1;
}

static uint8_t letter2cond(char l)
{
    switch (l) {
    case 'C':
        return 0;
    case 'Z':
        return 1;
    case 'S':
        return 2;
    case 'P':
        return 3;
    }
    return 0;
}

static int parse_instr(struct asm_ctx* ctx, char* instr)
{
    int instr_len = strlen(instr);
    char original_instr[8];

    strncpy(original_instr, instr, sizeof(original_instr) - 1);

    if (instr_len < 3)
        goto error;

    if (0 == strcmp(instr, ".org")) {
        ctx->dot_org = 1;
        return 0;
    }

    if (0 == strcmp(instr, ".set"))
        return 0;

    if (0 == strncmp(instr, "INP", 3)) {
        // INP/X
        int port;
        if (instr_len < 5)
            goto error;

        port = strtoul(instr + 4, NULL, 0);
        append_byte(ctx, 0x41 | (port << 1));
        return 0;
    }

    if (0 == strncmp(instr, "OUT", 3)) {
        // OUT/X
        int port;
        if (instr_len < 5)
            goto error;

        port = strtoul(instr + 4, NULL, 0);
        append_byte(ctx, 0x71 | (port << 1));
        return 0;
    }

    if (0 == strncmp(instr, "RST", 3)) {
        // RST/X
        int vect;
        if (instr_len < 5)
            goto error;

        vect = strtoul(instr + 4, NULL, 0);
        append_byte(ctx, 0x05 | (vect << 3));
        return 0;
    }

    if (0 == strncmp(instr, "RLC", 3)) {
        append_byte(ctx, 0x02);
        return 0;
    }

    if (0 == strncmp(instr, "RRC", 3)) {
        append_byte(ctx, 0x0C);
        return 0;
    }

    if (0 == strncmp(instr, "RAL", 3)) {
        append_byte(ctx, 0x12);
        return 0;
    }

    if (0 == strncmp(instr, "RAR", 3)) {
        append_byte(ctx, 0x1A);
        return 0;
    }

    // disambiguate CP, CA, CF and CT
    if (0 == strncmp(instr, "CP", 2))
        instr[0] = '_';

    switch (instr[0]) {
    case 'L': {
        uint8_t d, s;
        if (letter2register(instr[1], &d))
            goto error;
        if (instr[2] == 'I') {
            // LrI
            append_byte(ctx, 0x6 | (d << 3));
        } else {
            // Lrr
            if (letter2register(instr[2], &s))
                goto error;
            append_byte(ctx, 0xC0 | (d << 3) | s);
        }
    } break;
    case 'I':
    case 'D': {
        // INr DCr
        uint8_t d;
        if (letter2register(instr[2], &d))
            goto error;
        append_byte(ctx, (d << 3) | (instr[1] == 'C' ? 1 : 0));
    } break;
    case 'O':
    case 'A':
    case 'S':
    case 'N':
    case 'X':
    case '_': {
        // ALU
        uint8_t op;
        if (alu_op(instr, &op))
            goto error;
        if (instr[2] == 'I') {
            // xxI
            append_byte(ctx, 0x04 | (op << 3));
        } else {
            // xxr
            uint8_t s;
            if (letter2register(instr[2], &s))
                goto error;
            append_byte(ctx, 0x80 | (op << 3) | s);
        }
    } break;
    case 'J': // Jxx
    case 'C': // Cxx
    case 'R': // Rxx
    {
        uint8_t op;
        switch (instr[0]) {
        case 'J':
            op = 0x40;
            break;
        case 'C':
            op = 0x42;
            break;
        default:
            op = 0x03;
            break;
        }
        if ((instr[1] == 'F') || (instr[1] == 'T')) {
            if (instr[1] == 'T')
                op |= 0x20;
        } else {
            op |= 0x4;
        }
        append_byte(ctx, op | (letter2cond(instr[2]) << 3));
    } break;

    case 'H':
        // HALT
        append_byte(ctx, 0);
        break;
    default:
        goto error;
    }
    return 0;

error:
    ctx->status = ASM_ST_ERR_INSTR;
    strncpy(ctx->status_detail.err_instr, original_instr, sizeof(ctx->status_detail.err_instr) - 1);
    return 1;
}

static int parse_param(struct asm_ctx* ctx, char* param)
{
    if (*param == '\0')
        return 0;

    if (ctx->dot_org) {
        ctx->pc      = strtoul(param, NULL, 0);
        ctx->dot_org = 0;
        return 0;
    }

    if (*param == '\'') {
        append_byte(ctx, param[1]);
    } else if (isdigit(*param)) {
        uint8_t v = strtoul(param, NULL, 0);
        append_byte(ctx, v);
    } else {
        declare_reference(ctx, param);
    }
    return 0;
}

void asm_ble(struct asm_ctx* ctx, int (*nextc)(void*), void* arg)
{
    char buffer[256];
    int c = 0;

    while (c >= 0) {
        int line_len;
        int comment = 0;
        char* ptr   = buffer;

        ctx->current_line_number++;

        for (line_len = 0, c = nextc(arg); line_len < (sizeof(buffer) - 1) && c >= 0 && c != '\n'; c = nextc(arg)) {
            if (c == ';')
                comment = 1;
            if (!comment)
                buffer[line_len++] = c;
        }

        buffer[line_len] = '\0';

        parse_label(ctx, &ptr);
        trim(&ptr);

        ptr = strtok(ptr, "\t ");
        if (!ptr)
            continue;
        if (parse_instr(ctx, ptr))
            return;

        while ((ptr = strtok(NULL, "\t "))) {
            if (parse_param(ctx, ptr))
                return;
        }
    }
    link(ctx);
}

void asm_free(struct asm_ctx* ctx)
{

    while (ctx->references) {
        struct reference* ref = ctx->references;
        ctx->references       = ctx->references->next;

        free(ref->name);
        free(ref);
    }

    while (ctx->references) {
        struct symbol* sym = ctx->symbols;
        ctx->symbols       = ctx->symbols->next;

        free(sym->name);
        free(sym);
    }

    if (ctx->output)
        free(ctx->output);
}
