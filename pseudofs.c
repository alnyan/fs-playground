#include "pseudofs.h"
#include "ofile.h"
#include "fs.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static int pseudo_vnode_find(vnode_t *vn, const char *name, vnode_t **res);
static int pseudo_vnode_creat(vnode_t *at, const char *name, int mode, int opt, vnode_t **res);

static ssize_t pseudo_vnode_read(struct ofile *fd, void *buf, size_t count);
static ssize_t pseudo_vnode_write(struct ofile *fd, const void *buf, size_t count);

static char *keyvalue;
static size_t keyvalue_cap = 10;
static size_t keyvalue_len = 0;

static void keyvalue_init(void) {
    keyvalue = (char *) malloc(256 * keyvalue_cap);
    memset(keyvalue, 0, 256 * keyvalue_cap);
}

static int keyvalue_find(const char *key) {
    for (size_t i = 0; i < keyvalue_len; ++i) {
        const char *e = strchr(&keyvalue[i * 256], '=');
        assert(e);
        if (!strncmp(&keyvalue[i * 256], key, e - &keyvalue[i * 256])) {
            return i;
        }
    }
    return -1;
}

static void keyvalue_put(const char *key, const char *value) {
    int index = keyvalue_find(key);
    if (index >= 0) {
        char *e = strchr(&keyvalue[index * 256], '=') + 1;
        strcpy(e, value);
        return;
    }

    if (keyvalue_len == keyvalue_cap) {
        abort();
    }

    strcpy(&keyvalue[keyvalue_len * 256], key);
    keyvalue[keyvalue_len * 256 + strlen(key)] = '=';
    strcpy(&keyvalue[keyvalue_len * 256 + strlen(key) + 1], value);
    ++keyvalue_len;
}

static const char *keyvalue_get(const char *k) {
    int index;
    if ((index = keyvalue_find(k)) < 0) {
        return NULL;
    }
    return strchr(&keyvalue[index * 256], '=') + 1;
}

// :
static vnode_t pseudofs_root_vnode;
static struct vnode_operations pseudofs_vnode_ops = {
    .find = pseudo_vnode_find,

    .creat = pseudo_vnode_creat,

    .open = NULL,
    .close = NULL,
    .read = pseudo_vnode_read,
    .write = pseudo_vnode_write
};

static vnode_t *pseudo_create_reg(const char *key) {
    vnode_t *res = (vnode_t *) malloc(sizeof(vnode_t)); // TODO: vnode_create
    res->op = &pseudofs_vnode_ops;
    // Store node type in private data
    res->fs = NULL;
    res->fs_data = strdup(key);
    res->refcount = 1;
    res->type = VN_REG;

    return res;
}

static int pseudo_vnode_find(vnode_t *vn, const char *name, vnode_t **res) {
    printf("pseudo_vnode_find %s\n", name);
    if (vn == &pseudofs_root_vnode) {
        int index = keyvalue_find(name);
        if (index >= 0) {
            *res = pseudo_create_reg(name);
            return 0;
        }
    }
    // Others are not a directory or not found
    return -ENOENT;
}

static int pseudo_vnode_creat(vnode_t *at, const char *name, int mode, int opt, vnode_t **res) {
    printf("pseudo_vnode_creat %s\n", name);
    assert(at);

    if (at == &pseudofs_root_vnode) {
        *res = pseudo_create_reg(name);
        keyvalue_put(name, "");
        return 0;
    } else {
        return -EROFS;
    }
}

#define MIN(x, y) ((x) < (y) ? (x) : (y))

static ssize_t pseudo_vnode_read(struct ofile *fd, void *buf, size_t count) {
    printf("pseudo_vnode_read\n");
    assert(fd);
    vnode_t *vn = fd->vnode;
    assert(vn);
    const char *key = vn->fs_data;
    assert(key);

    const char *data = keyvalue_get(key);
    if (!data) {
        return -1;
    }

    size_t cnt = MIN(strlen(data), count);
    strncpy(buf, data, cnt);

    return cnt;
}

static ssize_t pseudo_vnode_write(struct ofile *fd, const void *buf, size_t count) {
    printf("pseudo_vnode_write\n");
    assert(fd);
    vnode_t *vn = fd->vnode;
    assert(vn);
    const char *key = vn->fs_data;
    assert(key);

    keyvalue_put(key, buf);

    return count;
}

static vnode_t *pseudo_fs_get_root(struct fs *fs) {
    return &pseudofs_root_vnode;
}

static struct fs_class pseudofs_class = {
    "pseudo",
    pseudo_fs_get_root,
    NULL,
    NULL,
};

void pseudofs_class_init(void) {
    fs_class_register(&pseudofs_class);

    keyvalue_init();
}

// Node definitions
static vnode_t pseudofs_root_vnode = {
    VN_DIR,
    0xFFFFFFFF, // Resident node
    NULL,
    NULL,
    NULL,
    NULL,
    &pseudofs_vnode_ops
};
