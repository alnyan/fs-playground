#include "ext2.h"
#include "ofile.h"
#include "fs.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

#define ext2_super(e)       ((struct ext2_extsb *) (e)->fs_private)

// Forward declaration of ext2 vnode functions
static int ext2_vnode_find(vnode_t *vn, const char *name, vnode_t **resvn);
static int ext2_vnode_creat(vnode_t *at, const char *name, mode_t mode, int opt, vnode_t **resvn);
static int ext2_vnode_open(vnode_t *vn, int opt);
static int ext2_vnode_opendir(vnode_t *vn, int opt);
static ssize_t ext2_vnode_read(struct ofile *fd, void *buf, size_t count);
static int ext2_vnode_readdir(struct ofile *fd);
static void ext2_vnode_destroy(vnode_t *vn);
static int ext2_vnode_stat(vnode_t *vn, struct stat *st);

static struct vnode_operations ext2_vnode_ops = {
    .find = ext2_vnode_find,
    .creat = ext2_vnode_creat,
    .destroy = ext2_vnode_destroy,

    .stat = ext2_vnode_stat,

    .opendir = ext2_vnode_opendir,
    .readdir = ext2_vnode_readdir,

    .open = ext2_vnode_open,
    .read = ext2_vnode_read
};

static enum vnode_type ext2_inode_type(struct ext2_inode *i) {
    uint16_t v = i->type_perm & 0xF000;
    switch (v) {
    case EXT2_TYPE_DIR:
        return VN_DIR;
    case EXT2_TYPE_REG:
        return VN_REG;
    default:
        fprintf(stderr, "Unknown file type: %x\n", v);
        abort();
    }
}

static int ext2_read_block(fs_t *ext2, uint32_t block_no, void *buf) {
    if (!block_no) {
        return -1;
    }
    //printf("ext2_read_block %u\n", block_no);
    int res = blk_read(ext2->blk, buf, block_no * ext2_super(ext2)->block_size, ext2_super(ext2)->block_size);

    if (res < 0) {
        fprintf(stderr, "ext2: Failed to read %uth block\n", block_no);
    }

    return res;
}

static int ext2_write_block(fs_t *ext2, uint32_t block_no, const void *buf) {
    if (!block_no) {
        return -1;
    }

    int res = blk_write(ext2->blk, buf, block_no * ext2_super(ext2)->block_size, ext2_super(ext2)->block_size);

    if (res < 0) {
        fprintf(stderr, "ext2: Failed to write %uth block\n", block_no);
    }

    return res;
}

static int ext2_write_inode_block(fs_t *ext2, struct ext2_inode *inode, uint32_t index, const void *buf) {
    if (index < 12) {
        uint32_t block_number = inode->direct_blocks[index];
        return ext2_write_block(ext2, block_number, buf);
    } else {
        // TODO
        abort();
    }
}

static int ext2_read_inode_block(fs_t *ext2, struct ext2_inode *inode, uint32_t index, void *buf) {
    if (index < 12) {
        // Use direct ptrs
        uint32_t block_number = inode->direct_blocks[index];
        return ext2_read_block(ext2, block_number, buf);
    } else {
        struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
        // Use buf as indirection block buffer (I think we're allowed to do so)

        if (index < 12 + (sb->block_size / 4)) {
            // Single indirection
            if (ext2_read_block(ext2, inode->l1_indirect_block, buf) < 0) {
                return -EIO;
            }

            uint32_t block_number = ((uint32_t *) buf)[index - 12];
            return ext2_read_block(ext2, block_number, buf);
        } else {
            // Not implemented yet
            return -EIO;
        }
    }
}

static int ext2_read_inode(fs_t *ext2, struct ext2_inode *inode, uint32_t ino) {
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
    //printf("ext2_read_inode %d\n", ino);
    char inode_block_buffer[sb->block_size];

    uint32_t ino_block_group_number = (ino - 1) / sb->sb.block_group_size_inodes;
    //printf("inode block group number = %d\n", ino_block_group_number);
    uint32_t ino_inode_table_block = sb->block_group_descriptor_table[ino_block_group_number].inode_table_block;
    //printf("inode table is at block %d\n", ino_inode_table_block);
    uint32_t ino_inode_index_in_group = (ino - 1) % sb->sb.block_group_size_inodes;
    //printf("inode entry index in the group = %d\n", ino_inode_index_in_group);
    uint32_t ino_inode_block_in_group = (ino_inode_index_in_group * sb->inode_struct_size) / sb->block_size;
    //printf("inode entry offset is %d blocks\n", ino_inode_block_in_group);
    uint32_t ino_inode_block_number = ino_inode_block_in_group + ino_inode_table_block;
    //printf("inode block number is %uth block\n", ino_inode_block_number);

    //struct ext2_inode *root_inode_block_inode_table = (struct ext2_inode *) root_inode_block_buf;
    if (ext2_read_block(ext2, ino_inode_block_number, inode_block_buffer) < 0) {
        printf("ext2: failed to load inode#%d block\n", ino);
        return -1;
    }

    uint32_t ino_entry_in_block = (ino_inode_index_in_group * sb->inode_struct_size) % sb->block_size;
    memcpy(inode, &inode_block_buffer[ino_entry_in_block], sb->inode_struct_size);

    return 0;
}

