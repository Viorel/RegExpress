/*!
 * \file assert_eval.hpp
 * \brief Zero-width assertion evaluation (`^ $ \b \B \A \Z \< \>`) as free functions over a subject and a
 *        position, shared by the Pike VM and the one-pass extractor.
 *
 * The multiline / trailing-newline subtleties are resolved at compile time into the \ref real::detail::assert_kind
 * carried by an `assert_position` op, so evaluation here is a pure predicate on `(text, pos)` plus the word-ness
 * mode. Both callers read these functions — the Pike VM per instruction, the one-pass runtime per edge
 * condition — so their notion of a boundary agrees by construction rather than by review.
 */
#ifndef REAL_ASSERT_EVAL_HPP
#define REAL_ASSERT_EVAL_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "real/core/charclass.hpp"
#include "real/core/program.hpp"
#include "real/unicode/unicode_props.hpp"
#include "real/unicode/utf8.hpp"

namespace real::detail {

  /*!
   * \brief Word-ness of the code point **ending at** \p pos — the left side of a boundary. False at the text
   *        start or on a malformed sequence; ASCII / bytes / `re.A` (\p ascii_word) stay byte-level.
   * \param[in] text       Subject.
   * \param[in] pos        Boundary position.
   * \param[in] ascii_word Restrict word-ness to ASCII.
   * \return Whether the preceding code point is a word character.
   */
  [[nodiscard]] constexpr bool word_before(std::string_view text,
                                           std::size_t      pos,
                                           bool             ascii_word)
  {
    if (pos == 0) {
      return false;
    }
    const auto prev {static_cast<std::uint8_t>(text[pos - 1])};
    // ASCII fast path: an ASCII byte is a whole one-byte code point, and is_word_cp agrees with
    // is_ascii_word_byte on it — so the common case skips the back-decode entirely.
    if (prev < 0x80U || ascii_word) {
      return is_ascii_word_byte(prev);
    }
    std::size_t i     {pos - 1};
    std::size_t steps {0};
    // Walk back over continuation bytes, bounded to the longest UTF-8 sequence: a longer run is
    // malformed by definition, and the bound is what keeps one boundary test from scanning the subject.
    while (i > 0 && (static_cast<std::uint8_t>(text[i]) & 0xC0U) == 0x80U && steps < 3) {
      --i;
      ++steps;
    }
    const decoded_codepoint dc {decode_codepoint_strict(text, i)};
    if (!dc.valid || i + dc.length != pos) {
      return false; // malformed, or the sequence does not end exactly at pos
    }
    return is_word_cp(dc.cp);
  }

  /*!
   * \brief Word-ness of the code point **starting at** \p pos — the right side of a boundary. False at the
   *        text end or on a malformed sequence; ASCII / bytes / `re.A` stay byte-level.
   * \param[in] text       Subject.
   * \param[in] pos        Boundary position.
   * \param[in] ascii_word Restrict word-ness to ASCII.
   * \return Whether the following code point is a word character.
   */
  [[nodiscard]] constexpr bool word_after(std::string_view text,
                                          std::size_t      pos,
                                          bool             ascii_word)
  {
    if (pos >= text.size()) {
      return false;
    }
    const auto here {static_cast<std::uint8_t>(text[pos])};
    if (here < 0x80U || ascii_word) {
      return is_ascii_word_byte(here);
    }
    const decoded_codepoint dc {decode_codepoint_strict(text, pos)};
    return dc.valid && is_word_cp(dc.cp);
  }

  /*!
   * \brief Evaluates a zero-width assertion at \p pos in \p text.
   * \param[in] kind       The assertion to evaluate.
   * \param[in] text       The full subject (assertions read `pos - 1` and `pos`, so never a substring).
   * \param[in] pos        The position at which to evaluate it.
   * \param[in] ascii_word Word-ness mode for `\b \B \< \>`: byte-level ASCII when true, Unicode when false.
   * \return `true` if the assertion holds there.
   */
  [[nodiscard]] constexpr bool assertion_holds(assert_kind      kind,
                                               std::string_view text,
                                               std::size_t      pos,
                                               bool             ascii_word)
  {
    const std::size_t len {text.size()};
    const auto        byte_at = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    bool              result {};
    switch (kind) {
      case assert_kind::text_start:
        result = pos == 0;
        break;
      case assert_kind::text_end:
        result = pos == len;
        break;
      case assert_kind::text_end_or_final_newline:
        result = pos == len || (pos + 1 == len && byte_at(pos) == '\n');
        break;
      case assert_kind::line_start:
        result = pos == 0 || byte_at(pos - 1) == '\n';
        break;
      case assert_kind::line_end:
        result = pos == len || byte_at(pos) == '\n';
        break;
      case assert_kind::word_boundary:
      case assert_kind::not_word_boundary:
        {
          const bool before {word_before(text, pos, ascii_word)};
          const bool after  {word_after(text, pos, ascii_word)};
          result = (before != after) == (kind == assert_kind::word_boundary);
        }
        break;
      case assert_kind::word_start:
      case assert_kind::word_end:
        {
          const bool before {word_before(text, pos, ascii_word)};
          const bool after  {word_after(text, pos, ascii_word)};
          result = kind == assert_kind::word_start ? (!before && after) : (before && !after);
        }
        break;
    }
    return result;
  }
} // namespace real::detail

#endif // REAL_ASSERT_EVAL_HPP
