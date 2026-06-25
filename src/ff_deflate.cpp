/*
 * FF-DEFLATE
 * Copyright (c) 2026 FF-LEAF. All rights reserved.
 */

#include "ff_deflate.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string.h>

namespace ffdeflate {
namespace {

static const uint32_t NIL = 0xffffffffu;
static const int MAX_BITS = 15;

static const uint16_t LENGTH_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const uint8_t LENGTH_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const uint16_t DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
    6145, 8193, 12289, 16385, 24577
};

static const uint8_t DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

uint16_t reverse_bits(uint16_t value, int bits) {
    uint16_t out = 0;
    for (int i = 0; i < bits; ++i) {
        out = static_cast<uint16_t>((out << 1) | (value & 1u));
        value >>= 1;
    }
    return out;
}

uint32_t hash3(const uint8_t* p) {
    uint32_t v = (static_cast<uint32_t>(p[0]) << 16) ^
                 (static_cast<uint32_t>(p[1]) << 8) ^
                 static_cast<uint32_t>(p[2]);
    v *= 2654435761u;
    return (v >> 17) & (FF_HASH_SIZE - 1u);
}

struct BitWriter {
    uint8_t* out;
    size_t cap;
    size_t pos;
    uint64_t bits;
    int bit_count;
    bool overflow;

    void init(uint8_t* dest, size_t dest_cap) {
        out = dest;
        cap = dest_cap;
        pos = 0;
        bits = 0;
        bit_count = 0;
        overflow = false;
    }

    void put(uint32_t value, int count) {
        if (overflow) return;
        bits |= (static_cast<uint64_t>(value) << bit_count);
        bit_count += count;
        while (bit_count >= 8) {
            if (pos >= cap) {
                overflow = true;
                return;
            }
            out[pos++] = static_cast<uint8_t>(bits & 0xffu);
            bits >>= 8;
            bit_count -= 8;
        }
    }

    void flush_byte() {
        if (bit_count > 0) put(0, 8 - bit_count);
    }
};

struct BitReader {
    const uint8_t* in;
    size_t size;
    size_t pos;
    uint64_t bits;
    int bit_count;
    bool bad;

    void init(const uint8_t* source, size_t source_size) {
        in = source;
        size = source_size;
        pos = 0;
        bits = 0;
        bit_count = 0;
        bad = false;
    }

    uint32_t get(int count) {
        while (bit_count < count) {
            if (pos >= size) {
                bad = true;
                return 0;
            }
            bits |= static_cast<uint64_t>(in[pos++]) << bit_count;
            bit_count += 8;
        }
        uint32_t out = static_cast<uint32_t>(bits & ((1u << count) - 1u));
        bits >>= count;
        bit_count -= count;
        return out;
    }

