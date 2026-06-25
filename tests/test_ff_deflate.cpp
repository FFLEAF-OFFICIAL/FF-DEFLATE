/*
 * FF-DEFLATE
 * Copyright (c) 2026 FF-LEAF. All rights reserved.
 */

#include "ff_deflate.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>

static volatile size_t g_new_calls = 0;

void* operator new(size_t n) {
    ++g_new_calls;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void* operator new[](size_t n) {
    ++g_new_calls;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

static bool check(ffdeflate::Status st, const char* what) {
    if (st != ffdeflate::FF_OK) {
        std::cerr << what << " failed: " << ffdeflate::status_string(st) << "\n";
        return false;
    }
    return true;
}

int main() {
    using namespace ffdeflate;

    static uint8_t input[262144];
    static uint8_t compressed[320000];
    static uint8_t decompressed[262144];

    const char* seed =
        "FF-DEFLATE is a fast pure C++ lossless data compression library. "
        "The low-level tdefl and tinfl APIs use caller-owned state only. ";
    size_t seed_len = std::strlen(seed);
    for (size_t i = 0; i < sizeof(input); ++i) {
        input[i] = static_cast<uint8_t>(seed[(i + (i >> 7)) % seed_len] ^ (i & 7));
    }

    TDeflState def;
    TInflState infl;
    size_t comp_size = sizeof(compressed);
    size_t before_new = g_new_calls;
    Status st = tdefl_compress_zlib(&def, input, sizeof(input), compressed, &comp_size);
    size_t after_compress_new = g_new_calls;
    if (!check(st, "tdefl_compress_zlib")) return 1;

    size_t out_size = sizeof(decompressed);
    st = tinfl_decompress_zlib(&infl, compressed, comp_size, decompressed, &out_size);
    size_t after_inflate_new = g_new_calls;
    if (!check(st, "tinfl_decompress_zlib")) return 1;

    if (out_size != sizeof(input) || std::memcmp(input, decompressed, sizeof(input)) != 0) {
        std::cerr << "round trip mismatch\n";
        return 1;
    }

    out_size = sizeof(decompressed);
    st = inflate_zlib(&infl, compressed, comp_size, decompressed, &out_size);
    size_t after_alias_inflate_new = g_new_calls;
    if (!check(st, "inflate_zlib")) return 1;
    if (out_size != sizeof(input) || std::memcmp(input, decompressed, sizeof(input)) != 0) {
        std::cerr << "inflate_zlib mismatch\n";
        return 1;
    }

    if (after_compress_new != before_new ||
        after_inflate_new != before_new ||
        after_alias_inflate_new != before_new) {
        std::cerr << "low-level API allocated on heap\n";
        return 1;
    }

    std::cout << "zlib round trip OK\n";
    std::cout << "inflate_zlib alias OK\n";
    std::cout << "low-level heap allocations: 0\n";
    std::cout << "input bytes: " << sizeof(input) << "\n";
    std::cout << "compressed bytes: " << comp_size << "\n";

    ZipEntry entries[2];
    entries[0].name = "hello.txt";
    entries[0].data.assign(seed, seed + seed_len);
    entries[1].name = "payload.bin";
    entries[1].data.assign(input, input + 4096);

    st = zip_write("ff_deflate_test.zip", entries, 2);
    if (!check(st, "zip_write")) return 1;

    ZipEntry extra;
    extra.name = "appended.txt";
    const char* app = "Appended with FF-DEFLATE ZIP64 writer.";
    extra.data.assign(app, app + std::strlen(app));
    st = zip_append("ff_deflate_test.zip", &extra, 1);
    if (!check(st, "zip_append")) return 1;

    std::vector<ZipEntry> loaded;
    st = zip_read("ff_deflate_test.zip", loaded);
    if (!check(st, "zip_read")) return 1;
    if (loaded.size() != 3 || loaded[0].data != entries[0].data ||
        loaded[1].data != entries[1].data || loaded[2].data != extra.data) {
        std::cerr << "zip data mismatch\n";
        return 1;
    }
    std::cout << "ZIP write/read/append OK\n";

    const uint32_t w = 96;
    const uint32_t h = 64;
    std::vector<uint8_t> rgba(w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t p = (static_cast<size_t>(y) * w + x) * 4u;
            rgba[p + 0] = static_cast<uint8_t>((x * 255u) / (w - 1u));
            rgba[p + 1] = static_cast<uint8_t>((y * 255u) / (h - 1u));
            rgba[p + 2] = static_cast<uint8_t>(((x ^ y) * 5u) & 255u);
            rgba[p + 3] = 255;
        }
    }
    st = png_write_rgba8("ff_deflate_test.png", w, h, &rgba[0]);
    if (!check(st, "png_write_rgba8")) return 1;
    std::cout << "PNG write OK\n";

    return 0;
}
