#!/bin/bash
repo='https://github.com/sqlite/sqlite.git'
. test/thirdparty/common
git reset --hard 86f477edaa17767b39c7bae5b67cac8580f7a8c1

CC=$rvcc CFLAGS=-D_GNU_SOURCE ./configure
sed -i 's/^wl=.*/wl=-Wl,/; s/^pic_flag=.*/pic_flag=-fPIC/' libtool
$make clean
$make
# 最后测试结果：sessionfuzz-data1.db:  339 cases, 0 crashes
# 存在已知错误，最后返回值应为 2
$make test