    void align_byte() {
        bits = 0;
        bit_count = 0;
    }
};

void fixed_code(int symbol, uint16_t& code, int& bits) {
    if (symbol <= 143) {
        bits = 8;
        code = reverse_bits(static_cast<uint16_t>(0x30 + symbol), bits);
    } else if (symbol <= 255) {
        bits = 9;
        code = reverse_bits(static_cast<uint16_t>(0x190 + symbol - 144), bits);
    } else if (symbol <= 279) {
        bits = 7;
        code = reverse_bits(static_cast<uint16_t>(symbol - 256), bits);
    } else {
        bits = 8;
        code = reverse_bits(static_cast<uint16_t>(0xc0 + symbol - 280), bits);
    }
}

void emit_fixed_symbol(BitWriter& bw, int symbol) {
    uint16_t code = 0;
    int bits = 0;
    fixed_code(symbol, code, bits);
    bw.put(code, bits);
}

int length_code(int len) {
    if (len == 258) return 28;
    for (int i = 0; i < 28; ++i) {
        int next = LENGTH_BASE[i + 1];
        if (len < next) return i;
    }
    return 28;
}

int dist_code(int dist) {
    for (int i = 0; i < 29; ++i) {
        if (dist < DIST_BASE[i + 1]) return i;
    }
    return 29;
}

void emit_match(BitWriter& bw, int len, int dist) {
    int lc = length_code(len);
    emit_fixed_symbol(bw, 257 + lc);
    if (LENGTH_EXTRA[lc]) bw.put(static_cast<uint32_t>(len - LENGTH_BASE[lc]), LENGTH_EXTRA[lc]);

    int dc = dist_code(dist);
    bw.put(reverse_bits(static_cast<uint16_t>(dc), 5), 5);
    if (DIST_EXTRA[dc]) bw.put(static_cast<uint32_t>(dist - DIST_BASE[dc]), DIST_EXTRA[dc]);
}

void insert_hash(TDeflState* s, const uint8_t* data, size_t size, uint32_t pos) {
    if (pos + 2 >= size) return;
    uint32_t h = hash3(data + pos);
    s->prev[pos & (FF_WINDOW_SIZE - 1u)] = s->hash_head[h];
    s->hash_head[h] = pos;
}

void find_match(TDeflState* s,
                const uint8_t* data,
                size_t size,
                uint32_t pos,
                int& best_len,
                int& best_dist) {
    best_len = 0;
    best_dist = 0;
    if (pos + 3 > size) return;

    uint32_t cur = s->hash_head[hash3(data + pos)];
    int chain = 128;
    const int nice_len = 128;
    while (cur != NIL && chain-- > 0) {
        if (cur >= pos) break;
        uint32_t dist = pos - cur;
        if (dist > FF_WINDOW_SIZE) break;

        if (data[cur + best_len] == data[pos + best_len] &&
            data[cur] == data[pos] &&
            data[cur + 1] == data[pos + 1] &&
            data[cur + 2] == data[pos + 2]) {
            int len = 3;
            int max_len = static_cast<int>(std::min<size_t>(258, size - pos));
            while (len < max_len && data[cur + len] == data[pos + len]) ++len;
            if (len > best_len) {
                best_len = len;
                best_dist = static_cast<int>(dist);
                if (len >= nice_len) break;
            }
        }
        cur = s->prev[cur & (FF_WINDOW_SIZE - 1u)];
    }
}

struct Huffman {
    uint16_t* count;
    uint16_t* symbol;
};

Status build_huffman(Huffman h, const uint8_t* lengths, int n) {
    uint16_t offsets[16];
    for (int i = 0; i <= MAX_BITS; ++i) h.count[i] = 0;
    for (int i = 0; i < n; ++i) {
        if (lengths[i] > MAX_BITS) return FF_INPUT_CORRUPT;
        ++h.count[lengths[i]];
    }
    if (h.count[0] == n) return FF_INPUT_CORRUPT;

    int left = 1;
    for (int len = 1; len <= MAX_BITS; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return FF_INPUT_CORRUPT;
    }

    offsets[1] = 0;
    for (int len = 1; len < MAX_BITS; ++len) {
        offsets[len + 1] = static_cast<uint16_t>(offsets[len] + h.count[len]);
    }

    for (int sym = 0; sym < n; ++sym) {
        uint8_t len = lengths[sym];
        if (len != 0) h.symbol[offsets[len]++] = static_cast<uint16_t>(sym);
    }
    return FF_OK;
}

int decode_symbol(BitReader& br, Huffman h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= MAX_BITS; ++len) {
        code |= static_cast<int>(br.get(1));
        int count = h.count[len];
        if (code < first + count) {
            return h.symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    br.bad = true;
    return -1;
}

void make_fixed_lengths(uint8_t* litlen, uint8_t* dist) {
    for (int i = 0; i <= 143; ++i) litlen[i] = 8;
    for (int i = 144; i <= 255; ++i) litlen[i] = 9;
    for (int i = 256; i <= 279; ++i) litlen[i] = 7;
    for (int i = 280; i <= 287; ++i) litlen[i] = 8;
    for (int i = 0; i < 32; ++i) dist[i] = 5;
}

Status inflate_codes(TInflState* s,
                     BitReader& br,
                     Huffman lit,
                     Huffman dist,
                     uint8_t* out,
                     size_t out_cap,
                     size_t& out_pos) {
    for (;;) {
        int sym = decode_symbol(br, lit);
        if (br.bad || sym < 0) return FF_INPUT_CORRUPT;
        if (sym < 256) {
            if (out_pos >= out_cap) return FF_OUTPUT_FULL;
            out[out_pos++] = static_cast<uint8_t>(sym);
        } else if (sym == 256) {
            return FF_OK;
        } else if (sym <= 285) {
            int lc = sym - 257;
            int len = LENGTH_BASE[lc] + static_cast<int>(br.get(LENGTH_EXTRA[lc]));
            int dsym = decode_symbol(br, dist);
            if (br.bad || dsym < 0 || dsym >= 30) return FF_INPUT_CORRUPT;
            int distance = DIST_BASE[dsym] + static_cast<int>(br.get(DIST_EXTRA[dsym]));
            if (distance <= 0 || static_cast<size_t>(distance) > out_pos) return FF_INPUT_CORRUPT;
            if (out_pos + static_cast<size_t>(len) > out_cap) return FF_OUTPUT_FULL;
            for (int i = 0; i < len; ++i) {
                out[out_pos] = out[out_pos - static_cast<size_t>(distance)];
                ++out_pos;
            }
        } else {
            return FF_INPUT_CORRUPT;
        }
        (void)s;
    }
}

Status inflate_dynamic(TInflState* s,
                       BitReader& br,
                       uint8_t* out,
                       size_t out_cap,
                       size_t& out_pos) {
    static const uint8_t ORDER[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    int hlit = static_cast<int>(br.get(5)) + 257;
    int hdist = static_cast<int>(br.get(5)) + 1;
    int hclen = static_cast<int>(br.get(4)) + 4;
    if (br.bad || hlit > 286 || hdist > 32) return FF_INPUT_CORRUPT;

    uint8_t clen[19];
    for (int i = 0; i < 19; ++i) clen[i] = 0;
    for (int i = 0; i < hclen; ++i) clen[ORDER[i]] = static_cast<uint8_t>(br.get(3));

    Huffman code_len = {s->counts[0], s->symbols[0]};
    Status st = build_huffman(code_len, clen, 19);
    if (st != FF_OK) return st;

    int total = hlit + hdist;
    int idx = 0;
    while (idx < total) {
        int sym = decode_symbol(br, code_len);
        if (br.bad || sym < 0) return FF_INPUT_CORRUPT;
        if (sym <= 15) {
            s->lengths[idx++] = static_cast<uint8_t>(sym);
        } else if (sym == 16) {
            if (idx == 0) return FF_INPUT_CORRUPT;
            int rep = 3 + static_cast<int>(br.get(2));
            uint8_t prev = s->lengths[idx - 1];
            while (rep-- && idx < total) s->lengths[idx++] = prev;
        } else if (sym == 17) {
            int rep = 3 + static_cast<int>(br.get(3));
            while (rep-- && idx < total) s->lengths[idx++] = 0;
        } else if (sym == 18) {
            int rep = 11 + static_cast<int>(br.get(7));
            while (rep-- && idx < total) s->lengths[idx++] = 0;
        } else {
            return FF_INPUT_CORRUPT;
        }
        if (br.bad) return FF_INPUT_CORRUPT;
    }
    if (idx != total) return FF_INPUT_CORRUPT;

    Huffman lit = {s->counts[0], s->symbols[0]};
    Huffman dist = {s->counts[1], s->symbols[1]};
    st = build_huffman(lit, s->lengths, hlit);
    if (st != FF_OK) return st;
    st = build_huffman(dist, s->lengths + hlit, hdist);
    if (st != FF_OK) return st;
    return inflate_codes(s, br, lit, dist, out, out_cap, out_pos);
}

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xffu));
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xffu));
}

