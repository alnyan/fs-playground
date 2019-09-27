#include "vfs.h"
#include "fs.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#include <stdio.h>

static struct vfs_node root_node;
struct vfs_user_context vfs_ctx;

static int vfs_find(vnode_t *cwd_vnode, const char *path, vnode_t **res_vnode);
static int vfs_access_internal(int desm, mode_t mode, uid_t uid, gid_t gid);

static int vfs_setcwd_rel(vnode_t *at, const char *path) {
    // cwd is absolute path
    vnode_t *new_cwd;
    int res;
    if ((res = vfs_find(at, path, &new_cwd)) != 0) {
        return res;
    }

    vnode_ref(new_cwd);
    if (new_cwd->type != VN_DIR) {
        vnode_unref(new_cwd);
        return -ENOTDIR;
    }

    if (vfs_ctx.cwd_vnode) {
        vnode_unref(vfs_ctx.cwd_vnode);
    }
    vfs_ctx.cwd_vnode = new_cwd;

    return 0;
}

int vfs_setcwd(const char *cwd) {
    return vfs_setcwd_rel(NULL, cwd);
}

int vfs_chdir(const char *cwd_rel) {
    return vfs_setcwd_rel(vfs_ctx.cwd_vnode, cwd_rel);
}

static int vfs_open_access_mask(int oflags) {
    if (oflags & O_EXEC) {
        return X_OK;
    }

    switch (oflags & O_ACCMODE) {
    case O_WRONLY:
        return W_OK;
    case O_RDONLY:
        return R_OK;
    case O_RDWR:
        return R_OK | W_OK;
    default:
        abort();
    }
}

static int vfs_access_internal(int desm, mode_t mode, uid_t uid, gid_t gid) {
    if (vfs_ctx.uid == 0) {
        if (desm & X_OK) {
            // Check if anyone at all can execute this
            if (!(mode & (S_IXOTH | S_IXGRP | S_IXUSR))) {
                return -EACCES;
            }
        }

        return 0;
    }

    if (uid == vfs_ctx.uid) {
        if ((desm & R_OK) && !(mode & S_IRUSR)) {
            return -EACCES;
        }
        if ((desm & W_OK) && !(mode & S_IWUSR)) {
            return -EACCES;
        }
        if ((desm & X_OK) && !(mode & S_IXUSR)) {
            return -EACCES;
        }
    } else if (gid == vfs_ctx.gid) {
        if ((desm & R_OK) && !(mode & S_IRGRP)) {
            return -EACCES;
        }
        if ((desm & W_OK) && !(mode & S_IWGRP)) {
            return -EACCES;
        }
        if ((desm & X_OK) && !(mode & S_IXGRP)) {
            return -EACCES;
        }
    } else {
        if ((desm & R_OK) && !(mode & S_IROTH)) {
            return -EACCES;
        }
        if ((desm & W_OK) && !(mode & S_IWOTH)) {
            return -EACCES;
        }
        if ((desm & X_OK) && !(mode & S_IXOTH)) {
            return -EACCES;
        }
    }

    return 0;
}

void vfs_init(void) {
    // Setup testing usage context
    vfs_ctx.cwd_vnode = NULL;

    // Setup root node
    strcpy(root_node.name, "[root]");
    root_node.vnode = NULL;
    root_node.real_vnode = NULL;
    root_node.parent = NULL;
    root_node.cdr = NULL;
    root_node.child = NULL;
}

static const char *vfs_path_element(char *dst, const char *src) {
    const char *sep = strchr(src, '/');
    if (!sep) {
        strcpy(dst, src);
        return NULL;
    } else {
        strncpy(dst, src, sep - src);
        dst[sep - src] = 0;
        while (*sep == '/') {
            ++sep;
        }
        if (!*sep) {
            return NULL;
        }
        return sep;
    }
}

void vfs_node_free(struct vfs_node *node) {
    assert(node && node->vnode);
    assert(node->vnode->refcount == 0);
    free(node);
}

struct vfs_node *vfs_node_create(const char *name, vnode_t *vn) {
    assert(vn);
    struct vfs_node *node = (struct vfs_node *) malloc(sizeof(struct vfs_node));
    vn->refcount = 0;
    vn->tree_node = node;
    node->vnode = vn;
    strcpy(node->name, name);
    node->ismount = 0;
    node->real_vnode = NULL;
    node->parent = NULL;
    node->child = NULL;
    node->cdr = NULL;
    return node;
}

