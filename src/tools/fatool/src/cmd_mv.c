/*
 * cmd_mv.c - fatool mv command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "vfs/ufs.h"
#include "cmds.h"
#include <stdio.h>

int cmd_mv(struct fatool_context *ctx, int argc, char **argv)
{
    int err;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted.\n");
        return 1;
    }

    if (argc < 3) {
        fprintf(stderr, "Usage: fatool mv <old> <new>\n");
        return 1;
    }

    err = ufs_rename(ctx->sb, argv[1], argv[2]);
    if (err) {
        fprintf(stderr, "Failed to rename '%s' to '%s': error %d\n",
                argv[1], argv[2], err);
        return 1;
    }

    return 0;
}
