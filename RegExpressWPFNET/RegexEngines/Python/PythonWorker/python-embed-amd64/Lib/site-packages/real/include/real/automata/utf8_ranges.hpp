/*!
 * \file utf8_ranges.hpp
 * \brief Code-point range → canonical UTF-8 byte-range sequences (RE2 / rust regex-syntax `Utf8Sequences`).
 *
 * Shared by the compiler (`.` and negated classes) and the lazy DFA (a `klass_cp` as a byte
 * sub-automaton); it includes no engine header, so neither caller has to include the other.
 */
#ifndef REAL_UTF8_RANGES_HPP
#define REAL_UTF8_RANGES_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace real::detail {

  /*! \brief One byte-range step `[lo, hi]` of a UTF-8 sequence produced by the code-point-range algorithm. */
  struct utf8_byte_range
  {
    std::uint8_t lo {}; //!< Low byte (inclusive).
    std::uint8_t hi {}; //!< High byte (inclusive).
  };

  /*! \brief A canonical UTF-8 byte-range sequence (1–4 steps) covering part of a code-point range. */
  struct utf8_byte_seq
  {
    utf8_byte_range parts[4] {}; //!< The per-byte ranges.
    std::size_t     length   {}; //!< Number of active steps (1–4).
  };

  /*!
   * \brief Encodes \p cp to its UTF-8 bytes in \p out, returning the length (1–4).
   * \param[in]  cp  The code point to encode; must be a scalar value (not checked).
   * \param[out] out Receives the bytes; only the first \e n are written, \e n being the return value.
   * \return The encoded length, 1 to 4.
   */
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
   * \brief Appends to \p out the byte-range sequences recognising exactly the UTF-8 encodings of
   *        `[start, end]` (RE2 / rust regex-syntax `Utf8Sequences`).
   *
   * Splits at UTF-8 length boundaries, then at continuation-byte boundaries, until each piece is one
   * tuple of byte ranges. The output has no overlong form; surrogates are not excluded here (see
   * \ref utf8_range_sequences).
   *
   * \param[in]     start First code point of the range.
   * \param[in]     end   Last code point of the range, inclusive; an inverted range appends nothing.
   * \param[in,out] out   Receives the sequences, appended.
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
      if (start <= max && max < end) {
        utf8_push_range(start, max, out);
        utf8_push_range(max + 1, end, out);
        return;
      }
    }
    for (unsigned i = 1; i < 4; ++i) {
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

  /*!
   * \brief Canonical UTF-8 byte-range sequences for the code-point range `[lo, hi]`, excluding the
   *        surrogate block `[U+D800, U+DFFF]` (so a negated class never matches a surrogate encoding).
   * \param[in] lo First code point of the range.
   * \param[in] hi Last code point of the range, inclusive.
   * \return The canonical sequences covering it, surrogates excluded — empty for an inverted range, or
   *         for one lying wholly inside the surrogate block.
   */
  constexpr std::vector<utf8_byte_seq> utf8_range_sequences(std::uint32_t lo,
                                                            std::uint32_t hi)
  {
    std::vector<utf8_byte_seq> out;
    if (hi < 0xD800U || lo > 0xDFFFU) {
      utf8_push_range(lo, hi, out);
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