/**
 * @brief The same as vfs_find, but more internal to the VFS - it operates
 *        on VFS path tree instead of vnodes (as they have no hierarchy defined)
 */
static int vfs_find_tree(struct vfs_node *root_node, const char *path, struct vfs_node **res_node) {
    if (!path || !*path) {
        // The path refers to the node itself
        *res_node = root_node;
        return 0;
    }

    // Assuming the path is normalized
    char path_element[256];
    const char *child_path = vfs_path_element(path_element, path);

    // TODO: this should also be handled by path canonicalizer
    while (!strcmp(path_element, ".")) {
        if (!child_path) {
            // The node we're looking for is this node
            *res_node = root_node;
            return 0;
        }
        child_path = vfs_path_element(path_element, child_path);
    }
    if (!strcmp(path_element, "..")) {
        if (root_node->parent) {
            return vfs_find_tree(root_node->parent, child_path, res_node);
        } else {
            // TODO: handle root escape
            abort();
        }
    }

    vnode_t *root_vnode = root_node->vnode;
    assert(root_vnode);
    int res;

    // 1. Make sure we're either looking inside a directory
    if (root_vnode->type == VN_DIR) {
        // 3.1. It's a directory, try looking up path element inside
        //      the path tree
        for (struct vfs_node *it = root_node->child; it; it = it->cdr) {
            if (!strcmp(it->name, path_element)) {
                // Found matching path element
                if (!child_path) {
                    // We're at terminal path element - which means we've
                    // found what we're looking for
                    //vnode_ref(root_vnode);
                    *res_node = it;
                    return 0;
                } else {
                    //printf("Entering vfs_node %s\n", it->name);
                    // Continue searching deeper
                    if ((res = vfs_find_tree(it, child_path, res_node)) != 0) {
                        // Nothing found
                        return res;
                    }

                    // Found something
                    return 0;
                }
            }
        }

        // 3.2. Nothing found in path tree - request the fs to find
        //      the vnode for the path element given
        vnode_t *child_vnode = NULL;
        struct vfs_node *child_node = NULL;

        //printf("Calling op->find on %s\n", root_node->name);
        if ((res = root_vnode->op->find(root_vnode, path_element, &child_vnode)) != 0) {
            // fs didn't find anything - no such file or directory exists
            return res;
        }

        // 3.3. Found some vnode, attach it to the VFS tree
        child_node = vfs_node_create(path_element, child_vnode);

        // Prepend it to parent's child list
        child_node->parent = root_node;
        child_node->cdr = root_node->child;
        root_node->child = child_node;

        if (!child_path) {
            // We're at terminal path element - return the node
            *res_node = child_node;
            return 0;
        } else {
            //printf("Entering vfs_node %s\n", child_node->name);
            if ((res = vfs_find_tree(child_node, child_path, res_node)) != 0) {
                // Nothing found in the child
                return res;
            }

            // Found what we're looking for
            // Now have not only a child reference, but also someone uses the
            // node down the tree, so increment the refcounter
            return 0;
        }
    } else {
        // Not a directory/mountpoint - cannot contain anything
        return -ENOENT;
    }
}

static int vfs_find_at(vnode_t *root_vnode, const char *path, vnode_t **res_vnode) {
    // The input path should be without leading slash and relative to root_vnode
    struct vfs_node *res_node = NULL;
    int res;

    if (!root_vnode) {
        // Root node contains no vnode - which means there's no root
        // at all
        if (!root_node.vnode) {
            return -ENOENT;
        }

        res = vfs_find_tree(&root_node, path, &res_node);
    } else {
        assert(root_vnode->tree_node);

        res = vfs_find_tree((struct vfs_node *) root_vnode->tree_node, path, &res_node);
    }

    if (res != 0) {
        return res;
    }

    assert(res_node);
    *res_vnode = res_node->vnode;
    return 0;
}

static int vfs_find(vnode_t *cwd_vnode, const char *path, vnode_t **res_vnode) {
    if (*path != '/') {
        return vfs_find_at(cwd_vnode, path, res_vnode);
    } else {
        // Use root as search base
        while (*path == '/') {
            ++path;
        }

        return vfs_find_at(NULL, path, res_vnode);
    }
}

static void vfs_dump_node(struct vfs_node *node, int o) {
    for (int i = 0; i < o; ++i) {
        printf("  ");
    }
    printf("% 4d %s", node->vnode->refcount, node->name);
    if (node->vnode->type == VN_DIR) {
        if (node->ismount) {
            printf(" (mount)");
        }
        printf(":\n");
        for (struct vfs_node *it = node->child; it; it = it->cdr) {
            vfs_dump_node(it, o + 1);
        }
    } else {
        printf("\n");
    }
}

