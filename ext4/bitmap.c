// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/bitmap.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <linux/buffer_head.h>
#include "ext4.h"

/*
文件系统中某个位图（bitmap）中未使用的（即“空闲的”）位的数量
通过计算位图中总位数减去已使用的位数，得到空闲位的数量。

char *bitmap：指向位图的指针。位图是一个字节数组，每个位表示一个资源（如磁盘块或inode）的使用状态。
位为0表示空闲，位为1表示已使用。
unsigned int numchars：位图的长度（以字节为单位）。
*/
unsigned int ext4_count_free(char *bitmap, unsigned int numchars)
{
    // memweight的作用是统计位图中所有字节中值为1的位的数量
    return numchars * BITS_PER_BYTE - memweight(bitmap, numchars);
}

/*
验证 Ext4 文件系统中某个块组（block group）的 inode 位图（inode bitmap）的校验和（checksum）
返回值为1表示校验和验证通过，返回值为0表示校验和验证失败

Ext4 文件系统默认情况下不开启块组描述符的校验和功能
如果设置了 GDT_CSUM 特性标志，块组描述符的校验和会启用
mkfs.ext4 -O metadata_csum /dev/sdX

yum install e2fsprogs
dumpe2fs /dev/sda1 , 在 dumpe2fs 的输出中，找到 “Filesystem features” 部分
metadata_csum：表示启用了元数据校验和功能。
gdt_csum：表示启用了块组描述符校验和功能。
*/
int ext4_inode_bitmap_csum_verify(struct super_block *sb, ext4_group_t group,
                                  struct ext4_group_desc *gdp,
                                  struct buffer_head *bh, int sz)
{
    __u32 hi;
    __u32 provided, calculated;
    struct ext4_sb_info *sbi = EXT4_SB(sb);

    // 是否启用元数据校验和
    if (!ext4_has_metadata_csum(sb))
        return 1;

    provided = le16_to_cpu(gdp->bg_inode_bitmap_csum_lo);
    calculated = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)bh->b_data, sz);
    // 如果块组描述符的大小足够大，包含高16位校验和
    if (sbi->s_desc_size >= EXT4_BG_INODE_BITMAP_CSUM_HI_END)
    {
        hi = le16_to_cpu(gdp->bg_inode_bitmap_csum_hi);
        provided |= (hi << 16);
    }
    else
        calculated &= 0xFFFF; // 如果块组描述符的大小不够大，只包含低16位校验和

    return provided == calculated;
}

/*
计算并设置 Ext4 文件系统中某个块组（block group）的 inode 位图（inode bitmap）的校验和。
*/
void ext4_inode_bitmap_csum_set(struct super_block *sb, ext4_group_t group,
                                struct ext4_group_desc *gdp,
                                struct buffer_head *bh, int sz)
{
    __u32 csum;
    struct ext4_sb_info *sbi = EXT4_SB(sb);

    if (!ext4_has_metadata_csum(sb))
        return;

    csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)bh->b_data, sz);
    gdp->bg_inode_bitmap_csum_lo = cpu_to_le16(csum & 0xFFFF);
    if (sbi->s_desc_size >= EXT4_BG_INODE_BITMAP_CSUM_HI_END)
        gdp->bg_inode_bitmap_csum_hi = cpu_to_le16(csum >> 16);
}

/*
验证 Ext4 文件系统中某个块组（block group）的块位图（block bitmap）的校验和。
*/
int ext4_block_bitmap_csum_verify(struct super_block *sb, ext4_group_t group,
                                  struct ext4_group_desc *gdp,
                                  struct buffer_head *bh)
{
    __u32 hi;
    __u32 provided, calculated;
    struct ext4_sb_info *sbi = EXT4_SB(sb);
    int sz = EXT4_CLUSTERS_PER_GROUP(sb) / 8;

    if (!ext4_has_metadata_csum(sb))
        return 1;

    provided = le16_to_cpu(gdp->bg_block_bitmap_csum_lo);
    calculated = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)bh->b_data, sz);
    if (sbi->s_desc_size >= EXT4_BG_BLOCK_BITMAP_CSUM_HI_END)
    {
        hi = le16_to_cpu(gdp->bg_block_bitmap_csum_hi);
        provided |= (hi << 16);
    }
    else
        calculated &= 0xFFFF;

    if (provided == calculated)
        return 1;

    return 0;
}

/*
计算并设置 Ext4 文件系统中某个块组（block group）的块位图（block bitmap）的校验和
*/
void ext4_block_bitmap_csum_set(struct super_block *sb, ext4_group_t group,
                                struct ext4_group_desc *gdp,
                                struct buffer_head *bh)
{
    int sz = EXT4_CLUSTERS_PER_GROUP(sb) / 8;
    __u32 csum;
    struct ext4_sb_info *sbi = EXT4_SB(sb);

    if (!ext4_has_metadata_csum(sb))
        return;

    csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)bh->b_data, sz);
    gdp->bg_block_bitmap_csum_lo = cpu_to_le16(csum & 0xFFFF);
    if (sbi->s_desc_size >= EXT4_BG_BLOCK_BITMAP_CSUM_HI_END)
        gdp->bg_block_bitmap_csum_hi = cpu_to_le16(csum >> 16);
}
