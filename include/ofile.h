#pragma once
#include "node.h"
#include <sys/types.h>

struct ofile {
    int mode;
    vnode_t *vnode;
    size_t pos;
};
