/*
 * cmd_info.c - fatool info command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cmds.h"
#include <stdio.h>

int cmd_info(struct fatool_context *ctx, int argc, char **argv)
{
    struct ufs_statfs buf;
    (void)argc;
    (void)argv;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted.\n");
        return 1;
    }

    printf("Filesystem info:\n");
    printf("  Block size:    %u\n", ctx->sb->s_blocksize);
    printf("  Max file size: %llu\n", (unsigned long long)ctx->sb->s_maxbytes);

    if (ufs_statfs(ctx->sb, &buf) == 0) {
        printf("  Total blocks:  %lu\n", buf.f_blocks);
        printf("  Free blocks:   %lu\n", buf.f_bfree);
        printf("  Block size:    %lu\n", buf.f_bsize);
        printf("  Total inodes:  %lu\n", buf.f_files);
        printf("  Free inodes:   %lu\n", buf.f_ffree);
        printf("  Max namelen:   %lu\n", buf.f_namelen);
    }

    return 0;
}