void vfs_dump_tree(void) {
    if (!root_node.vnode) {
        return;
    }
    vfs_dump_node(&root_node, 0);
}

void vfs_vnode_path(char *path, vnode_t *vn) {
    size_t c = 0;
    struct vfs_node *node = vn->tree_node;
    struct vfs_node *backstack[10] = { NULL };
    if (!node) {
        sprintf(path, "<unknown:%p>", vn);
        return;
    }
    size_t bstp = 10;
    while (node) {
        if (bstp == 0) {
            abort();
        }

        backstack[--bstp] = node;
        node = node->parent;
    }

    for (size_t i = bstp; i < 10; ++i) {
        size_t len = strlen(backstack[i]->name);
        strcpy(path + c, backstack[i]->name);
        c += len;
        if (i != 9) {
            path[c] = '/';
        }
        ++c;
    }
}

static int vfs_mount_internal(struct vfs_node *at, void *blkdev, const char *fs_name, const char *opt) {
    struct fs_class *fs_class;

    if ((fs_class = fs_class_by_name(fs_name)) == NULL) {
        return -EINVAL;
    }

    // Create a new fs instance/mount
    struct fs *fs;

    if ((fs = fs_create(fs_class, blkdev, NULL)) == NULL) {
        return -EINVAL;
    }

    assert(fs_class->get_root);
    if ((fs_class->mount != NULL) && (fs_class->mount(fs, opt) != 0)) {
        return -1;
    }

    if (!at) {
        at = &root_node;
    }

    vnode_t *fs_root;
    vnode_t *old_vnode = at->vnode;

    if (at->child) {
        // Target directory already has child nodes loaded in memory, return "busy"
        // TODO: destroy fs
        return -EBUSY;
    }

    if (at->ismount) {
        // TODO: report error and destroy fs
        abort();
    }

    // Try to get root
    if ((fs_root = fs_class->get_root(fs)) == NULL) {
        // TODO: report error and destroy fs
        abort();
    }

    // If it's a root mount, set root vnode
    printf("Mounting new fs on %s\n", at->name);
    at->vnode = fs_root;
    at->real_vnode = old_vnode;
    at->ismount = 1;
    fs_root->tree_node = at;

    return 0;
}

int vfs_mount(const char *target, void *blkdev, const char *fs_name, const char *opt) {
    struct vfs_node *mount_at;
    vnode_t *vnode_mount_at;
    int res;

    if (!root_node.vnode) {
        // Root does not yet exist, check if we're mounting root:
        if (!strcmp(target, "/")) {
            printf("MOUNTING NEW ROOTFS\n");
            return vfs_mount_internal(NULL, blkdev, fs_name, opt);
        }

        // Otherwise we cannot perform mounting
        return -ENOENT;
    }

    // Lookup the tree node we're mounting at
    if ((res = vfs_find(vfs_ctx.cwd_vnode, target, &vnode_mount_at)) != 0) {
        return res;
    }

    // Get tree node
    mount_at = vnode_mount_at->tree_node;
    assert(mount_at);

    return vfs_mount_internal(mount_at, blkdev, fs_name, opt);
}

int vfs_umount(const char *target) {
    if (!root_node.vnode) {
        // No root, don't even bother umounting anything
        return -ENOENT;
    }

    // Lookup target vnode's tree_node
    assert(target);
    vnode_t *at_vnode;
    struct vfs_node *at;
    int res;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, target, &at_vnode)) != 0) {
        return res;
    }

    at = at_vnode->tree_node;
    assert(at);

    if (!at->ismount) {
        // Not mounted
        return -EINVAL;
    }

    if (at->child) {
        // There're some used vnodes down the tree
        return -EBUSY;
    }

    at->vnode = at->real_vnode;
    at->ismount = 0;

    if (at_vnode == vfs_ctx.cwd_vnode) {
        // Umounting the cwd
        vfs_ctx.cwd_vnode = NULL;
    }
    at_vnode->refcount = 0;
    vnode_free(at_vnode);

    return 0;
}

static void vfs_path_parent(char *dst, const char *path) {
    // The function expects normalized paths without . and ..
    // Possible inputs:
    //  "/" -> "/"
    //  "/dir/x/y" -> "/dir/x"

    const char *slash = strrchr(path, '/');
    if (!slash) {
        dst[0] = 0;
        return;
    }

    strncpy(dst, path, slash - path);
    dst[slash - path] = 0;
}

