#!/bin/bash
repo='https://github.com/lua/lua.git'
. test/thirdparty/common
git reset --hard be908a7d4d8130264ad67c5789169769f824c5d1

sed -i 's/CC= gcc/CC?= gcc/' makefile
CC=$rvcc $make all
./all
