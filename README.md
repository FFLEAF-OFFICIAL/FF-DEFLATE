# FF-DEFLATE

FF-DEFLATE is a portable, pure C++ lossless data compression library with a compact low-level DEFLATE API and optional ZIP/PNG helpers.

The low-level `tdefl` and `tinfl` functions use caller-owned state and caller-owned buffers only. They do not allocate from the heap, which makes them suitable for constrained software environments where predictable memory behavior matters.

Developer and GitHub account: `FF-LEAF`

## Features

- Pure C++11, no external runtime dependencies.
- Raw DEFLATE and zlib-wrapped DEFLATE APIs.
- Inflate/decompression APIs through `tinfl_decompress_*` and `inflate_*`.
- Heap-free low-level `tdefl_*` and `tinfl_*` calls.
- Caller-owned compression and decompression state.
- DEFLATE inflater supports stored, fixed-Huffman, and dynamic-Huffman blocks.
- Optional ZIP write, read, and append APIs with ZIP64 records.
- Optional RGBA8 PNG writer.
- MinGW 6.3.0 Makefile and cross-platform CMake build.

## Quick Start

```cpp
#include "ff_deflate.h"

#include <vector>

ffdeflate::TDeflState def;
ffdeflate::TInflState infl;

size_t compressed_size = ffdeflate::tdefl_compress_bound(input_size);
std::vector<uint8_t> compressed(compressed_size);

ffdeflate::Status st = ffdeflate::tdefl_compress_zlib(
    &def, input, input_size, &compressed[0], &compressed_size);

compressed.resize(compressed_size);

std::vector<uint8_t> restored(input_size);
size_t restored_size = restored.size();
st = ffdeflate::tinfl_decompress_zlib(
    &infl, &compressed[0], compressed.size(), &restored[0], &restored_size);
```

You can also call the clearer inflate wrapper:

```cpp
st = ffdeflate::inflate_zlib(
    &infl, &compressed[0], compressed.size(), &restored[0], &restored_size);
```

For heap-free use, provide static, stack, or externally managed buffers instead of `std::vector`.

## Build

### MinGW

The included Makefile defaults to:

```text
C:\MinGW\bin\g++.exe
```

Build and test:

```sh
mingw32-make
mingw32-make test
```

To use another compiler:

```sh
mingw32-make CXX=g++ AR=ar
```

### CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Build options:

- `FF_DEFLATE_BUILD_TESTS=ON`
- `FF_DEFLATE_BUILD_EXAMPLES=ON`

## Repository Layout

```text
include/        Public API
src/            Library implementation
tests/          Round-trip, ZIP, PNG, and heap-allocation checks
examples/       Small usage examples
docs/           API notes
.github/        CI, issue templates, and PR template
```

## Low-Level API Contract

These functions are intended to remain heap-allocation free:

- `tdefl_compress_raw`
- `tdefl_compress_zlib`
- `tinfl_decompress_raw`
- `tinfl_decompress_zlib`

The optional ZIP and PNG convenience APIs may use `std::vector`, `std::string`, and file streams.

## Test Coverage

The test executable checks:

- zlib compression/decompression round trip.
- `inflate_zlib` wrapper behavior.
- zero heap allocations during low-level compression and decompression.
- ZIP write/read/append behavior.
- PNG file generation.
- ZIP compatibility with standard readers can be verified externally.

## Version

Current version: `0.1.0`

Public version macros:

- `FF_DEFLATE_VERSION_MAJOR`
- `FF_DEFLATE_VERSION_MINOR`
- `FF_DEFLATE_VERSION_PATCH`
- `FF_DEFLATE_VERSION_STRING`

## License

Copyright (c) 2026 FF-LEAF. All rights reserved.

This project is proprietary source. See [LICENSE](LICENSE).
