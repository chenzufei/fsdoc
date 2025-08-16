// SPDX-License-Identifier: GPL-2.0
/*
 * Interface between ext4 and JBD
 */
/*
ext4 文件系统与日志模块 JBD2之间的接口实现。
它主要负责管理文件系统的日志操作，确保文件系统在发生错误或崩溃时能够恢复到一致的状态。

对外接口：
ext4_journal_start
ext4_journal_stop



文件系统挂载时，ext4 会初始化日志模块
ext4_load_journal：加载日志
ext4_get_dev_journal：获取日志设备
jbd2_journal_init_dev：初始化日志设备

启动日志事务
ext4_journal_start

标记元数据为脏
ext4_mark_inode_dirty -》jbd2_journal_dirty_metadata   //将元数据标记为脏并添加到日志事务中

提交日志事务
ext4_journal_stop -》jbd2_journal_stop


日志事务提交后，JBD2 会负责将日志数据回写到磁盘
jbd2_log_start_commit：开始提交日志事务
jbd2_log_wait_commit：等待日志事务完成
jbd2_complete_transaction：完成日志事务的回写

日志恢复
jbd2_journal_recover：恢复日志

关闭日志
ext4_journal_abort：中止当前的日志事务
jbd2_journal_destroy：销毁日志模块
*/

#include "ext4_jbd2.h"

#include <trace/events/ext4.h>

/* Just increment the non-pointer handle value */
static handle_t *ext4_get_nojournal(void)
{
    handle_t *handle = current->journal_info;
    unsigned long ref_cnt = (unsigned long)handle;

    BUG_ON(ref_cnt >= EXT4_NOJOURNAL_MAX_REF_COUNT);

    ref_cnt++;
    handle = (handle_t *)ref_cnt;

    current->journal_info = handle;
    return handle;
}

/* Decrement the non-pointer handle value */
static void ext4_put_nojournal(handle_t *handle)
{
    unsigned long ref_cnt = (unsigned long)handle;

    BUG_ON(ref_cnt == 0);

    ref_cnt--;
    handle = (handle_t *)ref_cnt;

    current->journal_info = handle;
}

/*
 * Wrappers for jbd2_journal_start/end.
 */
static int ext4_journal_check_start(struct super_block *sb)
{
    journal_t *journal;

    might_sleep();

    // 检查文件系统是否被强制关闭，如果是则返回 -EIO
    if (unlikely(ext4_forced_shutdown(EXT4_SB(sb))))
        return -EIO;

    // 检查文件系统是否为只读，如果是则返回 -EROFS
    if (sb_rdonly(sb))
        return -EROFS;

    // 检查文件系统是否被冻结，如果是则触发警告
    WARN_ON(sb->s_writers.frozen == SB_FREEZE_COMPLETE);
    journal = EXT4_SB(sb)->s_journal;
    /*
     * Special case here: if the journal has aborted behind our
     * backs (eg. EIO in the commit thread), then we still need to
     * take the FS itself readonly cleanly.
     */
    // 检查日志是否已中止，如果是则调用 ext4_abort 并返回 -EROFS
    if (journal && is_journal_aborted(journal))
    {
        ext4_abort(sb, "Detected aborted journal");
        return -EROFS;
    }
    return 0;
}

/*
启动一个日志事务
函数名前的双下划线（__）通常用于表示该函数是内部使用的
*/
handle_t *__ext4_journal_start_sb(struct super_block *sb, unsigned int line,
                                  int type, int blocks, int rsv_blocks)
{
    journal_t *journal;
    int err;

    trace_ext4_journal_start(sb, blocks, rsv_blocks, _RET_IP_);
    // 检查是否可以启动日志操作
    err = ext4_journal_check_start(sb);
    if (err < 0)
        return ERR_PTR(err);

    journal = EXT4_SB(sb)->s_journal;
    if (!journal)
        return ext4_get_nojournal();
    return jbd2__journal_start(journal, blocks, rsv_blocks, GFP_NOFS,
                               type, line);
}

