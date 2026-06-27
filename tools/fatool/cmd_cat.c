/*
 * cmd_cat.c - fatool cat command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>

int cmd_cat(struct fatool_context *ctx, int argc, char **argv)
{
    struct ufs_file *file;
    char buf[4096];
    ssize_t n;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted.\n");
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "Usage: fatool cat <file>\n");
        return 1;
    }

    int err = ufs_open(ctx->sb, argv[1], &file);
    if (err) {
        fprintf(stderr, "Failed to open '%s': error %d\n", argv[1], err);
        return 1;
    }

    while ((n = ufs_read(file, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
    }

    ufs_close(file);
    return 0;
}
