#克隆btrfs（ubunut linux）

git config --global http.postBuffer 524288000                   //之前克隆过程中会出错（fatal: 过早的文件结束符(EOF)），使用此命令后解决
git clone --depth 1 https://github.com/kdave/btrfs-devel        //之所以添加depth 1，是为了减少克隆下载时间

几个遇到的问题
1、基于windows 10克隆，会出如下错误，因此放弃了在windows上直接克隆。网上给的原因一个是用了windows预留字，一个是大小写非敏感。
deleted: drivers/gpu/drm/nouveau/nvkm/subdev/i2c/aux.c
modified: include/uapi/linux/netfilter/xt_CONNMARK.h

2、ubunut linux是基于virtualBox的虚拟机。
