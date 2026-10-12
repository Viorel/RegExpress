/*!
 * \file simd.hpp
 * \brief A uniform 16-lane mask interface over NEON and SSE2, so the loops that consume it (pike.hpp,
 *        prefilter.hpp) carry no `#if` ISA branch of their own.
 *
 * `mask_t` is opaque and per-ISA (nibble-packed `std::uint64_t` on NEON, bit-packed `std::uint32_t` on
 * SSE2): callers touch it only through the lane primitives at the end (`empty`, `first_lane`, `clear_first`,
 * `window_all_set`, `first_clear_lane`, `next_set_lane`). The load functions build masks from
 * member sets, byte ranges, byte pairs and nibble fingerprints, loading bytes by memcpy (MISRA: no pointer
 * type-pun). The x86 SSSE3 and AVX2 functions are built per function (`target`) where the build lacks the
 * extension, and run only after a `cpuid` check; `avx2_literal_scan` is the one whole scan loop here.
 *
 * No runner compiles both the NEON and the x86 bodies, so none line-covers both: hence this file's
 * `COV_FLOOR_IGNORE` in the Makefile, the contract being guarded by sanitize, the fuzz corpus and each
 * ISA's own runs over the identical interface.
 */
#ifndef REAL_SIMD_HPP
#define REAL_SIMD_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#if defined(__ARM_NEON)
#  include <arm_neon.h>    // NEON 16-byte membership masks (aarch64 floor)
#elif defined(__SSE2__)
#  include <emmintrin.h>   // SSE2 16-byte membership masks (x86-64 floor)
#  if defined(__SSSE3__)
#    include <tmmintrin.h> // SSSE3 byte shuffle: the nibble fingerprint, where the build enables it
#  elif defined(__GNUC__) || defined(__clang__)
#    include <cpuid.h>     // whether the running CPU has SSSE3, for the fingerprint chosen at run time
#    include <tmmintrin.h> // SSSE3 byte shuffle, for functions built for it alone
#  endif
#  if defined(__AVX2__) || defined(__GNUC__) || defined(__clang__)
#    include <cpuid.h>     // whether the running CPU has AVX2, for the literal scan chosen at run time
#    include <immintrin.h> // AVX2 32-byte compares: the literal scan, built for it alone where the build lacks it
#  endif
#endif

namespace real::detail {

  //! \brief One byte in all 16 lanes, as plain bytes: not a vector type, so it sits in `std::array` with no
  //!        attribute to drop, and in plans that targets without vectors also carry.
  using byte_splat = std::array<std::uint8_t, 16>;

#if defined(__ARM_NEON)

  using mask_t = std::uint64_t; //!< Opaque 16-lane mask, 4 bits/lane (0xF set, 0x0 clear) — NEON has no movemask, only the narrowing shift a byte compare feeds.

  /*!
   * \brief Mask of \p buf16 against up to 8 single-byte members (the alternation first-byte set,
   *        `pattern_hints::small_set`).
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members The candidate bytes, \p count of them valid.
   * \param[in] count   Number of valid \p members (1..8).
   */
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

  /*!
   * \brief \ref load_members_mask against exactly eight members, unrolled: a caller with fewer repeats one of
   *        them in the unused slots. Unrolled by hand because the compiler decides by the size of the
   *        surrounding function, and in a scan loop its choice halved a six-member set's speed.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Eight member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members8_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    const uint8x16_t blk {vld1q_u8(buf16)};
    const uint8x16_t e01 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[0])), vceqq_u8(blk, vdupq_n_u8(members[1])))};
    const uint8x16_t e23 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[2])), vceqq_u8(blk, vdupq_n_u8(members[3])))};
    const uint8x16_t e45 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[4])), vceqq_u8(blk, vdupq_n_u8(members[5])))};
    const uint8x16_t e67 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[6])), vceqq_u8(blk, vdupq_n_u8(members[7])))};
    const uint8x16_t eq  {vorrq_u8(vorrq_u8(e01, e23), vorrq_u8(e45, e67))};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief \ref load_members8_mask for at most four members: four slots, the same padding rule.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Four member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members4_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    const uint8x16_t blk {vld1q_u8(buf16)};
    const uint8x16_t e01 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[0])), vceqq_u8(blk, vdupq_n_u8(members[1])))};
    const uint8x16_t e23 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[2])), vceqq_u8(blk, vdupq_n_u8(members[3])))};
    const uint8x16_t eq  {vorrq_u8(e01, e23)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief \ref load_members8_mask for at most six members: six slots, the same padding rule.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Six member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members6_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    const uint8x16_t blk {vld1q_u8(buf16)};
    const uint8x16_t e01 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[0])), vceqq_u8(blk, vdupq_n_u8(members[1])))};
    const uint8x16_t e23 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[2])), vceqq_u8(blk, vdupq_n_u8(members[3])))};
    const uint8x16_t e45 {vorrq_u8(vceqq_u8(blk, vdupq_n_u8(members[4])), vceqq_u8(blk, vdupq_n_u8(members[5])))};
    const uint8x16_t eq  {vorrq_u8(vorrq_u8(e01, e23), e45)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief Mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
   *        (prefilter.hpp's `class_range_count`).
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] lo0   Lower bound of the first range.
   * \param[in] hi0   Upper bound of the first range.
   * \param[in] lo1   Lower bound of the second range (`lo1 > hi1` encodes "no second range").
   * \param[in] hi1   Upper bound of the second range.
   */
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