void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xffu));
}

uint16_t read16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read64(const uint8_t* p) {
    uint64_t out = 0;
    for (int i = 7; i >= 0; --i) out = (out << 8) | p[i];
    return out;
}

bool read_file(const char* path, std::vector<uint8_t>& data) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz < 0) return false;
    f.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(sz));
    if (!data.empty()) f.read(reinterpret_cast<char*>(&data[0]), sz);
    return f.good() || f.eof();
}

bool write_file(const char* path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(&data[0]), static_cast<std::streamsize>(data.size()));
    return f.good();
}

void put_png32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xffu));
    v.push_back(static_cast<uint8_t>(x & 0xffu));
}

void png_chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    put_png32(png, static_cast<uint32_t>(data.size()));
    size_t start = png.size();
    png.push_back(static_cast<uint8_t>(type[0]));
    png.push_back(static_cast<uint8_t>(type[1]));
    png.push_back(static_cast<uint8_t>(type[2]));
    png.push_back(static_cast<uint8_t>(type[3]));
    png.insert(png.end(), data.begin(), data.end());
    uint32_t crc = crc32(&png[start], png.size() - start);
    put_png32(png, crc);
}

}  // namespace

void tdefl_init(TDeflState* state) {
    if (!state) return;
    for (size_t i = 0; i < FF_HASH_SIZE; ++i) state->hash_head[i] = NIL;
    for (size_t i = 0; i < FF_WINDOW_SIZE; ++i) state->prev[i] = NIL;
}

