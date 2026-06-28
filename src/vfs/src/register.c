/*
 * register.c - Filesystem type registration
 *
 * Maintains a linked list of registered filesystem types,
 * analogous to Linux's register_filesystem().
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "internal.h"

static struct ufs_filesystem_type *ufs_filesystems = NULL;
static pthread_mutex_t ufs_fs_lock = PTHREAD_MUTEX_INITIALIZER;

int ufs_register_fs(struct ufs_filesystem_type *fs)
{
    struct ufs_filesystem_type **p;
    int err = 0;

    if (!fs || !fs->name || !fs->mount)
        return -UFS_EINVAL;

    pthread_mutex_lock(&ufs_fs_lock);

    /* Check for duplicate name */
    p = &ufs_filesystems;
    while (*p) {
        if (strcmp((*p)->name, fs->name) == 0) {
            err = -UFS_EEXIST;
            goto out;
        }
        p = &(*p)->next;
    }

    /* Insert at end */
    fs->next = NULL;
    *p = fs;

out:
    pthread_mutex_unlock(&ufs_fs_lock);
    return err;
}

int ufs_unregister_fs(struct ufs_filesystem_type *fs)
{
    struct ufs_filesystem_type **p;
    int err = -UFS_ENOENT;

    if (!fs) return -UFS_EINVAL;

    pthread_mutex_lock(&ufs_fs_lock);
    p = &ufs_filesystems;
    while (*p) {
        if (*p == fs) {
            *p = fs->next;
            fs->next = NULL;
            err = 0;
            goto out;
        }
        p = &(*p)->next;
    }
out:
    pthread_mutex_unlock(&ufs_fs_lock);
    return err;
}

struct ufs_filesystem_type *ufs_get_fs(const char *name)
{
    struct ufs_filesystem_type *p;
    struct ufs_filesystem_type *result = NULL;

    pthread_mutex_lock(&ufs_fs_lock);
    p = ufs_filesystems;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            result = p;
            break;
        }
        p = p->next;
    }
    pthread_mutex_unlock(&ufs_fs_lock);
    return result;
}

struct ufs_filesystem_type *ufs_get_fs_list(void)
{
    struct ufs_filesystem_type *p;
    pthread_mutex_lock(&ufs_fs_lock);
    p = ufs_filesystems;
    pthread_mutex_unlock(&ufs_fs_lock);
    return p;
}