static int ext2_write_inode(fs_t *ext2, const struct ext2_inode *inode, uint32_t ino) {
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
    //printf("ext2_read_inode %d\n", ino);
    char inode_block_buffer[sb->block_size];
    int res;

    uint32_t ino_block_group_number = (ino - 1) / sb->sb.block_group_size_inodes;
    //printf("inode block group number = %d\n", ino_block_group_number);
    uint32_t ino_inode_table_block = sb->block_group_descriptor_table[ino_block_group_number].inode_table_block;
    //printf("inode table is at block %d\n", ino_inode_table_block);
    uint32_t ino_inode_index_in_group = (ino - 1) % sb->sb.block_group_size_inodes;
    //printf("inode entry index in the group = %d\n", ino_inode_index_in_group);
    uint32_t ino_inode_block_in_group = (ino_inode_index_in_group * sb->inode_struct_size) / sb->block_size;
    //printf("inode entry offset is %d blocks\n", ino_inode_block_in_group);
    uint32_t ino_inode_block_number = ino_inode_block_in_group + ino_inode_table_block;
    //printf("inode block number is %uth block\n", ino_inode_block_number);

    // Need to read the block to modify it
    if ((res = ext2_read_block(ext2, ino_inode_block_number, inode_block_buffer)) < 0) {
        return res;
    }

    uint32_t ino_entry_in_block = (ino_inode_index_in_group * sb->inode_struct_size) % sb->block_size;
    memcpy(&inode_block_buffer[ino_entry_in_block], inode, sb->inode_struct_size);

    // Write the block back
    if ((res = ext2_write_block(ext2, ino_inode_block_number, inode_block_buffer)) < 0) {
        return res;
    }

    return 0;
}

static int ext2_write_superblock(fs_t *ext2) {
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
    return blk_write(ext2->blk, sb, EXT2_SBOFF, EXT2_SBSIZ);
}

static int ext2_alloc_inode(fs_t *ext2, uint32_t *ino) {
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;
    char block_buffer[sb->block_size];
    uint32_t res_ino = 0;
    uint32_t res_group_no = 0;
    uint32_t res_ino_number_in_group = 0;
    int res;

    // Look through BGDT to find any block groups with free inodes
    for (size_t i = 0; i < sb->block_group_count; ++i) {
        if (sb->block_group_descriptor_table[i].free_inodes > 0) {
            // Found a block group with free inodes
            printf("Allocating an inode inside block group #%zu\n", i);

            // Read inode usage bitmap
            if ((res = ext2_read_block(ext2,
                                       sb->block_group_descriptor_table[i].inode_usage_bitmap_block,
                                       block_buffer)) < 0) {
                return res;
            }

            // Find a free bit
            // Think this should be fine on amd64
            for (size_t j = 0; j < sb->block_size / sizeof(uint64_t); ++j) {
                // Get bitmap qword
                uint64_t qw = ((uint64_t *) block_buffer)[j];
                // If not all bits are set in this qword, find exactly which one
                if (qw != ((uint64_t) -1)) {
                    for (size_t k = 0; k < 64; ++k) {
                        if (!(qw & (1 << k))) {
                            res_ino_number_in_group = k + j * 64;
                            res_group_no = i;
                            res_ino = res_ino_number_in_group + i * sb->sb.block_group_size_inodes + 1;
                            break;
                        }
                    }

                    if (res_ino) {
                        break;
                    }
                }

                if (res_ino) {
                    break;
                }
            }
        }

        if (res_ino) {
            break;
        }
    }
    if (res_ino == 0) {
        return -ENOSPC;
    }

    // Write updated bitmap
    ((uint64_t *) block_buffer)[res_ino_number_in_group / 64] |= (1 << (res_ino_number_in_group % 64));
    if ((res = ext2_write_block(ext2,
                                sb->block_group_descriptor_table[res_group_no].inode_usage_bitmap_block,
                                block_buffer)) < 0) {
        return res;
    }

    // Write updated BGDT
    --sb->block_group_descriptor_table[res_group_no].free_inodes;
    for (size_t i = 0; i < sb->block_group_descriptor_table_size_blocks; ++i) {
        void *blk_ptr = (void *) (((uintptr_t) sb->block_group_descriptor_table) + i * sb->block_size);

        if ((res = ext2_write_block(ext2, sb->block_group_descriptor_table_block + i, blk_ptr)) < 0) {
            return res;
        }
    }

    // Update global inode count and flush superblock
    --sb->sb.free_inode_count;
    if ((res = ext2_write_superblock(ext2)) < 0) {
        return res;
    }

    *ino = res_ino;

    return 0;
}

