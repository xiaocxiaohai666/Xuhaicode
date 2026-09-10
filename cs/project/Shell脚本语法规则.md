# Shell 脚本语法规则速查手册

> 适用于 Bash / Shell 脚本编写（Linux 环境）

---

## 一、文件基础规则

### 1.1 文件扩展名与权限
```bash
# 推荐扩展名： .sh （不是必须，但便于识别）
myscript.sh

# 给脚本加执行权限（只需一次，之后可以 ./运行）
chmod +x myscript.sh

# 运行方式
./myscript.sh          # 方式一：直接运行（需要执行权限 + Shebang）
bash myscript.sh       # 方式二：指定解释器运行（不需要执行权限）
```

### 1.2 第一行必须写 Shebang（必写）
```bash
#!/bin/bash             # ✅ 推荐：用 Bash 解释器（功能最全）
#!/bin/sh               # ✅ 可用：用 POSIX Shell（兼容性好，功能少）
#!/usr/bin/env bash     # ✅ 可用：自动找系统里的 bash（跨平台推荐）

# ❌ 错误：Shebang 不能写在第2行及以后
# （第1行空了）
#!/bin/bash             # ← 这样写无效！
```

---

## 二、注释规则

```bash
# 这是一行注释（# 后面的内容全被忽略）

echo "hello"            # 这是行尾注释，# 前面是代码，后面是说明

# Shell 没有多行注释！想注释多行就每行都写 #
# 第一行注释
# 第二行注释
# 第三行注释

# ❌ 不要用这些：Shell 不认
/* 这样的多行注释 Shell 会报错 */
// 双斜线也不是注释
```

---

## 三、最容易踩的坑（必背）

| 坑点 | 正确写法 | 错误写法 | 报错信息 |
|------|---------|---------|---------|
| `=` 两边不能有空格 | `name="tom"` | `name = "tom"` | `name: command not found` |
| `[ ]` 两边必须有空格 | `if [ -f a.txt ]` | `if [-f a.txt]` | `[-f: command not found` |
| 取变量值要加 `$` | `echo $name` | `echo name` | 直接输出字母 name |
| 每个 `if` 必须有 `fi` 配对 | `if ... then ... fi` | 漏写 `fi` | `syntax error: unexpected end of file` |

---

## 四、变量

### 4.1 定义与取值
```bash
# 定义变量：等号两边绝对不能有空格！
name="小明"
age=18
ip="127.0.0.1"

# 取值：加 $ 符号
echo $name              # 输出：小明
echo ${name}            # 等价写法，推荐用这个，更清晰

# 错误示例（注意空格！）
name = "小明"           # ❌ 等号两边有空格 = 灾难
```

### 4.2 双引号 vs 单引号
```bash
city="北京"

# 双引号：里面的变量会被替换（解析 $）
echo "我住在 $city"     # 输出：我住在 北京

# 单引号：原样输出，不会解析 $
echo '我住在 $city'     # 输出：我住在 $city

# 推荐：用变量的地方都加双引号，防空格
file="my photo.jpg"
rm "$file"              # ✅ 正确：能识别空格
rm $file                # ❌ 危险：会当成 rm my 和 photo.jpg 两个文件
```

### 4.3 常用内置变量
| 变量 | 含义 | 示例值 |
|------|------|--------|
| `$0` | 脚本本身的文件名 | `./ser_start.sh` |
| `$1` `$2` `$3` | 第1/2/3个命令行参数 | `./s.sh a b` → `$1=a, $2=b` |
| `$#` | 参数的个数 | `./s.sh a b` → `2` |
| `$@` | 所有参数（数组形式） | 所有参数列表 |
| `$$` | 当前脚本的进程ID | `12345` |
| `$?` | 上一条命令的返回值（0=成功，非0=失败） | `0` 或 `1` |
| `$(pwd)` 或 `\`pwd\`` | 取命令的执行结果 | `/home/user/project` |

---

## 五、条件判断（if / else）

### 5.1 基本语法
```bash
if [ 条件 ]
then
    # 条件成立时执行
elif [ 条件2 ]
then
    # 条件2成立时执行
else
    # 都不成立时执行
fi                          # ← 不能漏！fi 是 if 反过来写
```

### 5.2 常用判断条件

**文件判断：**
```bash
if [ -f "service.conf" ]    # 文件存在且是普通文件？
if [ -d "output" ]          # 目录存在？
if [ -x "service" ]         # 文件有执行权限？
if [ -s "a.txt" ]           # 文件存在且大小 > 0？
if [ ! -f "a.txt" ]         # 文件不存在？（! 取反）
```

**数值比较：**
| 写法 | 含义 | 示例 |
|------|------|------|
| `-eq` | 等于 equal | `if [ $a -eq 10 ]` |
| `-ne` | 不等于 not equal | `if [ $a -ne 10 ]` |
| `-gt` | 大于 greater than | `if [ $a -gt 5 ]` |
| `-lt` | 小于 less than | `if [ $a -lt 5 ]` |
| `-ge` | 大于等于 | `if [ $a -ge 10 ]` |
| `-le` | 小于等于 | `if [ $a -le 10 ]` |

