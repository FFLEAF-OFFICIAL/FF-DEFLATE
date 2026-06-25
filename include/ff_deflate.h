#ifndef FF_DEFLATE_H
#define FF_DEFLATE_H

/*
 * FF-DEFLATE
 * Copyright (c) 2026 FF-LEAF. All rights reserved.
 */

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace ffdeflate {

#define FF_DEFLATE_VERSION_MAJOR 0
#define FF_DEFLATE_VERSION_MINOR 1
#define FF_DEFLATE_VERSION_PATCH 0
#define FF_DEFLATE_VERSION_STRING "0.1.0"

enum Status {
    FF_OK = 0,
    FF_BAD_PARAM,
    FF_OUTPUT_FULL,
    FF_INPUT_CORRUPT,
    FF_UNSUPPORTED,
    FF_IO_ERROR
};

static const size_t FF_WINDOW_SIZE = 32768u;
static const size_t FF_HASH_SIZE = 32768u;

struct TDeflState {
    uint32_t hash_head[FF_HASH_SIZE];
    uint32_t prev[FF_WINDOW_SIZE];
};

struct TInflState {
    uint16_t counts[2][16];
    uint16_t symbols[2][320];
    uint8_t lengths[320];
};

void tdefl_init(TDeflState* state);
size_t tdefl_compress_bound(size_t source_size);

Status tdefl_compress_raw(TDeflState* state,
                          const uint8_t* source,
                          size_t source_size,
                          uint8_t* dest,
                          size_t* dest_size);

Status tdefl_compress_zlib(TDeflState* state,
                           const uint8_t* source,
                           size_t source_size,
                           uint8_t* dest,
                           size_t* dest_size);

void tinfl_init(TInflState* state);

Status tinfl_decompress_raw(TInflState* state,
                            const uint8_t* source,
                            size_t source_size,
                            uint8_t* dest,
                            size_t* dest_size);

Status tinfl_decompress_zlib(TInflState* state,
                             const uint8_t* source,
                             size_t source_size,
                             uint8_t* dest,
                             size_t* dest_size);

Status inflate_raw(TInflState* state,
                   const uint8_t* source,
                   size_t source_size,
                   uint8_t* dest,
                   size_t* dest_size);

Status inflate_zlib(TInflState* state,
                    const uint8_t* source,
                    size_t source_size,
                    uint8_t* dest,
                    size_t* dest_size);

uint32_t adler32(const uint8_t* data, size_t size);
uint32_t crc32(const uint8_t* data, size_t size);

struct ZipEntry {
    std::string name;
    std::vector<uint8_t> data;
};

Status zip_write(const char* path, const ZipEntry* entries, size_t entry_count);
Status zip_read(const char* path, std::vector<ZipEntry>& entries);
Status zip_append(const char* path, const ZipEntry* entries, size_t entry_count);

Status png_write_rgba8(const char* path,
                       uint32_t width,
                       uint32_t height,
                       const uint8_t* rgba);

const char* status_string(Status status);

}  // namespace ffdeflate

#endif  // FF_DEFLATE_H