/*
停止一个日志事务
*/
int __ext4_journal_stop(const char *where, unsigned int line, handle_t *handle)
{
    struct super_block *sb;
    int err;
    int rc;

    // 检查句柄是否有效
    if (!ext4_handle_valid(handle))
    {
        ext4_put_nojournal(handle);
        return 0;
    }

    err = handle->h_err;
    if (!handle->h_transaction)
    {
        rc = jbd2_journal_stop(handle);
        return err ? err : rc;
    }

    sb = handle->h_transaction->t_journal->j_private;
    rc = jbd2_journal_stop(handle);

    if (!err)
        err = rc;
    if (err)
        __ext4_std_error(sb, where, line, err);
    return err;
}

/*
启动保留日志事务
__ext4_journal_start_sb和__ext4_journal_start_reserved的区别是什么？
*/
handle_t *__ext4_journal_start_reserved(handle_t *handle, unsigned int line,
                                        int type)
{
    struct super_block *sb;
    int err;

    if (!ext4_handle_valid(handle))
        return ext4_get_nojournal();

    sb = handle->h_journal->j_private;
    trace_ext4_journal_start_reserved(sb, handle->h_buffer_credits,
                                      _RET_IP_);
    err = ext4_journal_check_start(sb);
    if (err < 0)
    {
        jbd2_journal_free_reserved(handle);
        return ERR_PTR(err);
    }

    err = jbd2_journal_start_reserved(handle, type, line);
    if (err < 0)
        return ERR_PTR(err);
    return handle;
}

/*
中止日志事务
*/
static void ext4_journal_abort_handle(const char *caller, unsigned int line,
                                      const char *err_fn,
                                      struct buffer_head *bh,
                                      handle_t *handle, int err)
{
    char nbuf[16];
    const char *errstr = ext4_decode_error(NULL, err, nbuf);

    BUG_ON(!ext4_handle_valid(handle));

    if (bh)
        BUFFER_TRACE(bh, "abort");

    if (!handle->h_err)
        handle->h_err = err;

    if (is_handle_aborted(handle))
        return;

    printk(KERN_ERR "EXT4-fs: %s:%d: aborting transaction: %s in %s\n",
           caller, line, errstr, err_fn);

    jbd2_journal_abort_handle(handle);
}

int __ext4_journal_get_write_access(const char *where, unsigned int line,
                                    handle_t *handle, struct buffer_head *bh)
{
    int err = 0;

    might_sleep();

    if (ext4_handle_valid(handle))
    {
        err = jbd2_journal_get_write_access(handle, bh);
        if (err)
            ext4_journal_abort_handle(where, line, __func__, bh,
                                      handle, err);
    }
    return err;
}

/*
 * The ext4 forget function must perform a revoke if we are freeing data
 * which has been journaled.  Metadata (eg. indirect blocks) must be
 * revoked in all cases.
 *
 * "bh" may be NULL: a metadata block may have been freed from memory
 * but there may still be a record of it in the journal, and that record
 * still needs to be revoked.
 *
 * If the handle isn't valid we're not journaling, but we still need to
 * call into ext4_journal_revoke() to put the buffer head.
 */
int __ext4_forget(const char *where, unsigned int line, handle_t *handle,
                  int is_metadata, struct inode *inode,
                  struct buffer_head *bh, ext4_fsblk_t blocknr)
{
    int err;

    might_sleep();

    trace_ext4_forget(inode, is_metadata, blocknr);
    BUFFER_TRACE(bh, "enter");

    jbd_debug(4, "forgetting bh %p: is_metadata = %d, mode %o, "
                 "data mode %x\n",
              bh, is_metadata, inode->i_mode,
              test_opt(inode->i_sb, DATA_FLAGS));

    /* In the no journal case, we can just do a bforget and return */
    if (!ext4_handle_valid(handle))
    {
        bforget(bh);
        return 0;
    }