  /*!
   * \brief Mask of the lanes where `buf16_a[l] == a` AND `buf16_b[l] == b` — the two-byte substring
   *        prefilter (prefilter.hpp's `simd_literal_scan`).
   *
   * The two windows are the SAME 16 candidate starts probed at two needle offsets (\p buf16_b shifted by
   * the offset delta), so lane `l` answers "could the needle start at candidate `l`?" for both probes;
   * survivors still get a full verify. Its SSE2 twin below serves the same caller.
   * \param[in] buf16_a 16 already-loaded bytes at the candidate starts (the caller's MISRA-clean memcpy).
   * \param[in] a       The needle byte expected at the first probe offset.
   * \param[in] buf16_b 16 already-loaded bytes at the candidate starts + delta (same, shifted).
   * \param[in] b       The needle byte expected at the second probe offset.
   */
  inline mask_t load_pair_mask(const std::uint8_t * buf16_a,
                               std::uint8_t         a,
                               const std::uint8_t * buf16_b,
                               std::uint8_t         b)
  {
    const uint8x16_t eq_a {vceqq_u8(vld1q_u8(buf16_a), vdupq_n_u8(a))};
    const uint8x16_t eq_b {vceqq_u8(vld1q_u8(buf16_b), vdupq_n_u8(b))};
    const uint8x16_t hit  {vandq_u8(eq_a, eq_b)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(hit), 4)), 0);
  }

  /*!
   * \brief Whether one of the 64 starts at \p lead64 has \p a there and \p b at the matching byte of \p trail64:
   *        four pair compares OR-ed and one horizontal max, the reject test of prefilter.hpp's
   *        `simd_literal_scan`. NEON has no movemask (a narrowing shift and a lane move per mask), so per-block
   *        masks (\ref load_pair_mask) are built only for a round this says holds a candidate.
   * \param[in] lead64  64 already-loaded bytes at the candidate starts (the caller's MISRA-clean memcpy).
   * \param[in] a       The needle byte expected at the first probe offset.
   * \param[in] trail64 64 already-loaded bytes at the candidate starts + delta.
   * \param[in] b       The needle byte expected at the second probe offset.
   * \return True when some start holds both bytes.
   */
  inline bool any_pair64(const std::uint8_t * lead64,
                         std::uint8_t         a,
                         const std::uint8_t * trail64,
                         std::uint8_t         b)
  {
    const uint8x16_t va  {vdupq_n_u8(a)};
    const uint8x16_t vb  {vdupq_n_u8(b)};
    const uint8x16_t h0  {vandq_u8(vceqq_u8(vld1q_u8(lead64), va), vceqq_u8(vld1q_u8(trail64), vb))};
    const uint8x16_t h1  {vandq_u8(vceqq_u8(vld1q_u8(lead64 + 16), va), vceqq_u8(vld1q_u8(trail64 + 16), vb))};
    const uint8x16_t h2  {vandq_u8(vceqq_u8(vld1q_u8(lead64 + 32), va), vceqq_u8(vld1q_u8(trail64 + 32), vb))};
    const uint8x16_t h3  {vandq_u8(vceqq_u8(vld1q_u8(lead64 + 48), va), vceqq_u8(vld1q_u8(trail64 + 48), vb))};
    return vmaxvq_u8(vorrq_u8(vorrq_u8(h0, h1), vorrq_u8(h2, h3))) != 0U;
  }

  /*!
   * \brief Mask of the 16 starts at \p at where some pair `(lead[i] at the start, probe[i] at start + delta[i])`
   *        sits: the pairs' hits OR-ed as vectors and narrowed once, rather than one mask per pair.
   * \param[in] at    The first of the 16 starts; `at + 15 + delta[i]` must be readable for every pair.
   * \param[in] lead  Each pair's first byte, splatted.
   * \param[in] probe Each pair's second byte, splatted.
   * \param[in] delta Each pair's offset between them.
   * \param[in] count How many pairs.
   * \return The mask.
   */
  inline mask_t load_pairs_mask(const char         * at,
                                const byte_splat   * lead,
                                const byte_splat   * probe,
                                const std::uint8_t * delta,
                                std::size_t          count)
  {
    uint8x16_t blk {};
    std::memcpy(&blk, at, 16); // MISRA-clean byte loads (no pointer type-pun)
    uint8x16_t hit {vdupq_n_u8(0)};
    for (std::size_t i = 0; i < count; ++i) {
      uint8x16_t far {};
      std::memcpy(&far, at + delta[i], 16);
      hit = vorrq_u8(hit, vandq_u8(vceqq_u8(blk, vld1q_u8(lead[i].data())), vceqq_u8(far, vld1q_u8(probe[i].data()))));
    }
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(hit), 4)), 0);
  }

