/*
 * api.c - Public API implementation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include <stdlib.h>
#include "internal.h"

int ufs_mount(const char *device, const char *fstype,
              struct ufs_super_block **sb_out)
{
    struct ufs_filesystem_type *fs;
    struct ufs_bdev *bdev;
    struct ufs_super_block *sb;
    int err;

    if (!device || !fstype || !sb_out)
        return -UFS_EINVAL;

    /* Look up filesystem type */
    fs = ufs_get_fs(fstype);
    if (!fs) {
        ufs_log_err("unknown filesystem type '%s'", fstype);
        return -UFS_ENOENT;
    }

    /* Open block device */
    bdev = ufs_bdev_open(device, 0);
    if (!bdev)
        return -UFS_EIO;

    /* Initialize buffer cache */
    err = ufs_bcache_init(bdev);
    if (err) {
        ufs_bdev_close(bdev);
        return err;
    }

    /* Allocate super block */
    sb = (struct ufs_super_block *)calloc(1, sizeof(struct ufs_super_block));
    if (!sb) {
        ufs_bcache_destroy(bdev);
        ufs_bdev_close(bdev);
        return -UFS_ENOMEM;
    }

    /* Call filesystem mount routine */
    err = fs->mount(bdev, &sb);
    if (err) {
        ufs_bcache_destroy(bdev);
        ufs_bdev_close(bdev);
        free(sb);
        return err;
    }

    sb->s_fs_type = fs;
    *sb_out = sb;
    return 0;
}

int ufs_umount(struct ufs_super_block *sb)
{
    if (!sb) return -UFS_EINVAL;

    /* Flush buffer cache */
    if (sb->s_bdev)
        ufs_bcache_flush(sb->s_bdev);

    /* Destroy filesystem */
    ufs_super_destroy(sb);

    free(sb);
    return 0;
}

int ufs_open(struct ufs_super_block *sb, const char *path,
             struct ufs_file **file_out)
{
    struct ufs_inode *inode;
    int err;

    if (!sb || !path || !file_out)
        return -UFS_EINVAL;

    err = ufs_path_resolve(sb, path, &inode);
    if (err < 0) return err;

    err = ufs_file_open(inode, file_out, 0);
    if (err) return err;

    return 0;
}

ssize_t ufs_read(struct ufs_file *file, void *buf, size_t count)
{
    return ufs_file_read(file, buf, count);
}

ssize_t ufs_write(struct ufs_file *file, const void *buf, size_t count)
{
    return ufs_file_write(file, buf, count);
}

int ufs_close(struct ufs_file *file)
{
    return ufs_file_close(file);
}

loff_t ufs_seek(struct ufs_file *file, loff_t offset, int whence)
{
    return ufs_file_seek(file, offset, whence);
}

int ufs_fsync(struct ufs_file *file)
{
    return ufs_file_fsync(file);
}

int ufs_create(struct ufs_super_block *sb, const char *path, umode_t mode,
               struct ufs_file **file_out)
{
    struct ufs_inode *parent, *inode;
    const char *name;
    int err;

    if (!sb || !path || !file_out) return -UFS_EINVAL;

    err = ufs_path_parent(sb, path, &parent, &name);
    if (err < 0) return err;
    if (!*name) return -UFS_EEXIST;
    if (!parent->i_op || !parent->i_op->create)
        return -UFS_ENOSYS;

    err = parent->i_op->create(parent, name, mode, &inode);
    if (err < 0) return err;

    return ufs_file_open(inode, file_out, UFS_O_WRONLY);
}

struct ufs_dirent_cb_data {
    char ***names;
    int *count;
    int capacity;
};

static int collect_dirent(struct ufs_dir_context *ctx,
                           const struct ufs_dirent *de)
{
    struct ufs_dirent_cb_data *data = (struct ufs_dirent_cb_data *)ctx->priv;

    /* Skip . and .. */
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        return 0;

    if (*data->count >= data->capacity) {
        data->capacity = data->capacity ? data->capacity * 2 : 16;
        char **new_names = (char **)realloc(*data->names,
                                             data->capacity * sizeof(char *));
        if (!new_names) return -UFS_ENOMEM;
        *data->names = new_names;
    }

    (*data->names)[*data->count] = strdup(de->d_name);
    if (!(*data->names)[*data->count]) return -UFS_ENOMEM;
    (*data->count)++;
    return 0;
}

