/*
 * cmds.h - fatool command declarations
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _FATOOL_CMDS_H
#define _FATOOL_CMDS_H

#include "ufs.h"

/* All commands return 0 on success, non-zero on error */
struct fatool_context {
    struct ufs_super_block *sb;
    const char *device;
    const char *fstype;
};

/* Command function signature */
typedef int (*cmd_func_t)(struct fatool_context *ctx, int argc, char **argv);

/* Command entry */
struct cmd_entry {
    const char *name;
    cmd_func_t func;
    const char *help;
};

/* Command implementations */
int cmd_mount(struct fatool_context *ctx, int argc, char **argv);
int cmd_umount(struct fatool_context *ctx, int argc, char **argv);
int cmd_ls(struct fatool_context *ctx, int argc, char **argv);
int cmd_cat(struct fatool_context *ctx, int argc, char **argv);
int cmd_cp(struct fatool_context *ctx, int argc, char **argv);
int cmd_mv(struct fatool_context *ctx, int argc, char **argv);
int cmd_rm(struct fatool_context *ctx, int argc, char **argv);
int cmd_mkdir(struct fatool_context *ctx, int argc, char **argv);
int cmd_info(struct fatool_context *ctx, int argc, char **argv);

/* Command table */
static const struct cmd_entry cmd_table[] = {
    {"mount",  cmd_mount,  "mount <image> [fstype]  Mount a filesystem image"},
    {"umount", cmd_umount, "umount                  Unmount current filesystem"},
    {"ls",     cmd_ls,     "ls [path]               List directory contents"},
    {"cat",    cmd_cat,    "cat <file>              Display file contents"},
    {"cp",     cmd_cp,     "cp <src> <dst>          Copy file within image"},
    {"mv",     cmd_mv,     "mv <old> <new>          Rename file or directory"},
    {"rm",     cmd_rm,     "rm <path>               Remove a file"},
    {"mkdir",  cmd_mkdir,  "mkdir <path>            Create a directory"},
    {"info",   cmd_info,   "info [path]             Show filesystem info"},
    {NULL, NULL, NULL}
};

#endif /* _FATOOL_CMDS_H */
