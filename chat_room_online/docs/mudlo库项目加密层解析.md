# muduo 库项目加密层解析

> 项目：chat_room_online
> 模块：传输层封装 `wire`
> 源码：[include/crypto.hpp](../include/crypto.hpp)、[src/crypto.cpp](../src/crypto.cpp)

---

## 0. 一句话概括

加密层把「**明文 JSON 消息**」和「**网络上的字节**」隔开：

- 业务代码只看到 `std::string`（一条消息的 JSON）；
- 底下到底是「明文 + `'\n'`」还是「AES-256-GCM 加密帧」，由**编译开关**决定；
- 协议层（`Message{type,seq,code,msg,data}`）一行都不用改。

对外只有四个函数：

| 函数 | 作用 |
|---|---|
| `wire::init(hex_key)` | 初始化密钥，失败返回 false |
| `wire::enabled()` | 当前是加密版还是明文版 |
| `wire::encode(plain)` | 明文 → 线上字节（一帧） |
| `wire::decode(buf, avail, consumed, plain)` | 从缓冲区切出一帧 → 明文 |

---

## 1. 为什么需要这一层

三个独立的需求，正好都落在「传输」这一层：

1. **TCP 没有消息边界**。它只保证字节有序，不保证一条消息对应一段字节。所以必须自己切帧：明文模式靠 `'\n'`，加密模式靠长度头。
2. **明文传输不安全**。同局域网抓包就能读到聊天内容，也能随手篡改。需要机密性（别人看不懂）+ 完整性（改了能被发现）。
3. **不想动业务协议**。加密不应该让 17 个 handler、35 种消息类型全部重写。所以把加解密关在一个独立的 `wire` 命名空间里，业务只调 `encode` / `decode`。

---

## 2. 文件与调用关系

```
include/crypto.hpp        对外声明：init / enabled / encode / decode
src/crypto.cpp            实现：密钥解析、帧格式、OpenSSL 调用
      │
      ├─ 服务端 src/server/chatserver.cpp   收包循环里 decode，发包时 encode
      ├─ 服务端 src/server/chatservice.cpp  send_message 里 encode
      ├─ 客户端 src/client/main.cpp         发送时 encode，接收时 decode
      ├─ 服务端 src/server/main.cpp         启动时 init()
      └─ 压测器 tools/bench.cpp             同样 init / encode / decode
```

关键调用点：

| 位置 | 调用 |
|---|---|
| [src/server/chatservice.cpp](../src/server/chatservice.cpp) `send_message` | `out = wire::encode(out)`，空串则不发 |
| [src/server/chatserver.cpp](../src/server/chatserver.cpp) 收包循环 | `while(wire::decode(...)) { buf->retrieve(consumed); ... }` |
| [src/client/main.cpp](../src/client/main.cpp) | 发送前 `encode`，收到后 `decode` |
| [src/server/main.cpp](../src/server/main.cpp) | `if(!wire::init()) return 1;` |

---

## 3. 编译开关：ENABLE_CRYPTO

### 3.1 开关链路

```
build.sh               ./build.sh          → CRYPTO=ON
                       ./build.sh --no-crypto → CRYPTO=OFF
        │
        ▼
CMakeLists.txt         option(ENABLE_CRYPTO "..." ON)
                       cmake -DENABLE_CRYPTO=ON|OFF
        │
        ▼
各子目录 CMakeLists     if(ENABLE_CRYPTO)
                           target_compile_definitions(... PRIVATE ENABLE_CRYPTO)
                           target_link_libraries(... crypto)   # OpenSSL libcrypto
        │
        ▼
crypto.cpp             #ifdef ENABLE_CRYPTO ...
```

`ENABLE_CRYPTO` 是 **CMake 的缓存开关**，不是编译器宏；`target_compile_definitions` 才把它变成预处理宏。

### 3.2 两种模式的差别

| | 加密版（ON） | 明文版（OFF） |
|---|---|---|
| 分帧 | 4 字节长度头 | `'\n'` |
| 机密性 | AES-256-GCM | 无 |
| 完整性 | 16 字节认证标签 | 无 |
| 每帧额外字节 | 32（4 + 12 + 16） | 1 |
| 依赖 | OpenSSL `libcrypto` | 无 |
| 业务代码 | 完全相同 | 完全相同 |

注意：**明文版的可执行文件里根本没有加解密代码**——`#ifdef ENABLE_CRYPTO` 之外的东西被预处理器整段裁掉。

---

## 4. 帧格式

### 4.1 结构

