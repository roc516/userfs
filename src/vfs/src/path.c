/*
 * path.c - Path resolution (walking path components via lookup)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "internal.h"

/*
 * Walk a path relative to a starting directory.
 * Handles "." and ".." components.
 * Trailing slash is allowed and indicates the target must be a directory.
 */
int ufs_path_walk(struct ufs_super_block *sb, struct ufs_inode *dir,
                  const char *pathname, struct ufs_inode **out)
{
    struct ufs_inode *cur = dir;
    struct ufs_inode *next = NULL;
    char component[256];
    const char *p = pathname;
    const char *end;
    int err = 0;
    int must_be_dir = 0;

    if (!cur || !pathname)
        return -UFS_EINVAL;

    /* If absolute path, start from root */
    if (*p == '/') {
        /* Find root inode - i_ino == 1 */
        cur = ufs_iget(sb, 1);
        if (!cur) {
            ufs_log_err("path_walk: root inode not found (host table empty?)");
            return -UFS_ENOENT;
        }
        /* Skip leading slashes */
        while (*p == '/') p++;
    }

    if (!*p) {
        /* Just root */
        *out = cur;
        return 0;
    }

    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (!*p) {
            must_be_dir = 1;
            break;
        }

        /* Extract next component */
        end = p;
        while (*end && *end != '/') end++;

        if ((size_t)(end - p) >= sizeof(component)) {
            err = -UFS_ENAMETOOLONG;
            goto out;
        }
        memcpy(component, p, end - p);
        component[end - p] = '\0';
        p = end;

        if (strcmp(component, ".") == 0) {
            continue;
        }

        if (strcmp(component, "..") == 0) {
            /* Go up - for now this is tricky without parent pointers.
             * We'll handle this via the FS lookup for ".." */
        }

        /* Check current is a directory */
        if (!S_ISDIR(cur->i_mode)) {
            err = -UFS_ENOTDIR;
            goto out;
        }

        /* Lookup component */
        if (!cur->i_op || !cur->i_op->lookup) {
            err = -UFS_ENOSYS;
            goto out;
        }

        err = cur->i_op->lookup(cur, component, &next);
        if (err < 0) {
            if (*p == '\0' && err == -UFS_ENOENT && must_be_dir) {
                /* Accept - the final component not found is OK if
                 * caller is creating it */
            }
            goto out;
        }

        cur = next;
        next = NULL;
    }

    if (must_be_dir && !S_ISDIR(cur->i_mode)) {
        err = -UFS_ENOTDIR;
        goto out;
    }

    *out = cur;
    return 0;

out:
    if (cur && cur != dir)
        /* leak for now - FIXME */;
    return err;
}

/*
 * Resolve a path to an inode.
 */
int ufs_path_resolve(struct ufs_super_block *sb, const char *path,
                     struct ufs_inode **inode_out)
{
    struct ufs_inode *root = ufs_iget(sb, 1);
    if (!root) return -UFS_ENOENT;
    return ufs_path_walk(sb, root, path, inode_out);
}

/*
 * Resolve parent directory and final component.
 */
int ufs_path_parent(struct ufs_super_block *sb, const char *path,
                    struct ufs_inode **parent, const char **name)
{
    char parent_path[1024];
    const char *leaf;
    int err;

    memset(parent_path, 0, sizeof(parent_path));
    err = ufs_path_split(path, parent_path, sizeof(parent_path), &leaf);
    if (err < 0) return err;

    if (*leaf) {
        /* Resolve parent path */
        err = ufs_path_resolve(sb, parent_path[0] ? parent_path : "/",
                                parent);
    } else {
        /* Whole path is root */
        *parent = ufs_iget(sb, 1);
        if (!*parent) return -UFS_ENOENT;
        *name = "";
    }

    if (err < 0) return err;
    *name = leaf;
    return 0;
}
