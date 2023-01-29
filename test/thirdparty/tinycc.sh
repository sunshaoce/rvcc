#!/bin/bash
repo='https://github.com/TinyCC/tinycc.git'
. test/thirdparty/common
git reset --hard aea2b53123b987a14d99126be22947ebda455082

./configure --cc=$rvcc
$make clean
$make
$make CC=cc test
