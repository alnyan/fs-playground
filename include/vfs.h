#pragma once
#include "tree.h"
#include "node.h"
#include "ofile.h"
#include "stat.h"

// Internal VFS tree node
struct vfs_node {
    char name[256];
    // Current vnode unless mnt, otherwise the root node of
    // the mounted filesystem
    vnode_t *vnode;
    // Real vnode if mountpoint
    vnode_t *real_vnode;
    int ismount;
    // Parent ref
    struct vfs_node *parent;
    // Linked list of children
    struct vfs_node *child;
    struct vfs_node *cdr;
};

// TODO: in real use case this will be extracted from
//       process data struct
extern char vfs_cwd[1024];
int vfs_setcwd(const char *cwd);
int vfs_chdir(const char *cwd_rel);

void vfs_init(void);
void vfs_dump_tree(void);
void vfs_vnode_path(char *path, vnode_t *vn);

// Tree node ops
void vfs_node_free(struct vfs_node *n);
struct vfs_node *vfs_node_create(const char *name, vnode_t *vn);

int vfs_mount(const char *at, void *blk, const char *fs_name, const char *fs_opt);
int vfs_umount(const char *target);
// File ops
int vfs_statat(vnode_t *at, const char *path, struct stat *st);
int vfs_stat(const char *path, struct stat *st);
int vfs_creat(struct ofile *fd, const char *path, int mode, int opt);
int vfs_open(struct ofile *fd, const char *path, int mode, int opt);
int vfs_open_node(struct ofile *fd, vnode_t *vn, int opt);
void vfs_close(struct ofile *fd);
ssize_t vfs_read(struct ofile *fd, void *buf, size_t count);
ssize_t vfs_write(struct ofile *fd, const void *buf, size_t count);
struct dirent *vfs_readdir(struct ofile *fd);
