/** vim: ft=c.doxygen
 * @file node.h
 * @brief vnode
 */
#pragma once
#include <stdint.h>
#include <sys/types.h>

#define O_RDONLY    (1 << 0)
#define O_WRONLY    (1 << 1)
#define O_RDWR      (O_RDONLY | O_WRONLY)
#define O_EXEC      (1 << 2)

struct ofile;
typedef struct vnode vnode_t;
typedef struct fs fs_t;

enum vnode_type {
    VN_REG,
    VN_DIR,
    VN_MNT
};

/**
 * @brief Set of functions implemented by filesystem driver
 */
struct vnode_operations {
    // File tree traversal, node instance operations
    int (*find) (vnode_t *node, const char *path, vnode_t **res);
    void (*destroy) (vnode_t *node);

    // File access
    int (*open) (vnode_t *node, int opt);
    void (*close) (struct ofile *fd);
    ssize_t (*read) (struct ofile *fd, void *buf, size_t count);
};

struct vnode {
    enum vnode_type type;

    uint32_t refcount;

    fs_t *fs;
    void *fs_data;

    void *tree_node;
    struct vnode *mnt;

    struct vnode_operations *op;
};

void vnode_ref(vnode_t *vn);
void vnode_unref(vnode_t *vn);