size_t tdefl_compress_bound(size_t source_size) {
    return source_size + (source_size / 8u) + 1024u + 16u;
}

Status tdefl_compress_raw(TDeflState* state,
                          const uint8_t* source,
                          size_t source_size,
                          uint8_t* dest,
                          size_t* dest_size) {
    if (!state || (!source && source_size) || !dest || !dest_size) return FF_BAD_PARAM;

    BitWriter bw;
    bw.init(dest, *dest_size);
    tdefl_init(state);
    bw.put(1, 1);
    bw.put(1, 2);

    uint32_t pos = 0;
    while (pos < source_size) {
        int best_len = 0;
        int best_dist = 0;
        find_match(state, source, source_size, pos, best_len, best_dist);
        if (best_len >= 3) {
            emit_match(bw, best_len, best_dist);
            for (int j = 0; j < best_len; ++j) {
                insert_hash(state, source, source_size, pos + static_cast<uint32_t>(j));
            }
            pos += static_cast<uint32_t>(best_len);
        } else {
            emit_fixed_symbol(bw, source[pos]);
            insert_hash(state, source, source_size, pos);
            ++pos;
        }
        if (bw.overflow) return FF_OUTPUT_FULL;
    }

    emit_fixed_symbol(bw, 256);
    bw.flush_byte();
    if (bw.overflow) return FF_OUTPUT_FULL;
    *dest_size = bw.pos;
    return FF_OK;
}

Status tdefl_compress_zlib(TDeflState* state,
                           const uint8_t* source,
                           size_t source_size,
                           uint8_t* dest,
                           size_t* dest_size) {
    if (!dest_size || *dest_size < 6) return FF_OUTPUT_FULL;
    if (!dest) return FF_BAD_PARAM;
    dest[0] = 0x78;
    dest[1] = 0x01;
    size_t raw_cap = *dest_size - 6;
    Status st = tdefl_compress_raw(state, source, source_size, dest + 2, &raw_cap);
    if (st != FF_OK) return st;
    uint32_t ad = adler32(source, source_size);
    size_t p = 2 + raw_cap;
    if (p + 4 > *dest_size) return FF_OUTPUT_FULL;
    dest[p++] = static_cast<uint8_t>((ad >> 24) & 0xffu);
    dest[p++] = static_cast<uint8_t>((ad >> 16) & 0xffu);
    dest[p++] = static_cast<uint8_t>((ad >> 8) & 0xffu);
    dest[p++] = static_cast<uint8_t>(ad & 0xffu);
    *dest_size = p;
    return FF_OK;
}

void tinfl_init(TInflState* state) {
    if (state) memset(state, 0, sizeof(*state));
}

