#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include "vfs.h"
#include "ext2.h"
#include "testblk.h"

#include <string.h>
#include <ctype.h>
#include <errno.h>

static const char *errno_str(int r) {
    switch (r) {
        case -EIO:
            return "I/O error";
        case -ENOENT:
            return "No such file or directory";
        case -EINVAL:
            return "Invalid argument";
        case -EROFS:
            return "Read-only filesystem";
        case -ENOTDIR:
            return "Not a directory";
        case -EISDIR:
            return "Is a directory";
        case -EEXIST:
            return "File exists";
        case -ENOSPC:
            return "No space left on device";
        default:
            return "Unknown error";
    }
}

static void dumpstat(char *buf, const struct stat *st) {
    char t = '-';

    switch (st->st_mode & S_IFMT) {
    case S_IFDIR:
        t = 'd';
        break;
    }

    sprintf(buf, "%c%c%c%c%c%c%c%c%c%c % 5d % 5d %u %u", t,
        (st->st_mode & S_IRUSR) ? 'r' : '-',
        (st->st_mode & S_IWUSR) ? 'w' : '-',
        (st->st_mode & S_IXUSR) ? 'x' : '-',
        (st->st_mode & S_IRGRP) ? 'r' : '-',
        (st->st_mode & S_IWGRP) ? 'w' : '-',
        (st->st_mode & S_IXGRP) ? 'x' : '-',
        (st->st_mode & S_IROTH) ? 'r' : '-',
        (st->st_mode & S_IWOTH) ? 'w' : '-',
        (st->st_mode & S_IXOTH) ? 'x' : '-',
        st->st_uid,
        st->st_gid,
        st->st_size,
        st->st_ino);
}

static int shell_stat(const char *path) {
    struct stat st;
    int res = vfs_stat(path, &st);

    if (res == 0) {
        char buf[64];
        dumpstat(buf, &st);
        printf("%s\t%s\n", buf, path);
    }

    return res;
}

static int shell_tree(const char *arg) {
    vfs_dump_tree();
    return 0;
}

static int shell_ls(const char *arg) {
    struct ofile fd;
    int res = vfs_open(&fd, arg, 0, O_DIRECTORY | O_RDONLY);
    if (res < 0) {
        return res;
    }

    struct dirent *ent;
    while ((ent = vfs_readdir(&fd))) {
        printf("dirent %s\n", ent->d_name);
    }

    vfs_close(&fd);

    return res;
}

static int shell_ls_detail(const char *arg) {
    struct ofile fd;
    struct dirent *ent;
    struct stat st;
    char fullp[512];
    int res;

    if ((res = vfs_open(&fd, arg, 0, O_DIRECTORY | O_RDONLY)) < 0) {
        return res;
    }

    while ((ent = vfs_readdir(&fd))) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        if ((res = vfs_statat(fd.vnode, ent->d_name, &st)) < 0) {
            return res;
        }

        dumpstat(fullp, &st);
        printf("%s\t%s\n", fullp, ent->d_name);
    }

    vfs_close(&fd);
    return 0;
}

static int shell_cat(const char *arg) {
    struct ofile fd;
    int res;
    char buf[512];
    size_t bread = 0;

    if ((res = vfs_open(&fd, arg, 0, O_RDONLY)) != 0) {
        return res;
    }

    while ((res = vfs_read(&fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, res, stdout);
        bread += res;
    }

    vfs_close(&fd);

    printf("\n%zuB total\n", bread);
    return 0;
}

static int shell_setcwd(const char *arg) {
    return vfs_setcwd(arg);
}

static int shell_cd(const char *arg) {
    return vfs_chdir(arg);
}

static int shell_touch(const char *arg) {
    int res;
    struct ofile fd;

    if ((res = vfs_creat(&fd, arg, 0644, O_RDWR)) != 0) {
        return res;
    }

    vfs_close(&fd);

    return 0;
}

static int shell_hello(const char *arg) {
    struct ofile fd;
    int res;
    char linebuf[512];

    printf("= ");
    if (!fgets(linebuf, sizeof(linebuf), stdin)) {
        return -1;
    }

    if ((res = vfs_open(&fd, arg, 0644, O_CREAT | O_WRONLY)) < 0) {
        return res;
    }

    if ((res = vfs_write(&fd, linebuf, strlen(linebuf))) < 0) {
        return res;
    }

    vfs_close(&fd);

    return 0;
}

static int shell_trunc(const char *arg) {
    struct ofile fd;
    int res;

    // TODO: implement O_TRUNC
    if ((res = vfs_open(&fd, arg, 0644, O_WRONLY)) < 0) {
        return res;
    }

    if ((res = vfs_truncate(&fd, 0)) < 0) {
        return res;
    }

    vfs_close(&fd);

    return 0;
}

static struct {
    const char *name;
    int (*fn) (const char *arg);
} shell_cmds[] = {
    { "stat", shell_stat },
    { "tree", shell_tree },
    { "cat", shell_cat },
    { "ll", shell_ls_detail },
    { "ls", shell_ls },
    { "setcwd", shell_setcwd },
    { "touch", shell_touch },
    { "hello", shell_hello },
    { "trunc", shell_trunc },
    { "cd", shell_cd },
};

static void shell(void) {
    char linebuf[256];
    char namebuf[64];
    const char *cmd;
    const char *arg;

    while (1) {
        fputs("> ", stdout);
        if (!fgets(linebuf, sizeof(linebuf), stdin)) {
            break;
        }

        size_t i = strlen(linebuf);
        if (i == 0) {
            continue;
        }
        --i;
        while (isspace(linebuf[i])) {
            linebuf[i] = 0;
            --i;
        }

        const char *p = strchr(linebuf, ' ');
        if (p) {
            strncpy(namebuf, linebuf, p - linebuf);
            namebuf[p - linebuf] = 0;
            cmd = namebuf;
            arg = p + 1;
        } else {
            cmd = linebuf;
            arg = "";
        }

        for (int i = 0; i < sizeof(shell_cmds) / sizeof(shell_cmds[0]); ++i) {
            if (!strcmp(shell_cmds[i].name, cmd)) {
                int res = shell_cmds[i].fn(arg);
                if (res != 0) {
                    fprintf(stderr, "%s: %s\n", linebuf, errno_str(res));
                }

                goto found;
            }
        }

        fprintf(stderr, "Command not found: %s\n", cmd);
found:
        continue;
    }
}

int main(int argc, const char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image-file>\n", argv[0]);
        return -1;
    }

    if (access(argv[1], O_RDONLY) < 0) {
        perror(argv[1]);
        return -1;
    }

    int res;
    struct ofile fd0;
    struct stat st0;
    vnode_t *file1;
    char buf[513];

    vfs_init();
    ext2_class_init();
    testblk_init(argv[1]);

    // Mount ext2 as rootfs
    if ((res = vfs_mount("/", &testblk_dev, "ext2", NULL)) != 0) {
        fprintf(stderr, "Failed to mount rootfs\n");
        return -1;
    }

    shell();

    // Cleanup
    if ((res = vfs_umount("/")) != 0) {
        fprintf(stderr, "Failed to umount /\n");
        return -1;
    }
    testblk_dev.destroy(&testblk_dev);

    return 0;
}
