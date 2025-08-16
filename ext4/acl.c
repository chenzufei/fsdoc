// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/acl.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 */

/*
ACL（Access Control List，访问控制列表）是一种用于控制对文件和目录访问权限的机制。
它提供了比传统Unix权限模型（用户、组、其他）更细粒度的访问控制。
ACL允许你为特定的用户或组设置不同的访问权限，而不仅仅是将文件或目录的权限分为所有者、组和其他用户。

查看acl               getfacl filename
                      getfacl /home/user1/project


为用户设置权限
                      setfacl -m u:username:permissions filename
                      setfacl -m u:user2:rw- /home/user1/project

为组设置权限
                      setfacl -m g:groupname:permissions filename
                      setfacl -m g:developers:rw- /home/user1/project


删除特定用户或组的权限
                      setfacl -x u:username filename

删除所有ACL
                      setfacl -b filename

设置默认ACL
                      setfacl -m d:u:username:permissions filename

例如，为目录/home/user1/project设置默认ACL，使新创建的文件和目录继承这些权限：
setfacl -m d:u:user2:rw- /home/user1/project

检查ACL
ls -l /home/user1/project
drwxrwx---+ 2 user1 developers 4096 Aug 16 12:00 /home/user1/project
+符号表示该文件或目录有额外的ACL


大多数现代Linux文件系统都支持ACL，包括ext3、ext4、XFS、Btrfs等。
在ext4中，ACL支持是默认启用的，但可以通过noacl挂载选项来禁用。
启用或禁用ACL支持
mount -o acl /dev/sda1 /mnt
mount -o noacl /dev/sda1 /mnt


ext2是较早的文件系统，主要提供基本的文件存储功能，没有内置对ACL的直接支持。
ext3文件系统在ext2的基础上增加了对ACL的直接支持。
ext4文件系统继承并增强了ext3的ACL支持。

*/
#include <linux/quotaops.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include "xattr.h"
#include "acl.h"

/*
 * Convert from filesystem to in-memory representation.
 */
/*
将存储在磁盘上的ACL数据转换为内存中的表示形式
value 是磁盘上的ACL数据，size 是数据的大小
*/
static struct posix_acl *
ext4_acl_from_disk(const void *value, size_t size)
{
    const char *end = (char *)value + size;
    int n, count;
    struct posix_acl *acl; // acl 是指向内存中ACL结构的指针

    // 检查输入数据是否为空，大小是否足够，以及版本号是否匹配
    if (!value)
        return NULL;
    if (size < sizeof(ext4_acl_header))
        return ERR_PTR(-EINVAL);
    if (((ext4_acl_header *)value)->a_version !=
        cpu_to_le32(EXT4_ACL_VERSION))
        return ERR_PTR(-EINVAL);

    // 跳过头部，计算ACL条目数量
    // 如果数量为负或为零，返回错误指针或NULL
    value = (char *)value + sizeof(ext4_acl_header);
    count = ext4_acl_count(size);
    if (count < 0)
        return ERR_PTR(-EINVAL);
    if (count == 0)
        return NULL;

    // 分配内存中的ACL结构
    acl = posix_acl_alloc(count, GFP_NOFS);
    if (!acl)
        return ERR_PTR(-ENOMEM);

    // 遍历磁盘上的ACL条目，将其转换为内存中的表示形式
    for (n = 0; n < count; n++)
    {
        ext4_acl_entry *entry =
            (ext4_acl_entry *)value;
        if ((char *)value + sizeof(ext4_acl_entry_short) > end)
            goto fail;

        // 根据ACL条目的类型，处理不同的字段
        acl->a_entries[n].e_tag = le16_to_cpu(entry->e_tag);
        acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);

        switch (acl->a_entries[n].e_tag)
        {
        case ACL_USER_OBJ:
        case ACL_GROUP_OBJ:
        case ACL_MASK:
        case ACL_OTHER:
            value = (char *)value +
                    sizeof(ext4_acl_entry_short);
            break;

        case ACL_USER:
            value = (char *)value + sizeof(ext4_acl_entry);
            if ((char *)value > end)
                goto fail;
            acl->a_entries[n].e_uid =
                make_kuid(&init_user_ns,
                          le32_to_cpu(entry->e_id)); // 转换用户ID
            break;
        case ACL_GROUP:
            value = (char *)value + sizeof(ext4_acl_entry);
            if ((char *)value > end)
                goto fail;
            acl->a_entries[n].e_gid =
                make_kgid(&init_user_ns,
                          le32_to_cpu(entry->e_id)); // 转换用户组ID
            break;

        default:
            goto fail;
        }
    }
    if (value != end)
        goto fail;

    // 返回内存中的ACL结构
    return acl;

