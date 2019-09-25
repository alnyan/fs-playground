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

static void dumpstat(char *buf, const struct stat *st) {
    char t = '-';

    switch (st->st_mode & S_IFMT) {
    case S_IFDIR:
        t = 'd';
        break;
    }

    sprintf(buf, "%c%c%c%c%c%c%c%c%c%c % 5d % 5d %u", t,
        (st->st_mode & S_IRUSR) ? 'r' : '-',
        (st->st_mode & S_IWUSR) ? 'w' : '-',
        (st->st_mode & S_IXUSR) ? 'x' : '-',
        (st->st_mode & S_IRGRP) ? 'r' : '-',
        (st->st_mode & S_IWGRP) ? 'w' : '-',
        (st->st_mode & S_IXGRP) ? 'x' : '-',
        (st->st_mode & S_IROTH) ? 'r' : '-',
        (st->st_mode & S_IWOTH) ? 'w' : '-',
        (st->st_mode & S_IXOTH) ? 'x' : '-',
        st->st_uid,
        st->st_gid,
        st->st_size);
}

int main() {
    int res;
    struct ofile fd0;
    struct stat st0;
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

    printf("stat test.txt\n");
    if ((res = vfs_stat("test.txt", &st0)) != 0) {
        fprintf(stderr, "stat(test.txt): %s\n", errno_str(res));
        return -1;
    }
    dumpstat(buf, &st0);
    printf("%s\t%s\n", buf, "test.txt");
    printf("stat a\n");
    if ((res = vfs_stat("a", &st0)) != 0) {
        fprintf(stderr, "stat(a): %s\n", errno_str(res));
        return -1;
    }
    dumpstat(buf, &st0);
    printf("%s\t%s\n", buf, "a");

    vfs_dump_tree();

    // Cleanup
    vfs_umount(NULL);
    testblk_dev.destroy(&testblk_dev);

    return 0;
}
