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
    // Link destintation if symlink
    struct vfs_node *link;
    int ismount;
    // Parent ref
    struct vfs_node *parent;
    // Linked list of children
    struct vfs_node *child;
    struct vfs_node *cdr;
};

struct statvfs {
    uint64_t f_bsize;
    uint64_t f_frsize;
    fsblkcnt_t f_blocks;
    fsblkcnt_t f_bfree;
    fsblkcnt_t f_bavail;
    fsblkcnt_t f_files;
    fsblkcnt_t f_ffree;
    fsblkcnt_t f_favail;
    uint64_t f_fsid;
    uint64_t f_flag;
    uint64_t f_namemax;
};

// TODO: in real use case this will be extracted from
//       process data struct
struct vfs_ioctx {
    // Process' current working directory
    vnode_t *cwd_vnode;
    uid_t uid;
    gid_t gid;
};
//extern struct vfs_user_context vfs_ctx;

int vfs_setcwd(struct vfs_ioctx *ctx, const char *cwd);
int vfs_chdir(struct vfs_ioctx *ctx, const char *cwd_rel);

void vfs_init(void);
void vfs_dump_tree(void);
void vfs_vnode_path(char *path, vnode_t *vn);

// Tree node ops
void vfs_node_free(struct vfs_node *n);
struct vfs_node *vfs_node_create(const char *name, vnode_t *vn);

int vfs_mount(struct vfs_ioctx *ctx, const char *at, void *blk, const char *fs_name, const char *fs_opt);
int vfs_umount(struct vfs_ioctx *ctx, const char *target);

int vfs_readlink(struct vfs_ioctx *ctx, const char *path, char *buf);
int vfs_readlinkat(struct vfs_ioctx *ctx, vnode_t *at, const char *path, char *buf);
int vfs_symlink(struct vfs_ioctx *ctx, const char *target, const char *linkpath);

// File ops
int vfs_truncate(struct vfs_ioctx *ctx, struct ofile *fd, size_t length);
int vfs_creat(struct vfs_ioctx *ctx, struct ofile *fd, const char *path, int mode, int opt);
int vfs_open(struct vfs_ioctx *ctx, struct ofile *fd, const char *path, int mode, int opt);
int vfs_open_node(struct vfs_ioctx *ctx, struct ofile *fd, vnode_t *vn, int opt);
void vfs_close(struct vfs_ioctx *ctx, struct ofile *fd);
ssize_t vfs_read(struct vfs_ioctx *ctx, struct ofile *fd, void *buf, size_t count);
ssize_t vfs_write(struct vfs_ioctx *ctx, struct ofile *fd, const void *buf, size_t count);
int vfs_unlink(struct vfs_ioctx *ctx, const char *path);

int vfs_stat(struct vfs_ioctx *ctx, const char *path, struct stat *st);
int vfs_statat(struct vfs_ioctx *ctx, vnode_t *at, const char *path, struct stat *st);
int vfs_chmod(struct vfs_ioctx *ctx, const char *path, mode_t mode);
int vfs_chown(struct vfs_ioctx *ctx, const char *path, uid_t uid, gid_t gid);
int vfs_access(struct vfs_ioctx *ctx, const char *path, int mode);
// Directroy ops
int vfs_mkdir(struct vfs_ioctx *ctx, const char *path, mode_t mode);
struct dirent *vfs_readdir(struct vfs_ioctx *ctx, struct ofile *fd);

int vfs_statvfs(struct vfs_ioctx *ctx, const char *path, struct statvfs *st);