// Append an inode to directory
static int ext2_dir_add_inode(fs_t *ext2, vnode_t *dir, const char *name, uint32_t ino) {
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
    for (size_t i = 0; i < dir_size_blocks; ++i) {
        if ((res = ext2_read_inode_block(ext2, dir_inode, i, block_buffer)) < 0) {
            return res;
        }

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

        // TODO: ???
        abort();
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

static int ext2_fs_mount(fs_t *fs, const char *opt) {
    int res;
    printf("ext2_fs_mount()\n");
    struct ext2_extsb *sb = fs->fs_private;

    // ext2's private data is its superblock structure
    sb = malloc(EXT2_SBSIZ);
    fs->fs_private = sb;

    // Read the superblock from blkdev
    if ((res = blk_read(fs->blk, sb, EXT2_SBOFF, EXT2_SBSIZ)) != EXT2_SBSIZ) {
        free(fs->fs_private);

        printf("ext2: superblock read failed\n");
        return -EINVAL;
    }

    // Check if superblock is ext2
    if (sb->sb.magic != EXT2_MAGIC) {
        free(sb);

        printf("ext2: magic mismatch\n");
        return -EINVAL;
    }

    // Check if we have an extended ext2 sb
    if (sb->sb.version_major == 0) {
        // Initialize params which are missing in non-extended sbs
        sb->inode_struct_size = 128;
        sb->first_non_reserved = 11;
    }
    sb->block_size = 1024 << sb->sb.block_size_log;

    // Load block group descriptor table
    // Get descriptor table size
    uint32_t block_group_descriptor_table_length = sb->sb.block_count / sb->sb.block_group_size_blocks;
    if (block_group_descriptor_table_length * sb->sb.block_group_size_blocks < sb->sb.block_count) {
        ++block_group_descriptor_table_length;
    }
    sb->block_group_count = block_group_descriptor_table_length;

    uint32_t block_group_descriptor_table_size_blocks = 32 * block_group_descriptor_table_length /
                                                        sb->block_size + 1;

    uint32_t block_group_descriptor_table_block = 2;
    if (sb->block_size > 1024) {
        block_group_descriptor_table_block = 1;
    }
    sb->block_group_descriptor_table_block = block_group_descriptor_table_block;
    sb->block_group_descriptor_table_size_blocks = block_group_descriptor_table_size_blocks;

    // Load all block group descriptors into memory
    printf("Allocating %u bytes for BGDT\n", sb->block_group_descriptor_table_size_blocks * sb->block_size);
    sb->block_group_descriptor_table = (struct ext2_grp_desc *) malloc(sb->block_group_descriptor_table_size_blocks * sb->block_size);

    for (size_t i = 0; i < sb->block_group_descriptor_table_size_blocks; ++i) {
        ext2_read_block(fs, i + sb->block_group_descriptor_table_block,
                        (void *) (((uintptr_t) sb->block_group_descriptor_table) + i * sb->block_size));
    }

    return 0;
}

static int ext2_fs_umount(fs_t *fs) {
    struct ext2_extsb *sb = (struct ext2_extsb *) fs->fs_private;
    // Free block group descriptor table
    free(sb->block_group_descriptor_table);
    // Free superblock
    free(sb);
    return 0;
}

static vnode_t *ext2_fs_get_root(fs_t *fs) {
    struct ext2_extsb *sb = fs->fs_private;
    printf("ext2_fs_get_root()\n");

    struct ext2_inode *inode = (struct ext2_inode *) malloc(sb->inode_struct_size);
    // Read root inode (2)
    if (ext2_read_inode(fs, inode, EXT2_ROOTINO) != 0) {
        free(inode);
        return NULL;
    }

    vnode_t *res = (vnode_t *) malloc(sizeof(vnode_t));

    res->fs = fs;
    res->fs_data = inode;
    res->fs_number = EXT2_ROOTINO;
    res->op = &ext2_vnode_ops;
    res->type = ext2_inode_type(inode);

    return res;
}


static struct fs_class ext2_class = {
    "ext2",
    ext2_fs_get_root,
    ext2_fs_mount,
    ext2_fs_umount,
};

void ext2_class_init(void) {
    fs_class_register(&ext2_class);
}

//// vnode function implementation

static int ext2_vnode_find(vnode_t *vn, const char *name, vnode_t **res) {
    fs_t *ext2 = vn->fs;
    struct ext2_extsb *sb = vn->fs->fs_private;
    struct ext2_inode *inode = vn->fs_data;

    char buffer[sb->block_size];
    struct ext2_dirent *dirent = NULL;

    size_t block_count = (inode->size_lower + (sb->block_size - 1)) / sb->block_size;
    char ent_name[256];
    size_t index = 0;

    while (index < block_count) {
        // Read directory contents block
        if (ext2_read_inode_block(ext2, inode, index, buffer) < 0) {
            return -EIO;
        }

        size_t offset = 0;
        while (1) {
            dirent = (struct ext2_dirent *) &buffer[offset];
            if (!dirent->len) {
                break;
            }
            if (dirent->ino) {
                if (strlen(name) == dirent->name_len && !strncmp(dirent->name, name, dirent->name_len)) {
                    // Found the entry
                    vnode_t *out = (vnode_t *) malloc(sizeof(vnode_t));
                    out->op = &ext2_vnode_ops;
                    out->fs = ext2;

                    struct ext2_inode *result_inode = (struct ext2_inode *) malloc(sizeof(struct ext2_inode));
                    if (ext2_read_inode(ext2, result_inode, dirent->ino) != 0) {
                        return -EIO;
                    }

                    out->fs_data = result_inode;
                    out->fs_number = dirent->ino;
                    out->type = ext2_inode_type(result_inode);

                    *res = out;
                    //printf("Lookup %s in ino %d = %d\n", name, vn->fs_number, out->fs_number);
                    return 0;
                }
            }
            offset += dirent->len;
            if (offset >= sb->block_size) {
                break;
            }
        }

        ++index;
    }

    return -ENOENT;
}

static int ext2_vnode_opendir(vnode_t *vn, int opt) {
    assert(vn->type == VN_DIR);
    return 0;
}

static int ext2_vnode_open(vnode_t *vn, int opt) {
    // Writing is not yet implemented
    if (opt & O_WRONLY) {
        return -EROFS;
    }

    assert(vn->type == VN_REG);
    return 0;
}

static int ext2_vnode_creat(vnode_t *at, const char *name, mode_t mode, int opt, vnode_t **resvn) {
    fs_t *ext2 = at->fs;
    assert(at->type == VN_DIR);
    assert(/* Don't support making directories like this */ !(mode & O_DIRECTORY));
    struct ext2_extsb *sb = (struct ext2_extsb *) ext2->fs_private;

    uint32_t new_ino;
    int res;

    // Allocate new inode number
    if ((res = ext2_alloc_inode(ext2, &new_ino)) != 0) {
        printf("Failed to allocate inode\n");
        return res;
    }

    printf("Allocated inode %d\n", new_ino);

    // Create an inode struct in memory
    struct ext2_inode *ent_inode = (struct ext2_inode *) malloc(sb->inode_struct_size);

    // Now create an entry in parents dirent list
    if ((res = ext2_dir_add_inode(ext2, at, name, new_ino)) < 0) {
        return res;
    }

    // Fill the inode
    ent_inode->flags = 0;
    ent_inode->dir_acl = 0;
    ent_inode->frag_block_addr = 0;
    ent_inode->gen_number = 0;
    ent_inode->hard_link_count = 0;
    ent_inode->acl = 0;
    ent_inode->os_value_1 = 0;
    memset(ent_inode->os_value_2, 0, sizeof(ent_inode->os_value_2));
    // TODO: time support in kernel
    ent_inode->atime = 0;
    ent_inode->mtime = 0;
    ent_inode->ctime = 0;
    ent_inode->dtime = 0;

    memset(ent_inode->direct_blocks, 0, sizeof(ent_inode->direct_blocks));
    ent_inode->l1_indirect_block = 0;
    ent_inode->l2_indirect_block = 0;
    ent_inode->l3_indirect_block = 0;

    // TODO: only regular files can be created this way now
    ent_inode->type_perm = (mode & 0x1FF) | (EXT2_TYPE_REG);
    // TODO: obtain these from process context in kernel
    ent_inode->uid = 0;
    ent_inode->gid = 0;
    ent_inode->disk_sector_count = 0;
    ent_inode->size_upper = 0;
    ent_inode->size_lower = 0;

    // Write the inode
    if ((res = ext2_write_inode(ext2, ent_inode, new_ino)) < 0) {
        return res;
    }

    // Create the resulting vnode
    vnode_t *vn = (vnode_t *) malloc(sizeof(vnode_t));
    vn->fs = ext2;
    vn->fs_data = ent_inode;
    vn->fs_number = new_ino;
    vn->op = &ext2_vnode_ops;
    vn->type = VN_REG;

    *resvn = vn;

    return 0;
}

#define MIN(x, y) ((x) > (y) ? (y) : (x))
static ssize_t ext2_vnode_read(struct ofile *fd, void *buf, size_t count) {
    vnode_t *vn = fd->vnode;
    struct ext2_inode *inode = (struct ext2_inode *) vn->fs_data;
    struct ext2_extsb *sb = vn->fs->fs_private;

    size_t nread = MIN(inode->size_lower - fd->pos, count);

    if (nread == 0) {
        return -1;
    }

    size_t block_number = fd->pos / sb->block_size;
    size_t nblocks = (nread + sb->block_size - 1) / sb->block_size;
    char block_buffer[sb->block_size];

    for (size_t i = 0; i < nblocks; ++i) {
        if (ext2_read_inode_block(vn->fs, inode, i + block_number, block_buffer) < 0) {
            return -EIO;
        }
        if (i == 0) {
            size_t ncpy = MIN(sb->block_size - fd->pos % sb->block_size, nread);
            memcpy(buf, block_buffer + fd->pos % sb->block_size, ncpy);
        } else {
            size_t ncpy = MIN(sb->block_size, nread - sb->block_size * i);
            memcpy(buf + sb->block_size * i, block_buffer, ncpy);
        }
    }

    return nread;
}

// TODO: replace this with getdents
static int ext2_vnode_readdir(struct ofile *fd) {
    vnode_t *vn = fd->vnode;
    struct ext2_inode *inode = (struct ext2_inode *) vn->fs_data;
    struct ext2_extsb *sb = vn->fs->fs_private;

    if (fd->pos >= inode->size_lower) {
        return -1;
    }

    size_t block_number = fd->pos / sb->block_size;
    char block_buffer[sb->block_size];

    if (ext2_read_inode_block(vn->fs, inode, block_number, block_buffer) < 0) {
        return -EIO;
    }

    size_t block_offset = fd->pos % sb->block_size;
    struct ext2_dirent *ext2dir = (struct ext2_dirent *) &block_buffer[block_offset];

    if (ext2dir->len == 0) {
        // If entry size is zero, guess we're finished - align the fd->pos up to block size
        fd->pos = (fd->pos + sb->block_size - 1) / sb->block_size;
        return -1;
    }

    struct dirent *vfsdir = (struct dirent *) fd->dirent_buf;

    vfsdir->d_ino = ext2dir->ino;
    strncpy(vfsdir->d_name, ext2dir->name, ext2dir->name_len);
    vfsdir->d_name[ext2dir->name_len] = 0;
    vfsdir->d_reclen = ext2dir->len;
    vfsdir->d_type = ext2dir->type_ind;
    // Not implemented, I guess
    vfsdir->d_off = 0;

    fd->pos += ext2dir->len;

    return 0;
}

static void ext2_vnode_destroy(vnode_t *vn) {
    // Release inode struct
    free(vn->fs_data);
}

static int ext2_vnode_stat(vnode_t *vn, struct stat *st) {
    assert(vn && vn->fs);
    struct ext2_inode *inode = (struct ext2_inode *) vn->fs_data;
    assert(inode);
    struct ext2_extsb *sb = (struct ext2_extsb *) vn->fs->fs_private;
    assert(sb);

    st->st_atime = inode->atime;
    st->st_ctime = inode->ctime;
    st->st_mtime = inode->mtime;
    st->st_dev = 0;     // Not implemented
    st->st_rdev = 0;    // Not implemented
    st->st_gid = inode->gid;
    st->st_uid = inode->uid;
    st->st_mode = inode->type_perm;
    st->st_size = inode->size_lower;
    st->st_blocks = (inode->size_lower + sb->block_size - 1) / sb->block_size;
    st->st_blksize = sb->block_size;
    st->st_nlink = 0;
    st->st_ino = vn->fs_number;

    return 0;
}
