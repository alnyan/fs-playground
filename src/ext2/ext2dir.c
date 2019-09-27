// ext2fs directory content operations
#include "ext2.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>

// XXX:
//      Basically, the driver written by me does not yet
//      support directories larger than one block
// Append an inode to directory
int ext2_dir_add_inode(fs_t *ext2, vnode_t *dir, const char *name, uint32_t ino) {
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
    char block_buffer[sb->block_size];
    struct ext2_inode *dir_inode = dir->fs_data;
    struct ext2_dirent *entptr, *resent;
    int res;
    int found = 0;
    int32_t write_block_index = -1;
    size_t resent_len = 0;

    size_t req_free = strlen(name) + sizeof(struct ext2_dirent);
    // Align up 4 bytes
    req_free = (req_free + 3) & ~3;

    // Try reading parent dirent blocks to see if any has
    // some space to fit our file
    size_t dir_size_blocks = (dir_inode->size_lower + sb->block_size - 1) / sb->block_size;
    assert(dir_size_blocks == 1);

    for (size_t i = 0; i < dir_size_blocks; ++i) {
        if ((res = ext2_read_inode_block(ext2, dir_inode, i, block_buffer)) < 0) {
            return res;
        }

        entptr = NULL;
        size_t off = 0;
        while (off < sb->block_size) {
            entptr = (struct ext2_dirent *) &block_buffer[off];
            if (entptr->len == 0) {
                break;
            }
            if (!entptr->ino) {
                // Possibly free space
                break;
            }

            ssize_t extra = entptr->len - sizeof(struct ext2_dirent) - entptr->name_len;
            // Align up 4 bytes
            extra = (extra + 3) & ~3;

            if (extra > req_free) {
                // Resize previous entry
                entptr->len = (entptr->name_len + sizeof(struct ext2_dirent) + 3) & ~3;
                off += entptr->len;
                resent = (struct ext2_dirent *) &block_buffer[off];
                resent_len = sb->block_size - off;
                write_block_index = i;
                found = 1;
                break;
            }

            off += entptr->len;
        }

        if (found) {
            break;
        }
    }

    if (!found) {
        // Couldn't insert an inode
        return -ENOSPC;
    }

    // Place new entry
    resent->len = resent_len;
    resent->ino = ino;
    resent->type_ind = 0;
    resent->name_len = strlen(name);
    strncpy(resent->name, name, resent->name_len);

    assert(write_block_index != -1);
    if ((res = ext2_write_inode_block(ext2, dir_inode, write_block_index, block_buffer)) < 0) {
        return res;
    }

    return 0;
}

int ext2_dir_remove_inode(fs_t *ext2, vnode_t *dir, const char *name, uint32_t ino) {
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
    char block_buffer[sb->block_size];
    struct ext2_inode *dir_inode = dir->fs_data;
    struct ext2_dirent *entptr, *to_reloc, *prev;
    int res;
    int is_first;
    int found = 0;

    size_t dir_size_blocks = (dir_inode->size_lower + sb->block_size - 1) / sb->block_size;
    assert(dir_size_blocks == 1);

    for (size_t i = 0; i < dir_size_blocks; ++i) {
        if ((res = ext2_read_inode_block(ext2, dir_inode, i, block_buffer)) < 0) {
            return res;
        }
        is_first = 1;
        prev = NULL;
        entptr = NULL;

        uint32_t off = 0;
        while (off < sb->block_size) {
            prev = entptr;
            entptr = (struct ext2_dirent *) &block_buffer[off];
            if (entptr->len == 0) {
                break;
            }
            if (!entptr->ino) {
                // Possibly free space
                break;
            }

            if (entptr->name_len == strlen(name) && !strncmp(name, entptr->name, entptr->name_len)) {
                found = 1;
                break;
            }

            is_first = 0;
            off += entptr->len;
        }

        if (!found) {
            continue;
        }

        assert(entptr);

        // Sanity check that we're actually removing
        // the requested inode reference
        assert(entptr->ino == ino);

        // Get rid of the found entry
        // Find the next entry to join with
        if (off + entptr->len >= sb->block_size) {
            // We're the last entry

            if (is_first) {
                // If we're also the first one - the entire block is free now
                // TODO: somehow free blocks that are in the middle of the directory
                // This should not be possible for the first block of any
                // directory as it contains "." and ".."
                printf("That's the first and the last node\n");
                abort();
            } else {
                assert(prev);
                prev->len += entptr->len;
                printf("Removing last node\n");
                // Write the block back
                return ext2_write_inode_block(ext2, dir_inode, i, block_buffer);
            }
        } else {
            uint32_t next_off = off + entptr->len;
            to_reloc = (struct ext2_dirent *) &block_buffer[next_off];
            uint32_t next_len = to_reloc->len;
            uint32_t total = next_len + entptr->len;

            memmove(&block_buffer[off], &block_buffer[next_off], next_len);
            entptr->len = total;

            // Write the block back
            return ext2_write_inode_block(ext2, dir_inode, i, block_buffer);
        }
    }

    return -EIO;
}
