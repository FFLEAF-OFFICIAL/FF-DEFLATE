# Contributing

Thanks for helping improve FF-DEFLATE, maintained by FF-LEAF.

## Development Setup

Build with the MinGW Makefile:

```sh
mingw32-make
mingw32-make test
```

Or build with CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Low-Level API Rule

The low-level APIs are intended for constrained environments:

- `tdefl_compress_raw`
- `tdefl_compress_zlib`
- `tinfl_decompress_raw`
- `tinfl_decompress_zlib`

These functions must not allocate from the heap. Use caller-owned state and buffers only.

The optional ZIP and PNG helpers may use standard library containers and file I/O.

## Pull Requests

By submitting a pull request, you agree that FF-LEAF may use, modify, and include your contribution in FF-DEFLATE.

- Keep public API changes small and documented.
- Add or update tests for behavior changes.
- Prefer portable C++11.
- Avoid external runtime dependencies.
