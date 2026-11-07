#!/bin/bash
#指定脚本用bash来解释执行（注释必须单独一行，不能跟在 shebang 后面，否则会被当成 bash 的参数）

set -x  #这是bash的内置命令，相当于设置一种模式，这里是每执行一条命令前把命令本身打印出来，方便去构建过程/排查错误
cd "$(dirname "$0")" || exit 1   #dirname "$0"表示这是脚本自身的路径，意思就是说到达脚本所在的目录，
#不管你在哪一个路径下执行 ./build.sh 都有效，因为自动会自动到达这个脚本所在的目录

#$()这种格式叫做命令替换，bash先执行里面括号里面的命令，把输出结果当作字符串类型替换到这里，所以最外层要加一个双引号。

# 加密传输默认开启，传 --no-crypto 可关闭（关闭后编译明文行协议版本，不需要链接 OpenSSL）
CRYPTO=ON #定义一个CRYPTO变量来进行默认值的初始化
for arg in "$@"; do #for循环遍历所有指令(不含脚本名)
    case "$arg" in #获取arg的值并于--no-crypto进行比较，如果是等于--no-crypto的话，那么就将这个变量值赋值为OFF.
        --no-crypto) CRYPTO=OFF ;; #在脚本中;;表示for循环中的break语句。
        #这个红色的小括号是用来区分条件判断与实现方式的符号。
       # case "$arg" in
       #模式1) 命令1 ;;      # 这个 ) 就是「模式列表结束」
       #模式2) 命令2 ;;
       #*)    命令3 ;;
#esac
        *) echo "未知参数: $arg（可用: --no-crypto）" >&2; exit 2 ;;
        #>&2,表示将遇到的错误，即echo的输出重定向到fd = 2 的标准错误。不是标准输出
    esac
    #case结束
done
#for循环结束

mkdir -p "$(pwd)/build" #创建build目录，-p表示就算目录已存在也不会报错

rm -rf "$(pwd)/build"/* #清空build目录里的所有文件（保留build目录本身）
#引号用来保护空格/特殊字符；需要通配符展开的`*` 、`?` 、`[...]` 一定要放在引号外面。

cd "$(pwd)/build" && cmake -DENABLE_CRYPTO=${CRYPTO} .. && make -j"$(nproc)"
 # 进 build，cmake .. 读取上一级的 CMakeLists.txt 生成 Makefile，再 make 编译
 # 显式传 -DENABLE_CRYPTO：默认值写在 CMakeLists 里，这里传是为了让 --no-crypto 生效
 # make -j"$(nproc)" 用满所有核并行编译，比串行快数倍，￥nproc是读取cpu核数。
