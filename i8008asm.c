/*
 * Copyright (c) 2022, Olivier Valentin
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdio.h>

#include "asm_bler.h"

int main()
{
    struct asm_ctx ctx = { 0 };

    asm_ble(&ctx, (int (*)(void*)) & getc, stdin);

    switch (ctx.status) {
    case ASM_ST_OK:
        fwrite(ctx.output, ctx.pc, 1, stdout);
        fprintf(stderr, "success\n");
        break;
    case ASM_ST_ERR_INSTR:
        fprintf(stderr, "Invalid instruction '%s' at line %d\n", ctx.status_detail.err_instr, ctx.current_line_number);
        return 1;
    case ASM_ST_ERR_SYM:
        fprintf(stderr, "Unknown symbol '%s' at line %d\n", ctx.status_detail.err_sym->name,
                ctx.status_detail.err_sym->line_number);
        return 1;
    }

    return 0;
}