#if defined(__aarch64__)
  /*!
   * \brief The fingerprint's six tables, loaded once per scan by \ref load_nibble3_tables.
   */
  struct nibble3_tables
  {
    uint8x16_t lo0; //!< Low nibble to bucket bits, first fingerprint byte.
    uint8x16_t lo1; //!< The same, second byte.
    uint8x16_t lo2; //!< The same, third byte.
    uint8x16_t hi0; //!< High nibble to bucket bits, first fingerprint byte.
    uint8x16_t hi1; //!< The same, second byte.
    uint8x16_t hi2; //!< The same, third byte.
  };

  /*!
   * \brief Loads the fingerprint's six tables for \ref load_nibble3_mask, once per scan: read from the plan
   *        per block, they reload on every block the compiler does not hoist them from.
   * \param[in]  lo  Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in]  hi  The same for the high nibble.
   * \param[out] out The tables in registers.
   */
  inline void load_nibble3_tables(const std::array<std::array<std::uint8_t, 16>, 3>& lo,
                                  const std::array<std::array<std::uint8_t, 16>, 3>& hi,
                                  nibble3_tables&                                    out)
  {
    out.lo0 = vld1q_u8(lo[0].data());
    out.lo1 = vld1q_u8(lo[1].data());
    out.lo2 = vld1q_u8(lo[2].data());
    out.hi0 = vld1q_u8(hi[0].data());
    out.hi1 = vld1q_u8(hi[1].data());
    out.hi2 = vld1q_u8(hi[2].data());
  }

  /*!
   * \brief One fingerprint byte's lookups on 16 starts: the bucket bits both of its nibbles allow.
   * \param[in] blk  The 16 bytes at that byte's offset.
   * \param[in] tlo  Its low-nibble table.
   * \param[in] thi  Its high-nibble table.
   * \return The bucket bits per start.
   */
  inline uint8x16_t nibble_lookup(uint8x16_t blk,
                                  uint8x16_t tlo,
                                  uint8x16_t thi)
  {
    return vandq_u8(vqtbl1q_u8(tlo, vandq_u8(blk, vdupq_n_u8(0x0F))), vqtbl1q_u8(thi, vshrq_n_u8(blk, 4)));
  }

  /*!
   * \brief \ref load_nibble3_mask on tables \ref load_nibble3_tables loaded.
   * \param[in] at The first of the 16 starts; `at + 17` must be readable.
   * \param[in] t  The tables.
   * \return The mask.
   */
  inline mask_t load_nibble3_mask(const char          * at,
                                  const nibble3_tables& t)
  {
    uint8x16_t b0 {};
    uint8x16_t b1 {};
    uint8x16_t b2 {};
    std::memcpy(&b0, at, 16); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&b1, at + 1, 16);
    std::memcpy(&b2, at + 2, 16);
    const uint8x16_t hit    {vandq_u8(vandq_u8(nibble_lookup(b0, t.lo0, t.hi0), nibble_lookup(b1, t.lo1, t.hi1)),
                                      nibble_lookup(b2, t.lo2, t.hi2))};
    const uint8x16_t marked {vtstq_u8(hit, hit)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(marked), 4)), 0);
  }

  /*!
   * \brief The fingerprint's first two lookups of the previous block, which \ref nibble3_step aligns against the
   *        current block's: zero before the first block, so that no start before it is marked.
   */
  struct nibble3_carry
  {
    uint8x16_t r0 {}; //!< Bucket bits the first fingerprint byte allowed, per byte of the previous block.
    uint8x16_t r1 {}; //!< The same for the second.
  };

  /*!
   * \brief A round's four blocks of bucket bits (\ref nibble3_round), named rather than an array: a vector type
   *        as a template argument loses its attributes (a GCC warning).
   */
  struct nibble3_hits
  {
    uint8x16_t h0 {}; //!< The first block's.
    uint8x16_t h1 {}; //!< The second's.
    uint8x16_t h2 {}; //!< The third's.
    uint8x16_t h3 {}; //!< The fourth's.
  };

  /*!
   * \brief One block's bucket bits for the starts `at - 2 .. at + 13`, \p carry advanced: \ref nibble3_step's work
   *        before its mask is narrowed, for \ref nibble3_round to test four at once.
   * \param[in]     at    The block; `at + 15` must be readable.
   * \param[in]     t     The tables.
   * \param[in,out] carry The previous block's first two lookups, replaced by this block's.
   * \return The bucket bits per start.
   */
  inline uint8x16_t nibble3_block(const char          * at,
                                  const nibble3_tables& t,
                                  nibble3_carry&        carry)
  {
    uint8x16_t b         {};
    std::memcpy(&b, at, 16); // MISRA-clean byte load (no pointer type-pun)
    const uint8x16_t r0  {nibble_lookup(b, t.lo0, t.hi0)};
    const uint8x16_t r1  {nibble_lookup(b, t.lo1, t.hi1)};
    const uint8x16_t hit {vandq_u8(nibble_lookup(b, t.lo2, t.hi2), vandq_u8(vextq_u8(carry.r1, r1, 15), vextq_u8(carry.r0, r0, 14)))};
    carry.r0 = r0;
    carry.r1 = r1;
    return hit;
  }

  /*!
   * \brief One block of the fingerprint scan, loaded once: its three lookups run on the same 16 bytes, the
   *        first two shifted by one and two lanes against the previous block's (\p carry), so the mask marks the
   *        starts `at - 2 .. at + 13`, each byte read once (\ref load_nibble3_mask loads three overlapping blocks).
   * \param[in]     at    The block; `at + 15` must be readable.
   * \param[in]     t     The tables.
   * \param[in,out] carry The previous block's first two lookups, replaced by this block's.
   * \return The mask, lane `l` for the start `at - 2 + l`.
   */
  inline mask_t nibble3_step(const char          * at,
                             const nibble3_tables& t,
                             nibble3_carry&        carry)
  {
    const uint8x16_t hit    {nibble3_block(at, t, carry)};
    const uint8x16_t marked {vtstq_u8(hit, hit)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(marked), 4)), 0);
  }

  /*!
   * \brief Four blocks of \ref nibble3_step at \p at, `at + 16`, `at + 32` and `at + 48`, kept as vectors, and
   *        whether any marks a start: one horizontal max for the four (NEON has no movemask; see \ref any_pair64).
   * \param[in]     at    The first block; `at + 63` must be readable.
   * \param[in]     t     The tables.
   * \param[in,out] carry As \ref nibble3_step, across the four.
   * \param[out]    hits  The four blocks' bucket bits, for \ref nibble3_mask_of.
   * \return True when some start of the four blocks is marked.
   */
  inline bool nibble3_round(const char          * at,
                            const nibble3_tables& t,
                            nibble3_carry&        carry,
                            nibble3_hits&         hits)
  {
    hits.h0 = nibble3_block(at, t, carry);
    hits.h1 = nibble3_block(at + 16, t, carry);
    hits.h2 = nibble3_block(at + 32, t, carry);
    hits.h3 = nibble3_block(at + 48, t, carry);
    return vmaxvq_u8(vorrq_u8(vorrq_u8(hits.h0, hits.h1), vorrq_u8(hits.h2, hits.h3))) != 0U;
  }

  /*!
   * \brief The mask of block \p k of a round's bucket bits from \ref nibble3_round.
   * \param[in] hits The round's bucket bits.
   * \param[in] k    The block, 0 to 3.
   * \return The mask, lane `l` for the start `block - 2 + l`.
   */
  inline mask_t nibble3_mask_of(const nibble3_hits& hits,
                                std::size_t         k)
  {
    uint8x16_t hit {hits.h3};
    switch (k) {
      case 0: hit = hits.h0; break;
      case 1: hit = hits.h1; break;
      case 2: hit = hits.h2; break;
      default: break;
    }
    const uint8x16_t marked {vtstq_u8(hit, hit)};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(marked), 4)), 0);
  }

  /*!
   * \brief Mask of the 16 starts at \p at whose three bytes all fall in one bucket's fingerprint: for byte `k`
   *        of a start, a bucket bit is set where both `lo[k][byte & 15]` and `hi[k][byte >> 4]` carry it, and a
   *        start is marked when some bit survives all three bytes. One table lookup per nibble (`tbl`, AArch64
   *        only), so the cost per block does not grow with the branch count.
   * \param[in] at The first of the 16 starts; `at + 17` must be readable.
   * \param[in] lo Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in] hi The same for the high nibble.
   * \return The mask.
   */
  inline mask_t load_nibble3_mask(const char                                       * at,
                                  const std::array<std::array<std::uint8_t, 16>, 3>& lo,
                                  const std::array<std::array<std::uint8_t, 16>, 3>& hi)
  {
    nibble3_tables t {};
    load_nibble3_tables(lo, hi, t);
    return load_nibble3_mask(at, t);
  }