int ufs_listdir(struct ufs_super_block *sb, const char *path,
                char ***names, int *count)
{
    struct ufs_inode *inode;
    struct ufs_file *file;
    struct ufs_dir_context ctx;
    struct ufs_dirent_cb_data cb_data;
    int err;

    if (!sb || !path || !names || !count)
        return -UFS_EINVAL;

    *names = NULL;
    *count = 0;

    err = ufs_path_resolve(sb, path, &inode);
    if (err < 0) return err;

    if (!S_ISDIR(inode->i_mode))
        return -UFS_ENOTDIR;

    err = ufs_file_open(inode, &file, 0);
    if (err) return err;

    cb_data.names = names;
    cb_data.count = count;
    cb_data.capacity = 0;

    ctx.priv = &cb_data;
    ctx.callback = collect_dirent;

    if (file->f_op && file->f_op->iterate)
        err = file->f_op->iterate(file, &ctx);
    else
        err = -UFS_ENOSYS;

    ufs_file_close(file);
    return err;
}

int ufs_mkdir(struct ufs_super_block *sb, const char *path, umode_t mode)
{
    struct ufs_inode *parent;
    const char *name;
    int err;

    if (!sb || !path) return -UFS_EINVAL;

    err = ufs_path_parent(sb, path, &parent, &name);
    if (err < 0) return err;

    if (!*name) return -UFS_EEXIST;  /* can't mkdir root */

    if (!parent->i_op || !parent->i_op->mkdir)
        return -UFS_ENOSYS;

    return parent->i_op->mkdir(parent, name, mode, NULL);
}

int ufs_rmdir(struct ufs_super_block *sb, const char *path)
{
    struct ufs_inode *parent;
    const char *name;
    int err;

    if (!sb || !path) return -UFS_EINVAL;

    err = ufs_path_parent(sb, path, &parent, &name);
    if (err < 0) return err;

    if (!*name) return -UFS_EBUSY;  /* can't rmdir root */

    if (!parent->i_op || !parent->i_op->rmdir)
        return -UFS_ENOSYS;

    return parent->i_op->rmdir(parent, name);
}

int ufs_unlink(struct ufs_super_block *sb, const char *path)
{
    struct ufs_inode *parent;
    const char *name;
    int err;

    if (!sb || !path) return -UFS_EINVAL;

    err = ufs_path_parent(sb, path, &parent, &name);
    if (err < 0) return err;

    if (!*name) return -UFS_EBUSY;

    if (!parent->i_op || !parent->i_op->unlink)
        return -UFS_ENOSYS;

    return parent->i_op->unlink(parent, name);
}

int ufs_rename(struct ufs_super_block *sb, const char *oldpath,
               const char *newpath)
{
    struct ufs_inode *old_parent, *new_parent;
    const char *old_name, *new_name;
    int err;

    if (!sb || !oldpath || !newpath) return -UFS_EINVAL;

    err = ufs_path_parent(sb, oldpath, &old_parent, &old_name);
    if (err < 0) return err;

    err = ufs_path_parent(sb, newpath, &new_parent, &new_name);
    if (err < 0) return err;

    if (!old_parent->i_op || !old_parent->i_op->rename)
        return -UFS_ENOSYS;

    return old_parent->i_op->rename(old_parent, old_name,
                                     new_parent, new_name);
}

int ufs_stat(struct ufs_super_block *sb, const char *path,
             struct ufs_stat *buf)
{
    struct ufs_inode *inode;
    int err;

    if (!sb || !path || !buf) return -UFS_EINVAL;

    err = ufs_path_resolve(sb, path, &inode);
    if (err < 0) return err;

    if (inode->i_op && inode->i_op->getattr)
        return inode->i_op->getattr(inode, buf);

    /* Default stat from inode fields */
    memset(buf, 0, sizeof(*buf));
    buf->st_ino = inode->i_ino;
    buf->st_mode = inode->i_mode;
    buf->st_nlink = inode->i_nlink;
    buf->st_size = inode->i_size;
    buf->st_blksize = sb->s_blocksize;
    buf->st_blocks = inode->i_blocks;
    return 0;
}

int ufs_statfs(struct ufs_super_block *sb, struct ufs_statfs *buf)
{
    if (!sb || !buf) return -UFS_EINVAL;

    if (sb->s_op && sb->s_op->statfs)
        return sb->s_op->statfs(sb, buf);

    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = sb->s_blocksize;
    return 0;
}
