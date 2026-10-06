/*!
 * \file charclass.hpp
 * \brief 256-bit byte set with O(1) membership, fully constexpr.
 *
 * Negation and whole-codepoint semantics are resolved at compile time; match time only tests
 * bitmaps. Also holds the ASCII sets behind `\d`, `\w`, `\s`, the UTF-8 lead/continuation sets,
 * and the per-lead bounds that reject a malformed sequence without decoding it.
 */
#ifndef REAL_CHARCLASS_HPP
#define REAL_CHARCLASS_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace real::detail {

  /*!
   * \brief A set of byte values (0–255) as a 256-bit bitmap.
   *
   * Bit manipulation uses unsigned operands only (MISRA forbids signed bitwise).
   */
  struct char_class
  {
    std::array<std::uint64_t, 4> bits {}; //!< Bitmap; bit `byte` is byte `byte`'s membership.

    /*!
     * \brief Adds byte \p byte to the set.
     * \param[in] byte The byte to insert.
     */
    constexpr void set(std::uint8_t byte)
    {
      const unsigned bit {byte};
      bits[bit >> 6U] |= std::uint64_t {1} << (bit & 63U);
    }

    /*!
     * \brief Adds the inclusive byte range `[low, high]` to the set.
     * \param[in] low  First byte of the range.
     * \param[in] high Last byte of the range (inclusive).
     */
    constexpr void set_range(std::uint8_t low,
                             std::uint8_t high)
    {
      if (low > high) {
        return; // callers pass low <= high; keeps the word math total
      }
      for (unsigned word = 0; word < bits.size(); ++word) {
        const unsigned word_low  {word * 64U};
        const unsigned word_high {word_low + 63U};
        if (high < word_low || low > word_high) {
          continue;
        }
        const unsigned a {low > word_low ? low - word_low : 0U};     // first bit, within the word
        const unsigned b {high < word_high ? high - word_low : 63U}; // last bit, within the word
        bits[word] |= (b - a == 63U)
                      ? ~std::uint64_t {0}
                      : (((std::uint64_t {1} << (b - a + 1U)) - 1U) << a);
      }
    }

    /*!
     * \brief Unions \p other into this set.
     * \param[in] other The set whose members are added to this one.
     */
    constexpr void merge(const char_class& other)
    {
      for (std::size_t i = 0; i < bits.size(); ++i) {
        bits[i] |= other.bits[i];
      }
    }

    /*!
     * \brief Complements the ASCII half (bytes 0–127) only.
     *
     * Bytes >= 0x80 are untouched: the compiler expands non-ASCII codepoints to UTF-8 sequences.
     */
    constexpr void invert_ascii()
    {
      bits[0] = ~bits[0];
      bits[1] = ~bits[1];
    }

    /*!
     * \brief Full 256-bit complement (bytes mode only).
     */
    constexpr void invert()
    {
      for (auto& word : bits) {
        word = ~word;
      }
    }

    /*!
     * \brief Tests membership of byte \p byte.
     * \param[in] byte The byte to test.
     * \return `true` if \p byte is in the set.
     */
    [[nodiscard]] constexpr bool test(std::uint8_t byte) const
    {
      const unsigned bit {byte};
      return ((bits[bit >> 6U] >> (bit & 63U)) & 1U) != 0;
    }

    /*!
     * \brief Reports whether the set has no members.
     * \return `true` if the set is empty.
     */
    [[nodiscard]] constexpr bool empty() const
    {
      return (bits[0] | bits[1] | bits[2] | bits[3]) == 0;
    }

    /*!
     * \brief Equality: the two bitmaps accept exactly the same byte set.
     * \return Whether every one of the 256 bits agrees.
     */
    constexpr bool operator==(const char_class&) const = default;
  };

  /*!
   * \brief Closes \p klass under ASCII case folding.
   *
   * Apply \e before negation: `[^a]` with `icase` then rejects both 'a' and 'A', as Python does.
   *
   * \param[in,out] klass The class to fold in place.
   */
  constexpr void fold_ascii_case(char_class& klass)
  {
    for (std::uint8_t upper = 'A'; upper <= 'Z'; ++upper) {
      const auto lower {static_cast<std::uint8_t>(upper + 32)};
      if (klass.test(upper)) {
        klass.set(lower);
      }
      if (klass.test(lower)) {
        klass.set(upper);
      }
    }
  }

  /*!
   * \brief Reports whether \p byte is an ASCII "word" byte (`[0-9A-Za-z_]`).
   * \param[in] byte The byte to classify.
   * \return `true` for ASCII word bytes, used by `\b` / `\w`.
   */
  [[nodiscard]] constexpr bool is_ascii_word_byte(std::uint8_t byte)
  {
    return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= 'a' && byte <= 'z') || byte == '_';
  }

  /*!
   * \brief The ASCII digit set behind `\d` (Python `re.ASCII` semantics).
   * \return The set `[0-9]`.
   */
  constexpr char_class digit_set()
  {
    char_class result;
    result.set_range('0', '9');
    return result;
  }

  /*!
   * \brief The ASCII word set behind `\w`.
   * \return The set `[0-9A-Za-z_]`.
   */
  constexpr char_class word_set()
  {
    char_class result;
    result.set_range('0', '9');
    result.set_range('A', 'Z');
    result.set_range('a', 'z');
    result.set('_');
    return result;
  }

  /*!
   * \brief The ASCII whitespace set behind `\s` under `flags::ascii` / `flags::bytes`.
   * \return The set `[ \t\n\r\f\v]`.
   *
   * \note Not `str.isspace()`: Python's ASCII `\s` rejects FS/GS/RS/US (`U+001C`–`U+001F`), which
   *       text mode accepts through the generated `space_ranges` table.
   */
  constexpr char_class space_set()
  {
    char_class result;
    result.set(' ');
    result.set('\t');
    result.set('\n');
    result.set('\r');
    result.set('\f');
    result.set('\v');
    return result;
  }

  // --- UTF-8 byte-class sets -------------------------------------------------
  // Shared by the compiler (continuation slots of a code-point class) and the prefilter (lead bytes
  // as the first-byte superset of one): one definition keeps the two in lock-step.

  /*!
   * \brief The UTF-8 continuation-byte set `10xxxxxx`.
   * \return The set `[0x80, 0xBF]`.
   */
  constexpr char_class utf8_cont_set()
  {
    char_class result;
    result.set_range(0x80, 0xBF);
    return result;
  }

  /*!
   * \brief The lead-byte set of a 2-byte UTF-8 sequence.
   * \return The set `[0xC2, 0xDF]`.
   */
  constexpr char_class utf8_lead2_set()
  {
    char_class result;
    result.set_range(0xC2, 0xDF);
    return result;
  }

  /*!
   * \brief The lead-byte set of a 3-byte UTF-8 sequence.
   * \return The set `[0xE0, 0xEF]`.
   */
  constexpr char_class utf8_lead3_set()
  {
    char_class result;
    result.set_range(0xE0, 0xEF);
    return result;
  }

  /*!
   * \brief The lead-byte set of a 4-byte UTF-8 sequence.
   * \return The set `[0xF0, 0xF4]`.
   */
  constexpr char_class utf8_lead4_set()
  {
    char_class result;
    result.set_range(0xF0, 0xF4);
    return result;
  }

  /*!
   * \brief `[lo, hi]` bounds for the FIRST continuation byte of a multi-byte UTF-8 sequence, given its
   *        lead byte — one entry of \ref utf8_second_byte_bounds_table.
   */
  struct utf8_second_byte_bounds
  {
    std::uint8_t lo {}; //!< Lowest valid first-continuation byte for this lead (inclusive).
    std::uint8_t hi {}; //!< Highest valid first-continuation byte for this lead (inclusive).
  };

  /*!
   * \brief Builds \ref utf8_second_byte_bounds_table.
   * \return The table, indexed by lead byte.
   */
  constexpr std::array<utf8_second_byte_bounds, 256> make_utf8_second_byte_bounds_table()
  {
    std::array<utf8_second_byte_bounds, 256> table {};
    for (auto& entry : table) {
      entry = {.lo = 0x80, .hi = 0xBF};
    }
    table[0xE0] = {.lo = 0xA0, .hi = 0xBF};
    table[0xED] = {.lo = 0x80, .hi = 0x9F};
    table[0xF0] = {.lo = 0x90, .hi = 0xBF};
    table[0xF4] = {.lo = 0x80, .hi = 0x8F};
    return table;
  }

  /*!
   * \brief First-continuation-byte bounds indexed by lead byte (only 0xC2–0xF4 are consulted).
   *
   * Default `[0x80, 0xBF]`; four leads narrow it (Unicode Table 3-7): `0xE0` drops the overlong
   * `[0x80, 0x9F]`, `0xED` the surrogates `[0xA0, 0xBF]`, `0xF0` the overlong `[0x80, 0x8F]`, `0xF4`
   * past-`U+10FFFF` `[0x90, 0xBF]`. 2-byte overlongs are already excluded by \ref utf8_lead2_set.
   *
   * Rejects those encodings without decoding, for a per-byte scan (pike.hpp's `.` fast path) where
   * \ref real::detail::decode_codepoint_strict is too costly.
   */
  inline constexpr std::array<utf8_second_byte_bounds, 256> utf8_second_byte_bounds_table {
    make_utf8_second_byte_bounds_table()};
} // namespace real::detail

#endif // REAL_CHARCLASS_HPP