#endif

  /*!
   * \brief Mask of the lanes where `buf16[l] == a` -- the single-byte scan (prefilter.hpp's
   *        `simd_byte_scan`). NEON only: x86-64 keeps the platform `memchr`, whose vectors are wider.
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] a     The byte sought.
   */
  inline mask_t load_byte_mask(const std::uint8_t * buf16,
                               std::uint8_t         a)
  {
    const uint8x16_t eq {vceqq_u8(vld1q_u8(buf16), vdupq_n_u8(a))};
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
  }

  /*!
   * \brief Whether \p a occurs anywhere in the 64 bytes at \p buf64: four compares OR-ed and one horizontal
   *        max, the reject test of prefilter.hpp's `simd_byte_scan`, which builds per-block masks
   *        (\ref load_byte_mask) only on a hit. NEON only, like its caller.
   * \param[in] buf64 64 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] a     The byte sought.
   */
  inline bool any_byte64(const std::uint8_t * buf64,
                         std::uint8_t         a)
  {
    const uint8x16_t needle {vdupq_n_u8(a)};
    const uint8x16_t lo     {vorrq_u8(vceqq_u8(vld1q_u8(buf64), needle), vceqq_u8(vld1q_u8(buf64 + 16), needle))};
    const uint8x16_t hi     {vorrq_u8(vceqq_u8(vld1q_u8(buf64 + 32), needle), vceqq_u8(vld1q_u8(buf64 + 48), needle))};
    return vmaxvq_u8(vorrq_u8(lo, hi)) != 0U;
  }

  inline constexpr unsigned lane_shift {2U};          //!< log2 of the bits a lane takes in \ref mask_t.
  inline constexpr mask_t   full_mask  {~mask_t {0}}; //!< Every one of the 16 lanes set.

  /*! \brief \p m with its first set lane cleared. */
  inline mask_t clear_first(mask_t m)
  {
    return m & ~(static_cast<mask_t>(0xF) << (4U * (static_cast<std::size_t>(std::countr_zero(m)) >> 2U)));
  }

#elif defined(__SSE2__)

  using mask_t = std::uint32_t; //!< Opaque 16-lane mask, 1 bit/lane — the form `_mm_movemask_epi8` yields.

  /*!
   * \brief Mask of \p buf16 against up to 8 single-byte members (the alternation first-byte set,
   *        `pattern_hints::small_set`). SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members The candidate bytes, \p count of them valid.
   * \param[in] count   Number of valid \p members (1..8).
   */
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

  /*!
   * \brief \ref load_members_mask against exactly eight members, unrolled. SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Eight member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members8_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    const auto    eq_at {[&](std::size_t i) { return _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))); }};
    const __m128i e01   {_mm_or_si128(eq_at(0), eq_at(1))};
    const __m128i e23   {_mm_or_si128(eq_at(2), eq_at(3))};
    const __m128i e45   {_mm_or_si128(eq_at(4), eq_at(5))};
    const __m128i e67   {_mm_or_si128(eq_at(6), eq_at(7))};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(e01, e23), _mm_or_si128(e45, e67))));
  }

  /*!
   * \brief \ref load_members8_mask for at most four members. SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Four member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members4_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    const auto    eq_at {[&](std::size_t i) { return _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))); }};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(eq_at(0), eq_at(1)), _mm_or_si128(eq_at(2), eq_at(3)))));
  }

  /*!
   * \brief \ref load_members8_mask for at most six members. SSE2 leg of the NEON overload above.
   * \param[in] buf16   16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] members Six member bytes.
   * \return The 16-lane mask.
   */
  inline mask_t load_members6_mask(const std::uint8_t * buf16,
                                   const std::uint8_t * members)
  {
    __m128i blk {};
    std::memcpy(&blk, buf16, 16); // MISRA-clean byte load (no pointer type-pun)
    const auto    eq_at {[&](std::size_t i) { return _mm_cmpeq_epi8(blk, _mm_set1_epi8(static_cast<char>(members[i]))); }};
    const __m128i e0123 {_mm_or_si128(_mm_or_si128(eq_at(0), eq_at(1)), _mm_or_si128(eq_at(2), eq_at(3)))};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_or_si128(e0123, _mm_or_si128(eq_at(4), eq_at(5)))));
  }

  /*!
   * \brief Mask of \p buf16 against a HOMOGENEOUS fixed-shape's shared <= 2-range set
   *        (prefilter.hpp's `class_range_count`). SSE2 leg of the NEON overload above.
   * \param[in] buf16 16 already-loaded bytes (the caller's MISRA-clean memcpy).
   * \param[in] lo0   Lower bound of the first range.
   * \param[in] hi0   Upper bound of the first range.
   * \param[in] lo1   Lower bound of the second range (`lo1 > hi1` encodes "no second range").
   * \param[in] hi1   Upper bound of the second range.
   */
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

  /*!
   * \brief The pairs' mask of 16 starts, OR-ed as vectors with one movemask. SSE2 leg of the NEON overload above;
   *        the splats come pre-built, as splatting costs four instructions per byte without SSSE3's shuffle.
   * \param[in] at    The first of the 16 starts; `at + 15 + delta[i]` must be readable for every pair.
   * \param[in] lead  Each pair's first byte, splatted.
   * \param[in] probe Each pair's second byte, splatted.
   * \param[in] delta Each pair's offset between them.
   * \param[in] count How many pairs.
   * \return The mask.
   */
  inline mask_t load_pairs_mask(const char         * at,
                                const byte_splat   * lead,
                                const byte_splat   * probe,
                                const std::uint8_t * delta,
                                std::size_t          count)
  {
    __m128i blk {};
    std::memcpy(&blk, at, 16); // MISRA-clean byte loads (no pointer type-pun)
    __m128i hit {_mm_setzero_si128()};
    for (std::size_t i = 0; i < count; ++i) {
      __m128i far {};
      std::memcpy(&far, at + delta[i], 16);
      __m128i a   {};
      __m128i b   {};
      std::memcpy(&a, lead[i].data(), 16);
      std::memcpy(&b, probe[i].data(), 16);
      hit = _mm_or_si128(hit, _mm_and_si128(_mm_cmpeq_epi8(blk, a), _mm_cmpeq_epi8(far, b)));
    }
    return static_cast<mask_t>(_mm_movemask_epi8(hit));
  }

#if defined(__SSSE3__) || defined(__GNUC__) || defined(__clang__)
  // The SSSE3 bodies below: built for SSSE3 where the translation unit is not.
#  if defined(__SSSE3__)
#    define REAL_TARGET_SSSE3
#  else
#    define REAL_TARGET_SSSE3 __attribute__((target("ssse3")))
#  endif
#  if !defined(__SSSE3__)
  /*!
   * \brief Whether the running CPU has SSSE3, asked once (`cpuid` leaf 1, ECX bit 9). Not
   *        `__builtin_cpu_supports`: that reads a model libgcc or compiler-rt fills in, which not every link
   *        provides.
   * \return True when \ref load_nibble3_mask may run.
   */
  inline bool cpu_has_ssse3()
  {
    static const bool has {[] {
                             unsigned int eax {};
                             unsigned int ebx {};
                             unsigned int ecx {};
                             unsigned int edx {};
                             return __get_cpuid(1U, &eax, &ebx, &ecx, &edx) != 0 && (ecx & (1U << 9U)) != 0U;
                           }()};
    return has;
  }

