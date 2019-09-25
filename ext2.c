#include "ext2.h"
#include "fs.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static int ext2_fs_mount(fs_t *fs, const char *opt) {
    int res;
    printf("ext2_fs_mount()\n");

    // ext2's private data is its superblock structure
    fs->fs_private = malloc(EXT2_SBSIZ);

    // Read the superblock from blkdev
    //if ((res = blk_read(fs->blkdev, fs->fs_private, EXT2_SBOFF, EXT2_SBSIZ)) != EXT2_SBSIZ) {
    //    free(fs->fs_private);

    //    return -EINVAL;
    //}

    return -EINVAL;
}

static int ext2_fs_umount(fs_t *fs) {
    return 0;
}

static vnode_t *ext2_fs_get_root(fs_t *fs) {
    return NULL;
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