Status tinfl_decompress_raw(TInflState* state,
                            const uint8_t* source,
                            size_t source_size,
                            uint8_t* dest,
                            size_t* dest_size) {
    if (!state || (!source && source_size) || !dest || !dest_size) return FF_BAD_PARAM;

    tinfl_init(state);
    BitReader br;
    br.init(source, source_size);
    size_t out_pos = 0;
    bool final = false;
    while (!final) {
        final = br.get(1) != 0;
        int type = static_cast<int>(br.get(2));
        if (br.bad) return FF_INPUT_CORRUPT;

        if (type == 0) {
            br.align_byte();
            uint16_t len = static_cast<uint16_t>(br.get(16));
            uint16_t nlen = static_cast<uint16_t>(br.get(16));
            if (br.bad || static_cast<uint16_t>(len ^ 0xffffu) != nlen) return FF_INPUT_CORRUPT;
            if (out_pos + len > *dest_size) return FF_OUTPUT_FULL;
            for (uint16_t i = 0; i < len; ++i) dest[out_pos++] = static_cast<uint8_t>(br.get(8));
            if (br.bad) return FF_INPUT_CORRUPT;
        } else if (type == 1) {
            uint8_t litlen[288];
            uint8_t distlen[32];
            make_fixed_lengths(litlen, distlen);
            Huffman lit = {state->counts[0], state->symbols[0]};
            Huffman dist = {state->counts[1], state->symbols[1]};
            Status st = build_huffman(lit, litlen, 288);
            if (st != FF_OK) return st;
            st = build_huffman(dist, distlen, 32);
            if (st != FF_OK) return st;
            st = inflate_codes(state, br, lit, dist, dest, *dest_size, out_pos);
            if (st != FF_OK) return st;
        } else if (type == 2) {
            Status st = inflate_dynamic(state, br, dest, *dest_size, out_pos);
            if (st != FF_OK) return st;
        } else {
            return FF_INPUT_CORRUPT;
        }
    }
    *dest_size = out_pos;
    return FF_OK;
}

Status tinfl_decompress_zlib(TInflState* state,
                             const uint8_t* source,
                             size_t source_size,
                             uint8_t* dest,
                             size_t* dest_size) {
    if (!source || source_size < 6 || !dest_size) return FF_BAD_PARAM;
    uint8_t cmf = source[0];
    uint8_t flg = source[1];
    if ((cmf & 0x0f) != 8 || ((static_cast<uint16_t>(cmf) << 8) + flg) % 31 != 0) return FF_INPUT_CORRUPT;
    if (flg & 0x20) return FF_UNSUPPORTED;
    size_t raw_size = source_size - 6;
    Status st = tinfl_decompress_raw(state, source + 2, raw_size, dest, dest_size);
    if (st != FF_OK) return st;
    uint32_t got = (static_cast<uint32_t>(source[source_size - 4]) << 24) |
                   (static_cast<uint32_t>(source[source_size - 3]) << 16) |
                   (static_cast<uint32_t>(source[source_size - 2]) << 8) |
                   static_cast<uint32_t>(source[source_size - 1]);
    if (got != adler32(dest, *dest_size)) return FF_INPUT_CORRUPT;
    return FF_OK;
}

Status inflate_raw(TInflState* state,
                   const uint8_t* source,
                   size_t source_size,
                   uint8_t* dest,
                   size_t* dest_size) {
    return tinfl_decompress_raw(state, source, source_size, dest, dest_size);
}

Status inflate_zlib(TInflState* state,
                    const uint8_t* source,
                    size_t source_size,
                    uint8_t* dest,
                    size_t* dest_size) {
    return tinfl_decompress_zlib(state, source, source_size, dest, dest_size);
}

uint32_t adler32(const uint8_t* data, size_t size) {
    const uint32_t MOD = 65521u;
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < size; ++i) {
        a += data[i];
        b += a;
        if ((i & 4095u) == 4095u) {
            a %= MOD;
            b %= MOD;
        }
    }
    a %= MOD;
    b %= MOD;
    return (b << 16) | a;
}

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

