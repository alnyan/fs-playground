#include "pseudofs.h"
#include "ofile.h"
#include "fs.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static int pseudo_vnode_find(vnode_t *vn, const char *name, vnode_t **res);
static ssize_t pseudo_vnode_read(struct ofile *fd, void *buf, size_t count);
// :
static vnode_t pseudofs_root_vnode;
static struct vnode_operations pseudofs_vnode_ops = {
    .find = pseudo_vnode_find,

    .open = NULL,
    .read = pseudo_vnode_read
};

static vnode_t *pseudo_create_reg(int t) {
    vnode_t *res = (vnode_t *) malloc(sizeof(vnode_t)); // TODO: vnode_create
    res->op = &pseudofs_vnode_ops;
    // Store node type in private data
    res->fs = NULL;
    res->fs_data = (void *) (uintptr_t) t;
    res->refcount = 1;
    res->type = VN_REG;

    return res;
}

static int pseudo_vnode_find(vnode_t *vn, const char *name, vnode_t **res) {
    if (vn == &pseudofs_root_vnode) {
        // ":" contains ":null" and ":zero"
        if (!strcmp(name, "null")) {
            *res = pseudo_create_reg(0);
            return 0;
        }
        if (!strcmp(name, "zero")) {
            *res = pseudo_create_reg(1);
            return 0;
        }
        if (!strcmp(name, "a")) {
            *res = pseudo_create_reg(2);
            return 0;
        }
    }
    // Others are not a directory or not found
    return -ENOENT;
}

static ssize_t pseudo_vnode_read(struct ofile *fd, void *buf, size_t count) {
    vnode_t *vn = fd->vnode;
    int node_type = (int) (uintptr_t) vn->fs_data;

    switch (node_type) {
    case 0:
        return 0;
    case 1:
        memset(buf, 0, count);
        return count;
    case 2:
        memset(buf, 'a', count);
        return count;
    default:
        return -EINVAL;
    }
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
