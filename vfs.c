#include "vfs.h"
#include "fs.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#include <stdio.h>

static struct vfs_node root_node;

void vfs_init(void) {
    // Setup root node
    strcpy(root_node.name, "[root]");
    root_node.vnode = NULL;
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
    vn->tree_node = node;
    node->vnode = vn;
    strcpy(node->name, name);
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
    // Path cannot be null
    // TODO: maybe it can - just return the root node itself
    assert(path);

    // Assuming the path is normalized
    char path_element[256];
    const char *child_path = vfs_path_element(path_element, path);
    vnode_t *root_vnode = root_node->vnode;
    assert(root_vnode);
    int res;

    // 1. Make sure we're either looking inside a directory or a mountpoint
    if (root_vnode->type == VN_MNT) {
        // 2.1. Get the mount root vnode and do vfs_find on it
        abort(); // TODO: support nested mounting
    } else if (root_vnode->type == VN_DIR) {
        // 3.1. It's a directory, try looking up path element inside
        //      the path tree
        for (struct vfs_node *it = root_node->child; it; it = it->cdr) {
            if (!strcmp(it->name, path_element)) {
                // Found matching path element
                if (!child_path) {
                    // We're at terminal path element - which means we've
                    // found what we're looking for
                    vnode_ref(root_vnode);
                    *res_node = it;
                    return 0;
                } else {
                    // Continue searching deeper
                    if ((res = vfs_find_tree(it, child_path, res_node)) != 0) {
                        // Nothing found
                        return res;
                    }

                    // Found something
                    vnode_ref(root_vnode);
                    return 0;
                }
            }
        }

        // 3.2. Nothing found in path tree - request the fs to find
        //      the vnode for the path element given
        vnode_t *child_vnode = NULL;
        struct vfs_node *child_node = NULL;

        if ((res = root_vnode->op->find(root_vnode, path_element, &child_vnode)) != 0) {
            // fs didn't find anything - no such file or directory exists
            return res;
        }

        // 3.3. Found some vnode, attach it to the VFS tree
        child_node = vfs_node_create(path_element, child_vnode);
        child_vnode->refcount = 0;

        // Prepend it to parent's child list
        child_node->parent = root_node;
        child_node->cdr = root_node->child;
        root_node->child = child_node;

        if (!child_path) {
            // We're at terminal path element - return the node
            *res_node = child_node;
            vnode_ref(root_vnode);
            return 0;
        } else {
            if ((res = vfs_find_tree(child_node, child_path, res_node)) != 0) {
                // Nothing found in the child
                // Additionally, no one uses child now,
                // deallocate it
                vnode_unref(child_vnode);
                // If vnode_unref frees the vnode, it'll also
                // remove the associated vfs_node from parent's list
                return res;
            }

            // Found what we're looking for
            // Now have not only a child reference, but also someone uses the
            // node down the tree, so increment the refcounter
            vnode_ref(root_vnode);
            return 0;
        }
    } else {
        // Not a directory/mountpoint - cannot contain anything
        return -ENOENT;
    }
}

int vfs_find(vnode_t *root_vnode, const char *path, vnode_t **res_vnode) {
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

static void vfs_dump_node(struct vfs_node *node, int o) {
    for (int i = 0; i < o; ++i) {
        printf("  ");
    }
    printf("%s", node->name);
    if (node->vnode->type == VN_DIR || node->vnode->type == VN_MNT) {
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

int vfs_mount(vnode_t *at, void *blkdev, const char *fs_name, const char *opt) {
    struct fs_class *fs_class;

    if ((fs_class = fs_class_by_name(fs_name)) == NULL) {
        return -EINVAL;
    }

    // Create a new fs instance/mount
    struct fs *fs;

    if ((fs = fs_create(fs_class, blkdev, at)) == NULL) {
        return -EINVAL;
    }

    assert(fs_class->get_root);
    if ((fs_class->mount != NULL) && (fs_class->mount(fs, opt) != 0)) {
        return -1;
    }

    // If it's a root mount, set root vnode
    if (!at) {
        root_node.vnode = fs_class->get_root(fs);
        if (root_node.vnode) {
            root_node.vnode->tree_node = &root_node;
        }
    } else {
        // Increment refcounter for mountpoint
        vnode_ref(at);
    }

    return 0;
}

int vfs_open(struct ofile *of, vnode_t *vn, int opt) {
    assert(vn && vn->op && of);
    int res;

    if (vn->op->open) {
        if ((res = vn->op->open(vn, opt)) != 0) {
            return res;
        }
    }

    of->mode = opt;
    of->vnode = vn;
    of->pos = 0;

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

ssize_t vfs_read(struct ofile *fd, void *buf, size_t count) {
    assert(fd);
    vnode_t *vn = fd->vnode;
    assert(vn && vn->op);

    if (vn->op->read == NULL) {
        return -EINVAL;
    }

    ssize_t nr = vn->op->read(fd, buf, count);

    if (nr > 0) {
        fd->pos += nr;
    }

    return nr;
}