Status zip_write(const char* path, const ZipEntry* entries, size_t entry_count) {
    if (!path || (!entries && entry_count)) return FF_BAD_PARAM;

    struct Central {
        std::string name;
        uint32_t crc;
        uint64_t comp_size;
        uint64_t uncomp_size;
        uint64_t offset;
    };

    std::vector<uint8_t> zip;
    std::vector<Central> central;
    central.reserve(entry_count);

    for (size_t i = 0; i < entry_count; ++i) {
        const ZipEntry& e = entries[i];
        if (e.name.empty() || e.name.size() > 65535u) return FF_BAD_PARAM;
        TDeflState def;
        size_t cap = tdefl_compress_bound(e.data.size());
        std::vector<uint8_t> comp(cap);
        Status st = tdefl_compress_raw(&def,
                                       e.data.empty() ? 0 : &e.data[0],
                                       e.data.size(),
                                       comp.empty() ? 0 : &comp[0],
                                       &cap);
        if (st != FF_OK) return st;
        comp.resize(cap);

        Central c;
        c.name = e.name;
        c.crc = crc32(e.data.empty() ? 0 : &e.data[0], e.data.size());
        c.comp_size = comp.size();
        c.uncomp_size = e.data.size();
        c.offset = zip.size();
        central.push_back(c);

        put32(zip, 0x04034b50u);
        put16(zip, 45);
        put16(zip, 0x0800);
        put16(zip, 8);
        put16(zip, 0);
        put16(zip, 0);
        put32(zip, c.crc);
        put32(zip, 0xffffffffu);
        put32(zip, 0xffffffffu);
        put16(zip, static_cast<uint16_t>(e.name.size()));
        put16(zip, 20);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        put16(zip, 0x0001);
        put16(zip, 16);
        put64(zip, c.uncomp_size);
        put64(zip, c.comp_size);
        zip.insert(zip.end(), comp.begin(), comp.end());
    }

    uint64_t cd_offset = zip.size();
    for (size_t i = 0; i < central.size(); ++i) {
        const Central& c = central[i];
        put32(zip, 0x02014b50u);
        put16(zip, 45);
        put16(zip, 45);
        put16(zip, 0x0800);
        put16(zip, 8);
        put16(zip, 0);
        put16(zip, 0);
        put32(zip, c.crc);
        put32(zip, 0xffffffffu);
        put32(zip, 0xffffffffu);
        put16(zip, static_cast<uint16_t>(c.name.size()));
        put16(zip, 28);
        put16(zip, 0);
        put16(zip, 0);
        put16(zip, 0);
        put32(zip, 0);
        put32(zip, 0xffffffffu);
        zip.insert(zip.end(), c.name.begin(), c.name.end());
        put16(zip, 0x0001);
        put16(zip, 24);
        put64(zip, c.uncomp_size);
        put64(zip, c.comp_size);
        put64(zip, c.offset);
    }
    uint64_t cd_size = zip.size() - cd_offset;
    uint64_t zip64_eocd_offset = zip.size();

    put32(zip, 0x06064b50u);
    put64(zip, 44);
    put16(zip, 45);
    put16(zip, 45);
    put32(zip, 0);
    put32(zip, 0);
    put64(zip, central.size());
    put64(zip, central.size());
    put64(zip, cd_size);
    put64(zip, cd_offset);

    put32(zip, 0x07064b50u);
    put32(zip, 0);
    put64(zip, zip64_eocd_offset);
    put32(zip, 1);

    put32(zip, 0x06054b50u);
    put16(zip, 0);
    put16(zip, 0);
    put16(zip, 0xffffu);
    put16(zip, 0xffffu);
    put32(zip, 0xffffffffu);
    put32(zip, 0xffffffffu);
    put16(zip, 0);

    return write_file(path, zip) ? FF_OK : FF_IO_ERROR;
}

