#include <stdio.h>
#include "vfs.h"
#include "ext2.h"
#include "testblk.h"

#include <errno.h>

static const char *errno_str(int r) {
    switch (r) {
        case -EIO:
            return "I/O error";
        case -ENOENT:
            return "No such file or directory";
        case -EINVAL:
            return "Invalid argument";
        case -EROFS:
            return "Read-only filesystem";
        default:
            return "Unknown error";
    }
}

int main() {
    int res;
    struct ofile fd0;
    vnode_t *file1;
    char buf[513];

    vfs_init();
    ext2_class_init();
    testblk_init("ext2.img");

    if ((res = vfs_mount(NULL, &testblk_dev, "ext2", NULL)) != 0) {
        fprintf(stderr, "Failed to mount rootfs\n");
        return -1;
    }

    if ((res = vfs_open(&fd0, "a/b.txt", 0, O_RDONLY)) != 0) {
        fprintf(stderr, "test.txt: %s\n", errno_str(res));
        return -1;
    }

    size_t bread_total = 0;
    while ((res = vfs_read(&fd0, buf, sizeof(buf) - 1)) > 0) {
        printf("%d bytes\n", res);
        buf[res] = 0;
        printf("READ DATA\n%s\n", buf);
        bread_total += res;
    }
    printf("Total: %zu\n", bread_total);

    vfs_close(&fd0);

    vfs_dump_tree();

    // Cleanup
    vfs_umount(NULL);
    testblk_dev.destroy(&testblk_dev);

    return 0;
}
