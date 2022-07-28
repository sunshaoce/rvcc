#!/bin/bash

# 声明一个函数
assert() {
  # 程序运行的 期待值 为参数1
  expected="$1"
  # 输入值 为参数2
  input="$2"

  # 运行程序，传入期待值，将生成结果写入tmp.s汇编文件。
  # 如果运行不成功，则会执行exit退出。成功时会短路exit操作
  ./rvcc "$input" > tmp.s || exit
  # 编译rvcc产生的汇编文件
  clang -o tmp tmp.s
  # $RISCV/bin/riscv64-unknown-linux-gnu-gcc -static -o tmp tmp.s

  # 运行生成出来目标文件
  ./tmp
  # $RISCV/bin/qemu-riscv64 -L $RISCV/sysroot ./tmp
  # $RISCV/bin/spike --isa=rv64gc $RISCV/riscv64-unknown-linux-gnu/bin/pk ./tmp

  # 获取程序返回值，存入 实际值
  actual="$?"

  # 判断实际值，是否为预期值
  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

# assert 期待值 输入值
# [1] 返回指定数值
assert 0 '{ return 0; }'
assert 42 '{ return 42; }'

# [2] 支持+ -运算符
assert 34 '{ return 12-34+56; }'

# [3] 支持空格
assert 41 '{ return  12 + 34 - 5 ; }'

# [5] 支持* / ()运算符
assert 47 '{ return 5+6*7; }'
assert 15 '{ return 5*(9-6); }'
assert 17 '{ return 1-8/(2*2)+3*6; }'

# [6] 支持一元运算的+ -
assert 10 '{ return -10+20; }'
assert 10 '{ return - -10; }'
assert 10 '{ return - - +10; }'
assert 48 '{ return ------12*+++++----++++++++++4; }'

# [7] 支持条件运算符
assert 0 '{ return 0==1; }'
assert 1 '{ return 42==42; }'
assert 1 '{ return 0!=1; }'
assert 0 '{ return 42!=42; }'
assert 1 '{ return 0<1; }'
assert 0 '{ return 1<1; }'
assert 0 '{ return 2<1; }'
assert 1 '{ return 0<=1; }'
assert 1 '{ return 1<=1; }'
assert 0 '{ return 2<=1; }'
assert 1 '{ return 1>0; }'
assert 0 '{ return 1>1; }'
assert 0 '{ return 1>2; }'
assert 1 '{ return 1>=0; }'
assert 1 '{ return 1>=1; }'
assert 0 '{ return 1>=2; }'
assert 1 '{ return 5==2+3; }'
assert 0 '{ return 6==4+3; }'
assert 1 '{ return 0*9+5*2==4+4*(6/3)-2; }'

# [9] 支持;分割语句
assert 3 '{ 1; 2;return 3; }'
assert 12 '{ 12+23;12+99/3;return 78-66; }'

# [10] 支持单字母变量
assert 3 '{ a=3;return a; }'
assert 8 '{ a=3; z=5;return a+z; }'
assert 6 '{ a=b=3;return a+b; }'
assert 5 '{ a=3;b=4;a=1;return a+b; }'

# [11] 支持多字母变量
assert 3 '{ foo=3;return foo; }'
assert 74 '{ foo2=70; bar4=4;return foo2+bar4; }'

# [12] 支持return
assert 1 '{ return 1; 2; 3; }'
assert 2 '{ 1; return 2; 3; }'
assert 3 '{ 1; 2; return 3; }'

# [13] 支持{...}
assert 3 '{ {1; {2;} return 3;} }'

# 如果运行正常未提前退出，程序将显示OK
echo OK
