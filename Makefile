SRC=test.c \
	hash.c \
	vfs.c \
	ext2.c \
	pseudofs.c \
	fs_class.c \
	node.c

all:
	gcc -ggdb -Iinclude -o test $(SRC)
