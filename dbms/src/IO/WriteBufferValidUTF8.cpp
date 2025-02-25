#include <Core/Types.h>
#include <IO/WriteBufferValidUTF8.h>
#include <Poco/UTF8Encoding.h>

#if __SSE2__
#include <emmintrin.h>
#endif


namespace DB
{
const size_t WriteBufferValidUTF8::DEFAULT_SIZE = 4096;

namespace
{
// clang-format off
/** Index into the table below with the first byte of a UTF-8 sequence to
 * get the number of trailing bytes that are supposed to follow it.
 * Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
 * left as-is for anyone who may want to do such conversion, which was
 * allowed in earlier algorithms.
 */
const UInt8 length_of_utf8_sequence[256] =
{
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,5,5,5,5,6,6,6,6
};
// clang-format on
} // namespace


WriteBufferValidUTF8::WriteBufferValidUTF8(
    WriteBuffer & output_buffer,
    bool group_replacements,
    const char * replacement,
    size_t size)
    : BufferWithOwnMemory<WriteBuffer>(std::max(static_cast<size_t>(32), size))
    , output_buffer(output_buffer)
    , group_replacements(group_replacements)
    , replacement(replacement)
{
}


inline void WriteBufferValidUTF8::putReplacement()
{
    if (replacement.empty() || (group_replacements && just_put_replacement))
        return;

    just_put_replacement = true;
    output_buffer.write(replacement.data(), replacement.size());
}


inline void WriteBufferValidUTF8::putValid(char * data, size_t len)
{
    if (len == 0)
        return;

    just_put_replacement = false;
    output_buffer.write(data, len);
}


void WriteBufferValidUTF8::nextImpl()
{
    char * p = memory.data();
    char * valid_start = p;

    while (p < pos)
    {
#if __SSE2__
        /// Fast skip of ASCII
        static constexpr size_t SIMD_BYTES = 16;
        const char * simd_end = p + (pos - p) / SIMD_BYTES * SIMD_BYTES;

        while (p < simd_end && !_mm_movemask_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(p))))
            p += SIMD_BYTES;

        if (!(p < pos))
            break;
#endif

        size_t len = length_of_utf8_sequence[static_cast<unsigned char>(*p)];

        if (len > 4)
        {
            /// Invalid start of sequence. Skip one byte.
            putValid(valid_start, p - valid_start);
            putReplacement();
            ++p;
            valid_start = p;
        }
        else if (p + len > pos)
        {
            /// Sequence was not fully written to this buffer.
            break;
        }
        else if (Poco::UTF8Encoding::isLegal(reinterpret_cast<unsigned char *>(p), len))
        {
            /// Valid sequence.
            p += len;
        }
        else
        {
            /// Invalid sequence. Skip just first byte.
            putValid(valid_start, p - valid_start);
            putReplacement();
            ++p;
            valid_start = p;
        }
    }

    putValid(valid_start, p - valid_start);

    size_t cnt = pos - p;

    /// Shift unfinished sequence to start of buffer.
    for (size_t i = 0; i < cnt; ++i)
        memory[i] = p[i];

    working_buffer = Buffer(&memory[cnt], &memory[0] + memory.size());
}


void WriteBufferValidUTF8::finish()
{
    /// Write all complete sequences from buffer.
    nextImpl();

    /// If unfinished sequence at end, then write replacement.
    if (working_buffer.begin() != memory.data())
        putReplacement();
}

} // namespace DB
