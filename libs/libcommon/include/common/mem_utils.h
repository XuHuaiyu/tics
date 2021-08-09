#pragma once
#include <common/simd.h>
#include <common/unaligned.h>

#include <cstddef>
#include <cstdint>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace mem_utils
{
namespace _detail
{

using ConstBytePtr = char const *;

/// @attention one should not use these loop functions directly in the first place,
/// they do not check CPU feature and only compare bytes up to the multiple of vector size.
/// Use `memoryEqual` as the entrance instead.

#ifdef TIFLASH_ENABLE_AVX_SUPPORT
bool memoryEqualAVX2x4Loop(ConstBytePtr & p1, ConstBytePtr & p2, size_t & size);
#endif

#ifdef TIFLASH_ENABLE_AVX512_SUPPORT
bool memoryEqualAVX512x4Loop(ConstBytePtr & p1, ConstBytePtr & p2, size_t & size);
#endif

#ifdef TIFLASH_ENABLE_ASIMD_SUPPORT
__attribute__((pure)) bool memoryEqualASIMD(ConstBytePtr p1, ConstBytePtr p2, size_t size);
#endif

#if defined(__SSE2__)

/** Compare strings for equality.
  * The approach is controversial and does not win in all cases.
  * For more information, see hash_map_string_2.cpp
  */

// clang-format off
__attribute__((always_inline, pure)) inline bool memoryEqualSSE2Fixed(const char * p1, const char * p2)
{
    return 0xFFFF == _mm_movemask_epi8(_mm_cmpeq_epi8(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1)),
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2))));
}

__attribute__((always_inline, pure)) inline bool memoryEqualSSE2x4Fixed(const char * p1, const char * p2)
{
    return 0xFFFF == _mm_movemask_epi8(
        _mm_and_si128(
            _mm_and_si128(
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2))),
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1) + 1),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2) + 1))),
            _mm_and_si128(
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1) + 2),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2) + 2)),
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1) + 3),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2) + 3)))));
}

__attribute__((always_inline, pure)) inline bool memoryEqualSSE2(const char * p1, const char * p2, size_t size)
{
    while (size >= 64)
    {
        if (memoryEqualSSE2x4Fixed(p1, p2))
        {
            p1 += 64;
            p2 += 64;
            size -= 64;
        }
        else
            return false;
    }

    switch ((size % 64) / 16)
    {
        case 3: if (!memoryEqualSSE2Fixed(p1 + 32, p2 + 32)) return false; [[fallthrough]];
        case 2: if (!memoryEqualSSE2Fixed(p1 + 16, p2 + 16)) return false; [[fallthrough]];
        case 1: if (!memoryEqualSSE2Fixed(p1     , p2     )) return false; [[fallthrough]];
        case 0: break;
    }

    p1 += (size % 64) / 16 * 16;
    p2 += (size % 64) / 16 * 16;

    switch (size % 16)
    {
        case 15: if (p1[14] != p2[14]) return false; [[fallthrough]];
        case 14: if (p1[13] != p2[13]) return false; [[fallthrough]];
        case 13: if (p1[12] != p2[12]) return false; [[fallthrough]];
        case 12: if (unalignedLoad<uint32_t>(p1 + 8) == unalignedLoad<uint32_t>(p2 + 8)) goto l8; else return false;
        case 11: if (p1[10] != p2[10]) return false; [[fallthrough]];
        case 10: if (p1[9] != p2[9]) return false; [[fallthrough]];
        case 9:  if (p1[8] != p2[8]) return false;
        l8: [[fallthrough]];
        case 8:  return unalignedLoad<uint64_t>(p1) == unalignedLoad<uint64_t>(p2);
        case 7:  if (p1[6] != p2[6]) return false; [[fallthrough]];
        case 6:  if (p1[5] != p2[5]) return false; [[fallthrough]];
        case 5:  if (p1[4] != p2[4]) return false; [[fallthrough]];
        case 4:  return unalignedLoad<uint32_t>(p1) == unalignedLoad<uint32_t>(p2);
        case 3:  if (p1[2] != p2[2]) return false; [[fallthrough]];
        case 2:  return unalignedLoad<uint16_t>(p1) == unalignedLoad<uint16_t>(p2);
        case 1:  if (p1[0] != p2[0]) return false; [[fallthrough]];
        case 0:  break;
    }

    return true;
}
// clang-format on
#endif
} // namespace _detail

/// compare two memory area.
/// this function tries to utilize runtime available vectorization technology.
/// it performs better than `std::memcmp`, especially for those OS with a
/// relatively old libc.
__attribute__((always_inline, pure)) inline bool memoryEqual(const char * p1, const char * p2, size_t size) noexcept
{
    if (p1 == p2)
        return true;

    do
    {
        using namespace simd_option;

#ifdef TIFLASH_ENABLE_ASIMD_SUPPORT
        // for ASIMD target, it is a little bit different because all the compare function is defined in a
        // separate file other than the main loop itself.
        if (ENABLE_ASIMD && SIMDRuntimeSupport(SIMDFeature::asimd))
        {
            return Detail::memoryEqualASIMD(p1, p2, size);
        }
#endif

#ifdef TIFLASH_ENABLE_AVX512_SUPPORT
        if (ENABLE_AVX512 && SIMDRuntimeSupport(SIMDFeature::avx512f) && SIMDRuntimeSupport(SIMDFeature::avx512vl))
        {
            if (!_detail::memoryEqualAVX512x4Loop(p1, p2, size))
            {
                return false;
            }
            break;
        }
#endif
#ifdef TIFLASH_ENABLE_AVX_SUPPORT
        if (ENABLE_AVX && SIMDRuntimeSupport(SIMDFeature::avx2))
        {
            if (!_detail::memoryEqualAVX2x4Loop(p1, p2, size))
            {
                return false;
            }
            break;
        }
#endif
    } while (false);
#if defined(__SSE2__)
    return _detail::memoryEqualSSE2(p1, p2, size);
#else
    return 0 == memcmp(p1, p2, size);
#endif
}

} // namespace mem_utils
