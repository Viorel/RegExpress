/*!
 * \file utf8.hpp
 * \brief UTF-8 position arithmetic for match iteration.
 *
 * The matching engine never needs this — multi-byte constructs are compiled
 * to byte-level alternatives. It is used only by match iteration to advance
 * past an empty match by one whole codepoint, matching Python's behaviour.
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
   * Unlike \ref codepoint_advance (a lenient forward-progress helper for match iteration), this
   * *validates*: it rejects a lone continuation byte, a truncated sequence, an **overlong** encoding
   * (e.g. `C0 80` for NUL), a UTF-16 surrogate, and any code point above `U+10FFFF` (which also
   * covers the invalid lead bytes `0xC0`/`0xC1` and `0xF5`–`0xFF`). It is the pattern-side decoder for
   * raw UTF-8 literals; a rejection is a malformed pattern, not a silent literal.
   *
   * \note The three mask tests below have been replaced by a lead-byte lookup table (256 entries, then
   *       folded to 32) and reverted both times. The table is exactly equivalent — a differential over
   *       every 1- and 2-byte sequence, comparing `cp`, `length` and `valid`, found zero divergence —
   *       and it wins when measured in a binary of its own. It loses in the published harness, and it
   *       loses on ASCII rows that never call this function: this header is included everywhere and
   *       its translation unit sits at a codegen cliff (docs/design.dox §10.1), so a change here moves
   *       unrelated rows by more than it moves its own. An isolated probe cannot see that. Measure any
   *       change to this file in `make bench-engines`, never in a probe alone.
   *
   * \note Nor can the function be moved out of that blast radius. A dedicated translation unit is
   *       foreclosed by the header-only design, so the only remaining lever is the inlining decision,
   *       and the two supported compilers want it in OPPOSITE directions: `noinline` costs one heavily
   *       on the code-point loops and does not buy the other. It also regresses builds where this
   *       function was already emitted out of line, which places the cost in the unit's inlining budget
   *       rather than in this function's own body. What the decoder costs the slowest code-point rows
   *       is therefore not reachable by rewriting it: it needs the header-only constraint relaxed or
   *       the call sites specialised, and neither is a decode problem.
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
   * A code-point boundary is any byte that is **not** a UTF-8 continuation byte (`10xxxxxx`). This
   * advances over the byte at \p pos and every continuation byte that follows, landing on the next
   * boundary (or the end). It is deliberately the SAME notion of "boundary" the matcher uses to seed
   * search positions (`seed_viable` in pike.hpp only starts a match at a non-continuation byte), so
   * empty-match stepping and match starts stay in lock-step. For well-formed text this is exactly the
   * code point's length (1–4). For malformed text (an overlong such as `C0 80`, a truncated or lone
   * continuation, an invalid lead) the continuation run is stepped over as one unit — the documented
   * code-point-alignment policy, not a special case. Forward progress is always >= 1 byte.
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
      ++i; // skip UTF-8 continuation bytes (10xxxxxx) to the next code-point boundary
    }
    return i - pos;
  }

  /*!
   * \brief Number of bytes from the code-point boundary immediately before \p end back to \p end --
   *        the mirror of \ref codepoint_advance, for the width of the LAST code point in a
   *        well-formed run ending at \p end (a possessive cp-class loop's own last iteration: the
   *        loop only ever advanced by `codepoint_advance`-consistent steps, so walking backward
   *        over continuation bytes lands on the same boundary walking forward would have stopped
   *        at). Capped at 4 (the longest valid UTF-8 sequence) and never walks past \p floor, so a
   *        malformed/truncated run can never read out of the caller's own known-valid range.
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
      ++w; // text[end - w] is itself a continuation byte -- the lead byte is further back still
    }
    return w;
  }
} // namespace real::detail

#endif // REAL_UTF8_HPP
