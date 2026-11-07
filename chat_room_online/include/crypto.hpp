#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include<cstddef>
#include<string>

//该项目的加密方式是AES-256-GCM，流式加密，不填充，所以密文长度 = 明文长度




//线上字节流的编解码层
//不开 ENABLE_CRYPTO 时就是原来的「JSON + '\n'」，开了以后每条消息独立加密封装
//业务代码只认 encode / decode，不关心底下是不是密文
namespace wire {

//单帧上限，防止畸形长度或者恶意信息头把内存撑爆
//那么何为单帧，单帧就是传输层上一次独立加密封装的单位，在我们设计的这个项目中就是对应一条信息。
//单帧的格式为：
 //[ 长度 4B ][ IV 12B ][ 密文 N B ][ 认证标签 16B ]
 //└────────────── body = 12 + N + 16 ───────────────┘
 //长度是为了在这个项目中告诉接收方，这个消息的边界在哪里，这是必须的，因为tcp是字节流，没有消息边界的。
 //IV是初始向量，每一个帧中的iv值都是不同的随机值，iv自身不要求保密
const size_t kMaxFrame = 1024 * 1024;

//初始化密钥：64 位十六进制（32 字节）；传空串用内置默认值
//返回 false 表示密钥格式不对
bool init(const std::string &hex_key = "");

//当前编译进来的版本是否启用了加密
bool enabled();

//明文 → 线上字节
//  明文模式：plain + '\n'
//  加密模式：长度(4,大端) + IV(12) + 密文 + 认证标签(16)
std::string encode(const std::string &plain);

//从缓冲区里尝试解出一帧
//  返回 true ：consumed 为本次吃掉的字节数，plain 为解出的明文
//  返回 false：还不够一帧（或认证明文校验失败），调用方应继续收数据或断开
bool decode(const char *buf,size_t avail,size_t &consumed,std::string &plain);

}

#endif
