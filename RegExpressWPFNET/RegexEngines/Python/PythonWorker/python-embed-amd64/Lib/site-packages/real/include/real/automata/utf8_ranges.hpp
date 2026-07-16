/*!
 * \file utf8_ranges.hpp
 * \brief Code-point range → canonical UTF-8 byte-range sequences (RE2 / rust regex-syntax `Utf8Sequences`).
 *
 * Turning a code-point range `[lo, hi]` into the byte-range steps that recognise exactly its UTF-8
 * encodings — no overlong forms, no surrogate encodings — is needed in two places: the compiler expands
 * `.` and negated classes this way, and the lazy DFA expands a `klass_cp` this way so a Unicode shorthand
 * becomes a byte-transition sub-automaton. This header is the shared, dependency-light home of that
 * algorithm (only `<cstdint>`/`<vector>`), so neither caller has to include the other.
 */
#ifndef REAL_UTF8_RANGES_HPP
#define REAL_UTF8_RANGES_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace real::detail {

  //! \brief One byte-range step `[lo, hi]` of a UTF-8 sequence produced by the code-point-range algorithm.
  struct utf8_byte_range
  {
    std::uint8_t lo {}; //!< Low byte (inclusive).
    std::uint8_t hi {}; //!< High byte (inclusive).
  };

  //! \brief A canonical UTF-8 byte-range sequence (1–4 steps) covering part of a code-point range.
  struct utf8_byte_seq
  {
    utf8_byte_range parts[4] {}; //!< The per-byte ranges.
    std::size_t     length   {}; //!< Number of active steps (1–4).
  };

  //! \brief Encodes \p cp to its UTF-8 bytes in \p out, returning the length (1–4).
  constexpr std::size_t encode_utf8_bytes(std::uint32_t cp,
                                          std::uint8_t (&out)[4])
  {
    if (cp < 0x80U) {
      out[0] = static_cast<std::uint8_t>(cp);
      return 1;
    }
    if (cp < 0x800U) {
      out[0] = static_cast<std::uint8_t>(0xC0U | (cp >> 6U));
      out[1] = static_cast<std::uint8_t>(0x80U | (cp & 0x3FU));
      return 2;
    }
    if (cp < 0x10000U) {
      out[0] = static_cast<std::uint8_t>(0xE0U | (cp >> 12U));
      out[1] = static_cast<std::uint8_t>(0x80U | ((cp >> 6U) & 0x3FU));
      out[2] = static_cast<std::uint8_t>(0x80U | (cp & 0x3FU));
      return 3;
    }
    out[0] = static_cast<std::uint8_t>(0xF0U | (cp >> 18U));
    out[1] = static_cast<std::uint8_t>(0x80U | ((cp >> 12U) & 0x3FU));
    out[2] = static_cast<std::uint8_t>(0x80U | ((cp >> 6U) & 0x3FU));
    out[3] = static_cast<std::uint8_t>(0x80U | (cp & 0x3FU));
    return 4;
  }

  /*!
   * \brief Splits `[start, end]` (same UTF-8 length after the length-boundary split) into contiguous
   *        byte-range sequences (RE2 / rust regex-syntax `Utf8Sequences`). Every produced sequence is
   *        canonical by construction — no overlong forms, no surrogates — which is exactly the
   *        security property qE needs. Appends to \p out.
   */
  constexpr void utf8_push_range(std::uint32_t               start,
                                 std::uint32_t               end,
                                 std::vector<utf8_byte_seq>& out)
  {
    if (start > end) {
      return;
    }
    constexpr std::uint32_t length_max[4] {0x7FU, 0x7FFU, 0xFFFFU, 0x10FFFFU};
    for (const std::uint32_t max : length_max) {
      if (start <= max && max < end) { // range spans a UTF-8 length boundary: split there
        utf8_push_range(start, max, out);
        utf8_push_range(max + 1, end, out);
        return;
      }
    }
    for (unsigned i = 1; i < 4; ++i) { // split so each continuation byte covers a contiguous sub-range
      const std::uint32_t mask {(1U << (6U * i)) - 1U};
      if ((start & ~mask) != (end & ~mask)) {
        if ((start & mask) != 0U) {
          utf8_push_range(start, start | mask, out);
          utf8_push_range((start | mask) + 1U, end, out);
          return;
        }
        if ((end & mask) != mask) {
          utf8_push_range(start, (end & ~mask) - 1U, out);
          utf8_push_range(end & ~mask, end, out);
          return;
        }
      }
    }
    std::uint8_t      start_bytes[4] {};
    std::uint8_t      end_bytes[4]   {};
    const std::size_t n              {encode_utf8_bytes(start, start_bytes)};
    encode_utf8_bytes(end, end_bytes);
    utf8_byte_seq seq                {};
    seq.length = n;
    for (std::size_t j = 0; j < n; ++j) {
      seq.parts[j] = {.lo = start_bytes[j], .hi = end_bytes[j]};
    }
    out.push_back(seq);
  }

  //! \brief Canonical UTF-8 byte-range sequences for the code-point range `[lo, hi]`, excluding the
  //!        surrogate block `[U+D800, U+DFFF]` (so a negated class never matches a surrogate encoding).
  constexpr std::vector<utf8_byte_seq> utf8_range_sequences(std::uint32_t lo,
                                                            std::uint32_t hi)
  {
    std::vector<utf8_byte_seq> out;
    if (hi < 0xD800U || lo > 0xDFFFU) {
      utf8_push_range(lo, hi, out); // no surrogate overlap
    }
    else {
      if (lo <= 0xD7FFU) {
        utf8_push_range(lo, 0xD7FFU, out);
      }
      if (hi >= 0xE000U) {
        utf8_push_range(0xE000U, hi, out);
      }
    }
    return out;
  }
} // namespace real::detail

#endif // REAL_UTF8_RANGES_HPP