static const char *vfs_path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return path;
    }

    return slash + 1;
}

static int vfs_creat_internal(vnode_t *at, const char *name, int mode, int opt, vnode_t **resvn) {
    // Create a file without opening it
    assert(at && at->op && at->tree_node);
    int res;

    if (!at->op->creat) {
        return -EROFS;
    }

    // TODO: add uid/gid to vnode and creat() of the
    // filesystem shall write these to resulting inode

    if ((res = at->op->creat(at, name, mode, opt, resvn)) != 0) {
        return res;
    }

    struct vfs_node *parent_node = at->tree_node;
    struct vfs_node *child_node = vfs_node_create(name, *resvn);

    // Prepend it to parent's child list
    child_node->parent = parent_node;
    child_node->cdr = parent_node->child;
    parent_node->child = child_node;

    return 0;
}

int vfs_creat(struct ofile *of, const char *path, int mode, int opt) {
    vnode_t *parent_vnode = NULL;
    vnode_t *vnode = NULL;
    int res;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) == 0) {
        return vfs_open_node(of, vnode, opt & ~O_CREAT);
    }

    if (*path == '/') {
        // Get parent vnode
        char parent_path[1024];
        vfs_path_parent(parent_path, path);

        if ((res = vfs_find(NULL, parent_path, &parent_vnode)) != 0) {
            printf("Parent does not exist: %s\n", parent_path);
            // Parent doesn't exist, too - error
            return res;
        }
    } else {
        char parent_path[1024];
        vfs_path_parent(parent_path, path);

        if (!*parent_path) {
            parent_path[0] = '.';
            parent_path[1] = 0;
        }

        // Find parent
        if ((res = vfs_find(vfs_ctx.cwd_vnode, parent_path, &parent_vnode)) != 0) {
            printf("Parent does not exist: %s\n", parent_path);
            return res;
        }
    }

    if (parent_vnode->type != VN_DIR) {
        // Parent is not a directory
        return -ENOTDIR;
    }

    printf("Path: %s\n", path);
    path = vfs_path_basename(path);

    if (!path) {
        return -EINVAL;
    }

    if ((res = vfs_creat_internal(parent_vnode, path, mode, opt & ~O_CREAT, &vnode)) != 0) {
        // Could not create entry
        return res;
    }

    if (!of) {
        vnode_ref(vnode);
        vnode_unref(vnode);
        // Need opening the file, but no descriptor provided
        return -EINVAL;
    }

    return vfs_open_node(of, vnode, opt & ~O_CREAT);
}

int vfs_open(struct ofile *of, const char *path, int mode, int opt) {
    assert(of);
    // Try to find the file
    int res;
    vnode_t *vnode = NULL;

    // TODO: normalize path

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) != 0) {
        if (!(opt & O_CREAT)) {
            return -ENOENT;
        }

        return vfs_creat(of, path, mode, opt);
    }

    return vfs_open_node(of, vnode, opt & ~O_CREAT);
}

int vfs_open_node(struct ofile *of, vnode_t *vn, int opt) {
    // TODO: O_APPEND
    assert(vn && vn->op && of);
    int res;

    if (opt & O_DIRECTORY) {
        assert((opt & O_ACCMODE) == O_RDONLY);
        // How does one truncate a directory?
        assert(!(opt & O_TRUNC));
        assert(!(opt & O_CREAT));
        vnode_ref(vn);

        if (vn->type != VN_DIR) {
            vnode_unref(vn);
            return -ENOTDIR;
        }

        // opendir
        if (vn->op->opendir) {
            if ((res = vn->op->opendir(vn, opt)) != 0) {
                vnode_unref(vn);
                return res;
            }
        } else {
            vnode_unref(vn);
            return -EINVAL;
        }

        of->flags = opt;
        of->vnode = vn;
        of->pos = 0;

        return res;
    }

    // Check flag sanity
    // Can't have O_CREAT here
    if (opt & O_CREAT) {
        vnode_ref(vn);
        vnode_unref(vn);
        return -EINVAL;
    }
    // Can't be both (RD|WR) and EX
    if (opt & O_EXEC) {
        if (opt & O_ACCMODE) {
            vnode_ref(vn);
            vnode_unref(vn);
            return -EACCES;
        }
    }

    // TODO: check permissions here
    if (vn->type == VN_DIR) {
        vnode_ref(vn);
        vnode_unref(vn);
        return -EISDIR;
    }

    of->vnode = vn;
    of->flags = opt;
    of->pos = 0;

    if (opt & O_APPEND) {
        // TODO: rewrite open() to accept struct ofile *
        // instead of vnode so that open() function of the
        // vnode can properly set of->pos
        vnode_ref(vn);
        vnode_unref(vn);
        fprintf(stderr, "O_APPEND not yet implemented\n");
        return -EINVAL;
    }

    // Check if file has to be truncated before opening it
    if (opt & O_TRUNC) {
        if (!vn->op->truncate) {
            vnode_ref(vn);
            vnode_unref(vn);
            return -EINVAL;
        }

        if ((res = vn->op->truncate(of, 0)) != 0) {
            vnode_ref(vn);
            vnode_unref(vn);
            return res;
        }
    }

    if (vn->op->open) {
        if ((res = vn->op->open(vn, opt)) != 0) {
            vnode_ref(vn);
            vnode_unref(vn);
            return res;
        }
    }

    vnode_ref(vn);
    return 0;
}