#  endif

  /*!
   * \brief The fingerprint's six tables, loaded once per scan by \ref load_nibble3_tables.
   */
  struct nibble3_tables
  {
    __m128i lo0; //!< Low nibble to bucket bits, first fingerprint byte.
    __m128i lo1; //!< The same, second byte.
    __m128i lo2; //!< The same, third byte.
    __m128i hi0; //!< High nibble to bucket bits, first fingerprint byte.
    __m128i hi1; //!< The same, second byte.
    __m128i hi2; //!< The same, third byte.
  };

  /*!
   * \brief Loads the fingerprint's six tables for \ref load_nibble3_mask once per scan, as the NEON twin's.
   * \param[in]  lo  Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in]  hi  The same for the high nibble.
   * \param[out] out The tables in registers.
   */
  inline void load_nibble3_tables(const std::array<std::array<std::uint8_t, 16>, 3>& lo,
                                  const std::array<std::array<std::uint8_t, 16>, 3>& hi,
                                  nibble3_tables&                                    out)
  {
    std::memcpy(&out.lo0, lo[0].data(), 16); // MISRA-clean byte copies (no pointer type-pun)
    std::memcpy(&out.lo1, lo[1].data(), 16);
    std::memcpy(&out.lo2, lo[2].data(), 16);
    std::memcpy(&out.hi0, hi[0].data(), 16);
    std::memcpy(&out.hi1, hi[1].data(), 16);
    std::memcpy(&out.hi2, hi[2].data(), 16);
  }

  /*!
   * \brief One fingerprint byte's lookups on 16 starts: the bucket bits both of its nibbles allow.
   * \param[in] blk The 16 bytes at that byte's offset.
   * \param[in] tlo Its low-nibble table.
   * \param[in] thi Its high-nibble table.
   * \return The bucket bits per start.
   */
  REAL_TARGET_SSSE3
  inline __m128i nibble_lookup(const __m128i& blk,
                               const __m128i& tlo,
                               const __m128i& thi)
  {
    const __m128i low4 {_mm_set1_epi8(0x0F)};
    return _mm_and_si128(_mm_shuffle_epi8(tlo, _mm_and_si128(blk, low4)),
                         _mm_shuffle_epi8(thi, _mm_and_si128(_mm_srli_epi16(blk, 4), low4)));
  }

  /*!
   * \brief \ref load_nibble3_mask on tables \ref load_nibble3_tables loaded.
   * \param[in] at The first of the 16 starts; `at + 17` must be readable.
   * \param[in] t  The tables.
   * \return The mask.
   */
  REAL_TARGET_SSSE3
  inline mask_t load_nibble3_mask(const char          * at,
                                  const nibble3_tables& t)
  {
    __m128i b0 {};
    __m128i b1 {};
    __m128i b2 {};
    std::memcpy(&b0, at, 16); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&b1, at + 1, 16);
    std::memcpy(&b2, at + 2, 16);
    const __m128i hit         {_mm_and_si128(_mm_and_si128(nibble_lookup(b0, t.lo0, t.hi0), nibble_lookup(b1, t.lo1, t.hi1)),
                                             nibble_lookup(b2, t.lo2, t.hi2))};
    const mask_t  empty_lanes {static_cast<mask_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(hit, _mm_setzero_si128())))};
    return (~empty_lanes) & 0xFFFFU;
  }

  /*!
   * \brief The fingerprint's first two lookups of the previous block, as the NEON twin's: zero before the first.
   */
  struct nibble3_carry
  {
    __m128i r0 {}; //!< Bucket bits the first fingerprint byte allowed, per byte of the previous block.
    __m128i r1 {}; //!< The same for the second.
  };

  /*!
   * \brief A round's four blocks of bucket bits (\ref nibble3_round), named, as the NEON twin's.
   */
  struct nibble3_hits
  {
    __m128i h0 {}; //!< The first block's.
    __m128i h1 {}; //!< The second's.
    __m128i h2 {}; //!< The third's.
    __m128i h3 {}; //!< The fourth's.
  };

  /*!
   * \brief One block's bucket bits for the starts `at - 2 .. at + 13`, as the NEON twin's.
   * \param[in]     at    The block; `at + 15` must be readable.
   * \param[in]     t     The tables.
   * \param[in,out] carry The previous block's first two lookups, replaced by this block's.
   * \return The bucket bits per start.
   */
  REAL_TARGET_SSSE3
  inline __m128i nibble3_block(const char          * at,
                               const nibble3_tables& t,
                               nibble3_carry&        carry)
  {
    __m128i b         {};
    std::memcpy(&b, at, 16); // MISRA-clean byte load (no pointer type-pun)
    const __m128i r0  {nibble_lookup(b, t.lo0, t.hi0)};
    const __m128i r1  {nibble_lookup(b, t.lo1, t.hi1)};
    const __m128i hit {_mm_and_si128(nibble_lookup(b, t.lo2, t.hi2), _mm_and_si128(_mm_alignr_epi8(r1, carry.r1, 15), _mm_alignr_epi8(r0, carry.r0, 14)))};
    carry.r0 = r0;
    carry.r1 = r1;
    return hit;
  }

  /*!
   * \brief One block of the fingerprint scan, loaded once, as the NEON twin's: the mask marks the starts
   *        `at - 2 .. at + 13`.
   * \param[in]     at    The block; `at + 15` must be readable.
   * \param[in]     t     The tables.
   * \param[in,out] carry The previous block's first two lookups, replaced by this block's.
   * \return The mask, lane `l` for the start `at - 2 + l`.
   */
  REAL_TARGET_SSSE3
  inline mask_t nibble3_step(const char          * at,
                             const nibble3_tables& t,
                             nibble3_carry&        carry)
  {
    const mask_t empty_lanes {static_cast<mask_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(nibble3_block(at, t, carry), _mm_setzero_si128())))};
    return (~empty_lanes) & 0xFFFFU;
  }

  /*!
   * \brief Four blocks of \ref nibble3_step, as the NEON twin's: kept as vectors, and whether any marks a start.
   * \param[in]     at    The first block; `at + 63` must be readable.
   * \param[in]     t     The tables.
   * \param[in,out] carry As \ref nibble3_step, across the four.
   * \param[out]    hits  The four blocks' bucket bits, for \ref nibble3_mask_of.
   * \return True when some start of the four blocks is marked.
   */
  REAL_TARGET_SSSE3
  inline bool nibble3_round(const char          * at,
                            const nibble3_tables& t,
                            nibble3_carry&        carry,
                            nibble3_hits&         hits)
  {
    hits.h0 = nibble3_block(at, t, carry);
    hits.h1 = nibble3_block(at + 16, t, carry);
    hits.h2 = nibble3_block(at + 32, t, carry);
    hits.h3 = nibble3_block(at + 48, t, carry);
    const __m128i any {_mm_or_si128(_mm_or_si128(hits.h0, hits.h1), _mm_or_si128(hits.h2, hits.h3))};
    return _mm_movemask_epi8(_mm_cmpeq_epi8(any, _mm_setzero_si128())) != 0xFFFF;
  }

  /*!
   * \brief The mask of block \p k of a round's bucket bits from \ref nibble3_round.
   * \param[in] hits The round's bucket bits.
   * \param[in] k    The block, 0 to 3.
   * \return The mask, lane `l` for the start `block - 2 + l`.
   */
  REAL_TARGET_SSSE3
  inline mask_t nibble3_mask_of(const nibble3_hits& hits,
                                std::size_t         k)
  {
    __m128i hit {hits.h3};
    switch (k) {
      case 0: hit = hits.h0; break;
      case 1: hit = hits.h1; break;
      case 2: hit = hits.h2; break;
      default: break;
    }
    const mask_t empty_lanes {static_cast<mask_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(hit, _mm_setzero_si128())))};
    return (~empty_lanes) & 0xFFFFU;
  }

  /*!
   * \brief \ref load_nibble3_mask's x86 leg, on the SSSE3 byte shuffle, which is not in the x86-64 floor. Where
   *        the build enables SSSE3 it is an ordinary function; elsewhere it is built for SSSE3 alone and may run
   *        only once \ref cpu_has_ssse3 said so -- and it inlines only into a caller built the same way.
   * \param[in] at The first of the 16 starts; `at + 17` must be readable.
   * \param[in] lo Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in] hi The same for the high nibble.
   * \return The mask.
   */
  REAL_TARGET_SSSE3
  inline mask_t load_nibble3_mask(const char                                         * at,
                                  const std::array<std::array<std::uint8_t, 16>, 3>&   lo,
                                  const std::array<std::array<std::uint8_t, 16>, 3>&   hi)
  {
    nibble3_tables t {};
    load_nibble3_tables(lo, hi, t);
    return load_nibble3_mask(at, t);
  }

