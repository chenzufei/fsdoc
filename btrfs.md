# 克隆btrfs（ubunut linux）

git config --global http.postBuffer 524288000                   //之前克隆过程中会出错（fatal: 过早的文件结束符(EOF)），使用此命令后解决
git clone --depth 1 https://github.com/kdave/btrfs-devel        //之所以添加depth 1，是为了减少克隆下载时间

遇到的问题:基于windows 10克隆，会出如下错误，因此放弃了在windows上直接克隆。网上给的原因一个是用了windows预留字，一个是大小写非敏感。
deleted: drivers/gpu/drm/nouveau/nvkm/subdev/i2c/aux.c
modified: include/uapi/linux/netfilter/xt_CONNMARK.h

ubunut linux是基于virtualBox的虚拟机。


# 修改代码并提交patch

git add xxx
git commit -s
git format-patch -1           //生产patch

<标题>             //修改的大类：patch的主要描述
<空行>
<详细描述>

./scripts/get_maintainer.pl fs/btrfs/extent-tree.c
Chris Mason <clm@fb.com> (maintainer:BTRFS FILE SYSTEM)
Josef Bacik <josef@toxicpanda.com> (maintainer:BTRFS FILE SYSTEM)
David Sterba <dsterba@suse.com> (maintainer:BTRFS FILE SYSTEM)
linux-btrfs@vger.kernel.org (open list:BTRFS FILE SYSTEM)
linux-kernel@vger.kernel.org (open list)

vi ~/.gitconfig
[sendemail]
        from = chenzufei@gmail.com
        smtpencryption = tls
        smtpserver = smtp.gmail.com
        smtpuser = chenzufei@gmail.com
        smtppass = xxxxx               //gmail启用了应用专用密码
        smtpserverport = 587
        suppresscc = self   
        chainreplyto = false

git send-email --to chenzufei@163.com 0001-btrfs-test.patch --smtp-debug=1    //成功发送了邮件