void vfs_close(struct ofile *of) {
    assert(of);
    vnode_t *vn = of->vnode;
    assert(vn && vn->op);

    if (vn->op->close) {
        vn->op->close(of);
    }

    vnode_unref(of->vnode);
}

int vfs_statat(vnode_t *at, const char *path, struct stat *st) {
    assert(at && path && st);
    int res;
    vnode_t *vnode;

    if ((res = vfs_find(at, path, &vnode)) != 0) {
        return res;
    }

    vnode_ref(vnode);

    if (!vnode->op || !vnode->op->stat) {
        res = -EINVAL;
    } else {
        res = vnode->op->stat(vnode, st);
    }

    vnode_unref(vnode);
    return res;
}

int vfs_stat(const char *path, struct stat *st) {
    assert(path && st);
    vnode_t *vnode;
    int res;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) != 0) {
        return res;
    }

    vnode_ref(vnode);

    if (!vnode->op || !vnode->op->stat) {
        res = -EINVAL;
    } else {
        res = vnode->op->stat(vnode, st);
    }

    vnode_unref(vnode);
    return res;
}

ssize_t vfs_read(struct ofile *fd, void *buf, size_t count) {
    assert(fd);
    vnode_t *vn = fd->vnode;
    assert(vn && vn->op);

    if (fd->flags & O_DIRECTORY) {
        return -EISDIR;
    }
    if ((fd->flags & O_ACCMODE) == O_WRONLY) {
        return -EINVAL;
    }
    if (vn->op->read == NULL) {
        return -EINVAL;
    }

    ssize_t nr = vn->op->read(fd, buf, count);

    if (nr > 0) {
        fd->pos += nr;
    }

    return nr;
}

ssize_t vfs_write(struct ofile *fd, const void *buf, size_t count) {
    assert(fd);
    vnode_t *vn = fd->vnode;
    assert(vn && vn->op);

    if (fd->flags & O_DIRECTORY) {
        return -EISDIR;
    }
    if ((fd->flags & O_ACCMODE) == O_RDONLY) {
        return -EINVAL;
    }
    if (vn->op->write == NULL) {
        return -EINVAL;
    }

    return vn->op->write(fd, buf, count);
}

int vfs_truncate(struct ofile *of, size_t length) {
    assert(of);
    if ((of->flags & O_ACCMODE) == O_RDONLY) {
        return -EINVAL;
    }
    vnode_t *vn = of->vnode;
    assert(vn && vn->op);

    if (!vn->op->truncate) {
        return -EINVAL;
    }

    return vn->op->truncate(of, length);
}

int vfs_unlink(const char *path) {
    // XXX: validate this with removing mounted roots
    assert(path);
    // Find the vnode to unlink
    int res;
    vnode_t *parent_vnode, *vnode;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) < 0) {
        return res;
    }

    assert(vnode && vnode->op);
    vnode_ref(vnode);

    // Get node parent
    struct vfs_node *node = vnode->tree_node;
    assert(node);

    if (!node->parent) {
        // Trying to unlink root node?
        vnode_unref(vnode);
        return -EACCES;
    }

    parent_vnode = node->parent->vnode;
    vnode_ref(parent_vnode);

    if (parent_vnode->op->unlink) {
        // TODO: handle
        //       unlink("path/to/node/./.")
        path = vfs_path_basename(path);
        assert(path);

        if ((res = parent_vnode->op->unlink(parent_vnode, vnode, path)) < 0) {
            vnode_unref(vnode);
            vnode_unref(parent_vnode);
            return res;
        }

        vnode_unref(vnode);
        vnode_unref(parent_vnode);
        return 0;
    } else {
        fprintf(stderr, "File system does not implement unlink()\n");
        // File node does not support unlinking
        vnode_unref(vnode);
        vnode_unref(parent_vnode);
        return -EINVAL;
    }
}

