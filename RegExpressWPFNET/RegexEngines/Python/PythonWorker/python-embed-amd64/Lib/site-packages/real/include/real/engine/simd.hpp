/*!
 * \file simd.hpp
 * \brief A uniform 16-lane mask interface over two ISAs (NEON, SSE2), so the decision/loop logic that
 *        consumes it (pike.hpp) is written ONCE, with no `#if` ISA branch of its own.
 *
 * `mask_t` is opaque and per-ISA (a nibble-packed `std::uint64_t` on NEON, a bit-packed
 * `std::uint32_t` on SSE2) — the loop that drives it never touches the raw bits, only the primitives
 * below, so nothing in pike.hpp needs to know which packing is behind a given build. Two "load" entry
 * points build a mask from 16 already-loaded bytes (a small first-byte set, or a homogeneous <= 2-range
 * class); the rest — `empty`, `first_lane`, `clear_first`, `window_all_set`, `first_clear_lane`,
 * `next_set_lane` — are what a block-scan-then-verify loop needs and nothing more.
 *
 * Every function here is either intrinsics-only or a few bit ops over an opaque scalar — no eligibility
 * decision, no candidate/skip loop, no memcpy of the SUBJECT text (the caller owns that, MISRA-clean).
 * That split is deliberate: the loop logic in pike.hpp is the same C++ on every ISA and is exercised by
 * the ordinary test suite regardless of which leg compiled; the mask primitives here are, by
 * construction, ISA-exclusive (the NEON body never compiles on x86 and vice versa), so a single-ISA CI
 * runner can never line-cover both — see the Makefile's `COV_FLOOR_IGNORE` for this file (guarded
 * instead by sanitize, the fuzz corpus, the correctness nets in test_quantifiers, and — the practical
 * proof — the twin ISA's own coverage of the identical contract).
 */
#ifndef REAL_SIMD_HPP
#define REAL_SIMD_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "real/version.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON)
#  include <arm_neon.h>  // NEON 16-byte membership masks (aarch64 floor)
#elif defined(__SSE2__)
#  include <emmintrin.h> // SSE2 16-byte membership masks (x86-64 floor)
#endif

namespace real::detail {

#if defined(__ARM_NEON)

  //! \brief Opaque 16-lane mask: nibble-packed (4 bits/lane, 0xF = set / 0x0 = clear — the vshrn
  //!        narrowing shift NEON produces from a byte compare has no cheaper 1-bit/lane form).
  using mask_t = std::uint64_t;

