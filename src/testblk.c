#include "testblk.h"

#include <assert.h>
#include <stdio.h>

static void blk_dump(const char *bytes, size_t siz) {
    size_t j = 0;
    printf("-----\n");
    for (size_t i = 0; i < siz; ++i) {
        if (j == 0) {
            printf("%04zx\t", i);
        }

        printf("%02hhx", bytes[i]);

        if (j == 8) {
            j = 0;
            printf("\n");
        } else {
            ++j;
        }
    }
    if (j != 0) {
        printf("\n");
    }
    printf("-----\n");
}

static ssize_t testblk_dev_read(struct blkdev *blk, void *buf, size_t off, size_t count) {
    if (fseek(blk->dev_data, off, SEEK_SET) != 0) {
        return -1;
    }
    ssize_t res = fread(buf, 1, count, blk->dev_data);
    return res;
}

static ssize_t testblk_dev_write(struct blkdev *blk, const void *buf, size_t off, size_t count) {
    if (fseek(blk->dev_data, off, SEEK_SET) != 0) {
        return -1;
    }
    ssize_t res = fwrite(buf, 1, count, blk->dev_data);
    fflush(blk->dev_data);
    if (res == 0) {
        printf("NO DATA WRITTEN\n");
    }
    return res;
}

static void testblk_dev_destroy(struct blkdev *blk) {
    printf("Closing device\n");
    fclose(blk->dev_data);
}

struct blkdev testblk_dev = {
    NULL,

    .read = testblk_dev_read,
    .write = testblk_dev_write,
    .destroy = testblk_dev_destroy
};

void testblk_init(const char *filename) {
    FILE *fp = fopen(filename, "r+b");
    assert(fp);
    testblk_dev.dev_data = fp;
}
