#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using tjs_uint8 = uint8_t;
using tjs_int8 = int8_t;
using tjs_int16 = int16_t;
using tjs_real = double;
using tjs_uint32 = uint32_t;
using tjs_int32 = int32_t;
using tjs_uint64 = uint64_t;
using tjs_int64 = int64_t;
using tTJSVariant = int64_t;
void TVPConsoleLog(const char*, ...) {}
namespace TJS {
struct tTJSBinaryStream {
    std::vector<uint8_t> data;
    size_t pos = 0;
    uint64_t GetSize() { return data.size(); }
    uint64_t GetPosition() { return pos; }
    void SetPosition(uint64_t p) { pos = p; }
    size_t Read(void* out, size_t n) {
        n = pos > data.size() ? 0 : std::min(n, data.size() - pos);
        if (n) std::memcpy(out, data.data() + pos, n);
        pos += n;
        return n;
    }
    void ReadBuffer(void* out, size_t n) {
        if (Read(out, n) != n) throw std::runtime_error("truncated stream");
    }
    uint8_t ReadI8LE() { uint8_t n; ReadBuffer(&n, 1); return n; }
    uint32_t ReadI32LE() {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n |= uint32_t(ReadI8LE()) << (8 * i);
        return n;
    }
};
}
struct Reader {
    TJS::tTJSBinaryStream* filePtr;
    struct { uint32_t offsetNames = 0; } _header;
    std::vector<uint32_t> nameIndexes;
    std::vector<std::string> namesCache;
};
struct tTJSNI_PsbFile : Reader {
    void refreshListInfo(std::vector<uint32_t>*, std::vector<uint32_t>*);
    int64_t number();
    void names();
};
struct emotefile : Reader {
    void refreshListInfo(std::vector<uint32_t>*, std::vector<uint32_t>*);
    int64_t number();
    void names();
    bool parseNumber(int64_t&, uint32_t);
    bool parseReal(double&, uint32_t);
    bool parseString(std::string&, uint32_t) { throw std::runtime_error("unexpected string conversion"); }
};