```
┌────────────┬────────────┬───────────────┬────────────┐
│ 长度 4B    │ IV 12B     │ 密文 N B      │ 标签 16B   │
└────────────┴────────────┴───────────────┴────────────┘
  base[0..3]   base[4..15]  base[16..]     末尾 16B
               └──────── body = 12 + N + 16 ───────────┘
                            body 的长度由前 4 字节声明
```

常量定义：

```cpp
const size_t kIvLen  = 12;            // GCM 标准 IV 长度（96 bit）
const size_t kTagLen = 16;            // GCM 认证标签
const size_t kHeadLen = 4 + kIvLen;   // 16：长度头 + IV，密文起点
const size_t kMaxFrame = 1024 * 1024; // 单帧上限 1MB，防畸形长度头
```

> 注：`kOverhead = 4 + kIvLen + kTagLen` 也有定义，但当前未被任何代码使用，属于可清理的死常量。

### 4.2 每个字段为什么存在

| 字段 | 作用 | 去掉会怎样 |
|---|---|---|
| 长度头 | 告诉对方「这一帧还有多少字节」 | TCP 无边界，无法切帧 |
| IV | GCM 要求每次加密唯一；12 字节随机生成 | 密钥流复用，可能被还原明文/伪造 |
| 密文 | 加密后的内容，长度 = 明文长度 | — |
| 认证标签 | 防篡改指纹；解密时校验 | 攻击者可以随便改内容 |

### 4.3 body 的精确定义

`body` **只计算长度头之后的字节**：

```
body = kIvLen + clen + flen + kTagLen
整帧总长 = 4 + body
```

两边必须严格一致：写时按 `4 + body` 组帧，读时按 `4 + body` 判断到齐并消费。如果 body 把 4 也算进去，对方会永远等 4 个不存在的字节。

### 4.4 为什么长度头必须手写大端

x86 是小端，直接 `memcpy(base, &body, 4)` 会写出「低字节在前」的顺序，跟协议约定的「高字节在前」相反。手写移位写法**与 CPU 字节序无关**：

写侧：

```cpp
base[0] = (body >> 24) & 0xFF;   // 最高字节
base[1] = (body >> 16) & 0xFF;
base[2] = (body >> 8)  & 0xFF;
base[3] =  body        & 0xFF;   // 最低字节
```

读侧（正好反向）：

```cpp
body = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
```

`static_cast<uint32_t>` 的作用：避免 `unsigned char` 提升为 `int` 后左移 24 位落进符号位（未定义行为）。

**字节序只规定字节之间怎么排，不改变字节内部的位**：`0xE8` 永远是 `11101000`，大端小端都一样。

---

## 5. 密钥与初始化

### 5.1 常量与存储

```cpp
const size_t kKeyLen = 32;                 // AES-256 需要 32 字节（256 bit）
unsigned char g_key[kKeyLen];              // 密钥字节
bool g_ready = false;                      // 密钥是否已就绪
```

- `unsigned char`：密钥是原始位模式，`0x80~0xFF` 用 `char` 会被当负数，语义错误；
- 定长数组：长度编译期确定，不扩容、不拷贝；
- 放在匿名 `namespace` 里 = **文件私有全局变量**，别的翻译单元不可见；
- `g_ready` 是安全默认值：没初始化成功就拒绝加解密，而不是拿全零密钥「假装加密」。

### 5.2 init()

```cpp
bool init(const std::string &hex_key){
#ifdef ENABLE_CRYPTO
    const std::string k = hex_key.empty() ? std::string(kDefaultKeyHex) : hex_key;
    if(!hex_decode(k,g_key,kKeyLen)){
        std::cerr << "密钥格式错误：需要 64 位十六进制字符（32 字节）" << std::endl;
        return false;
    }
    g_ready = true;
    return true;
#else
    (void)hex_key;   // 明文模式不需要密钥，显式丢弃参数以消除未使用警告
    return true;     // 恒成功，保证上层调用方式统一
#endif
}
```

要点：

- 没传密钥就用编译期默认值（`kDefaultKeyHex`），方便 demo 开箱即用；
- 格式错直接返回 false，[server/main.cpp](../src/server/main.cpp) 会打印错误并 `return 1`——**宁可起不来，也不带错密钥跑**；
- 明文模式必须返回 true，否则上层 `if(!wire::init())` 会把明文版也拦住。

### 5.3 hex_decode：十六进制文本 → 字节