fail:
    // 发生错误，释放已分配的ACL结构并返回错误指针
    posix_acl_release(acl);
    return ERR_PTR(-EINVAL);
}

/*
 * Convert from in-memory to filesystem representation.
 */
/*
将内存中的ACL数据转换为磁盘上的表示形式
acl 是内存中的ACL结构，size 是输出的磁盘数据大小
*/
static void *
ext4_acl_to_disk(const struct posix_acl *acl, size_t *size)
{
    ext4_acl_header *ext_acl; // ext_acl 是磁盘上的ACL头部结构
    char *e;
    size_t n;

    // 计算磁盘数据的大小并分配内存
    *size = ext4_acl_size(acl->a_count);
    ext_acl = kmalloc(sizeof(ext4_acl_header) + acl->a_count *
                                                    sizeof(ext4_acl_entry),
                      GFP_NOFS);
    if (!ext_acl)
        return ERR_PTR(-ENOMEM);

    // 设置ACL版本号，遍历内存中的ACL条目，将其转换为磁盘上的表示形式
    ext_acl->a_version = cpu_to_le32(EXT4_ACL_VERSION);
    e = (char *)ext_acl + sizeof(ext4_acl_header);
    for (n = 0; n < acl->a_count; n++)
    {
        const struct posix_acl_entry *acl_e = &acl->a_entries[n];
        ext4_acl_entry *entry = (ext4_acl_entry *)e;
        entry->e_tag = cpu_to_le16(acl_e->e_tag);
        entry->e_perm = cpu_to_le16(acl_e->e_perm);
        switch (acl_e->e_tag)
        {
        case ACL_USER:
            entry->e_id = cpu_to_le32(
                from_kuid(&init_user_ns, acl_e->e_uid));
            e += sizeof(ext4_acl_entry);
            break;
        case ACL_GROUP:
            entry->e_id = cpu_to_le32(
                from_kgid(&init_user_ns, acl_e->e_gid));
            e += sizeof(ext4_acl_entry);
            break;

        case ACL_USER_OBJ:
        case ACL_GROUP_OBJ:
        case ACL_MASK:
        case ACL_OTHER:
            e += sizeof(ext4_acl_entry_short);
            break;

        default:
            goto fail;
        }
    }
    return (char *)ext_acl;

fail:
    kfree(ext_acl);
    return ERR_PTR(-EINVAL);
}

/*
 * Inode operation get_posix_acl().
 *
 * inode->i_mutex: don't care
 */
/*
获取一个inode的ACL
inode 是目标inode，type 是ACL的类型（访问ACL或默认ACL）
*/
struct posix_acl *
ext4_get_acl(struct inode *inode, int type)
{
    int name_index;        // 扩展属性的索引
    char *value = NULL;    // 从磁盘读取的ACL数据
    struct posix_acl *acl; // 内存中的ACL结构
    int retval;

    // 根据ACL类型，设置扩展属性的索引
    switch (type)
    {
    case ACL_TYPE_ACCESS:
        name_index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
        break;
    case ACL_TYPE_DEFAULT:
        name_index = EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT;
        break;
    default:
        BUG();
    }

    // 获取扩展属性的大小，然后分配内存并读取扩展属性
    retval = ext4_xattr_get(inode, name_index, "", NULL, 0);
    if (retval > 0)
    {
        value = kmalloc(retval, GFP_NOFS);
        if (!value)
            return ERR_PTR(-ENOMEM);
        // 读取扩展属性
        retval = ext4_xattr_get(inode, name_index, "", value, retval);
    }
    if (retval > 0)
        // 如果成功读取扩展属性，将其转换为内存中的ACL结构
        acl = ext4_acl_from_disk(value, retval);
    else if (retval == -ENODATA || retval == -ENOSYS)
        // 没有数据或系统不支持，返回NULL
        acl = NULL;
    else
        acl = ERR_PTR(retval);
    kfree(value);

    return acl;
}

