/*
 * cmd_mkdir.c - fatool mkdir command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cmds.h"
#include <stdio.h>

int cmd_mkdir(struct fatool_context *ctx, int argc, char **argv)
{
    int err;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted.\n");
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "Usage: fatool mkdir <path>\n");
        return 1;
    }

    err = ufs_mkdir(ctx->sb, argv[1], 0755);
    if (err) {
        fprintf(stderr, "Failed to create '%s': error %d\n", argv[1], err);
        return 1;
    }

    return 0;
}
