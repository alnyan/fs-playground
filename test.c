#include <stdio.h>
#include "vfs.h"
#include "pseudofs.h"
#include "ext2.h"

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
    if ((res = vfs_find(NULL, "a", &file1)) != 0) {
        fprintf(stderr, "File not found\n");
        return -1;
    }

    // Open a descriptor using the file
    if ((res = vfs_open(&fd0, file1, O_RDONLY)) != 0) {
        fprintf(stderr, "Failed to open file for reading\n");
        return -1;
    }

    // Perform a read
    if ((res = vfs_read(&fd0, buf, sizeof(buf))) < 0) {
        fprintf(stderr, "Failed to read from the file\n");
        return -1;
    }
    buf[res - 1] = 0;
    printf("Data: %s\n", buf);

    // Close the fd
    vfs_close(&fd0);

    vfs_dump_tree();

    return 0;
}