    /* Never use the revoke function if we are doing full data
     * journaling: there is no need to, and a V1 superblock won't
     * support it.  Otherwise, only skip the revoke on un-journaled
     * data blocks. */

    if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA ||
        (!is_metadata && !ext4_should_journal_data(inode)))
    {
        if (bh)
        {
            BUFFER_TRACE(bh, "call jbd2_journal_forget");
            err = jbd2_journal_forget(handle, bh);
            if (err)
                ext4_journal_abort_handle(where, line, __func__,
                                          bh, handle, err);
            return err;
        }
        return 0;
    }

    /*
     * data!=journal && (is_metadata || should_journal_data(inode))
     */
    BUFFER_TRACE(bh, "call jbd2_journal_revoke");
    err = jbd2_journal_revoke(handle, blocknr, bh);
    if (err)
    {
        ext4_journal_abort_handle(where, line, __func__,
                                  bh, handle, err);
        __ext4_abort(inode->i_sb, where, line,
                     "error %d when attempting revoke", err);
    }
    BUFFER_TRACE(bh, "exit");
    return err;
}

int __ext4_journal_get_create_access(const char *where, unsigned int line,
                                     handle_t *handle, struct buffer_head *bh)
{
    int err = 0;

    if (ext4_handle_valid(handle))
    {
        err = jbd2_journal_get_create_access(handle, bh);
        if (err)
            ext4_journal_abort_handle(where, line, __func__,
                                      bh, handle, err);
    }
    return err;
}

int __ext4_handle_dirty_metadata(const char *where, unsigned int line,
                                 handle_t *handle, struct inode *inode,
                                 struct buffer_head *bh)
{
    int err = 0;

    might_sleep();

    set_buffer_meta(bh);
    set_buffer_prio(bh);
    if (ext4_handle_valid(handle))
    {
        err = jbd2_journal_dirty_metadata(handle, bh);
        /* Errors can only happen due to aborted journal or a nasty bug */
        if (!is_handle_aborted(handle) && WARN_ON_ONCE(err))
        {
            ext4_journal_abort_handle(where, line, __func__, bh,
                                      handle, err);
            if (inode == NULL)
            {
                pr_err("EXT4: jbd2_journal_dirty_metadata "
                       "failed: handle type %u started at "
                       "line %u, credits %u/%u, errcode %d",
                       handle->h_type,
                       handle->h_line_no,
                       handle->h_requested_credits,
                       handle->h_buffer_credits, err);
                return err;
            }
            ext4_error_inode(inode, where, line,
                             bh->b_blocknr,
                             "journal_dirty_metadata failed: "
                             "handle type %u started at line %u, "
                             "credits %u/%u, errcode %d",
                             handle->h_type,
                             handle->h_line_no,
                             handle->h_requested_credits,
                             handle->h_buffer_credits, err);
        }
    }
    else
    {
        if (inode)
            mark_buffer_dirty_inode(bh, inode);
        else
            mark_buffer_dirty(bh);
        if (inode && inode_needs_sync(inode))
        {
            sync_dirty_buffer(bh);
            if (buffer_req(bh) && !buffer_uptodate(bh))
            {
                struct ext4_super_block *es;

                es = EXT4_SB(inode->i_sb)->s_es;
                es->s_last_error_block =
                    cpu_to_le64(bh->b_blocknr);
                ext4_error_inode(inode, where, line,
                                 bh->b_blocknr,
                                 "IO error syncing itable block");
                err = -EIO;
            }
        }
    }
    return err;
}

int __ext4_handle_dirty_super(const char *where, unsigned int line,
                              handle_t *handle, struct super_block *sb)
{
    struct buffer_head *bh = EXT4_SB(sb)->s_sbh;
    int err = 0;

    ext4_superblock_csum_set(sb);
    if (ext4_handle_valid(handle))
    {
        err = jbd2_journal_dirty_metadata(handle, bh);
        if (err)
            ext4_journal_abort_handle(where, line, __func__,
                                      bh, handle, err);
    }
    else
        mark_buffer_dirty(bh);
    return err;
}
