# API Notes

FF-DEFLATE exposes two layers.

## Low-Level Compression

The low-level API uses caller-owned state and caller-owned buffers:

```cpp
ffdeflate::TDeflState def;
size_t out_size = ffdeflate::tdefl_compress_bound(input_size);
ffdeflate::Status st = ffdeflate::tdefl_compress_zlib(
    &def, input, input_size, output, &out_size);
```

The compressor emits fixed-Huffman DEFLATE blocks with LZ77 matches. The caller must provide enough output space. `tdefl_compress_bound` returns a conservative bound.

## Low-Level Decompression

```cpp
ffdeflate::TInflState infl;
size_t restored_size = restored_capacity;
ffdeflate::Status st = ffdeflate::tinfl_decompress_zlib(
    &infl, compressed, compressed_size, restored, &restored_size);
```

The inflater accepts stored, fixed-Huffman, and dynamic-Huffman DEFLATE blocks.

The same operation is also available through clearer wrapper names:

```cpp
ffdeflate::Status st = ffdeflate::inflate_zlib(
    &infl, compressed, compressed_size, restored, &restored_size);
```

`inflate_raw` and `inflate_zlib` are heap-free aliases of the `tinfl_decompress_*` functions.

## Optional Convenience APIs

The ZIP and PNG helpers are intended for normal hosted environments and may use heap-backed standard library containers:

- `zip_write`
- `zip_read`
- `zip_append`
- `png_write_rgba8`

Use only the low-level API in highly constrained environments.

## Status Values

`FF_OK` means success. Other status codes report invalid parameters, full output buffers, corrupt input, unsupported input features, or I/O errors.
