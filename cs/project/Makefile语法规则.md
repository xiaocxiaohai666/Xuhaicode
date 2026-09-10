# Makefile 语法规则速查手册

## 一、Makefile 是什么？核心理解

```
Makefile = 带「依赖关系」和「增量编译」的自动化编译配置文件

● 只负责两件事：
  ① 编译（从 .cpp 变成可执行文件）
  ② 清理（删除编译产物）

● 不负责：运行程序（那是 .sh 脚本的事，比如 ser_start.sh）

● 它不是「从上到下依次执行」的脚本，而是一本「菜单」：
  ┌─────────────────────────────────────────────┐
  │ 菜单上每一道菜 = 一个「目标」（冒号前面那个）│
  │ 你点 make 菜名 → 才做那道菜                  │
  │ 你啥都不点 → 做菜单第一道菜（第一个目标）    │
  └─────────────────────────────────────────────┘
```

---

## 二、基本结构（必背）

以项目里的 Makefile 为例拆解：

```makefile
# ────────────────────────────────────────
# 结构：目标 : 依赖文件列表
#        TAB   命令1
#        TAB   命令2
# ────────────────────────────────────────

all: service client
# ↑    ↑
# 目标  依赖（做 all 之前必须先有 service 和 client）

service: service.cpp service.h
# ↑       ↑ 依赖文件（这些文件比 service 新才会重新编译）
	g++ -o service service.cpp -ljsoncpp -lmysqlclient
#   ↑ ⚠️ 这里必须是 TAB 键！不是空格！绝对不能忘！

clean:
	rm -f *.o service client
```

### 最容易踩的坑：TAB vs 空格

| 写法 | 结果 |
|------|------|
| 命令前面是 **TAB 键** | ✅ 正确 |
| 命令前面是 **4个/8个空格** | ❌ 报错：`Makefile:xx: *** missing separator.  Stop.` |

> 💡 建议：在编辑器里开「显示空白字符」，一眼就能看到是 TAB 还是空格。

---

## 三、执行规则（怎么跑的）

### 3.1 怎么调用

```bash
make                    # 做第一个目标（就是 all）
make all                # 明确做 all
make service            # 只做 service
make clean              # 只做 clean
make -n                 # 模拟运行（只显示会执行什么，不真的执行）
make -B service         # 强制重新编译 service（忽略时间，必编）
```

### 3.2 clean 永远不会自动执行！

```bash
make            → ❌ 不执行 clean
make all        → ❌ 不执行 clean
make service    → ❌ 不执行 clean
make clean      → ✅ 执行（只有你明确写 clean 才会跑）
```

### 3.3 「增量编译」机制（Make 会偷懒）

Make 不会每次都傻傻重新编译，它会比较**时间戳**：

```
依赖文件(service.cpp) 的修改时间   >   目标文件(service) 的修改时间
        ↓ （源码改过了，比可执行文件新）
    重新 g++ 编译

依赖文件(service.cpp) 的修改时间   <   目标文件(service) 的修改时间
        ↓ （源码没动过）
    跳过！显示：Nothing to be done
```

示例：
```bash
# 第一次编译
make service
# g++ -o service service.cpp ...    ← 编译了

# 不改文件，再跑一次
make service
# make: 'service' is up to date.   ← 偷懒了，没编译

# 改一下 service.cpp（touch 模拟修改）
touch service.cpp
make service
# g++ -o service service.cpp ...    ← 又编译了
```

### 3.4 依赖链：make all 到底做了什么

```
make all
  │
  └─► all 的依赖：service client
        │
        ├─► service：检查 service.cpp
        │     需要编译？→ 是 → 跑 g++
        │
        └─► client：检查 client.cpp
              需要编译？→ 否 → 跳过（Nothing...）

  结束！clean 没有被任何人依赖 → ❌ 完全不执行
```

---

## 四、注释规则

```makefile
# 这是一行注释，# 后面全被忽略

all: service client     # 这是行尾注释，写在代码后面

# Makefile 只有 # 一种注释
# 没有 // 注释
# 没有 /* */ 多行注释
```

---

## 五、变量（高级一点，但很好用）

### 5.1 基本用法

```makefile
# 定义变量：大写推荐，= 两边随意空格
CXX      = g++
CXXFLAGS = -std=c++11 -Wall -g
LDFLAGS  = -lpthread -ljsoncpp -lmysqlclient
OUTPUT   = output

# 取值：$(变量名)
service: service.cpp
	$(CXX) $(CXXFLAGS) -o service service.cpp $(LDFLAGS)
#   ↑ 展开后 = g++ -std=c++11 -Wall -g -o service service.cpp -lpthread ...
```