  //! \brief Mask of \p buf16 against up to 8 single-byte members (the alternation first-byte set,
  //!        `pattern_hints::small_set`).
  //! \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
  //! \param[in] members The candidate bytes, \p count of them valid.
  //! \param[in] count   Number of valid \p members (1..8).
  inline mask_t load_members_mask(const std::uint8_t * buf16,
                                  const std::uint8_t * members,
                                  std::size_t          count)
  {
    const uint8x16_t  blk {vld1q_u8(buf16)};
    uint8x16_t        eq  {vdupq_n_u8(0)};
    for (std::size_t i = 0; i < count; ++i) {
      eq = vorrq_u8(eq, vceqq_u8(blk, vdupq_n_u8(members[i])));
    }
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  //! \brief Mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
  //!        (prefilter.hpp's `class_range_count`).
  //! \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
  //! \param[in] lo0    Lower bound of the first range.
  //! \param[in] hi0    Upper bound of the first range.
  //! \param[in] lo1    Lower bound of the second range (`lo1 > hi1` encodes "no second range").
  //! \param[in] hi1    Upper bound of the second range.
  inline mask_t load_range_mask(const std::uint8_t * buf16,
                                std::uint8_t         lo0,
                                std::uint8_t         hi0,
                                std::uint8_t         lo1,
                                std::uint8_t         hi1)
  {
    const uint8x16_t blk    {vld1q_u8(buf16)};
    const uint8x16_t lo0v   {vdupq_n_u8(lo0)};
    const uint8x16_t hi0v   {vdupq_n_u8(hi0)};
    const uint8x16_t lo1v   {vdupq_n_u8(lo1)};
    const uint8x16_t hi1v   {vdupq_n_u8(hi1)};
    const uint8x16_t below0 {vcltq_u8(blk, lo0v)};
    const uint8x16_t above0 {vcgtq_u8(blk, hi0v)};
    const uint8x16_t below1 {vcltq_u8(blk, lo1v)};
    const uint8x16_t above1 {vcgtq_u8(blk, hi1v)};
    const uint8x16_t out0   {vorrq_u8(below0, above0)}; // not in range0
    const uint8x16_t out1   {vorrq_u8(below1, above1)}; // not in range1 (always true when absent)
    const uint8x16_t bad    {vandq_u8(out0, out1)};     // fails both -- this lane mismatches
    const uint8x16_t good   {vmvnq_u8(bad)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(good), 4)), 0);
  }

  //! \brief `true` if no lane of \p m is set.
  inline bool empty(mask_t m)
  {
    return m == 0U;
  }

  //! \brief Index (0..15) of the first set lane of \p m. UB if \ref empty(m).
  inline std::size_t first_lane(mask_t m)
  {
    return static_cast<std::size_t>(std::countr_zero(m)) >> 2U;
  }

  //! \brief \p m with its first set lane cleared.
  inline mask_t clear_first(mask_t m)
  {
    const std::size_t lane {first_lane(m)};
    return m & ~(static_cast<mask_t>(0xF) << (4U * lane));
  }

  //! \brief `true` if every lane in `[start, start + len)` of \p m is set (`len` clamped to 16 - start
  //!        by the caller; a `len` reaching lane 16 is treated as the mask's natural width).
  inline bool window_all_set(mask_t      m,
                             std::size_t start,
                             std::size_t len)
  {
    const mask_t want {len >= 16 ? ~mask_t {0} : ((mask_t {1} << (4U * len)) - 1U)};
    return ((m >> (4U * start)) & want) == want;
  }

  //! \brief Index (0..15, absolute — not relative to \p start) of the first CLEAR lane in
  //!        `[start, start + len)` of \p m. UB if `window_all_set(m, start, len)`.
  inline std::size_t first_clear_lane(mask_t      m,
                                      std::size_t start,
                                      std::size_t len)
  {
    const mask_t want  {len >= 16 ? ~mask_t {0} : ((mask_t {1} << (4U * len)) - 1U)};
    const mask_t fails {(~(m >> (4U * start))) & want};
    return start + (static_cast<std::size_t>(std::countr_zero(fails)) >> 2U);
  }

  //! \brief Index (0..15) of the first set lane of \p m at or after \p from, or 16 if none.
  inline std::size_t next_set_lane(mask_t      m,
                                   std::size_t from)
  {
    if (from >= 16U) {
      return 16U;
    }
    const mask_t shifted {m >> (4U * from)};
    return shifted == 0U ? 16U : from + (static_cast<std::size_t>(std::countr_zero(shifted)) >> 2U);
  }

