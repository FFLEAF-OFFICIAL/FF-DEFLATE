/*
 * FF-DEFLATE
 * Copyright (c) 2026 FF-LEAF. All rights reserved.
 */

#include "ff_deflate.h"

#include <cstring>
#include <iostream>
#include <vector>

int main() {
    const char* text =
        "FF-DEFLATE compresses and restores data with caller-owned state.";
    const uint8_t* input = reinterpret_cast<const uint8_t*>(text);
    size_t input_size = std::strlen(text);

    ffdeflate::TDeflState def;
    size_t compressed_size = ffdeflate::tdefl_compress_bound(input_size);
    std::vector<uint8_t> compressed(compressed_size);

    ffdeflate::Status st = ffdeflate::tdefl_compress_zlib(
        &def, input, input_size, &compressed[0], &compressed_size);
    if (st != ffdeflate::FF_OK) {
        std::cerr << "compress failed: " << ffdeflate::status_string(st) << "\n";
        return 1;
    }
    compressed.resize(compressed_size);

    ffdeflate::TInflState infl;
    std::vector<uint8_t> restored(input_size);
    size_t restored_size = restored.size();
    st = ffdeflate::tinfl_decompress_zlib(
        &infl, &compressed[0], compressed.size(), &restored[0], &restored_size);
    if (st != ffdeflate::FF_OK) {
        std::cerr << "decompress failed: " << ffdeflate::status_string(st) << "\n";
        return 1;
    }

    std::cout << "input bytes: " << input_size << "\n";
    std::cout << "compressed bytes: " << compressed.size() << "\n";
    std::cout << "restored: " << std::string(restored.begin(), restored.end()) << "\n";
    return 0;
}
