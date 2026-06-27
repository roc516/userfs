/*
 * libufs.c - Library initialization and cleanup
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "internal.h"

/* Forward declarations for builtin filesystems */
extern struct ufs_filesystem_type ufs_fat_fs_type;

int ufs_init(void)
{
    int err;

    /* Register builtin filesystem types */
    err = ufs_register_fs(&ufs_fat_fs_type);
    if (err) {
        ufs_log_err("failed to register FAT filesystem: %d", err);
        return err;
    }

    ufs_log_debug("UserFS initialized");
    return 0;
}

void ufs_cleanup(void)
{
    /* Unregister builtin filesystem types */
    ufs_unregister_fs(&ufs_fat_fs_type);
}