Status zip_read(const char* path, std::vector<ZipEntry>& entries) {
    if (!path) return FF_BAD_PARAM;
    std::vector<uint8_t> zip;
    if (!read_file(path, zip)) return FF_IO_ERROR;
    if (zip.size() < 22) return FF_INPUT_CORRUPT;

    size_t eocd = std::numeric_limits<size_t>::max();
    size_t min_pos = zip.size() > 66000u ? zip.size() - 66000u : 0u;
    for (size_t p = zip.size() - 22; p + 4 <= zip.size() && p >= min_pos; --p) {
        if (read32(&zip[p]) == 0x06054b50u) {
            eocd = p;
            break;
        }
        if (p == 0) break;
    }
    if (eocd == std::numeric_limits<size_t>::max()) return FF_INPUT_CORRUPT;

    uint64_t entry_count = read16(&zip[eocd + 10]);
    uint64_t cd_size = read32(&zip[eocd + 12]);
    uint64_t cd_offset = read32(&zip[eocd + 16]);
    if ((entry_count == 0xffffu || cd_size == 0xffffffffu || cd_offset == 0xffffffffu) && eocd >= 20) {
        size_t loc = eocd - 20;
        if (read32(&zip[loc]) != 0x07064b50u) return FF_INPUT_CORRUPT;
        uint64_t zip64_off = read64(&zip[loc + 8]);
        if (zip64_off + 56 > zip.size() || read32(&zip[static_cast<size_t>(zip64_off)]) != 0x06064b50u) return FF_INPUT_CORRUPT;
        const uint8_t* z64 = &zip[static_cast<size_t>(zip64_off)];
        entry_count = read64(z64 + 32);
        cd_size = read64(z64 + 40);
        cd_offset = read64(z64 + 48);
    }
    if (cd_offset + cd_size > zip.size()) return FF_INPUT_CORRUPT;

    entries.clear();
    size_t p = static_cast<size_t>(cd_offset);
    for (uint64_t i = 0; i < entry_count; ++i) {
        if (p + 46 > zip.size() || read32(&zip[p]) != 0x02014b50u) return FF_INPUT_CORRUPT;
        uint16_t method = read16(&zip[p + 10]);
        uint32_t crc = read32(&zip[p + 16]);
        uint64_t comp_size = read32(&zip[p + 20]);
        uint64_t uncomp_size = read32(&zip[p + 24]);
        uint16_t name_len = read16(&zip[p + 28]);
        uint16_t extra_len = read16(&zip[p + 30]);
        uint16_t comment_len = read16(&zip[p + 32]);
        uint64_t local_off = read32(&zip[p + 42]);
        if (p + 46u + name_len + extra_len + comment_len > zip.size()) return FF_INPUT_CORRUPT;
        std::string name(reinterpret_cast<const char*>(&zip[p + 46]), name_len);
        const uint8_t* extra = &zip[p + 46 + name_len];
        size_t ep = 0;
        while (ep + 4 <= extra_len) {
            uint16_t id = read16(extra + ep);
            uint16_t sz = read16(extra + ep + 2);
            ep += 4;
            if (ep + sz > extra_len) return FF_INPUT_CORRUPT;
            if (id == 0x0001) {
                size_t zp = ep;
                if (uncomp_size == 0xffffffffu && zp + 8 <= ep + sz) {
                    uncomp_size = read64(extra + zp);
                    zp += 8;
                }
                if (comp_size == 0xffffffffu && zp + 8 <= ep + sz) {
                    comp_size = read64(extra + zp);
                    zp += 8;
                }
                if (local_off == 0xffffffffu && zp + 8 <= ep + sz) {
                    local_off = read64(extra + zp);
                }
            }
            ep += sz;
        }

        if (local_off + 30 > zip.size() || read32(&zip[static_cast<size_t>(local_off)]) != 0x04034b50u) return FF_INPUT_CORRUPT;
        const uint8_t* lh = &zip[static_cast<size_t>(local_off)];
        uint16_t lname = read16(lh + 26);
        uint16_t lextra = read16(lh + 28);
        uint64_t data_off = local_off + 30u + lname + lextra;
        if (data_off + comp_size > zip.size()) return FF_INPUT_CORRUPT;

        ZipEntry out;
        out.name = name;
        out.data.resize(static_cast<size_t>(uncomp_size));
        if (method == 0) {
            if (comp_size != uncomp_size) return FF_INPUT_CORRUPT;
            if (uncomp_size) memcpy(&out.data[0], &zip[static_cast<size_t>(data_off)], static_cast<size_t>(uncomp_size));
        } else if (method == 8) {
            TInflState infl;
            size_t out_size = out.data.size();
            Status st = tinfl_decompress_raw(&infl,
                                             &zip[static_cast<size_t>(data_off)],
                                             static_cast<size_t>(comp_size),
                                             out.data.empty() ? 0 : &out.data[0],
                                             &out_size);
            if (st != FF_OK || out_size != uncomp_size) return st == FF_OK ? FF_INPUT_CORRUPT : st;
        } else {
            return FF_UNSUPPORTED;
        }
        if (crc32(out.data.empty() ? 0 : &out.data[0], out.data.size()) != crc) return FF_INPUT_CORRUPT;
        entries.push_back(out);
        p += 46u + name_len + extra_len + comment_len;
    }
    return FF_OK;
}

