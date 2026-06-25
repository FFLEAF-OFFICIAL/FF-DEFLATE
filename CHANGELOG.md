# Changelog

Copyright (c) 2026 FF-LEAF. All rights reserved.

All notable changes to FF-DEFLATE will be documented in this file.

## 0.1.0 - 2026-06-25

- Initial pure C++ DEFLATE compressor and inflater.
- Added raw DEFLATE and zlib wrapper APIs.
- Added caller-owned low-level state for heap-free `tdefl` and `tinfl` operation.
- Added optional ZIP write, read, and append APIs with ZIP64 records.
- Added optional RGBA8 PNG writer.
- Added MinGW Makefile, CMake build, tests, examples, and GitHub CI.
- Added `inflate_raw` and `inflate_zlib` convenience wrappers for decompression.
