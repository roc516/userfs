/*
 * super.c - Super block management
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include <stdlib.h>
#include "internal.h"

/*
 * Initialize a super block with common fields.
 * Called by filesystem-specific mount routines.
 */
int ufs_super_init(struct ufs_super_block *sb, struct ufs_bdev *bdev,
                   struct ufs_filesystem_type *fs_type)
{
    memset(sb, 0, sizeof(*sb));
    sb->s_bdev = bdev;
    sb->s_fs_type = fs_type;
    sb->s_fs_info = NULL;
    sb->s_blocksize = bdev->sector_size;
    sb->s_blocksize_bits = 9;  /* 512 bytes default */
    sb->s_op = NULL;
    sb->s_flags = 0;
    sb->s_maxbytes = (loff_t)bdev->total_sectors * bdev->sector_size;
    return 0;
}

void ufs_super_destroy(struct ufs_super_block *sb)
{
    if (!sb) return;

    /* Call filesystem-specific cleanup */
    if (sb->s_op && sb->s_op->put_super)
        sb->s_op->put_super(sb);

    /* Free fs private data */
    free(sb->s_fs_info);
    sb->s_fs_info = NULL;

    /* Close block device */
    if (sb->s_bdev)
        ufs_bdev_close(sb->s_bdev);
    sb->s_bdev = NULL;
}