Status zip_append(const char* path, const ZipEntry* entries, size_t entry_count) {
    if (!path || (!entries && entry_count)) return FF_BAD_PARAM;
    std::vector<ZipEntry> all;
    std::ifstream probe(path, std::ios::binary);
    if (probe.good()) {
        Status st = zip_read(path, all);
        if (st != FF_OK) return st;
    }
    for (size_t i = 0; i < entry_count; ++i) all.push_back(entries[i]);
    return zip_write(path, all.empty() ? 0 : &all[0], all.size());
}

Status png_write_rgba8(const char* path,
                       uint32_t width,
                       uint32_t height,
                       const uint8_t* rgba) {
    if (!path || !rgba || width == 0 || height == 0) return FF_BAD_PARAM;
    uint64_t raw_size64 = (static_cast<uint64_t>(width) * 4u + 1u) * height;
    if (raw_size64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return FF_BAD_PARAM;
    size_t raw_size = static_cast<size_t>(raw_size64);
    std::vector<uint8_t> raw(raw_size);
    size_t stride = static_cast<size_t>(width) * 4u;
    for (uint32_t y = 0; y < height; ++y) {
        raw[static_cast<size_t>(y) * (stride + 1u)] = 0;
        memcpy(&raw[static_cast<size_t>(y) * (stride + 1u) + 1u],
               rgba + static_cast<size_t>(y) * stride,
               stride);
    }

    TDeflState def;
    size_t comp_size = tdefl_compress_bound(raw.size());
    std::vector<uint8_t> comp(comp_size);
    Status st = tdefl_compress_zlib(&def, raw.empty() ? 0 : &raw[0], raw.size(), &comp[0], &comp_size);
    if (st != FF_OK) return st;
    comp.resize(comp_size);

    std::vector<uint8_t> png;
    static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), SIG, SIG + 8);

    std::vector<uint8_t> ihdr;
    put_png32(ihdr, width);
    put_png32(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    png_chunk(png, "IHDR", ihdr);
    png_chunk(png, "IDAT", comp);
    std::vector<uint8_t> empty;
    png_chunk(png, "IEND", empty);

    return write_file(path, png) ? FF_OK : FF_IO_ERROR;
}

const char* status_string(Status status) {
    switch (status) {
        case FF_OK: return "FF_OK";
        case FF_BAD_PARAM: return "FF_BAD_PARAM";
        case FF_OUTPUT_FULL: return "FF_OUTPUT_FULL";
        case FF_INPUT_CORRUPT: return "FF_INPUT_CORRUPT";
        case FF_UNSUPPORTED: return "FF_UNSUPPORTED";
        case FF_IO_ERROR: return "FF_IO_ERROR";
        default: return "FF_UNKNOWN";
    }
}

}  // namespace ffdeflate
