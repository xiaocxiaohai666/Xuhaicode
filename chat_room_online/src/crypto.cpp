#include"crypto.hpp"

#include<cstdint>
#include<cstdlib>
#include<cstring>
#include<iostream>

#ifdef ENABLE_CRYPTO
#include<openssl/evp.h>
#include<openssl/rand.h>
#endif

namespace wire {
namespace {

#ifdef ENABLE_CRYPTO
const size_t kKeyLen = 32;//密钥总长度
const size_t kIvLen = 12;//IV长度
const size_t kTagLen = 16;//认证标签长度
//帧头 4 + IV + 标签，长度头本身不算在 body 里
const size_t kOverhead = 4 + kIvLen + kTagLen;//整个帧头部的固定开销
const size_t kHeadLen = 4 + kIvLen;//帧头长度，相当于是告诉你是从哪里开始放密文的

//内置默认密钥，编译期常量；生产环境应该用 init() 传自己的密钥覆盖
const char *kDefaultKeyHex =
    "6b1f8c2d4a7e90315c8b6d2f4a1e7039c5b8d2a6f4e1093857c2b6d4a8f0e1c3";

unsigned char g_key[kKeyLen];//存储密钥的数组
bool g_ready = false;//状态，用来存储密钥准备好了没有

//hex是十六进制的缩写这个函数的作用是将这个加密后的十六进制字符串的数据转换成字符串流
bool hex_decode(const std::string &hex,unsigned char *out,size_t n){
    if(hex.size() != n * 2){//因为一个字节需要两个十六进制字符串字符表示，而在编码的过程中，十六进制的字符串每2个字符串（每个字符单独占一个字节），所以加密后的字符个数（所占用的内存大小）是原来数据的两倍大小
    // 所以如果hex的长度不等于n的两倍的话，说明帧在传输过程中出现了问题。
        return false;
    }
    for(size_t i = 0;i < n;++i){
        //这个数组专门存储原来密钥中的十六进制中的一个字节中的数字是什么。
        const char pair[3] = {hex[i * 2],hex[i * 2 + 1],'\0'};//两个字符才是一个字节的十六进制数，所以是二倍开头。
    
        char *end = nullptr;
        const long v = strtol(pair,&end,16);//pair则是带解析的字符串嘛，end表示解析到哪一个字符就停止，也就是'\0'，第三个参数表示的是要转换的进制
        //它会将这个原本两个字节的字符串组合成一个16进制数，并将这个16进制数的真实值存在这一字节底下。
        if(end == pair || v < 0 || v > 255){//因为pair代表的是1个字节所存储的数据量，所以我们知道一个字节的数据最多存储-128-127,对吧但是我们的密钥是不会出现负数的，所以这里要加一个判断
            return false;
        }//如果解析完了之后pair还是为空，说明没有解析出来，中间的转换出现了错误，所以直接返回false。
        out[i] = static_cast<unsigned char>(v);//将原来的Long类型强转为无符号char类型，然后赋值到out数组。
    }
    return true;
}
#endif

}

bool enabled(){
#ifdef ENABLE_CRYPTO
    return true;
#else
    return false;
#endif
}

bool init(const std::string &hex_key){
#ifdef ENABLE_CRYPTO
    //判断是否为空，如果为空则使用默认值，不为空就是用传进来的值
    const std::string k = hex_key.empty() ? std::string(kDefaultKeyHex) : hex_key;
    if(!hex_decode(k,g_key,kKeyLen)){//判断是否解码失败，不是解密失败要区分！解密与解码是两回事
        std::cerr << "密钥格式错误：需要 64 位十六进制字符（32 字节）" << std::endl;
        return false;
    }
    //为什么不用rand()，因为rand是属于可预测的伪随机数，也就是线性同余，在这个项目里面相当于没加密，所以要用密码学专用的这个函数，它是真随机
    g_ready = true;//判单密钥格式是否准确的状态
    return true;
#else
    (void)hex_key;
    return true;
#endif
}

std::string encode(const std::string &plain){//加密函数，plain参数即使我们要加密的明文
#ifdef ENABLE_CRYPTO
    if(!g_ready){
        return std::string();
    }

    unsigned char iv[kIvLen];
    if(RAND_bytes(iv,kIvLen) != 1){//Rand_bytes是OpenSSL的密码学安全随机数生成函数
        return std::string();
    }

    //密文长度与明文相同（GCM 是流式认证加密，不填充），预留足量空间
    std::string out(kHeadLen + plain.size() + kTagLen,'\0');
    unsigned char *base = reinterpret_cast<unsigned char*>(&out[0]);//同一块内存换另一种类型去解释，不改值，语义上属于reinterpret
    unsigned char *ct = base + kHeadLen;//正文的起始位置

    int clen = 0;//这个表示实际产出的密文字节数
    int flen = 0;//这个表示追加的字节数
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();//这个是加解密上下文，也就是一次加密的全部状态都装在里面
    if(ctx == nullptr){
        return std::string();
    }
    const bool ok =
        EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,g_key,iv) == 1   //这是定算法+喂key+喂Iv
        && EVP_EncryptUpdate(ctx,ct,&clen,
                             reinterpret_cast<const unsigned char*>(plain.data()),
                             static_cast<int>(plain.size())) == 1       //将明文转换成密文，写进ct,clen回填长度
        && EVP_EncryptFinal_ex(ctx,ct + clen,&flen) == 1           //收尾
        && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,kTagLen,ct + clen + flen) == 1; //取出16字节认证标签，写到密文后面
    EVP_CIPHER_CTX_free(ctx);//无论成败都释放ctx,这一段是一次完整的AES——256-GCM加密，四步一条链，任何一部失败整条就是失败
    if(!ok){
        return std::string();
    }

    const uint32_t body = static_cast<uint32_t>(kIvLen + clen + flen + kTagLen);
    memcpy(base + 4,iv,kIvLen);
    base[0] = static_cast<unsigned char>((body >> 24) & 0xFF);
    base[1] = static_cast<unsigned char>((body >> 16) & 0xFF);
    base[2] = static_cast<unsigned char>((body >> 8) & 0xFF);
    base[3] = static_cast<unsigned char>(body & 0xFF);
    //这是讲IV拷贝进去，再把长度写成4字节的大端，为什么要用大端，因为这是由协议所决定的
    //改变的只是每个字节的相对位置，字节内部的每个位的相对位置不变
    out.resize(4 + body);
    return out;