```bash
# 示例：判断数值
age=18
if [ $age -ge 18 ]
then
    echo "成年人"
fi
```

**字符串比较：**
```bash
if [ "$a" = "$b" ]         # 字符串相等（= 两边有空格！）
if [ "$a" != "$b" ]        # 字符串不相等
if [ -z "$a" ]             # 字符串为空（长度为0）
if [ -n "$a" ]             # 字符串不为空

# 示例
name="小明"
if [ "$name" = "小明" ]
then
    echo "你好，小明"
fi
```

**逻辑运算：**
```bash
if [ $a -gt 5 ] && [ $a -lt 20 ]   # && = 并且（两个都真才真）
if [ $a -lt 5 ] || [ $a -gt 20 ]   # || = 或者（一个真就真）
```

---

## 六、循环

### 6.1 for 循环
```bash
# 遍历列表
for i in 1 2 3 4 5
do
    echo "数字：$i"
done

# 遍历文件
for file in *.cpp
do
    echo "找到C++文件：$file"
done

# C 语言风格（需要 bash）
for (( i=0; i<10; i++ ))
do
    echo $i
done
```

### 6.2 while 循环
```bash
n=1
while [ $n -le 5 ]
do
    echo "第 $n 次"
    n=$(( n + 1 ))        # 变量自增
done
```

---

## 七、函数

```bash
# 定义函数
greet() {
    echo "你好，$1！"      # $1 = 函数的第一个参数
}

# 调用函数（不需要括号！）
greet "小明"               # 输出：你好，小明！
greet "小红"               # 输出：你好，小红！

# 带返回值的函数（返回值只能是 0-255 的数字）
add() {
    sum=$(( $1 + $2 ))
    return $sum            # 只能返回数字
}
add 3 5
echo "结果：$?"            # 用 $? 取返回值：8
```

---

## 八、数组

```bash
# 定义数组
arr=("apple" "banana" "cherry")

# 取值
echo ${arr[0]}             # 第1个：apple
echo ${arr[1]}             # 第2个：banana
echo ${arr[@]}             # 全部元素：apple banana cherry
echo ${#arr[@]}            # 长度：3

# 遍历数组
for fruit in ${arr[@]}
do
    echo "水果：$fruit"
done
```

---

## 九、输入输出

### 9.1 输出 echo
```bash
echo "普通文本"
echo -e "这里会\t转义\n特殊字符"    # -e 启用转义：\t 制表 \n 换行
echo -n "不换行输出"                # -n 输出后不自动换行
```

### 9.2 输入 read
```bash
echo "请输入你的名字："
read username
echo "你好，$username"

# -p 可以直接加提示
read -p "请输入端口：" port
echo "端口号是 $port"
```

### 9.3 重定向
```bash
# 把输出写入文件（覆盖写）
echo "hello" > a.txt

# 追加写入（不覆盖）
echo "world" >> a.txt

# 错误输出一起重定向
make 1>log.txt 2>err.txt    # 1=标准输出  2=错误输出
make >log.txt 2>&1          # 错误也写到同一个文件
```

---

## 十、常用命令写法速查

| 需求 | 写法 |
|------|------|
| 取命令结果 | `today=$(date)` 或 `today=\`date\`` |
| 数字运算 | `sum=$(( a + b ))` |
| 切换到脚本所在目录 | `cd "$(dirname "$0")"` |
| 判断命令是否执行成功 | `if make; then echo "ok"; fi` |
| 上一条命令失败就退出 | `set -e`（脚本第二行加一次即可） |
| 未定义的变量报错 | `set -u`（防拼写错误） |
| 一行写多条命令 | `make clean && make`（前一个成功才执行后一个） |
| 一行写多条命令 | `make clean; make`（不管成功与否都执行） |
| 后台运行 | `./service &` |
| 暂停/休眠 | `sleep 3`（休眠3秒） |

---

## 十一、脚本推荐模板（直接抄用）

```bash
#!/bin/bash
set -e          # 遇到错误立即退出，避免错误扩散
# set -u        # 用到未定义的变量时报错（调试阶段建议打开）

# ==============================
# 函数区
# ==============================

log_info() {
    echo "[INFO]  $1"
}

log_error() {
    echo "[ERROR] $1"
}

# ==============================
# 主逻辑
# ==============================

# 切换到脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

log_info "脚本开始运行..."

# 你的代码写在这里

log_info "脚本运行完成 ✓"
```

---

## 十二、常见报错与解决

| 报错信息 | 原因 | 解决 |
|---------|------|------|
| `Permission denied` | 没有执行权限 | `chmod +x xxx.sh` |
| `bad interpreter` | Shebang 写错或第一行不是它 | 第一行写 `#!/bin/bash` |
| `command not found` | `=` 两边加空格了 / 命令拼错了 | `name="a"` 不要空格 |
| `syntax error` | `if` 漏了 `fi` / `for` 漏了 `done` | 检查配对的结束关键字 |
| `unary operator expected` | `[` 里变量为空没加引号 | `[ "$a" = "x" ]` 加双引号 |
| `too many arguments` | 变量值里有空格没加引号 | 所有变量取值都加 `"$var"` |
| `[: missing ]` | `]` 前面没空格 | `[ -f a.txt ]` 注意空格 |
