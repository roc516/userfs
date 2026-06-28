/*
 * inode.c - Inode management (hashtable, iget/iput)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "internal.h"

pthread_mutex_t ufs_inode_hash_lock = PTHREAD_MUTEX_INITIALIZER;
struct ufs_inode_hashtable ufs_inode_hashtable[UFS_MAX_INODE_HASH];

void ufs_inode_addref(struct ufs_inode *inode)
{
    if (inode)
        inode->i_nlink++;
}

void ufs_inode_delref(struct ufs_inode *inode)
{
    if (!inode) return;
    if (inode->i_nlink > 0)
        inode->i_nlink--;
}

struct ufs_inode *ufs_iget(struct ufs_super_block *sb, unsigned long ino)
{
    struct ufs_inode *inode;
    unsigned long h = ufs_inode_hash(ino);

    pthread_mutex_lock(&ufs_inode_hash_lock);
    inode = ufs_inode_hashtable[h].head;
    while (inode) {
        // We need a next pointer in inode... let's add one
        // For now just check equality - we'll use a different approach
        if (inode->i_ino == ino && inode->i_sb == sb)
            break;
        inode = inode->i_private;  /* abuse i_private as hash next */
    }
    pthread_mutex_unlock(&ufs_inode_hash_lock);
    return inode;
}

int ufs_inode_insert_hash(struct ufs_inode *inode)
{
    unsigned long h = ufs_inode_hash(inode->i_ino);

    pthread_mutex_lock(&ufs_inode_hash_lock);
    inode->i_private = (void *)ufs_inode_hashtable[h].head;
    ufs_inode_hashtable[h].head = inode;
    pthread_mutex_unlock(&ufs_inode_hash_lock);
    return 0;
}

void ufs_inode_remove_hash(struct ufs_inode *inode)
{
    unsigned long h = ufs_inode_hash(inode->i_ino);
    struct ufs_inode **pp;

    pthread_mutex_lock(&ufs_inode_hash_lock);
    pp = &ufs_inode_hashtable[h].head;
    while (*pp) {
        if (*pp == inode) {
            *pp = (struct ufs_inode *)(*pp)->i_private;
            break;
        }
        pp = (struct ufs_inode **)&(*pp)->i_private;
    }
    pthread_mutex_unlock(&ufs_inode_hash_lock);
}
