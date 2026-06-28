/*
 * cmd_mount.c - fatool mount command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "vfs/ufs.h"
#include "cmds.h"
#include <stdio.h>

int cmd_mount(struct fatool_context *ctx, int argc, char **argv)
{
    const char *device;
    const char *fstype = "vfat";
    int err;

    if (argc < 2) {
        fprintf(stderr, "Usage: fatool mount <image> [fstype]\n");
        return 1;
    }

    device = argv[1];
    if (argc > 2)
        fstype = argv[2];

    err = ufs_mount(device, fstype, &ctx->sb);
    if (err) {
        fprintf(stderr, "Failed to mount '%s' as %s: error %d\n",
                device, fstype, err);
        return 1;
    }

    ctx->device = device;
    ctx->fstype = fstype;
    printf("Mounted '%s' as %s\n", device, fstype);
    return 0;
}
