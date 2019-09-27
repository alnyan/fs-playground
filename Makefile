O=$(abspath build)
# libvfs.a itself - virtual file system implementation
LIBVFS=$(O)/libvfs.a
LIBVFS_OBJS=$(O)/blk.o \
			$(O)/fs_class.o \
			$(O)/hash.o \
			$(O)/node.o \
			$(O)/vfs.o
# libtestblk.a - File-mapped testing block device for
# 				 emulating a real hard drive/whatever
LIBTESTBLK=$(O)/libtestblk.a
LIBTESTBLK_OBJS=$(O)/testblk.o
# libext2.a - ext2 filesystem implementation
LIBEXT2=$(O)/libext2.a
LIBEXT2_OBJS=$(O)/ext2.o

# An applcation for testing all of these
# libraries
EXT2SH=$(O)/ext2sh
EXT2SH_OBJS=src/ext2sh.c \
			$(LIBEXT2) \
			$(LIBTESTBLK) \
			$(LIBVFS)

CFLAGS=-Iinclude

all: mkdirs $(EXT2SH)

mkdirs:
	mkdir -p $(O)
	mkdir -p stage

clean:
	rm -rf $(O)

image: mkdirs
	rm -f $(O)/ext2.img
	# This will produce 128MiB image
	dd if=/dev/zero of=$(O)/ext2.img bs=4K count=32768
	/sbin/mke2fs \
		-t ext2 \
		-d stage \
		$(O)/ext2.img

$(EXT2SH): $(EXT2SH_OBJS)
	$(CC) $(CFLAGS) -o $@ $(EXT2SH_OBJS)

$(LIBTESTBLK): $(LIBTESTBLK_OBJS)
	ar rcs $@ $(LIBTESTBLK_OBJS)

$(LIBVFS): $(LIBVFS_OBJS)
	ar rcs $@ $(LIBVFS_OBJS)

$(LIBEXT2): $(LIBEXT2_OBJS)
	ar rcs $@ $(LIBEXT2_OBJS)

$(O)/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
