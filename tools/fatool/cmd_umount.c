/*
 * cmd_umount.c - fatool umount command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cmds.h"
#include <stdio.h>

int cmd_umount(struct fatool_context *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted\n");
        return 1;
    }

    /* Perform unmount immediately */
    int err = ufs_umount(ctx->sb);
    if (err) {
        fprintf(stderr, "Unmount failed: %d\n", err);
        return 1;
    }
    ctx->sb = NULL;
    ctx->device = NULL;
    ctx->fstype = NULL;
    printf("Unmounted.\n");
    return 0;
}
