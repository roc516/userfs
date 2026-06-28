/*
 * cmd_ls.c - fatool ls command
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "vfs/ufs.h"
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_ls(struct fatool_context *ctx, int argc, char **argv)
{
    const char *path = "/";
    char **names = NULL;
    int count = 0;
    int i;

    if (!ctx->sb) {
        fprintf(stderr, "No filesystem mounted. Use 'fatool mount' first.\n");
        return 1;
    }

    if (argc > 1)
        path = argv[1];

    int err = ufs_listdir(ctx->sb, path, &names, &count);
    if (err) {
        fprintf(stderr, "Failed to list '%s': error %d\n", path, err);
        return 1;
    }

    struct ufs_stat st;
    for (i = 0; i < count; i++) {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s",
                 strcmp(path, "/") == 0 ? "" : path, names[i]);

        if (ufs_stat(ctx->sb, fullpath, &st) == 0) {
            printf("%c %10llu  %s\n",
                   S_ISDIR(st.st_mode) ? 'd' : '-',
                   (unsigned long long)st.st_size,
                   names[i]);
        } else {
            printf("? %10s  %s\n", "-", names[i]);
        }
        free(names[i]);
    }
    free(names);

    return 0;
}
