# cmake最低版本号
cmake_minimum_required( VERSION 3.10.0 )

# 项目名称
project( rvcc )

# 可执行文件rvcc的依赖文件
add_executable( rvcc
  main.c
)

# 编译参数
set( CMAKE_C_FLAGS "-std=c11 -g -fno-common" )
