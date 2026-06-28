/*
 * file.c - File handle management
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdlib.h>
#include "internal.h"

int ufs_file_open(struct ufs_inode *inode, struct ufs_file **file_out,
                  int flags)
{
    struct ufs_file *file;
    int err;

    if (!inode || !file_out)
        return -UFS_EINVAL;

    file = (struct ufs_file *)calloc(1, sizeof(struct ufs_file));
    if (!file) return -UFS_ENOMEM;

    file->f_inode = inode;
    file->f_op = inode->i_fop;
    file->f_pos = 0;
    file->f_flags = flags;
    file->f_private = NULL;

    if (file->f_op && file->f_op->open) {
        err = file->f_op->open(inode, file);
        if (err) {
            free(file);
            return err;
        }
    }

    *file_out = file;
    return 0;
}

int ufs_file_close(struct ufs_file *file)
{
    int err = 0;

    if (!file) return 0;

    if (file->f_op && file->f_op->release)
        err = file->f_op->release(file->f_inode, file);

    free(file);
    return err;
}

ssize_t ufs_file_read(struct ufs_file *file, void *buf, size_t count)
{
    ssize_t ret;

    if (!file || !file->f_op || !file->f_op->read)
        return -UFS_ENOSYS;

    ret = file->f_op->read(file, buf, count, file->f_pos);
    if (ret > 0)
        file->f_pos += ret;
    return ret;
}

ssize_t ufs_file_write(struct ufs_file *file, const void *buf, size_t count)
{
    ssize_t ret;

    if (!file || !file->f_op || !file->f_op->write)
        return -UFS_ENOSYS;

    ret = file->f_op->write(file, buf, count, file->f_pos);
    if (ret > 0)
        file->f_pos += ret;
    return ret;
}

loff_t ufs_file_seek(struct ufs_file *file, loff_t offset, int whence)
{
    loff_t new_pos;

    switch (whence) {
    case 0: /* SEEK_SET */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END */
        new_pos = file->f_inode->i_size + offset;
        break;
    default:
        return -UFS_EINVAL;
    }

    if (new_pos < 0)
        return -UFS_EINVAL;

    file->f_pos = new_pos;
    return new_pos;
}

int ufs_file_fsync(struct ufs_file *file)
{
    if (!file || !file->f_op || !file->f_op->fsync)
        return 0;  /* no-op if not supported */
    return file->f_op->fsync(file);
}