```cpp
bool hex_decode(const std::string &hex,unsigned char *out,size_t n){
    if(hex.size() != n * 2){        // 1 字节 = 2 个 hex 字符
        return false;
    }
    for(size_t i = 0;i < n;++i){
        const char pair[3] = {hex[i * 2],hex[i * 2 + 1],'\0'};
        char *end = nullptr;
        const long v = strtol(pair,&end,16);
        if(end == pair || v < 0 || v > 255){
            return false;
        }
        out[i] = static_cast<unsigned char>(v);
    }
    return true;
}
```

逐点说明：

| 片段 | 意义 |
|---|---|
| `hex.size() != n * 2` | hex 文本长度恒为字节数的 **2 倍**（`0x6b` → `"6b"` 两个字符） |
| `pair[3]` | 取相邻两个字符组成 C 字符串，`strtol` 需要 `'\0'` 结尾 |
| `strtol(pair,&end,16)` | 按 16 进制解析，返回数值，`end` 指回「解析停在哪」 |
| `end == pair` | 一个字符都没解析出来（如 `"zz"`）→ 非法 |
| `v < 0` / `v > 255` | 挡负号、挡超出 1 字节的值域 |
| `static_cast<unsigned char>(v)` | 收窄成 1 字节；因为已保证 0~255，属无损 |

**注意两处表述容易错**：

1. 这里的「2 倍」是 **hex 编码**与原始字节的关系，**与 AES 加密无关**。GCM 不填充，密文长度 = 明文长度，不会翻倍。
2. 它叫 decode，但做的是「文本 → 二进制」，**不是解密**。真正解密的是 `wire::decode`。

`strtol` 与 `std::stol` 的取舍：`stol` 要求 `std::string`（每轮要构造临时对象）且用异常报错；`strtol` 吃 `const char*`、用 `endptr` 返回状态，更轻。C++17 的 `std::from_chars` 是更好的替代，但本项目标准是 C++14。

**目前的小宽松**：只检查了 `end == pair`（有没有解析出东西），没检查 `end == pair + 2`（两位是否都吃完），所以 `" 1"`、`"+1"` 这类输入也能通过。若要严格，加一个条件即可。

### 5.4 线程安全

`init()` 只在启动时调用一次，之后多线程只读 `g_key`，无竞争。若将来支持「运行中换密钥」，需要加锁或改成原子指针指向不可变密钥对象。

---

## 6. encode：明文 → 一帧

### 6.1 签名

```cpp
std::string encode(const std::string &plain);
```

- `const`：不修改调用方数据；
- `&`：不拷贝（消息几千字节，拷贝纯浪费）；
- 输出用返回值，失败返回**空串**，调用方据此丢弃整条消息。

### 6.2 步骤

```cpp
unsigned char iv[kIvLen];
if(RAND_bytes(iv,kIvLen) != 1) return std::string();   // ① 随机 IV

std::string out(kHeadLen + plain.size() + kTagLen,'\0'); // ② 按最终长度预分配
unsigned char *base = reinterpret_cast<unsigned char*>(&out[0]); // ③ 缓冲区基址
unsigned char *ct = base + kHeadLen;                             // ④ 密文写入位置
```

| 步骤 | 说明 |
|---|---|
| ① `RAND_bytes` | OpenSSL 的密码学安全随机数（返回 1 成功），**不能用 `rand()`**——伪随机可预测，等于没加密 |
| ② 预分配 | `std::string(n, '\0')` 一次分配到位；`plain.size()` 能精确算总长，因为 GCM 不填充 |
| ③ `&out[0]` | C++14 的 `data()` 返回 `const char*` 不能写；`&out[0]` 拿非 const 地址 |
| ④ `ct` | `kHeadLen = 16`，跳过长度头和 IV；之后 `ct`、`ct+clen`、`ct+clen+flen` 都基于它 |

**指针稳定性约束**：拿到 `base`/`ct` 之后绝不能再改变 `out` 的长度（不能 `append`/`resize`），否则可能重新分配、指针失效。所以必须「先定长、再原地填」。

### 6.3 OpenSSL 四步

```cpp
const bool ok =
    EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,g_key,iv) == 1
 && EVP_EncryptUpdate(ctx,ct,&clen,plain.data(),plain.size()) == 1
 && EVP_EncryptFinal_ex(ctx,ct + clen,&flen) == 1
 && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,kTagLen,ct + clen + flen) == 1;
EVP_CIPHER_CTX_free(ctx);
if(!ok) return std::string();
```

| 步骤 | 作用 | 结果 |
|---|---|---|
| Init | 指定算法、密钥、IV | — |
| Update | 加密正文写到 `ct` | `clen` = 明文长度（不填充） |
| Final | 收尾 | `flen` 通常为 0 |
| GET_TAG | 取出 16 字节认证标签 | 写到密文之后 |

