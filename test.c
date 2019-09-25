#include <stdio.h>
#include "vfs.h"
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

    vfs_dump_tree();

    return 0;
}
