/*
 * cmd_cp.c - fatool cp command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "vfs/ufs.h"
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int cmd_cp(struct fatool_context *ctx, int argc, char **argv)
{
    struct ufs_file *dst_file = NULL;
    FILE *host_src = NULL;
    char *buf;
    ssize_t n;
    int err;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted.\n");
        return 1;
    }

    if (argc < 3) {
        fprintf(stderr, "Usage: fatool cp <host_src> <img_dst>\n");
        return 1;
    }

    /* Open source from host filesystem */
    host_src = fopen(argv[1], "rb");
    if (!host_src) {
        fprintf(stderr, "Failed to open host source '%s': %s\n",
                argv[1], strerror(errno));
        return 1;
    }

    /* Read entire source into buffer */
    buf = malloc(1024 * 1024);
    if (!buf) {
        fclose(host_src);
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    n = (ssize_t)fread(buf, 1, 1024 * 1024, host_src);
    if (n < 0) {
        free(buf);
        fclose(host_src);
        fprintf(stderr, "Read error from host '%s'\n", argv[1]);
        return 1;
    }
    fclose(host_src);

    /* Remove destination first if it exists */
    ufs_unlink(ctx->sb, argv[2]);

    /* Create the file on the image */
    err = ufs_create(ctx->sb, argv[2], 0644, &dst_file);
    if (err) {
        free(buf);
        fprintf(stderr, "Failed to create '%s' on image: %d\n",
                argv[2], err);
        return 1;
    }

    /* Write data */
    err = (int)ufs_write(dst_file, buf, (size_t)n);
    if (err < 0) {
        free(buf);
        ufs_close(dst_file);
        fprintf(stderr, "Write error: %d\n", err);
        return 1;
    }

    ufs_close(dst_file);
    printf("cp: %zd bytes copied from '%s' to '%s'\n", n, argv[1], argv[2]);
    free(buf);
    return 0;
}
