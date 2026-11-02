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
const size_t kKeyLen = 32;
const size_t kIvLen = 12;
const size_t kTagLen = 16;
//帧头 4 + IV + 标签，长度头本身不算在 body 里
const size_t kOverhead = 4 + kIvLen + kTagLen;
const size_t kHeadLen = 4 + kIvLen;

//内置默认密钥，编译期常量；生产环境应该用 init() 传自己的密钥覆盖
const char *kDefaultKeyHex =
    "6b1f8c2d4a7e90315c8b6d2f4a1e7039c5b8d2a6f4e1093857c2b6d4a8f0e1c3";

unsigned char g_key[kKeyLen];
bool g_ready = false;

bool hex_decode(const std::string &hex,unsigned char *out,size_t n){
    if(hex.size() != n * 2){
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
    const std::string k = hex_key.empty() ? std::string(kDefaultKeyHex) : hex_key;
    if(!hex_decode(k,g_key,kKeyLen)){
        std::cerr << "密钥格式错误：需要 64 位十六进制字符（32 字节）" << std::endl;
        return false;
    }
    g_ready = true;
    return true;
#else
    (void)hex_key;
    return true;
#endif
}

std::string encode(const std::string &plain){
#ifdef ENABLE_CRYPTO
    if(!g_ready){
        return std::string();
    }

    unsigned char iv[kIvLen];
    if(RAND_bytes(iv,kIvLen) != 1){
        return std::string();
    }

    //密文长度与明文相同（GCM 是流式认证加密，不填充），预留足量空间
    std::string out(kHeadLen + plain.size() + kTagLen,'\0');
    unsigned char *base = reinterpret_cast<unsigned char*>(&out[0]);
    unsigned char *ct = base + kHeadLen;

    int clen = 0;
    int flen = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(ctx == nullptr){
        return std::string();
    }
    const bool ok =
        EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,g_key,iv) == 1
        && EVP_EncryptUpdate(ctx,ct,&clen,
                             reinterpret_cast<const unsigned char*>(plain.data()),
                             static_cast<int>(plain.size())) == 1
        && EVP_EncryptFinal_ex(ctx,ct + clen,&flen) == 1
        && EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,kTagLen,ct + clen + flen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if(!ok){
        return std::string();
    }

    const uint32_t body = static_cast<uint32_t>(kIvLen + clen + flen + kTagLen);
    memcpy(base + 4,iv,kIvLen);
    base[0] = static_cast<unsigned char>((body >> 24) & 0xFF);
    base[1] = static_cast<unsigned char>((body >> 16) & 0xFF);
    base[2] = static_cast<unsigned char>((body >> 8) & 0xFF);
    base[3] = static_cast<unsigned char>(body & 0xFF);

    out.resize(4 + body);
    return out;
#else
    std::string out = plain;
    out.push_back('\n');
    return out;
#endif
}

bool decode(const char *buf,size_t avail,size_t &consumed,std::string &plain){
#ifdef ENABLE_CRYPTO
    if(!g_ready || avail < 4){
        return false;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char*>(buf);
    const uint32_t body = (static_cast<uint32_t>(p[0]) << 24)
                        | (static_cast<uint32_t>(p[1]) << 16)
                        | (static_cast<uint32_t>(p[2]) << 8)
                        | static_cast<uint32_t>(p[3]);

    //长度头不合法：小于最小开销，或超过上限
    if(body < kIvLen + kTagLen || body > kMaxFrame){
        return false;
    }
    if(avail < 4 + static_cast<size_t>(body)){
        return false;
    }

    const unsigned char *iv = p + 4;
    const unsigned char *ct = p + kHeadLen;
    const int clen = static_cast<int>(body) - static_cast<int>(kIvLen) - static_cast<int>(kTagLen);

    plain.assign(static_cast<size_t>(clen > 0 ? clen : 0),'\0');
    int outlen = 0;
    int flen = 0;
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
        && EVP_DecryptFinal_ex(ctx,dst != nullptr ? dst + outlen : nullptr,&flen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if(!ok){
        //认证失败：内容被篡改或密钥不对，整帧丢弃
        return false;
    }

    plain.resize(static_cast<size_t>(outlen + flen));
    consumed = 4 + static_cast<size_t>(body);
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
