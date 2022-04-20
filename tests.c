/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdio.h>
#include <stdlib.h>

#include "asm_bler.h"

struct feed_ctx {
    char* str;
    int idx;
};

static int feed(void* arg)
{
    struct feed_ctx* ctx = (struct feed_ctx*)arg;
    if (ctx->str[ctx->idx] == '\0')
        return -1;

    return ctx->str[ctx->idx++];
}

static void assert(int v, const char* fnct, const char* test)
{
    if (!v) {
        fprintf(stderr, "failure: %s %s\n", fnct, test);
        exit(1);
    }
}

#define ASSERT(v) assert(v, __FUNCTION__, #v)

static void test_lai()
{
    struct asm_ctx ctx     = { 0 };
    struct feed_ctx feeder = { .str = "LAI 0x42", 0 };

    asm_ble(&ctx, &feed, &feeder);

    ASSERT(ctx.pc == 2);
    ASSERT(ctx.output[0] == 0x06);
    ASSERT(ctx.output[1] == 0x42);

    asm_free(&ctx);
}

static void test_jmp()
{
    struct asm_ctx ctx     = { 0 };
    struct feed_ctx feeder = { .str = ".org 0x40\n\nloop: ADI 1\n\tJMP loop", 0 };

    asm_ble(&ctx, &feed, &feeder);

    ASSERT(ctx.pc == 0x45);
    ASSERT((ctx.output[0x40 + 2] & 0xC7) == 0x44);
    ASSERT(ctx.output[0x40 + 3] == 0x40);
    ASSERT(ctx.output[0x40 + 4] == 0x00);

    asm_free(&ctx);
}

static void test_ret()
{
    struct asm_ctx ctx     = { 0 };
    struct feed_ctx feeder = { .str = "RET", 0 };

    asm_ble(&ctx, &feed, &feeder);

    ASSERT(ctx.pc == 0x1);
    ASSERT((ctx.output[0] & 0xC7) == 0x07);

    asm_free(&ctx);
}

static void test_lam()
{
    struct asm_ctx ctx     = { 0 };
    struct feed_ctx feeder = { .str = "LAM", 0 };

    asm_ble(&ctx, &feed, &feeder);

    ASSERT(ctx.pc == 0x1);
    ASSERT(ctx.output[0] == 0xC7);

    asm_free(&ctx);
}

static void test_set()
{
    struct asm_ctx ctx     = { 0 };
    struct feed_ctx feeder = { .str = ".set ' '", 0 };

    asm_ble(&ctx, &feed, &feeder);

    ASSERT(ctx.pc == 0x1);
    ASSERT(ctx.output[0] == ' ');

    asm_free(&ctx);
}

int main()
{
    test_lai();
    test_jmp();
    test_ret();
    test_lam();
    test_set();

    fprintf(stdout, "Passed\n");
    return 0;
}