`EVP_CIPHER_CTX` 是**加解密上下文**，装着算法/密钥/IV/计数器等全部状态。它是不透明类型，只能 `_new()` 分配、`_free()` 释放。每次 `encode` 新建一个：每条消息 IV 不同、天然线程安全，代价是每条消息一次堆分配。

`ctx` 这个名字不是巧合，是 `context` 的业界通用缩写（OpenSSL 类型名本身就叫 `EVP_CIPHER_CTX`）。

### 6.4 组帧收尾

```cpp
const uint32_t body = kIvLen + clen + flen + kTagLen;  // 不含 4 字节长度头
memcpy(base + 4,iv,kIvLen);                            // IV 放到 base[4..15]
base[0..3] = body 的四个字节（大端）;
out.resize(4 + body);
return out;
```

---

## 7. decode：一帧 → 明文

### 7.1 签名与三态

```cpp
bool decode(const char *buf,size_t avail,size_t &consumed,std::string &plain);
```

| 参数 | 方向 | 含义 |
|---|---|---|
| `buf` | 输入 | 接收缓冲区首地址（只读） |
| `avail` | 输入 | 有效字节数（裸指针不带长度，必须单独传） |
| `consumed` | 输出 | 本次吃掉多少字节（`4 + body`） |
| `plain` | 输出 | 解出的明文，复用调用方的 string |

返回值是**三态信号**：`true` = 解出一帧；`false` = 数据不够（等下次 recv）**或** 帧非法/验签失败（丢弃）。两种 false 调用方分不出来，由上层「缓冲区超 1MB 就断连」兜底。

接口刻意只用「指针 + 长度」，不依赖 muduo 的 `Buffer`，这样 C 写的压测器也能直接复用。

### 7.2 三道闸门

```cpp
if(!g_ready || avail < 4) return false;                 // ① 至少能读到长度头
body = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];            // ② 大端读出 body
if(body < kIvLen + kTagLen || body > kMaxFrame) return false; // ③ 长度合法性
if(avail < 4 + body) return false;                      // ④ 一帧是否收齐
```

- 上界 `kMaxFrame = 1MB`：长度头是**对端给的、不可信的数字**，照着分配/等待会被撑爆内存；
- 下界 `12 + 16 = 28`：一帧至少要有 IV 和标签，否则 `clen` 会算成负数。

### 7.3 定位与长度

```cpp
const unsigned char *iv  = p + 4;                 // 跳过长度头
const unsigned char *ct  = p + kHeadLen;          // 跳过长度头 + IV
const int clen = body - kIvLen - kTagLen;         // 密文长度
```

```
p     p+4            p+16                          p+16+clen
│ 4B  │  IV 12B      │  密文 clen B               │ tag 16B │
        ▲              ▲
        iv             ct
```

### 7.4 解密 + 验签

```cpp
plain.assign(clen > 0 ? clen : 0,'\0');    // 预分配输出，长度 = 密文长度
unsigned char *dst = clen > 0 ? (unsigned char*)&plain[0] : nullptr;

const bool ok =
    EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,g_key,iv) == 1
 && (clen == 0 || EVP_DecryptUpdate(ctx,dst,&outlen,ct,clen) == 1)
 && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,kTagLen,
                       const_cast<unsigned char*>(ct + clen)) == 1
 && EVP_DecryptFinal_ex(ctx,dst != nullptr ? dst + outlen : nullptr,&flen) == 1;
EVP_CIPHER_CTX_free(ctx);
if(!ok) return false;                      // 认证失败：篡改或密钥错，整帧丢弃
```

要点：

- **认证发生在最后一步**。前两步已经解出了明文，但只有 `DecryptFinal_ex` 校验标签通过，这份明文才可信；失败直接 `return false`，**绝不把未认证的数据交给业务层**。
- `const_cast` 是因为 `EVP_CTRL_GCM_SET_TAG` 形参是 `void*`（非 const），但 OpenSSL 只读；这是对第三方 C 接口签名的妥协。
- `dst` 的三元判断：`clen == 0` 时不给空 string 取址，同时用 `clen == 0 ||` 短路跳过 Update。
- `plain.assign` 而不是 `resize`：`plain` 是跨帧复用的，要彻底清掉上一帧内容。
- `clen > 0 ? clen : 0`：`size_t` 无符号，若 `clen` 为负会转成天文数字导致巨型分配。

### 7.5 收尾

```cpp
plain.resize(outlen + flen);        // 长度修正为实际解出字节数
consumed = 4 + body;                // 与「是否收齐」的检查完全对称
return true;
```

