/*!
 * \file utf8.hpp
 * \brief Strict UTF-8 decoding and code-point position arithmetic.
 */
#ifndef REAL_UTF8_HPP
#define REAL_UTF8_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace real::detail {

  /*! \brief The result of a strict UTF-8 decode: the code point, its byte length, and validity. */
  struct decoded_codepoint
  {
    std::uint32_t cp     {}; //!< The decoded code point (meaningful only when \ref valid).
    std::size_t   length {}; //!< Bytes consumed by the sequence (1–4), or the bytes examined on failure.
    bool          valid  {}; //!< Whether the sequence is a well-formed, canonical code point.
  };

  /*!
   * \brief Strictly decodes and validates the UTF-8 sequence at `text[pos]`.
   *
   * Unlike the lenient \ref codepoint_advance, rejects a lone continuation, a truncated sequence, an
   * overlong (`C0 80`), a surrogate, and anything above `U+10FFFF` (so also leads `0xC0`/`0xC1` and
   * `0xF5`–`0xFF`). In a pattern, a rejection is a malformed pattern, not a silent literal.
   *
   * \note Keep the mask tests: an equivalent lead-byte table wins in an isolated probe but loses in
   *       `make bench-engines`, ASCII rows included, because this header sits at a codegen cliff
   *       (docs/design.dox §10.1). Measure any change here in `make bench-engines`, never a probe.
   * \note Not `noinline`: the two supported compilers want opposite inlining, and `noinline` costs one
   *       heavily on the code-point loops without helping the other.
   * \param[in] text A byte sequence.
   * \param[in] pos  Index of the lead byte; must be `< text.size()`.
   * \return The decoded code point with `valid == true`, or `valid == false` on any malformation.
   */
  constexpr decoded_codepoint decode_codepoint_strict(std::string_view text,
                                                      std::size_t      pos)
  {
    const auto lead {static_cast<std::uint8_t>(text[pos])};
    if (lead < 0x80U) {
      return {.cp = lead, .length = 1, .valid = true}; // ASCII
    }
    std::size_t   length {};
    std::uint32_t cp     {};
    std::uint32_t min_cp {}; // smallest code point this length may legally encode (overlong guard)
    if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      cp     = lead & 0x1FU;
      min_cp = 0x80U;
    }
    else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      cp     = lead & 0x0FU;
      min_cp = 0x800U;
    }
    else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      cp     = lead & 0x07U;
      min_cp = 0x10000U;
    }
    else {
      return {.cp = 0, .length = 1, .valid = false}; // lone continuation, or an invalid lead (0xF8–0xFF)
    }
    for (std::size_t i = 1; i < length; ++i) {
      if (pos + i >= text.size()) {
        return {.cp = 0, .length = i, .valid = false}; // truncated sequence
      }
      const auto byte {static_cast<std::uint8_t>(text[pos + i])};
      if ((byte & 0xC0U) != 0x80U) {
        return {.cp = 0, .length = i, .valid = false}; // expected a continuation byte
      }
      cp = (cp << 6U) | (byte & 0x3FU);
    }
    if (cp < min_cp) {
      return {.cp = cp, .length = length, .valid = false}; // overlong (covers 0xC0/0xC1 and E0/F0 …)
    }
    if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
      return {.cp = cp, .length = length, .valid = false}; // out of range (incl. 0xF5+) or surrogate
    }
    return {.cp = cp, .length = length, .valid = true};
  }

  /*!
   * \brief Number of bytes from \p pos to the next code-point boundary, for advancing past an empty
   *        match during iteration.
   *
   * A boundary is any non-continuation byte: the same notion `pike_vm::seed_viable` uses for match
   * starts, so empty-match stepping and match starts stay in lock-step. On malformed text the
   * continuation run is stepped over as one unit.
   *
   * \param[in] text The subject text.
   * \param[in] pos  Index of the lead byte; must be < text.size().
   * \return The advance in bytes (>= 1).
   */
  constexpr std::size_t codepoint_advance(std::string_view text,
                                          std::size_t      pos)
  {
    std::size_t i {pos + 1};
    while (i < text.size() && (static_cast<unsigned>(static_cast<std::uint8_t>(text[i])) & 0xC0U) == 0x80U) {
      ++i;
    }
    return i - pos;
  }

  /*!
   * \brief Width of the last code point before \p end, the mirror of \ref codepoint_advance.
   *
   * Exact for a run built by `codepoint_advance`-consistent steps. Capped at 4 and never walks past
   * \p floor, so a malformed run cannot read outside the caller's range.
   *
   * \param[in] text  The subject text.
   * \param[in] end   Index one past the last consumed byte; must be <= text.size() and > \p floor.
   * \param[in] floor Never walk back past this index (the run's own known start).
   * \return The retreat in bytes (>= 1, <= min(4, end - floor)).
   */
  constexpr std::size_t codepoint_retreat(std::string_view text,
                                          std::size_t      end,
                                          std::size_t      floor)
  {
    const std::size_t  max_back {(end - floor) < 4 ? (end - floor) : std::size_t {4}};
    std::size_t        w        {1};
    while (w < max_back &&
           (static_cast<unsigned>(static_cast<std::uint8_t>(text[end - w])) & 0xC0U) == 0x80U) {
      ++w;
    }
    return w;
  }
} // namespace real::detail

#endif // REAL_UTF8_HPP