#elif defined(__SSE2__)

  //! \brief Opaque 16-lane mask: bit-packed (1 bit/lane — `_mm_movemask_epi8` gives this natively).
  using mask_t = std::uint32_t;

  //! \brief Mask of \p buf16 against up to 8 single-byte members (the alternation first-byte set,
  //!        `pattern_hints::small_set`). SSE2 leg of the NEON overload above.
  //! \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
  //! \param[in] members The candidate bytes, \p count of them valid.
  //! \param[in] count   Number of valid \p members (1..8).
  inline mask_t load_members_mask(const std::uint8_t * buf16,
                                  const std::uint8_t * members,
                                  std::size_t          count)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    __m128i eq  {_mm_setzero_si128()};
    for (std::size_t i = 0; i < count; ++i) {
      eq = _mm_or_si128(eq, _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))));
    }
    return static_cast<mask_t>(_mm_movemask_epi8(eq));
  }

  //! \brief Mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
  //!        (prefilter.hpp's `class_range_count`). SSE2 leg of the NEON overload above.
  //! \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
  //! \param[in] lo0    Lower bound of the first range.
  //! \param[in] hi0    Upper bound of the first range.
  //! \param[in] lo1    Lower bound of the second range (`lo1 > hi1` encodes "no second range").
  //! \param[in] hi1    Upper bound of the second range.
  inline mask_t load_range_mask(const std::uint8_t * buf16,
                                std::uint8_t         lo0,
                                std::uint8_t         hi0,
                                std::uint8_t         lo1,
                                std::uint8_t         hi1)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    // SSE2 has no unsigned byte compare: bias both operands by XOR 0x80 first (an exact bijection
    // [0,255] -> [-128,127]) so signed cmplt/cmpgt on the biased values match the unsigned order.
    const __m128i bias   {_mm_set1_epi8(static_cast<char>(0x80))};
    blk = _mm_xor_si128(blk, bias);
    const __m128i lo0v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(lo0)), bias)};
    const __m128i hi0v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(hi0)), bias)};
    const __m128i lo1v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(lo1)), bias)};
    const __m128i hi1v   {_mm_xor_si128(_mm_set1_epi8(static_cast<char>(hi1)), bias)};
    const __m128i below0 {_mm_cmplt_epi8(blk, lo0v)};
    const __m128i above0 {_mm_cmpgt_epi8(blk, hi0v)};
    const __m128i below1 {_mm_cmplt_epi8(blk, lo1v)};
    const __m128i above1 {_mm_cmpgt_epi8(blk, hi1v)};
    const __m128i out0   {_mm_or_si128(below0, above0)};
    const __m128i out1   {_mm_or_si128(below1, above1)};
    const __m128i bad    {_mm_and_si128(out0, out1)};
    return (~static_cast<mask_t>(_mm_movemask_epi8(bad))) & 0xFFFFU;
  }

  //! \brief `true` if no lane of \p m is set.
  inline bool empty(mask_t m)
  {
    return m == 0U;
  }

  //! \brief Index (0..15) of the first set lane of \p m. UB if \ref empty(m).
  inline std::size_t first_lane(mask_t m)
  {
    return static_cast<std::size_t>(std::countr_zero(m));
  }

  //! \brief \p m with its first set lane cleared.
  inline mask_t clear_first(mask_t m)
  {
    return m & (m - 1U);
  }

  //! \brief `true` if every lane in `[start, start + len)` of \p m is set.
  inline bool window_all_set(mask_t      m,
                             std::size_t start,
                             std::size_t len)
  {
    const mask_t want {len >= 16 ? 0xFFFFU : ((mask_t {1} << len) - 1U)};
    return ((m >> start) & want) == want;
  }

  //! \brief Index (0..15, absolute — not relative to \p start) of the first CLEAR lane in
  //!        `[start, start + len)` of \p m. UB if `window_all_set(m, start, len)`.
  inline std::size_t first_clear_lane(mask_t      m,
                                      std::size_t start,
                                      std::size_t len)
  {
    const mask_t want  {len >= 16 ? 0xFFFFU : ((mask_t {1} << len) - 1U)};
    const mask_t fails {(~(m >> start)) & want};
    return start + static_cast<std::size_t>(std::countr_zero(fails));
  }

  //! \brief Index (0..15) of the first set lane of \p m at or after \p from, or 16 if none.
  inline std::size_t next_set_lane(mask_t      m,
                                   std::size_t from)
  {
    if (from >= 16U) {
      return 16U;
    }
    const mask_t shifted {m >> from};
    return shifted == 0U ? 16U : from + static_cast<std::size_t>(std::countr_zero(shifted));
  }

#endif
} // namespace real::detail

#endif // REAL_SIMD_HPP