#endif

#if defined(__AVX2__) || defined(__GNUC__) || defined(__clang__)
  // The AVX2 bodies below: built for AVX2 where the translation unit is not.
#  if defined(__AVX2__)
#    define REAL_TARGET_AVX2
#  else
#    define REAL_TARGET_AVX2 __attribute__((target("avx2")))
#  endif
#  if !defined(__AVX2__)
  /*!
   * \brief XCR0, the register state the operating system saves on a context switch (its bits 1 and 2: SSE and
   *        AVX). The instruction is XSAVE's, hence the target; the caller has checked OSXSAVE first.
   * \return XCR0.
   */
  __attribute__((target("xsave"))) inline std::uint64_t read_xcr0()
  {
    return _xgetbv(0U);
  }

  /*!
   * \brief Whether the running CPU can run AVX2 code, asked once: the AVX2 bit (`cpuid` leaf 7), and the
   *        operating system saving the 256-bit registers (OSXSAVE and AVX in leaf 1, then XCR0's SSE and AVX
   *        state bits) -- a CPU with AVX2 under a system that does not save them faults on the first one.
   * \return True when \ref avx2_literal_scan may run.
   */
  inline bool cpu_has_avx2()
  {
    static const bool has {[] {
                             unsigned int eax {};
                             unsigned int ebx {};
                             unsigned int ecx {};
                             unsigned int edx {};
                             if (__get_cpuid(1U, &eax, &ebx, &ecx, &edx) == 0 || (ecx & (1U << 27U)) == 0U
                                 || (ecx & (1U << 28U)) == 0U) {
                               return false;
                             }
                             if ((read_xcr0() & 6U) != 6U || __get_cpuid_max(0U, nullptr) < 7U) {
                               return false;
                             }
                             __cpuid_count(7U, 0U, eax, ebx, ecx, edx);
                             return (ebx & (1U << 5U)) != 0U;
                           }()};
    return has;
  }

