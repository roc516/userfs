/*
 * main.c - fatool CLI entry point
 *
 * Each command that accesses a filesystem takes the device path
 * as its first argument (or uses the current mount).
 *
 * Usage:
 *   fatool mount <device> [fstype]   - mount and keep state
 *   fatool ls [path]                 - uses current mount
 *   fatool cat <file>                - uses current mount
 *   fatool umount                    - unmount current
 *
 * Alternative (single-shot):
 *   fatool --img <device> <cmd> [args...]
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmds.h"

/* Global mount state (persists within one process invocation) */
static struct fatool_context g_ctx;
static int g_mount_stored = 0;

static void print_usage(void)
{
    int i;
    fprintf(stderr, "Usage: fatool <command> [args...]\n\n");
    fprintf(stderr, "Commands:\n");
    for (i = 0; cmd_table[i].name; i++) {
        fprintf(stderr, "  %-20s %s\n", cmd_table[i].name, cmd_table[i].help);
    }
    fprintf(stderr, "\nYou can chain commands in a single invocation:\n");
    fprintf(stderr, "  fatool mount fat32.img vfat ls / cat /hello.txt umount\n");
}

static int run_command(const char *cmd_name, struct fatool_context *ctx,
                       int argc, char **argv)
{
    int i;
    for (i = 0; cmd_table[i].name; i++) {
        if (strcmp(cmd_table[i].name, cmd_name) == 0)
            return cmd_table[i].func(ctx, argc, argv);
    }
    fprintf(stderr, "Unknown command: %s\n", cmd_name);
    return 1;
}

int main(int argc, char **argv)
{
    int i, ret = 0;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (ufs_init() < 0) {
        fprintf(stderr, "Failed to initialize UFS library\n");
        return 1;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));

    /* Parse and execute commands sequentially */
    i = 1;
    while (i < argc) {
        const char *cmd = argv[i];
        int cmd_argc;
        char **cmd_argv;

        /* Count args for this command (until next known command or end) */
        int j = i + 1;
        int is_known;

        do {
            is_known = 0;
            int k;
            if (j >= argc) break;
            for (k = 0; cmd_table[k].name; k++) {
                if (strcmp(argv[j], cmd_table[k].name) == 0) {
                    is_known = 1;
                    break;
                }
            }
            if (is_known) break;
            j++;
        } while (j < argc);

        cmd_argc = j - i;
        cmd_argv = argv + i;

        ret = run_command(cmd, &g_ctx, cmd_argc, cmd_argv);
        if (ret != 0)
            break;

        i = j;
    }

    /* Cleanup */
    if (g_ctx.sb) {
        ufs_umount(g_ctx.sb);
        g_ctx.sb = NULL;
    }
    ufs_cleanup();
    return ret;
}
