#!/bin/bash
repo='git@github.com:python/cpython.git'
. test/thirdparty/common
git reset --hard c75330605d4795850ec74fdc4d69aa5d92f76c00

# 在Python的 ./configure' 会错将rvcc识别为icc（其功能是rvcc的子集）
sed -i -e 1996,2011d configure.ac
autoreconf

CC=$rvcc ./configure
$make clean
$make
$make test