---

## 8. 明文模式（#else 分支）

| 函数 | 明文模式实现 |
|---|---|
| `init` | 空操作，恒返回 true |
| `encode` | `out = plain; out.push_back('\n');` |
| `decode` | 扫描 `'\n'`，兼容 `\r\n`，`plain.assign(buf, n)`，`consumed = i + 1` |

行为与改造前完全一致，所以加密层是**可回退的增量改造**，不是重写协议。

---

## 9. 术语与易错点

### 9.1 三个东西别混

```
plain（明文）  = {"type":31,...}        长度 N
密文           = 加密后的 N 字节         长度仍是 N
整帧           = 4 + 12 + N + 16 字节    比明文多 32 字节
```

「密文长度与明文相同」说的是**中间那段密文**，不是整帧。

### 9.2 两个 decode

| 函数 | 输入 → 输出 | 领域 |
|---|---|---|
| `hex_decode` | hex 文本 → 字节数组 | 数据编码 |
| `wire::decode` | 一帧字节 → 明文 JSON | 切帧 + 解密 |

### 9.3 flen / clen / outlen

| 变量 | 谁写 | 含义 | GCM 实际值 |
|---|---|---|---|
| `clen` | EncryptUpdate | 密文长度 | = 明文长度 |
| `outlen` | DecryptUpdate | 明文主体长度 | = 密文长度 |
| `flen` | Final | 收尾追加长度 | 0（无填充） |

`flen` **不是**「明文总有效长度」，总长是 `outlen + flen`。

### 9.4 GCM 为什么不填充

普通分组模式（CBC 等）要补齐到块大小倍数，密文可能变长；GCM 内部是 CTR 流模式 + GHASH 认证，逐字节异或，**不填充**，所以密文长度严格等于明文长度——`encode` 才能用 `plain.size()` 精确预留空间。

---

## 10. 已知限制与可改进点

| 问题 | 现状 | 可能的改进 |
|---|---|---|
| 密钥无协商 | 编译期默认 hex，`init` 可覆盖 | 上线前应走一次密钥交换/配置下发 |
| 每条消息新建 `EVP_CIPHER_CTX` | 有堆分配开销 | 可改用上下文复用池（需注意 IV 独立性） |
| `hex_decode` 未严格校验 | 只查 `end == pair` | 加 `end != pair + 2` 即严格 |
| `kOverhead` 定义未使用 | 死常量 | 删掉，或用于 body 计算 |
| `decode` 返回 false 二义 | 不够 / 非法都返回 false | 可改成枚举三态；目前靠上层 1MB 缓冲上限兜底 |
| 单帧上限 1MB | 业务消息实际只有几千字节 | 按需调整 `kMaxFrame` |
| 无密钥轮换 | `g_key` 只读一次 | 若支持热换密钥，需处理 `g_key` 并发读写 |

---

## 11. 端到端流程

### 发送（服务端为例）

```
业务 Message
   │  json j = m; out = j.dump();
   ▼
明文 JSON（一条）
   │  wire::encode(out)
   ▼
[4B 长度][12B IV][N B 密文][16B 标签]
   │  conn->send(frame)
   ▼
TCP 字节流
```

### 接收

```
TCP 字节流
   │  buf->append(...)
   ▼
muduo Buffer
   │  wire::decode(buf->peek(), buf->readableBytes(), consumed, plain)
   ▼
成功 → buf->retrieve(consumed) → handle_message(conn, plain)
失败 → 等更多数据（或整帧丢弃）
```

---

## 12. 常见问答

**Q：密钥为什么不用 `std::string` 存？**
A：OpenSSL 接口要 `const unsigned char*`；密钥是原始字节而非文本；`char` 可能有符号，`0xFF` 会变成 `-1`。

**Q：为什么不用 `ntohl` / `memcpy` 读写长度？**
A：`memcpy` 到 `uint32_t` 依赖主机字节序，还要处理对齐和严格别名；手写移位跨平台、零风险。`ntohl` 只是等价的库版本。

**Q：认证失败时，已经解出的明文会被用掉吗？**
A：不会。`DecryptFinal_ex` 失败 → `ok = false` → `return false`，明文虽在内存里但不会交给业务层。

**Q：加密会不会让流量翻倍？**
A：不会。密文 = 明文长度，每帧只多固定 32 字节。翻倍只发生在「字节 → 十六进制文本」的编码转换上，跟加密无关。

**Q：`ctx` 是随便起的名字吗？**
A：不是，`context` 的通用缩写，且与类型名 `EVP_CIPHER_CTX` 对齐。
