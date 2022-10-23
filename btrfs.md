# 克隆btrfs（ubunut linux）

git config --global http.postBuffer 524288000                   //之前克隆过程中会出错（fatal: 过早的文件结束符(EOF)），使用此命令后解决<br>
git clone --depth 1 https://github.com/kdave/btrfs-devel        //之所以添加depth 1，是为了减少克隆下载时间<br>

遇到的问题:基于windows 10克隆，会出如下错误，因此放弃了在windows上直接克隆。网上给的原因一个是用了windows预留字，一个是大小写非敏感。<br>
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

./scripts/get_maintainer.pl fs/btrfs/extent-tree.c<br>
Chris Mason <clm@fb.com> (maintainer:BTRFS FILE SYSTEM)<br>
Josef Bacik <josef@toxicpanda.com> (maintainer:BTRFS FILE SYSTEM)<br>
David Sterba <dsterba@suse.com> (maintainer:BTRFS FILE SYSTEM)<br>
linux-btrfs@vger.kernel.org (open list:BTRFS FILE SYSTEM)<br>
linux-kernel@vger.kernel.org (open list)<br>

vi ~/.gitconfig<br>
[sendemail]<br>
        from = chenzufei@gmail.com<br>
        smtpencryption = tls<br>
        smtpserver = smtp.gmail.com<br>
        smtpuser = chenzufei@gmail.com<br>
        smtppass = xxxxx               //gmail启用了应用专用密码<br>
        smtpserverport = 587<br>
        suppresscc = self<br>   
        chainreplyto = false<br>

git send-email --to chenzufei@163.com 0001-btrfs-test.patch --smtp-debug=1    //成功发送了邮件<br>
