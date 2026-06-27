/*
 * dentry.c - Directory entry caching and path component management
 *
 * A simplified dentry cache for path resolution.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "internal.h"

/*
 * Match a path component against a dentry.
 * Returns 1 if matched, 0 otherwise.
 */
int ufs_dentry_match(const char *name, const char *d_name)
{
    return strcmp(name, d_name) == 0;
}

/*
 * Split a path into its parent directory path and final component.
 * e.g., "/foo/bar/baz" -> "/foo/bar" and "baz"
 * Returns the length of the parent path.
 */
int ufs_path_split(const char *path, char *parent, int parent_size,
                   const char **name)
{
    const char *p;
    int len;

    if (!path || !*path)
        return -UFS_EINVAL;

    /* Skip leading slashes */
    while (*path == '/')
        path++;
    if (!*path) {
        /* Root directory */
        *name = "";
        return 0;
    }

    /* Find last '/' */
    p = strrchr(path, '/');
    if (!p) {
        /* Single component, parent is root */
        *name = path;
        return 0;
    }

    /* Extract parent path */
    len = (int)(p - path);
    if (len > 0) {
        if (parent && parent_size > 0) {
            int copy_len = len < parent_size ? len : parent_size - 1;
            strncpy(parent, path, copy_len);
            parent[copy_len] = '\0';
        }
    }

    /* Skip '/' separators before name */
    p++;
    *name = p;

    return len > 0 ? len : 0;
}