int vfs_chmod(const char *path, mode_t mode) {
    assert(path);
    vnode_t *vnode;
    int res;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) < 0) {
        return res;
    }

    vnode_ref(vnode);
    assert(vnode && vnode->op);

    if (!vnode->op->chmod) {
        return -EINVAL;
    }

    res = vnode->op->chmod(vnode, mode);

    vnode_unref(vnode);
    return res;
}

int vfs_chown(const char *path, uid_t uid, gid_t gid) {
    assert(path);
    vnode_t *vnode;
    int res;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) < 0) {
        return res;
    }

    vnode_ref(vnode);
    assert(vnode && vnode->op);

    if (!vnode->op->chown) {
        return -EINVAL;
    }

    res = vnode->op->chown(vnode, uid, gid);

    vnode_unref(vnode);
    return res;
}

struct dirent *vfs_readdir(struct ofile *fd) {
    assert(fd);
    if (!(fd->flags & O_DIRECTORY)) {
        return NULL;
    }
    vnode_t *vn = fd->vnode;
    assert(vn && vn->op);

    if (!vn->op->readdir) {
        return NULL;
    }

    if (vn->op->readdir(fd) == 0) {
        return (struct dirent *) fd->dirent_buf;
    }

    return NULL;
}

int vfs_mkdir(const char *path, mode_t mode) {
    vnode_t *parent_vnode = NULL;
    vnode_t *vnode = NULL;
    int res;

    // Check if a directory with such name already exists
    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) == 0) {
        vnode_ref(vnode);
        vnode_unref(vnode);
        return -EEXIST;
    }

    // Just copypasted this from creat()
    if (*path == '/') {
        // Get parent vnode
        char parent_path[1024];
        vfs_path_parent(parent_path, path);

        if ((res = vfs_find(NULL, parent_path, &parent_vnode)) != 0) {
            printf("Parent does not exist: %s\n", parent_path);
            // Parent doesn't exist, too - error
            return res;
        }
    } else {
        char parent_path[1024];
        vfs_path_parent(parent_path, path);

        if (!*parent_path) {
            parent_path[0] = '.';
            parent_path[1] = 0;
        }

        // Find parent
        if ((res = vfs_find(vfs_ctx.cwd_vnode, parent_path, &parent_vnode)) != 0) {
            printf("Parent does not exist: %s\n", parent_path);
            return res;
        }
    }

    vnode_ref(parent_vnode);

    if (parent_vnode->type != VN_DIR) {
        // Parent is not a directory
        vnode_unref(parent_vnode);
        return -ENOTDIR;
    }

    printf("Path: %s\n", path);
    path = vfs_path_basename(path);

    if (!path) {
        vnode_unref(parent_vnode);
        return -EINVAL;
    }

    if (!parent_vnode->op || !parent_vnode->op->mkdir) {
        vnode_unref(parent_vnode);
        return -EINVAL;
    }

    res = parent_vnode->op->mkdir(parent_vnode, path, mode);
    vnode_unref(parent_vnode);
    return res;
}

int vfs_access(const char *path, int mode) {
    assert(path);
    vnode_t *vnode;
    int res;

    mode_t vn_mode;
    uid_t vn_uid;
    gid_t vn_gid;

    if ((res = vfs_find(vfs_ctx.cwd_vnode, path, &vnode)) != 0) {
        return res;
    }

    vnode_ref(vnode);
    if (mode == F_OK) {
        vnode_unref(vnode);
        return 0;
    }

    assert(vnode->op);

    if (!vnode->op->access) {
        vnode_unref(vnode);
        // Filesystem has no permissions?
        return -EINVAL;
    }

    if ((res = vnode->op->access(vnode, &vn_uid, &vn_gid, &vn_mode)) < 0) {
        vnode_unref(vnode);
        return res;
    }

    vnode_unref(vnode);

    return vfs_access_internal(mode, vn_mode, vn_uid, vn_gid);
}