#  endif

  /*!
   * \brief \ref avx2_pair_block without the narrowing: the lanes as a vector, so four blocks can be OR-ed and
   *        tested once before any is narrowed to a mask.
   * \param[in]  at    The first candidate; `at + 31 + delta` must be readable.
   * \param[in]  delta The last byte's offset from a candidate.
   * \param[in]  lead  The first byte, splatted.
   * \param[in]  trail The last byte, splatted.
   * \param[out] hits  0xFF in each lane whose candidate has both bytes (by reference, for the reason
   *                   \ref avx2_pair_block states).
   */
  REAL_TARGET_AVX2
  inline void avx2_pair_hits(const char*    at,
                             std::size_t    delta,
                             const __m256i& lead,
                             const __m256i& trail,
                             __m256i&       hits)
  {
    __m256i a {};
    __m256i b {};
    std::memcpy(&a, at, 32); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&b, at + delta, 32);
    hits = _mm256_and_si256(_mm256_cmpeq_epi8(a, lead), _mm256_cmpeq_epi8(b, trail));
  }

  /*!
   * \brief The 32 candidate starts at \p at where the needle's first and last bytes both sit (\ref avx2_literal_scan's
   *        block). Its own function because a lambda inside a function built for AVX2 is not built for it.
   * \param[in] at    The first candidate; `at + 31 + delta` must be readable.
   * \param[in] delta The last byte's offset from a candidate.
   * \param[in] lead  The first byte, splatted (by reference: gcc refuses a 256-bit vector passed by value in a
   *                  translation unit not built for AVX, `-Wpsabi`).
   * \param[in] trail The last byte, splatted.
   * \return One bit per candidate.
   */
  REAL_TARGET_AVX2
  inline std::uint32_t avx2_pair_block(const char*    at,
                                       std::size_t    delta,
                                       const __m256i& lead,
                                       const __m256i& trail)
  {
    __m256i a {};
    __m256i b {};
    std::memcpy(&a, at, 32); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&b, at + delta, 32);
    return static_cast<std::uint32_t>(
      _mm256_movemask_epi8(_mm256_and_si256(_mm256_cmpeq_epi8(a, lead), _mm256_cmpeq_epi8(b, trail))));
  }

  /*!
   * \brief The fingerprint's six tables for \ref avx2_nibble3_mask, each repeated in both 128-bit lanes (the
   *        256-bit byte shuffle works within a lane). Filled once per scan, as \ref load_nibble3_tables.
   */
  struct avx2_nibble3_tables
  {
    __m256i lo0; //!< Low nibble to bucket bits, first fingerprint byte.
    __m256i lo1; //!< The same, second byte.
    __m256i lo2; //!< The same, third byte.
    __m256i hi0; //!< High nibble to bucket bits, first fingerprint byte.
    __m256i hi1; //!< The same, second byte.
    __m256i hi2; //!< The same, third byte.
  };

  /*!
   * \brief Fills \p out from the plan's 16-byte tables.
   * \param[in]  lo  Three 16-byte tables, low nibble to bucket bits, one per fingerprint byte.
   * \param[in]  hi  The same for the high nibble.
   * \param[out] out The tables, broadcast.
   */
  REAL_TARGET_AVX2
  inline void avx2_nibble3_broadcast(const std::array<std::array<std::uint8_t, 16>, 3>& lo,
                                     const std::array<std::array<std::uint8_t, 16>, 3>& hi,
                                     avx2_nibble3_tables&                               out)
  {
    __m128i t {}; // one lane at a time: MISRA-clean byte copies (no pointer type-pun)
    std::memcpy(&t, lo[0].data(), 16);
    out.lo0 = _mm256_broadcastsi128_si256(t);
    std::memcpy(&t, lo[1].data(), 16);
    out.lo1 = _mm256_broadcastsi128_si256(t);
    std::memcpy(&t, lo[2].data(), 16);
    out.lo2 = _mm256_broadcastsi128_si256(t);
    std::memcpy(&t, hi[0].data(), 16);
    out.hi0 = _mm256_broadcastsi128_si256(t);
    std::memcpy(&t, hi[1].data(), 16);
    out.hi1 = _mm256_broadcastsi128_si256(t);
    std::memcpy(&t, hi[2].data(), 16);
    out.hi2 = _mm256_broadcastsi128_si256(t);
  }

  /*!
   * \brief One fingerprint byte's lookups on 32 starts: the bucket bits both of its nibbles allow.
   * \param[in] blk  The 32 bytes at that byte's offset.
   * \param[in] tlo  Its low-nibble table, broadcast.
   * \param[in] thi  Its high-nibble table, broadcast.
   * \param[in] low4 0x0F in every byte.
   * \return The bucket bits per start.
   */
  REAL_TARGET_AVX2
  inline __m256i avx2_nibble_lookup(const __m256i& blk,
                                    const __m256i& tlo,
                                    const __m256i& thi,
                                    const __m256i& low4)
  {
    return _mm256_and_si256(_mm256_shuffle_epi8(tlo, _mm256_and_si256(blk, low4)),
                            _mm256_shuffle_epi8(thi, _mm256_and_si256(_mm256_srli_epi16(blk, 4), low4)));
  }

  /*!
   * \brief \ref load_nibble3_mask on 32 starts: the same six lookups, on tables \ref avx2_nibble3_broadcast
   *        filled.
   * \param[in] at The first of the 32 starts; `at + 33` must be readable.
   * \param[in] t  The broadcast tables.
   * \return One bit per start.
   */
  REAL_TARGET_AVX2
  inline std::uint32_t avx2_nibble3_mask(const char                * at,
                                         const avx2_nibble3_tables&  t)
  {
    const __m256i low4 {_mm256_set1_epi8(0x0F)};
    __m256i       b0   {};
    __m256i       b1   {};
    __m256i       b2   {};
    std::memcpy(&b0, at, 32); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&b1, at + 1, 32);
    std::memcpy(&b2, at + 2, 32);
    const __m256i hit {_mm256_and_si256(_mm256_and_si256(avx2_nibble_lookup(b0, t.lo0, t.hi0, low4),
                                                         avx2_nibble_lookup(b1, t.lo1, t.hi1, low4)),
                                        avx2_nibble_lookup(b2, t.lo2, t.hi2, low4))};
    return ~static_cast<std::uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, _mm256_setzero_si256())));
  }

  /*!
   * \brief The two-byte literal filter (prefilter.hpp's `simd_literal_scan`, same contract) on 32-byte blocks,
   *        two a round. Where the build lacks AVX2 it is built for AVX2 alone and may run only once
   *        \ref cpu_has_avx2 said so.
   * \param[in] text    The subject.
   * \param[in] pos     Index to start searching from.
   * \param[in] literal The needle, at least two bytes.
   * \return The index of the first occurrence at or after \p pos, else `npos` (`std::size_t(-1)`).
   */
  REAL_TARGET_AVX2
  inline std::size_t avx2_literal_scan(std::string_view text,
                                       std::size_t      pos,
                                       std::string_view literal)
  {
    constexpr std::size_t not_found {static_cast<std::size_t>(-1)};
    const std::size_t     len       {literal.size()};
    if (text.size() < len) {
      return not_found;
    }
    const std::size_t last  {text.size() - len}; // last index a match could start at
    const std::size_t delta {len - 1};           // the trail byte's offset from a candidate start
    const char* const base  {text.data()};
    const __m256i     lead  {_mm256_set1_epi8(literal.front())};
    const __m256i     trail {_mm256_set1_epi8(literal[delta])};
    std::size_t       p     {pos};
    // A block covers candidates [p, p + 32): the furthest reads its trail byte at p + 31 + delta, which
    // `p + 32 <= last + 1` keeps inside the text. Masks are consumed in block then lane order, so the first
    // verified hit is the leftmost. Four blocks a round, OR-ed and tested once: a round with no candidate
    // narrows nothing.
    while (p + 128 <= last + 1) {
      // Named, not an array of four: gcc drops the vector type's attributes as a template argument.
      __m256i h0 {};
      __m256i h1 {};
      __m256i h2 {};
      __m256i h3 {};
      avx2_pair_hits(base + p, delta, lead, trail, h0);
      avx2_pair_hits(base + p + 32, delta, lead, trail, h1);
      avx2_pair_hits(base + p + 64, delta, lead, trail, h2);
      avx2_pair_hits(base + p + 96, delta, lead, trail, h3);
      const __m256i any {_mm256_or_si256(_mm256_or_si256(h0, h1), _mm256_or_si256(h2, h3))};
      if (_mm256_testz_si256(any, any) == 0) {
        const std::uint32_t masks[4] {static_cast<std::uint32_t>(_mm256_movemask_epi8(h0)), // NOLINT(*-avoid-c-arrays)
                                      static_cast<std::uint32_t>(_mm256_movemask_epi8(h1)),
                                      static_cast<std::uint32_t>(_mm256_movemask_epi8(h2)),
                                      static_cast<std::uint32_t>(_mm256_movemask_epi8(h3))};
        for (std::size_t u = 0; u < 4; ++u) {
          for (std::uint32_t m {masks[u]}; m != 0U; m &= m - 1U) {
            const std::size_t cand {p + (u * 32) + static_cast<std::size_t>(std::countr_zero(m))};
            if (std::memcmp(base + cand, literal.data(), len) == 0) {
              return cand;
            }
          }
        }
      }
      p += 128;
    }
    for (; p + 32 <= last + 1; p += 32) {
      for (std::uint32_t m {avx2_pair_block(base + p, delta, lead, trail)}; m != 0U; m &= m - 1U) {
        const std::size_t cand {p + static_cast<std::size_t>(std::countr_zero(m))};
        if (std::memcmp(base + cand, literal.data(), len) == 0) {
          return cand;
        }
      }
    }
    for (; p <= last; ++p) { // tail: fewer than 32 candidate starts left
      if (base[p] == literal.front() && base[p + delta] == literal[delta]
          && std::memcmp(base + p, literal.data(), len) == 0) {
        return p;
      }
    }
    return not_found;
  }

