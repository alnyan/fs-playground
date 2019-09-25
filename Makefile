SRC=test.c \
	hash.c \
	vfs.c \
	ext2.c \
	fs_class.c \
	testblk.c \
	blk.c \
	node.c

all:
	gcc -ggdb -Iinclude -o test $(SRC)
