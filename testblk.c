#include "testblk.h"

#include <assert.h>
#include <stdio.h>

static ssize_t testblk_dev_read(struct blkdev *blk, void *buf, size_t off, size_t count) {
    if (fseek(blk->dev_data, off, SEEK_SET) != 0) {
        return -1;
    }
    return fread(buf, 1, count, blk->dev_data);
}

static ssize_t testblk_dev_write(struct blkdev *blk, const void *buf, size_t off, size_t count) {
    if (fseek(blk->dev_data, off, SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(buf, 1, count, blk->dev_data);
}

static void testblk_dev_destroy(struct blkdev *blk) {
    fclose(blk->dev_data);
}

struct blkdev testblk_dev = {
    NULL,

    .read = testblk_dev_read,
    .write = testblk_dev_write,
    .destroy = testblk_dev_destroy
};

void testblk_init(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    assert(fp);
    testblk_dev.dev_data = fp;
}