/*
 * Set the access or default ACL of an inode.
 *
 * inode->i_mutex: down unless called from ext4_new_inode
 */
/*
设置一个inode的ACL
handle 是日志句柄，inode 是目标inode，type 是ACL类型，acl 是要设置的ACL结构，xattr_flags 是扩展属性的标志
*/
static int
__ext4_set_acl(handle_t *handle, struct inode *inode, int type,
               struct posix_acl *acl, int xattr_flags)
{
    int name_index;
    void *value = NULL;
    size_t size = 0;
    int error;

    // 根据ACL类型，设置扩展属性的索引
    switch (type)
    {
    case ACL_TYPE_ACCESS:
        name_index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
        break;

    case ACL_TYPE_DEFAULT:
        name_index = EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT;
        if (!S_ISDIR(inode->i_mode))
            return acl ? -EACCES : 0;
        break;

    default:
        return -EINVAL;
    }
    if (acl)
    {
        // 将其转换为磁盘上的表示形式
        value = ext4_acl_to_disk(acl, &size);
        if (IS_ERR(value))
            return (int)PTR_ERR(value);
    }

    // 设置扩展属性
    error = ext4_xattr_set_handle(handle, inode, name_index, "",
                                  value, size, xattr_flags);

    kfree(value);
    if (!error)
    {
        // 更新inode的缓存ACL
        set_cached_acl(inode, type, acl);
    }

    return error;
}

/*
设置一个inode的ACL
inode 是目标inode，acl 是要设置的ACL结构，type 是ACL类型
*/
int ext4_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
    handle_t *handle;
    int error, credits, retries = 0;
    size_t acl_size = acl ? ext4_acl_size(acl->a_count) : 0;
    umode_t mode = inode->i_mode;
    int update_mode = 0;

    // 初始化inode的配额
    error = dquot_initialize(inode);
    if (error)
        return error;
retry:
    // 计算设置扩展属性所需的日志信用
    error = ext4_xattr_set_credits(inode, acl_size, false /* is_create */,
                                   &credits);
    if (error)
        return error;

    // 开始日志事务
    handle = ext4_journal_start(inode, EXT4_HT_XATTR, credits);
    if (IS_ERR(handle))
        return PTR_ERR(handle);

    // 设置的是访问ACL且提供了ACL结构，更新inode的模式
    if ((type == ACL_TYPE_ACCESS) && acl)
    {
        error = posix_acl_update_mode(inode, &mode, &acl);
        if (error)
            goto out_stop;
        update_mode = 1;
    }

    // 设置ACL
    // 如果成功且需要更新inode模式，更新inode的模式和时间戳，并标记inode为脏
    error = __ext4_set_acl(handle, inode, type, acl, 0 /* xattr_flags */);
    if (!error && update_mode)
    {
        inode->i_mode = mode;
        inode->i_ctime = current_time(inode);
        ext4_mark_inode_dirty(handle, inode);
    }
out_stop:
    // 停止日志事务
    ext4_journal_stop(handle);
    if (error == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
        goto retry;
    return error;
}

/*
 * Initialize the ACLs of a new inode. Called from ext4_new_inode.
 *
 * dir->i_mutex: down
 * inode->i_mutex: up (access to inode is still exclusive)
 */

/*
初始化一个新inode的ACL
handle 是日志句柄，inode 是新inode，dir 是父目录inode
*/
int ext4_init_acl(handle_t *handle, struct inode *inode, struct inode *dir)
{
    struct posix_acl *default_acl, *acl; // default_acl 和 acl 是默认ACL和访问ACL的内存结构
    int error;

    // 创建ACL
    error = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
    if (error)
        return error;

    // 如果有默认ACL，设置默认ACL
    if (default_acl)
    {
        error = __ext4_set_acl(handle, inode, ACL_TYPE_DEFAULT,
                               default_acl, XATTR_CREATE);
        posix_acl_release(default_acl);
    }

    // 如果有访问ACL，设置访问ACL
    if (acl)
    {
        if (!error)
            error = __ext4_set_acl(handle, inode, ACL_TYPE_ACCESS,
                                   acl, XATTR_CREATE);
        posix_acl_release(acl);
    }
    return error;
}
