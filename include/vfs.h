#pragma once
#include "tree.h"
#include "node.h"
#include "ofile.h"

// Internal VFS tree node
struct vfs_node {
    char name[256];
    vnode_t *vnode;
    // Parent ref
    struct vfs_node *parent;
    // Linked list of children
    struct vfs_node *child;
    struct vfs_node *cdr;
};

void vfs_init(void);
void vfs_dump_tree(void);
void vfs_vnode_path(char *path, vnode_t *vn);

// Tree node ops
void vfs_node_free(struct vfs_node *n);
struct vfs_node *vfs_node_create(const char *name, vnode_t *vn);

int vfs_find(vnode_t *root, const char *path, vnode_t **res);
int vfs_mount(vnode_t *at, void *blk, const char *fs_name, const char *fs_opt);
// File ops
int vfs_creat(struct ofile *fd, const char *path, int mode, int opt);
int vfs_open(struct ofile *fd, const char *path, int mode, int opt);
int vfs_open_node(struct ofile *fd, vnode_t *vn, int opt);
void vfs_close(struct ofile *fd);
ssize_t vfs_read(struct ofile *fd, void *buf, size_t count);
ssize_t vfs_write(struct ofile *fd, const void *buf, size_t count);
