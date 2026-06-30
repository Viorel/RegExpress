/*!
 * \file charclass.hpp
 * \brief 256-bit byte set with O(1) membership, fully constexpr.
 *
 * The engine only ever tests bitmaps: negation and "one whole codepoint"
 * semantics are resolved at compile time (see compiler.hpp), never at match
 * time. Also provides the ASCII sets behind `\d`, `\w` and `\s`.
 */
#ifndef REAL_CHARCLASS_HPP
#define REAL_CHARCLASS_HPP

#include "version.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace real::detail {

  /*!
   * \brief A set of byte values (0–255) as a 256-bit bitmap.
   *
   * Membership, insertion and complement are all O(1) or O(256) and constexpr.
   * All bit manipulation uses unsigned operands (MISRA forbids signed bitwise).
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
        return; // callers pass low <= high (the parser rejects [z-a]); keeps the word math total
      }
      // Set whole 64-bit words at once (4 iterations) instead of looping byte by byte.
      for (unsigned word = 0; word < bits.size(); ++word) {
        const unsigned word_low  {word * 64U};
        const unsigned word_high {word_low + 63U};
        if (high < word_low || low > word_high) {
          continue; // this word holds no byte of [low, high]
        }
        const unsigned a {low > word_low ? low - word_low : 0U};     // first bit set, within the word
        const unsigned b {high < word_high ? high - word_low : 63U}; // last bit set, within the word
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
     * Bytes >= 0x80 are left untouched; the compiler handles non-ASCII
     * codepoints as explicit UTF-8 multi-byte alternatives instead.
     */
    constexpr void invert_ascii()
    {
      bits[0] = ~bits[0];
      bits[1] = ~bits[1];
    }

    /*!
     * \brief Full 256-bit complement (binary mode: raw bytes, no UTF-8).
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

    constexpr bool operator==(const char_class&) const = default;
  };

  /*!
   * \brief Closes \p klass under ASCII case folding.
   *
   * Whenever a letter is present its other-case twin is added. Applied to a
   * class \e before negation, so `[^a]` with `icase` rejects both
   * 'a' and 'A', matching Python.
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
   * \brief The ASCII whitespace set behind `\s`.
   * \return The set `[ \t\n\r\f\v]`.
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
  // The single source of truth for how `.` and negated classes expand to bytes:
  // the compiler emits these sets (compiler.hpp) and the prefilter recognizes
  // the same shape (prefilter.hpp). Keeping them here keeps the two in lock-step.

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
} // namespace real::detail

#endif // REAL_CHARCLASS_HPP
