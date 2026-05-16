// common/compilation_cache.cpp - Compilation cache implementation

#include "compilation_cache.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <cstring>

namespace claw {

namespace {

// Minimal SHA-256 (same as parse_cache.cpp)
struct SHA256_CTX {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
};

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t ep0(uint32_t x) { return rotr(x,2) ^ rotr(x,13) ^ rotr(x,22); }
inline uint32_t ep1(uint32_t x) { return rotr(x,6) ^ rotr(x,11) ^ rotr(x,25); }
inline uint32_t sig0(uint32_t x) { return rotr(x,7) ^ rotr(x,18) ^ (x >> 3); }
inline uint32_t sig1(uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x >> 10); }

void sha256_transform(SHA256_CTX* ctx, const uint8_t data[]) {
    uint32_t a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];
    for (i=0,j=0; i<16; ++i, j+=4)
        m[i] = (data[j]<<24)|(data[j+1]<<16)|(data[j+2]<<8)|(data[j+3]);
    for (; i<64; ++i)
        m[i] = sig1(m[i-2]) + m[i-7] + sig0(m[i-15]) + m[i-16];
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i=0;i<64;++i){
        t1 = h + ep1(e) + ch(e,f,g) + K[i] + m[i];
        t2 = ep0(a) + maj(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

void sha256_init(SHA256_CTX* ctx) {
    ctx->count = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

void sha256_update(SHA256_CTX* ctx, const uint8_t data[], size_t len) {
    for (size_t i=0;i<len;++i){
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        if (ctx->count % 64 == 0) sha256_transform(ctx, ctx->buffer);
    }
}

void sha256_final(SHA256_CTX* ctx, uint8_t hash[]) {
    uint64_t bitcount = ctx->count * 8;
    size_t padlen = (ctx->count % 64 < 56) ? (56 - ctx->count % 64) : (120 - ctx->count % 64);
    uint8_t padding[2] = {0x80,0};
    sha256_update(ctx, padding, 1);
    uint8_t zero[1]={0};
    for (size_t i=1;i<padlen;++i) sha256_update(ctx, zero, 1);
    uint8_t bc[8];
    for (int i=7;i>=0;--i){ bc[i]=bitcount&0xff; bitcount>>=8; }
    sha256_update(ctx, bc, 8);
    for (int i=0;i<8;++i){
        hash[i*4]   = (ctx->state[i]>>24)&0xff;
        hash[i*4+1] = (ctx->state[i]>>16)&0xff;
        hash[i*4+2] = (ctx->state[i]>> 8)&0xff;
        hash[i*4+3] = (ctx->state[i]    )&0xff;
    }
}

std::string sha256_string(const std::string& input) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    uint8_t hash[32];
    sha256_final(&ctx, hash);
    std::ostringstream oss;
    for (int i=0;i<32;++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return oss.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CompilationCache
// ---------------------------------------------------------------------------

CompilationCache::CompilationCache() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) home = ".";
    cache_dir_ = std::string(home) + "/.claw/cache/compile";
}

bool CompilationCache::ensure_cache_dir() {
    struct stat st;
    if (stat(cache_dir_.c_str(), &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    std::string cmd = "mkdir -p " + cache_dir_;
    return system(cmd.c_str()) == 0;
}

std::string CompilationCache::compute_key(const std::string& source,
                                          const std::string& filename,
                                          const std::string& config_key) {
    return sha256_string(source + "\0" + filename + "\0" + config_key);
}

std::string CompilationCache::cache_path(const std::string& key) {
    return cache_dir_ + "/" + key + ".clbc";
}

bool CompilationCache::has_cache(const std::string& source,
                                 const std::string& filename,
                                 const std::string& config_key) {
    current_key_ = compute_key(source, filename, config_key);
    std::string path = cache_path(current_key_);
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::shared_ptr<bytecode::Module> CompilationCache::load_module() {
    std::string path = cache_path(current_key_);
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (buffer.size() < 4) return nullptr;

    // Check magic: "CLBC"
    if (buffer.size() < 8 || buffer[0] != 'C' || buffer[1] != 'L' || buffer[2] != 'B' || buffer[3] != 'C') {
        return nullptr;
    }

    // Skip the CLBC header and pass the rest to the bytecode reader
    std::vector<uint8_t> bytecode_buffer(buffer.begin() + 4, buffer.end());

    bytecode::BytecodeReader reader;
    auto mod_opt = reader.read_module(bytecode_buffer);
    if (!mod_opt) return nullptr;

    return std::make_shared<bytecode::Module>(*mod_opt);
}

void CompilationCache::save_module(const std::string& source,
                                   const std::string& filename,
                                   const std::string& config_key,
                                   const bytecode::Module& module) {
    if (!ensure_cache_dir()) return;
    current_key_ = compute_key(source, filename, config_key);
    std::string path = cache_path(current_key_);

    bytecode::BytecodeWriter writer;
    writer.write_module(module);
    const auto& buffer = writer.get_buffer();

    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    // Write magic header
    out.write("CLBC", 4);
    out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

void CompilationCache::invalidate_all() {
    struct stat st;
    if (stat(cache_dir_.c_str(), &st) == 0) {
        std::string cmd = "rm -rf " + cache_dir_ + "/*";
        system(cmd.c_str());
    }
}

} // namespace claw
