#include <stdio.h>
#include "vfs.h"
#include "pseudofs.h"
#include "ext2.h"

#include <errno.h>

static const char *errno_str(int r) {
    switch (r) {
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
    char buf[1024];

    vfs_init();
    ext2_class_init();
    pseudofs_class_init();

    // Setup root as ext2
    if ((res = vfs_mount(NULL, NULL, "pseudo", NULL)) != 0) {
        fprintf(stderr, "Failed to mount root filesystem\n");
        return -1;
    }

    // Lookup etc/file1.txt in root
    if ((res = vfs_creat(&fd0, "/b", 0644,  O_CREAT | O_WRONLY)) != 0) {
        fprintf(stderr, "Failed to open file for reading: %s\n", errno_str(res));
        return -1;
    }

    // Perform a write
    if ((res = vfs_write(&fd0, "value", 5)) < 0) {
        fprintf(stderr, "Failed to write to the file: %s\n", errno_str(res));
        return -1;
    }

    // Close the fd
    vfs_close(&fd0);

    // Now try to open the file for reading
    if ((res = vfs_open(&fd0, "/b", 0, O_RDONLY)) != 0) {
        fprintf(stderr, "Failed to open file for reading: %s\n", errno_str(res));
    }

    if ((res = vfs_read(&fd0, buf, sizeof(buf))) < 0) {
        fprintf(stderr, "Failed to read file: %s\n", errno_str(res));
    }

    printf("Data: %s\n", buf);

    vfs_close(&fd0);

    vfs_dump_tree();

    return 0;
}