### 5.2 推荐变量命名

| 变量名 | 约定含义 | 示例值 |
|--------|---------|--------|
| `CC` | C 编译器 | `gcc` |
| `CXX` | C++ 编译器 | `g++` |
| `CFLAGS` | C 编译参数 | `-O2 -Wall` |
| `CXXFLAGS` | C++ 编译参数 | `-std=c++11 -g` |
| `LDFLAGS` | 链接参数/库 | `-lpthread -lmysqlclient` |
| `TARGET` / `OUTPUT` | 输出目录或目标名 | `output` |

### 5.3 自动变量（省事）

```makefile
service: service.cpp
	g++ -o $@ $< $(LDFLAGS)
#          ↑   ↑
#          $@ = 当前目标名（service）
#          $< = 第一个依赖（service.cpp）

# 如果有多个依赖，用 $^ 代表全部
main: a.o b.o c.o
	g++ -o $@ $^
#          ↑ = a.o b.o c.o 全部
```

| 自动变量 | 含义 |
|---------|------|
| `$@` | 当前目标的名字（冒号左边的） |
| `$<` | 第一个依赖文件的名字 |
| `$^` | 所有依赖文件的名字（空格分隔） |

---

## 六、常见目标写法模板

套用您的项目，有以下几种常见目标：

### 6.1 all：默认总目标（菜单第一道菜）

```makefile
all: service client
# 把你想默认构建的东西都当依赖写在这里
```

### 6.2 单个可执行文件目标

```makefile
# 模式：目标名: 源文件 头文件
service: service.cpp service.h
	g++ -o service service.cpp -ljsoncpp -lmysqlclient
#   └─ 如果 service.cpp 或 service.h 改过 → 重编
```

### 6.3 clean：清理目标

```makefile
clean:
	rm -f *.o service client
#     ↑
#     -f = force，文件不存在也不报错（推荐加）
#     *.o = 所有 .o 中间文件
#     service client = 可执行文件
```

### 6.4 伪目标 .PHONY（选学）

```makefile
# 声明：下面这些名字「不是真实文件名，只是动作」
.PHONY: all clean

all:
	...
clean:
	...
```

> 什么时候需要？**当你的目录里真的有个文件叫 clean / all 的时候。**
> 不然 make clean 会以为你要判断那个文件，不执行 rm 命令。
> 平时写学习项目可以不用，出问题了再加回来即可。

---

## 七、模式规则（进阶）

如果源文件很多，可以写通用规则，不用一个个写：

```makefile
# 所有 .o 文件：从对应的 .cpp 文件编译出来
%.o: %.cpp
	g++ -c -o $@ $< $(CXXFLAGS)

# 这样写一个规则就顶几十个
```

---

## 八、完整示例（可直接抄）

这是一个「规范化版」的 Makefile，适合您这类双文件 C++ 项目：

```makefile
# ===========================
# 编译器与参数（用变量统一管理）
# ===========================
CXX      := g++
CXXFLAGS := -std=c++11 -Wall -g
LDFLAGS  := -lpthread -ljsoncpp -lmysqlclient

# ===========================
# 目标与依赖
# ===========================
all: service client

# 服务端：依赖 service.cpp + service.h
service: service.cpp service.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# 客户端：依赖 client.cpp + client.h
client: client.cpp client.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# 清理
clean:
	rm -f *.o service client
```

---

## 九、常见报错速查

| 报错信息 | 原因 | 解决办法 |
|---------|------|---------|
| `*** missing separator. Stop.` | 命令行前用了空格没TAB | 把空格改成 TAB |
| `No rule to make target 'xxx.cpp'` | 依赖的源文件拼错/不存在 | 检查文件名 |
| `Nothing to be done for 'xxx'` | 不是报错！是增量编译生效了 | 正常，源码没改就跳过 |
| `'clean' is up to date.` | 目录里有文件叫 clean | 加 `.PHONY: clean` |
| undefined reference to `xxx` | 链接库漏了 | 检查 `LDFLAGS` 里的 `-lxxx` |
| command not found | g++ 没装/库没装 | `sudo apt install g++ libjsoncpp-dev libmysqlclient-dev` |

---

## 十、配合 Shell 脚本的完整流程

```
ser_start.sh 负责「全套动作」：
   │
   ├─►  make clean    ← 调用 Makefile 的 clean（清理）
   │
   ├─►  make          ← 调用 Makefile 的 all（编译）
   │
   └─►  ./service service.conf   ← 启动程序（Makefile 不负责这个！）
```

Makefile 只管「编译和清理」，ser_start.sh 负责「编译 + 启动 + 传参」，两者分工合作。
