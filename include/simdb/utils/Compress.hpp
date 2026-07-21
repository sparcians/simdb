// <Compress.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/Exceptions.hpp"
#include <cstring>
#include <vector>
#include <zlib.h>

namespace simdb {

/// Compression modes of operation.
enum class CompressionModes { COMPRESSED, UNCOMPRESSED };

/// Compression levels for zlib.
enum class CompressionLevel {
    DISABLED = Z_NO_COMPRESSION,
    DEFAULT = Z_DEFAULT_COMPRESSION,
    FASTEST = Z_BEST_SPEED,
    HIGHEST = Z_BEST_COMPRESSION
};

/// Perform zlib compression.
inline void compressData(const void* data_ptr, size_t num_bytes, std::vector<char>& out, const int compression_level)
{
    if (num_bytes == 0)
    {
        out.clear();
        return;
    }

    z_stream defstream{};
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;

    defstream.avail_in = (uInt)num_bytes;
    defstream.next_in = (Bytef*)data_ptr;

    // Compression can technically result in a larger output, although it is not
    // likely except for possibly very small input vectors. There is no
    // deterministic value for the maximum number of bytes after decompression,
    // but we can choose a very safe minimum.
    auto max_bytes_after = num_bytes * 2;
    if (max_bytes_after < 1000)
    {
        max_bytes_after = 1000;
    }
    out.resize(max_bytes_after);

    defstream.avail_out = (uInt)(out.size());
    defstream.next_out = (Bytef*)(out.data());

    deflateInit(&defstream, compression_level);
    deflate(&defstream, Z_FINISH);
    deflateEnd(&defstream);

    auto num_bytes_after = (int)defstream.total_out;
    out.resize(num_bytes_after);
}

/// Perform zlib compression.
inline void compressData(const void* data_ptr, size_t num_bytes, std::vector<char>& out,
                         CompressionLevel compression_level = CompressionLevel::DEFAULT)
{
    compressData(data_ptr, num_bytes, out, static_cast<int>(compression_level));
}

/// Perform zlib compression on a vector.
template <typename T>
inline void compressData(const std::vector<T>& in, std::vector<char>& out, const int compression_level)
{
    const void* data_ptr = in.data();
    size_t num_bytes = in.size() * sizeof(T);
    compressData(data_ptr, num_bytes, out, compression_level);
}

/// Perform zlib compression on a vector.
template <typename T>
inline void compressData(const std::vector<T>& in, std::vector<char>& out,
                         CompressionLevel compression_level = CompressionLevel::DEFAULT)
{
    compressData(in, out, static_cast<int>(compression_level));
}

/// Perform zlib decompression.
template <typename T> inline void decompressData(const std::vector<char>& in, std::vector<T>& out)
{
    if (in.empty())
    {
        out.clear();
        return;
    }

    constexpr size_t CHUNK_SIZE = 1024 * 64;
    std::vector<char> decompressed_buffer;
    decompressed_buffer.reserve(CHUNK_SIZE);

    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    stream.avail_in = static_cast<uInt>(in.size());

    simdb_assert(inflateInit(&stream) == Z_OK, "Failed to initialize zlib inflate stream.");

    do
    {
        size_t current_size = decompressed_buffer.size();
        decompressed_buffer.resize(current_size + CHUNK_SIZE);
        stream.next_out = reinterpret_cast<Bytef*>(&decompressed_buffer[current_size]);
        stream.avail_out = CHUNK_SIZE;

        int ret = inflate(&stream, Z_NO_FLUSH);

        if (ret != Z_OK && ret != Z_STREAM_END)
        {
            inflateEnd(&stream);
        }
        simdb_assert(ret == Z_OK || ret == Z_STREAM_END,
                     "Decompression failed with zlib error code: " + std::to_string(ret));
    } while (stream.avail_out == 0);

    // Adjust actual used size
    size_t current_size = decompressed_buffer.size();
    decompressed_buffer.resize(current_size - stream.avail_out);
    inflateEnd(&stream);

    // Convert decompressed bytes to std::vector<T>
    size_t byte_count = decompressed_buffer.size();
    simdb_assert(byte_count % sizeof(T) == 0, "Decompressed data size is not aligned with type T.");

    size_t elem_count = byte_count / sizeof(T);
    out.resize(elem_count);
    std::memcpy(out.data(), decompressed_buffer.data(), byte_count);
}

} // namespace simdb