#else
    std::string out = plain;
    out.push_back('\n');
    return out;
#endif
}

bool decode(const char *buf,size_t avail,size_t &consumed,std::string &plain){//参数分别是缓冲区，以及缓冲区个数，输出，以及解出来的明文
#ifdef ENABLE_CRYPTO
    if(!g_ready || avail < 4){
        return false;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char*>(buf);
    const uint32_t body = (static_cast<uint32_t>(p[0]) << 24)
                        | (static_cast<uint32_t>(p[1]) << 16)
                        | (static_cast<uint32_t>(p[2]) << 8)
                        | static_cast<uint32_t>(p[3]);
    //这是将长度值，这一个，四个字节拼回成一个32位整数，与encode后面几行相反。
    //长度头不合法：小于最小开销，或超过上限
    if(body < kIvLen + kTagLen || body > kMaxFrame){
        return false;
    }
    if(avail < 4 + static_cast<size_t>(body)){
        return false;
    }

    const unsigned char *iv = p + 4;  //iv起始位
    const unsigned char *ct = p + kHeadLen;  //密文起始位
    const int clen = static_cast<int>(body) - static_cast<int>(kIvLen) - static_cast<int>(kTagLen);//密文实际长度

    plain.assign(static_cast<size_t>(clen > 0 ? clen : 0),'\0');//给明文准备输出缓冲区
    int outlen = 0; //输出长度
    int flen = 0; //final阶段追加的字节数
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(ctx == nullptr){
        return false;
    }
    unsigned char *dst = clen > 0 ? reinterpret_cast<unsigned char*>(&plain[0]) : nullptr;
    const bool ok =
        EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,g_key,iv) == 1
        && (clen == 0 || EVP_DecryptUpdate(ctx,dst,&outlen,ct,clen) == 1)
        //标签必须用可写指针传进去，OpenSSL 的接口签名如此
        && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,kTagLen,
                               const_cast<unsigned char*>(ct + clen)) == 1
        && EVP_DecryptFinal_ex(ctx,dst != nullptr ? dst + outlen : nullptr,&flen) == 1;  //拿自己算的GHASH和收到的标签来比对
    EVP_CIPHER_CTX_free(ctx); //解密
    if(!ok){
        //认证失败：内容被篡改或密钥不对，整帧丢弃
        return false;
    }
 
    //重新分配，根据实际密文长度来进行分配，保证json的正确性
    plain.resize(static_cast<size_t>(outlen + flen));
    consumed = 4 + static_cast<size_t>(body);//输出参数 ，调用方靠它把缓冲区往前推
    return true;
#else
    for(size_t i = 0;i < avail;++i){
        if(buf[i] == '\n'){
            size_t n = i;
            if(n > 0 && buf[n - 1] == '\r'){
                --n;
            }
            plain.assign(buf,n);
            consumed = i + 1;
            return true;
        }
    }
    return false;
#endif
}

}
