/** vim: ft=c.doxygen
 * @file node.h
 * @brief vnode
 */
#pragma once
#include <stdint.h>
#include <sys/types.h>

#if !defined(__linux__)
#include "dirent.h"
#include "stat.h"

#define O_RDONLY    (1 << 0)
#define O_WRONLY    (1 << 1)
#define O_RDWR      (O_RDONLY | O_WRONLY)
#define O_DIRECTORY (1 << 14)
#define O_CREAT     (1 << 15)
#else
#include <fcntl.h>
#include <dirent.h>
#endif

// O_EXEC is a special one for opening a node for
//        execution
#define O_EXEC      (1 << 2)

struct ofile;
typedef struct vnode vnode_t;
typedef struct fs fs_t;

enum vnode_type {
    VN_REG,
    VN_DIR,
    VN_BLK,
    VN_CHR
};

/**
 * @brief Set of functions implemented by filesystem driver
 */
struct vnode_operations {
    // File tree traversal, node instance operations
    int (*find) (vnode_t *node, const char *path, vnode_t **res);
    void (*destroy) (vnode_t *node);

    // File entry operations
    int (*creat) (vnode_t *node, const char *name, mode_t mode, int opt, vnode_t **res);
    int (*stat) (vnode_t *node, struct stat *st);

    // Directory access
    int (*opendir) (vnode_t *node, int opt);
    int (*readdir) (struct ofile *fd);

    // File access
    int (*open) (vnode_t *node, int opt);
    void (*close) (struct ofile *fd);
    ssize_t (*read) (struct ofile *fd, void *buf, size_t count);
    ssize_t (*write) (struct ofile *fd, const void *buf, size_t count);
};

struct vnode {
    enum vnode_type type;

    uint32_t refcount;

    fs_t *fs;
    // Private filesystem-specific data (like inode struct)
    void *fs_data;
    // Private filesystem-specific number (like inode number)
    uint32_t fs_number;

    void *tree_node;

    struct vnode_operations *op;
};

void vnode_ref(vnode_t *vn);
void vnode_unref(vnode_t *vn);
void vnode_free(vnode_t *vn);
