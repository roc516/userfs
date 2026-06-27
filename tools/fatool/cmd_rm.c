/*
 * cmd_rm.c - fatool rm command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cmds.h"
#include <stdio.h>

int cmd_rm(struct fatool_context *ctx, int argc, char **argv)
{
    int err;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted.\n");
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "Usage: fatool rm <path>\n");
        return 1;
    }

    err = ufs_unlink(ctx->sb, argv[1]);
    if (err) {
        fprintf(stderr, "Failed to remove '%s': error %d\n", argv[1], err);
        return 1;
    }

    return 0;
}
