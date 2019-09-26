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
static int ext2_vnode_open(vnode_t *vn, int opt);
static int ext2_vnode_opendir(vnode_t *vn, int opt);
static ssize_t ext2_vnode_read(struct ofile *fd, void *buf, size_t count);
static int ext2_vnode_readdir(struct ofile *fd);
static void ext2_vnode_destroy(vnode_t *vn);
static int ext2_vnode_stat(vnode_t *vn, struct stat *st);

static struct vnode_operations ext2_vnode_ops = {
    .find = ext2_vnode_find,
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
    uint32_t block_group_descriptor_table_size_blocks = 32 * block_group_descriptor_table_length /
                                                        sb->block_size + 1;

    uint32_t block_group_descriptor_table_block = 2;
    if (sb->block_size > 1024) {
        block_group_descriptor_table_block = 1;
    }

    // Load all block group descriptors into memory
    printf("Allocating %u bytes for BGDT\n", block_group_descriptor_table_size_blocks * sb->block_size);
    sb->block_group_descriptor_table = (struct ext2_grp_desc *) malloc(block_group_descriptor_table_size_blocks * sb->block_size);

    for (size_t i = 0; i < block_group_descriptor_table_size_blocks; ++i) {
        ext2_read_block(fs, i + block_group_descriptor_table_block,
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
                    printf("Lookup %s in ino %d = %d\n", name, vn->fs_number, out->fs_number);
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
