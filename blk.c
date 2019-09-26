#include "blk.h"

#include <assert.h>
#include <errno.h>

ssize_t blk_read(struct blkdev *blk, void *buf, size_t off, size_t lim) {
    assert(blk);

    if (blk->read) {
        return blk->read(blk, buf, off, lim);
    } else {
        return -EINVAL;
    }
}

ssize_t blk_write(struct blkdev *blk, const void *buf, size_t off, size_t lim) {
    assert(blk);

    if (blk->write) {
        return blk->write(blk, buf, off, lim);
    } else {
        return -EINVAL;
    }
}
