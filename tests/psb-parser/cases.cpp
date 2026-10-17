int failures = 0;
void check(bool ok, const char* label) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
void append(std::vector<uint8_t>& out, uint64_t n, int width) {
    for (int i = 0; i < width; ++i) out.push_back(uint8_t(n >> (i * 8)));
}
template<class R> void compatibility() {
    TJS::tTJSBinaryStream stream;
    R reader; reader.filePtr = &stream;
    for (int width : {3, 5, 6, 7}) {
        int bits = 8 * width;
        for (int64_t value : {-(int64_t(1) << (bits - 1)), -((int64_t(1) << (bits - 2)) + 17),
                              int64_t(-70000), int64_t(-1), int64_t(0), (int64_t(1) << (bits - 1)) - 1}) {
            stream.data = {uint8_t(PSB::NumberN0 + width)};
            append(stream.data, uint64_t(value), width); stream.pos = 0;
            check(reader.number() == value, "readAllObjs signed number");
            emotefile em; em.filePtr = &stream;
            int64_t number = 0; double real = 0;
            check(em.parseNumber(number, 0) && number == value, "parseNumber signed number");
            check(em.parseReal(real, 0) && real == double(value), "parseReal signed number");
        }
    }
    stream.data = {0, 1, 0, 0, 99, 1, 0, 0, 0, 88}; stream.pos = 0;
    std::vector<uint32_t> offsets{0, 5}, names;
    try {
        reader.refreshListInfo(&offsets, &names);
        check(names == std::vector<uint32_t>({256, 1}) && offsets == std::vector<uint32_t>({4, 9}),
              "v1 32-bit names and value offsets");
    } catch (const std::exception&) { check(false, "v1 refreshListInfo must resize"); }
    stream.data = {'a', 0, 'b', 0}; stream.pos = 0;
    reader.nameIndexes = {0, 2}; reader.names();
    check(reader.namesCache.size() == 2 && reader.namesCache[0].c_str() == std::string("a") &&
          reader.namesCache[1].c_str() == std::string("b"), "v1 loads reserved names");
}
int main(int argc, char**) {
    if (argc == 1) {
        for (int width = 1; width <= 8; ++width) {
            TJS::tTJSBinaryStream stream;
            append(stream.data, 2, width); stream.data.push_back(PSB::NumberN8 + 4);
            append(stream.data, 0xFEDCBA98, 4); append(stream.data, 7, 4);
            std::vector<uint32_t> out{42};
            check(PSB::parsePSBArray(&out, width, &stream) && out == std::vector<uint32_t>({42, 0xFEDCBA98, 7}),
                  "array count widths 1..8 and high-bit entries");
        }
        for (int width = 5; width <= 8; ++width) {
            TJS::tTJSBinaryStream stream;
            stream.data = {1, uint8_t(PSB::NumberN8 + width)};
            append(stream.data, 0xAABBCCDD12345678ULL, width);
            std::vector<uint32_t> out;
            check(PSB::parsePSBArray(&out, 1, &stream) && out == std::vector<uint32_t>({0x12345678}) &&
                  stream.pos == stream.data.size(), "wide entries consumed without oversized shifts");
        }
        for (auto bytes : std::vector<std::vector<uint8_t>>{{2, 13, 1}, {1, 12, 1}, {1, 21, 1}}) {
            TJS::tTJSBinaryStream stream{bytes}; std::vector<uint32_t> out{42};
            check(!PSB::parsePSBArray(&out, 1, &stream) && out == std::vector<uint32_t>({42}),
                  "reject truncated payload or invalid entry width before appending");
        }
        TJS::tTJSBinaryStream stream{{0, 0, 0, 0, 1, 0, 0, 0, 13}};
        std::vector<uint32_t> out;
        check(!PSB::parsePSBArray(&out, 8, &stream), "reject oversized 64-bit count");
        check(!PSB::parsePSBArray(&out, 0, &stream) && !PSB::parsePSBArray(&out, 9, &stream),
              "reject invalid count widths");
        stream = {{0, 13}};
        check(PSB::parsePSBArray(&out, 1, &stream) && out.empty(), "empty array");
    }
    compatibility<tTJSNI_PsbFile>(); compatibility<emotefile>();
    std::cout << "PSB parser: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
