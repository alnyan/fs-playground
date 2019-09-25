#pragma once
#include <stdint.h>

#define EXT2_MAGIC      ((uint16_t) 0xEF53)

#define EXT2_SBSIZ      1024

#define EXT2_GOOD       ((uint16_t) 1)
#define EXT2_BAD        ((uint16_t) 2)

#define EXT2_EACT_IGN   ((uint16_t) 1)
#define EXT2_EACT_REM   ((uint16_t) 2)
#define EXT2_EACT_PAN   ((uint16_t) 3)

struct ext2_sb {
    uint32_t inode_count;
    uint32_t block_count;
    uint32_t su_reserved;
    uint32_t free_block_count;
    uint32_t free_inode_count;
    uint32_t sb_block_number;
    uint32_t block_size_log;
    uint32_t frag_size_log;
    uint32_t block_group_size_blocks;
    uint32_t block_group_size_frags;
    uint32_t block_group_size_inodes;
    uint32_t last_mount_time;
    uint32_t last_mtime;
    uint16_t mount_count_since_fsck;
    uint16_t mount_max_before_fsck;
    uint16_t magic;
    uint16_t fs_state;
    uint16_t error_action;
    uint16_t version_minor;
    uint32_t last_fsck_time;
    uint32_t os_id;
    uint32_t version_major;
    uint16_t su_uid;
    uint16_t su_gid;
} __attribute__((packed));

struct ext2_extsb {
    struct ext2_sb sb;
    uint32_t first_non_reserved;
    uint16_t inode_struct_size;
    uint16_t backup_group_number;
    uint32_t optional_features;
    uint32_t required_features;
    uint32_t ro_required_features;
    char fsid[16];
    char volname[16];
    char last_mount_path[64];
    uint32_t compression;
    uint8_t prealloc_file_block_number;
    uint8_t prealloc_dir_block_number;
    uint16_t __un0;
    char journal_id[16];
    uint32_t journal_inode;
    uint32_t journal_dev;
    uint32_t orphan_inode_head;
} __attribute__((packed));

void ext2_class_init(void);