#endif

  /*!
   * \brief Mask of the candidate starts where both needle probes match -- the SSE2 leg of the NEON overload
   *        above (prefilter.hpp's `simd_literal_scan`).
   * \param[in] buf16_a 16 already-loaded bytes at the candidate starts (the caller's MISRA-clean memcpy).
   * \param[in] a       The needle byte expected at the first probe offset.
   * \param[in] buf16_b 16 already-loaded bytes at the candidate starts + delta (same, shifted).
   * \param[in] b       The needle byte expected at the second probe offset.
   * \return The 16-lane mask.
   */
  inline mask_t load_pair_mask(const std::uint8_t * buf16_a,
                               std::uint8_t         a,
                               const std::uint8_t * buf16_b,
                               std::uint8_t         b)
  {
    __m128i va {};
    __m128i vb {};
    std::memcpy(&va, buf16_a, 16); // MISRA-clean byte loads (no pointer type-pun)
    std::memcpy(&vb, buf16_b, 16);
    const __m128i eq_a {_mm_cmpeq_epi8(va, _mm_set1_epi8(static_cast<char>(a)))};
    const __m128i eq_b {_mm_cmpeq_epi8(vb, _mm_set1_epi8(static_cast<char>(b)))};
    return static_cast<mask_t>(_mm_movemask_epi8(_mm_and_si128(eq_a, eq_b)));
  }

  /*!
   * \brief Whether one of the 64 starts at \p lead64 has \p a there and \p b at the matching byte of \p trail64:
   *        the NEON twin's reject test, four pair compares OR-ed and one movemask, so that
   *        prefilter.hpp's `simd_literal_scan` is the same loop on both ISAs.
   * \param[in] lead64  64 already-loaded bytes at the candidate starts (the caller's MISRA-clean memcpy).
   * \param[in] a       The needle byte expected at the first probe offset.
   * \param[in] trail64 64 already-loaded bytes at the candidate starts + delta.
   * \param[in] b       The needle byte expected at the second probe offset.
   * \return True when some start holds both bytes.
   */
  inline bool any_pair64(const std::uint8_t * lead64,
                         std::uint8_t         a,
                         const std::uint8_t * trail64,
                         std::uint8_t         b)
  {
    const __m128i va  {_mm_set1_epi8(static_cast<char>(a))};
    const __m128i vb  {_mm_set1_epi8(static_cast<char>(b))};
    __m128i       any {_mm_setzero_si128()};
    for (std::size_t k = 0; k < 64; k += 16) {
      __m128i la {};
      __m128i tb {};
      std::memcpy(&la, lead64 + k, 16); // MISRA-clean byte loads (no pointer type-pun)
      std::memcpy(&tb, trail64 + k, 16);
      any = _mm_or_si128(any, _mm_and_si128(_mm_cmpeq_epi8(la, va), _mm_cmpeq_epi8(tb, vb)));
    }
    return _mm_movemask_epi8(any) != 0;
  }

  inline constexpr unsigned lane_shift {0U};      //!< log2 of the bits a lane takes in \ref mask_t.
  inline constexpr mask_t   full_mask  {0xFFFFU}; //!< Every one of the 16 lanes set.

  /*! \brief \p m with its first set lane cleared. */
  inline mask_t clear_first(mask_t m)
  {
    return m & (m - 1U);
  }

#endif

#if defined(__ARM_NEON) || defined(__SSE2__)
  // The lane helpers both masks share: a lane is (1 << lane_shift) bits, so each shift scales by it.
  inline constexpr unsigned lane_bits {1U << lane_shift}; //!< Bits a lane takes in \ref mask_t.

  /*! \brief `true` if no lane of \p m is set. */
  inline bool empty(mask_t m)
  {
    return m == 0U;
  }

  /*! \brief Index (0..15) of the first set lane of \p m. UB if `empty(m)`. */
  inline std::size_t first_lane(mask_t m)
  {
    return static_cast<std::size_t>(std::countr_zero(m)) >> lane_shift;
  }

  /*!
   * \brief `true` if every lane in `[start, start + len)` of \p m is set. The caller clamps \p len to
   *        `16 - start`; a \p len reaching lane 16 reads as the mask's full width.
   */
  inline bool window_all_set(mask_t      m,
                             std::size_t start,
                             std::size_t len)
  {
    const mask_t want {len >= 16 ? full_mask : ((mask_t {1} << (lane_bits * len)) - 1U)};
    return ((m >> (lane_bits * start)) & want) == want;
  }

  /*!
   * \brief Index (0..15, absolute — not relative to \p start) of the first CLEAR lane in
   *        `[start, start + len)` of \p m. UB if `window_all_set(m, start, len)`.
   */
  inline std::size_t first_clear_lane(mask_t      m,
                                      std::size_t start,
                                      std::size_t len)
  {
    const mask_t want  {len >= 16 ? full_mask : ((mask_t {1} << (lane_bits * len)) - 1U)};
    const mask_t fails {(~(m >> (lane_bits * start))) & want};
    return start + (static_cast<std::size_t>(std::countr_zero(fails)) >> lane_shift);
  }

  /*! \brief Index (0..15) of the first set lane of \p m at or after \p from, or 16 if none. */
  inline std::size_t next_set_lane(mask_t      m,
                                   std::size_t from)
  {
    if (from >= 16U) {
      return 16U;
    }
    const mask_t shifted {m >> (lane_bits * from)};
    return shifted == 0U ? 16U : from + (static_cast<std::size_t>(std::countr_zero(shifted)) >> lane_shift);
  }

#endif
} // namespace real::detail

#endif // REAL_SIMD_HPP
