/*!
 * \file std/regex_core.hpp
 * \brief std::regex compatibility, part 1/3: the constants, the error type, the backend-routing screens and
 *        `basic_regex`.
 */
#ifndef REAL_STD_REGEX_CORE_HPP
#define REAL_STD_REGEX_CORE_HPP

// Internal — do not include directly; the entry point is <real/compat/std/regex.hpp>.

#include <real/version.hpp>

#include <cstddef>
#include <initializer_list>
#include <atomic>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <real/real.hpp>

namespace real::compat {

  /*!
   * \brief Compatibility constants mirroring `std::regex_constants` (own values, mapped internally).
   */
  namespace regex_constants {

    /*!
     * \brief Grammar / option flags (own bit values; mapped to real::flags or std at construction).
     */
    enum syntax_option_type : unsigned
    {
      ECMAScript = 0,        //!< The default grammar.
      icase      = 1U << 0U, //!< Case-insensitive (ASCII).
      nosubs     = 1U << 1U, //!< Do not expose sub-expressions; std only, so rejected under `policy::strict`.
      optimize   = 1U << 2U, //!< Hint to favour matching speed; honoured as a no-op.
      collate    = 1U << 3U, //!< Locale-sensitive ranges; std only, so rejected under `policy::strict`.
      multiline  = 1U << 4U, //!< `^`/`$` match at line boundaries.
      basic      = 1U << 5U, //!< POSIX BRE — translated onto REAL when the pattern translates, else the std backend.
      extended   = 1U << 6U, //!< POSIX ERE — translated onto REAL when the pattern translates, else the std backend.
      awk        = 1U << 7U, //!< awk grammar (ERE + C escapes) — translated onto REAL when it translates, else std.
      grep       = 1U << 8U, //!< grep grammar (BRE, lines joined by `|`) — translated onto REAL when it translates, else std.
      egrep      = 1U << 9U, //!< egrep grammar (ERE, lines joined by `|`) — translated onto REAL when it translates, else std.
    };

    /*!
     * \brief Bitwise OR of two syntax options, so `ECMAScript | icase` stays typed.
     * \param[in] a Left operand.
     * \param[in] b Right operand.
     * \return Their union.
     */
    constexpr syntax_option_type operator|(syntax_option_type a,
                                           syntax_option_type b) noexcept
    {
      return static_cast<syntax_option_type>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    /*!
     * \brief Bitwise AND of two syntax options, for testing a bit.
     * \param[in] a Left operand.
     * \param[in] b Right operand.
     * \return Their intersection.
     */
    constexpr syntax_option_type operator&(syntax_option_type a,
                                           syntax_option_type b) noexcept
    {
      return static_cast<syntax_option_type>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }

    /*!
     * \brief Match-control flags: the common subset.
     */
    enum match_flag_type : unsigned
    {
      match_default    = 0,          //!< No constraint; the operation may stay on REAL.
      match_not_bol    = 1U << 0U,   //!< `^` does not match the start of the sequence.
      match_not_eol    = 1U << 1U,   //!< `$` does not match the end of the sequence.
      match_not_bow    = 1U << 2U,   //!< `\b` does not match at the start.
      match_not_eow    = 1U << 3U,   //!< `\b` does not match at the end.
      match_any        = 1U << 4U,   //!< Any match will do; REAL satisfies it by returning the leftmost one.
      match_not_null   = 1U << 5U,   //!< Do not match an empty sequence.
      match_continuous = 1U << 6U,   //!< The match must start at the first character.
      match_prev_avail = 1U << 7U,   //!< `--first` is valid, so `^` and `\b` may inspect the character before it.
      format_default    = 0,         //!< ECMAScript replacement syntax, copying the unmatched text.
      format_sed        = 1U << 8U,  //!< sed's replacement syntax: `&`, a backslash and a digit or character; `$` is literal.
      format_no_copy    = 1U << 9U,  //!< Do not copy the parts of the text that did not match.
      format_first_only = 1U << 10U, //!< Replace only the first match.
    };

    /*!
     * \brief Bitwise OR of two match flags.
     * \param[in] a Left operand.
     * \param[in] b Right operand.
     * \return Their union.
     */
    constexpr match_flag_type operator|(match_flag_type a,
                                        match_flag_type b) noexcept
    {
      return static_cast<match_flag_type>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    /*!
     * \brief Bitwise AND of two match flags, for testing a bit.
     * \param[in] a Left operand.
     * \param[in] b Right operand.
     * \return Their intersection.
     */
    constexpr match_flag_type operator&(match_flag_type a,
                                        match_flag_type b) noexcept
    {
      return static_cast<match_flag_type>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }

    /*!
     * \brief Bitwise complement of a match-flag set, for masking bits off.
     * \param[in] a The flags to complement.
     * \return Every bit of the underlying type that \p a does not hold.
     */
    constexpr match_flag_type operator~(match_flag_type a) noexcept
    {
      return static_cast<match_flag_type>(~static_cast<unsigned>(a));
    }

    using error_type = std::regex_constants::error_type;                                   //!< Error categories, aliased to std's so `regex_error::code()` is a true drop-in.

    inline constexpr error_type error_collate    {std::regex_constants::error_collate};    //!< As std's: an invalid collating element.
    inline constexpr error_type error_ctype      {std::regex_constants::error_ctype};      //!< As std's: an invalid character class.
    inline constexpr error_type error_escape     {std::regex_constants::error_escape};     //!< As std's: an invalid escape.
    inline constexpr error_type error_backref    {std::regex_constants::error_backref};    //!< As std's: an invalid back reference.
    inline constexpr error_type error_brack      {std::regex_constants::error_brack};      //!< As std's: mismatched brackets.
    inline constexpr error_type error_paren      {std::regex_constants::error_paren};      //!< As std's: mismatched parentheses.
    inline constexpr error_type error_brace      {std::regex_constants::error_brace};      //!< As std's: mismatched braces.
    inline constexpr error_type error_badbrace   {std::regex_constants::error_badbrace};   //!< As std's: an invalid range in braces.
    inline constexpr error_type error_range      {std::regex_constants::error_range};      //!< As std's: an invalid character range.
    inline constexpr error_type error_space      {std::regex_constants::error_space};      //!< As std's: out of memory.
    inline constexpr error_type error_badrepeat  {std::regex_constants::error_badrepeat};  //!< As std's: a repeat with nothing to repeat.
    inline constexpr error_type error_complexity {std::regex_constants::error_complexity}; //!< As std's; also a strict-policy rejection.
    inline constexpr error_type error_stack      {std::regex_constants::error_stack};      //!< As std's: out of stack.
  } // namespace regex_constants

  /*!
   * \brief `std::regex_error`-compatible exception; every regex error this layer reports has this type.
   *
   * - A syntax error: std's own `code()` and `what()`.
   * - A strict-policy rejection (\ref policy) of a pattern std accepts but REAL cannot run linearly:
   *   `error_complexity`, with a message naming REAL.
   * - An error the std backend raises while matching (`error_complexity`, `error_stack`): std's code.
   */
  class regex_error : public std::regex_error
  {
  public:

    /*!
     * \brief From a std error, keeping its code and message.
     * \param[in] error The standard library error to adopt.
     */
    explicit regex_error(const std::regex_error& error)
      : std::regex_error(error.code()),
        message_(error.what())
    {}

    /*!
     * \brief With a code alone, as `std::regex_error(code)`; the message is std's for that code.
     * \param[in] code The error category.
     */
    explicit regex_error(std::regex_constants::error_type code)
      : std::regex_error(code),
        message_(std::regex_error(code).what())
    {}

    /*!
     * \brief With an explicit code and message — the strict-policy rejection of a pattern REAL cannot
     *        represent linearly (`error_complexity`), carrying a REAL-identifiable message.
     * \param[in] code    The `std::regex_constants::error_type` to report.
     * \param[in] message The text \ref what returns.
     */
    regex_error(std::regex_constants::error_type code,
                std::string                      message)
      : std::regex_error(code),
        message_(std::move(message))
    {}

    /*!
     * \brief The message, which for a strict-policy rejection identifies REAL as the source.
     * \return A NUL-terminated message valid for this object's lifetime.
     */
    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

  private:

    std::string message_; //!< The originating error's detailed message.
  };

  /*!
   * \brief The policy for a pattern the linear engine cannot represent (a backreference, an unbounded
   *        lookbehind, a POSIX class, …): `strict`, the default, rejects it; `fallback` delegates it to
   *        `std::regex`, **forfeiting** the linear-time guarantee for that pattern.
   *
   * The guarantee covers the calls REAL runs (replace and iteration compose them: quadratic at worst, never
   * exponential). A call routed to std — a match flag REAL cannot honor, a `$0` format, a traversal REAL does
   * not model (`basic_regex::uses_real_traversal`) — runs on std's backtracker under either policy.
   */
  enum class policy : std::uint8_t
  {
    strict,   //!< Reject an ineligible pattern (throws `regex_error` with `error_complexity`). The default.
    fallback, //!< Delegate an ineligible pattern to `std::regex` (backtracking — not ReDoS-safe).
  };

  namespace detail {

    //! \brief Whether REAL can serve this instantiation: `char` with default traits only, anything else is std.
    //!        A compile-time gate: REAL's char-only code must be compiled out for other `CharT`, not skipped.
    template <typename CharT, typename Traits>
    inline constexpr bool real_eligible =
      std::is_same_v<CharT, char> && std::is_same_v<Traits, std::regex_traits<char>>;

    /*!
     * \brief Options REAL cannot serve once \ref translate_posix has declined: a POSIX grammar bit, and always
     *        `collate` and `nosubs` (REAL reports every group, std only group 0 under `nosubs`).
     * \param[in] f The syntax options requested.
     * \return `true` if, translation having declined, these options cannot be served by REAL.
     */
    inline bool grammar_forces_std(regex_constants::syntax_option_type f) noexcept
    {
      using namespace regex_constants;
      return (f & (basic | extended | awk | grep | egrep)) != ECMAScript
             || (f & collate) != ECMAScript
             || (f & nosubs) != ECMAScript;
    }

    /*!
     * \brief Pattern text REAL accepts but reads differently from std, screened out before REAL compiles it so
     *        that the divergence is never silent.
     *
     * `\0` and a digit: REAL reads a legacy octal escape (`\012` is a newline), libstdc++ a NUL then the digit.
     * `\C`: RE2's one-byte escape, reachable only through this layer's `flags::bytes`; not ECMAScript. An
     * inline-flags group (`(?i)`, `(?-s:...)`): REAL honors it, std rejects it. Over-matching is safe.
     * \param[in] p The pattern text.
     * \return `true` if it holds a construct only the `std` backend can serve.
     */
    [[nodiscard]] inline bool pattern_forces_std(std::string_view p) noexcept
    {
      const auto is_flag_or_dash = [](char c) {
                                     return c == '-' || c == 'i' || c == 'm' || c == 's' || c == 'x' || c == 'a' ||
                                            c == 'U';
                                   };
      for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '\\') {
          if (i + 2 < p.size() && p[i + 1] == '0' && p[i + 2] >= '0' && p[i + 2] <= '9') {
            return true;
          }
          if (i + 1 < p.size() && p[i + 1] == 'C') {
            return true; // \C
          }
          ++i; // consume the escaped character (so `\\0` is an escaped backslash, not `\0`)
          continue;
        }
        // An inline-flags group: after `(?` only a flag letter or `-` starts one. Keep `U` (REAL's ungreedy
        // swap) in the set, or REAL would honor `(?U)` where std rejects it.
        if (p[i] == '(' && i + 2 < p.size() && p[i + 1] == '?' && is_flag_or_dash(p[i + 2])) {
          return true;
        }
      }
      return false;
    }

    /*!
     * \brief Whether the native std keeps a final lone backslash of a `format_sed` format, as libstdc++ and libc++
     *        do; MS STL drops it. REAL's sed expansion follows the native std there.
     * \return `true` when `std::regex_replace("a", regex("a"), "\\", format_sed)` gives back a backslash; asked once.
     */
    [[nodiscard]] inline bool std_sed_keeps_final_backslash()
    {
      static const bool keeps {std::regex_replace(std::string {"a"}, std::regex {"a"}, std::string {"\\"},
                                                  std::regex_constants::format_sed)
                               == "\\"};
      return keeps;
    }

    /*!
     * \brief Whether the native std reads `$0` in a format as the whole match, as libstdc++ and libc++ do;
     *        `match_results::format` follows it, being the one place REAL expands a `$0` itself.
     * \return `true` when `std::regex_replace("a", regex("a"), "$0")` gives back `"a"`; asked once.
     */
    [[nodiscard]] inline bool std_dollar_zero_is_match()
    {
      static const bool whole {std::regex_replace(std::string {"a"}, std::regex {"a"}, std::string {"$0"}) == "a"};
      return whole;
    }

    /*!
     * \brief Expands a replacement format: the one rule set behind `match_results::format` and `regex_replace`.
     *
     * ECMAScript rules: a dollar followed by a dollar, an ampersand, a backtick, a quote or one or two digits
     * inserts a dollar, the match, the prefix, the suffix or that group (nothing for a group that does not
     * exist, its digits consumed; a second digit is taken greedily, `$015` being group 1 then a literal 5);
     * any other dollar, a final one included, is itself. `$0` reads as the native std reads it
     * (\ref std_dollar_zero_is_match). Under \p sed, POSIX sed's: `&` is the whole match, a backslash and a
     * digit that group, a backslash and any other character that character, and a final lone backslash
     * follows the native std (\ref std_sed_keeps_final_backslash).
     *
     * \tparam Sed   Select the sed rules.
     * \tparam CharT The character type.
     * \param[in] first  Start of the format.
     * \param[in] last   One past its end.
     * \param[in] put    Writes one character.
     * \param[in] group  Writes group `g` (nothing when it does not exist or took no part).
     * \param[in] prefix Writes the text before the match.
     * \param[in] suffix Writes the text after it.
     */
    template <bool Sed, typename CharT, typename Put, typename Group, typename Prefix, typename Suffix>
    REAL_ALWAYS_INLINE
    inline void expand_replacement(const CharT* first,
                                   const CharT* last,
                                   Put&&        put,
                                   Group&&      group,
                                   Prefix&&     prefix,
                                   Suffix&&     suffix)
    {
      const auto digit = [](CharT c) { return c >= CharT('0') && c <= CharT('9'); };
      if constexpr (Sed) { // a template parameter, not a test per character: that cost regex_replace 2 %
        for (const CharT* at {first}; at != last; ++at) {
          const CharT c {*at};
          if (c == CharT('&')) {
            group(std::size_t {0});
          }
          else if (c != CharT('\\')) {
            put(c);
          }
          else if (at + 1 == last) {
            if (std_sed_keeps_final_backslash()) {
              put(c);
            }
          }
          else if (const CharT next {*++at}; digit(next)) {
            group(static_cast<std::size_t>(next - CharT('0')));
          }
          else {
            put(next);
          }
        }
        return;
      }
      for (const CharT* at {first}; at != last; ++at) {
        const CharT c      {*at};
        if (c != CharT('$') || at + 1 == last) { // a final `$` is itself
          put(c);
          continue;
        }
        const CharT next {at[1]};
        if (next == CharT('$')) {
          put(next);
          ++at;
        }
        else if (next == CharT('&')) {
          group(std::size_t {0});
          ++at;
        }
        else if (next == CharT('`')) {
          prefix();
          ++at;
        }
        else if (next == CharT('\'')) {
          suffix();
          ++at;
        }
        else if (digit(next)) {
          const CharT* const digits {at + 1};
          std::size_t        g      {static_cast<std::size_t>(next - CharT('0'))};
          ++at;
          if (at + 1 != last && digit(at[1])) {
            g = (g * 10) + static_cast<std::size_t>(at[1] - CharT('0'));
            ++at;
          }
          if (g == 0 && !std_dollar_zero_is_match()) {
            put(c); // a std that reads `$0` literally
            for (const CharT* d {digits}; d != at + 1; ++d) {
              put(*d);
            }
          }
          else {
            group(g);
          }
        }
        else {
          put(c);
        }
      }
    }

    /*!
     * \brief Whether a replacement format holds `$0`, which std implementations read differently (libstdc++: the
     *        whole match; MS STL: a literal), so the replace routes to std; `$$` is an escaped dollar.
     * \param[in] fmt The replacement format.
     * \return `true` if it holds a construct whose meaning is platform-variant, so the replace routes to `std`.
     */
    [[nodiscard]] inline bool format_forces_std(std::string_view fmt) noexcept
    {
      for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '$' && i + 1 < fmt.size()) {
          if (fmt[i + 1] == '$') {
            ++i;         // `$$` — escaped literal dollar
          }
          else if (fmt[i + 1] == '0') {
            return true; // `$0…` platform-variant
          }
        }
      }
      return false;
    }

    /*!
     * \brief A POSIX bracket-class name to its ASCII (C-locale) range content, appended inside a `[...]` during
     *        ERE translation. Empty for an unknown name (the caller then falls back to std).
     * \param[in] name A POSIX class name without its brackets, e.g. `alpha`.
     * \return The equivalent range text for a bracket expression, or empty if the name is unknown.
     */
    inline std::string posix_class_ranges(std::string_view name)
    {
      if (name == "alpha") { return "A-Za-z"; }
      if (name == "digit") { return "0-9"; }
      if (name == "alnum") { return "0-9A-Za-z"; }
      if (name == "upper") { return "A-Z"; }
      if (name == "lower") { return "a-z"; }
      if (name == "xdigit") { return "0-9A-Fa-f"; }
      if (name == "space") { return "\\t\\n\\x0b\\f\\r "; }
      if (name == "blank") { return "\\t "; }
      if (name == "cntrl") { return "\\x00-\\x1f\\x7f"; }
      if (name == "print") { return "\\x20-\\x7e"; }
      if (name == "graph") { return "\\x21-\\x7e"; }
      if (name == "punct") { return "!-/:-@\\[-`{-~"; }
      return {};
    }

    /*!
     * \brief Translates a POSIX bracket expression, the same in BRE and ERE: a POSIX class becomes its ASCII
     *        ranges, other members pass through.
     * \param[in]     p   The pattern being translated.
     * \param[in,out] i   Cursor at the opening bracket; advanced past the expression on success.
     * \param[in,out] out Destination the translated bracket is appended to.
     * \return `false` on an unterminated expression, an unknown class or a collating element; \p out is then
     *         unchanged.
     */
    [[nodiscard]] inline bool translate_bracket(std::string_view p,
                                                std::size_t&     i,
                                                std::string&     out)
    {
      const std::size_t n   {p.size()};
      std::string       cls {'['};
      i += 1;
      if (i < n && p[i] == '^') { cls += '^'; i += 1; }
      if (i < n && p[i] == ']') { cls += "\\]"; i += 1; } // a leading `]` is a literal member in POSIX — escape
                                                          // it for REAL, where a bare `[]` opens an empty class
      while (i < n && p[i] != ']') {
        if (p[i] == '[' && i + 1 < n && p[i + 1] == ':') {
          const std::size_t close  {p.find(":]", i + 2)};
          if (close == std::string_view::npos) { return false; }
          const std::string ranges {posix_class_ranges(p.substr(i + 2, close - (i + 2)))};
          if (ranges.empty()) { return false; }
          cls += ranges;
          i    = close + 2;
        }
        else if (p[i] == '[' && i + 1 < n && (p[i + 1] == '.' || p[i + 1] == '=')) {
          return false; // collating [.x.] / equivalence [=x=]
        }
        else {
          cls += p[i];
          i   += 1;
        }
      }
      if (i >= n) { return false; } // unterminated class
      cls += ']';
      i   += 1;
      out += cls;
      return true;
    }

    /*!
     * \brief Appends REAL's form of the awk escape at \p i, as the exact byte `\xHH`: `\b` is a backspace, not a
     *        word boundary; `\a`, `\n`, `\t`, `\r`, `\f`, `\v` the controls; `\/` and `\"` literals; `\ddd` an
     *        octal byte, up to 0377.
     * \param[in]     p   The pattern being translated.
     * \param[in,out] i   Cursor at the backslash; advanced past the escape on success.
     * \param[in,out] out Destination the translated escape is appended to.
     * \return `false` if the escape is not one awk defines.
     */
    [[nodiscard]] inline bool append_awk_escape(std::string_view p,
                                                std::size_t&     i,
                                                std::string&     out)
    {
      const std::size_t n {p.size()};
      const char        d {p[i + 1]};
      const auto        emit_hex {[&out](unsigned v) {
                                    constexpr std::string_view hex {"0123456789abcdef"};
                                    out += "\\x";
                                    out += hex[(v >> 4U) & 0xFU];
                                    out += hex[v & 0xFU];
                                  }};
      switch (d) {
        case 'b': emit_hex(0x08U); i += 2; return true; // BACKSPACE, not a word boundary
        case 'a': emit_hex(0x07U); i += 2; return true;
        case 'n': emit_hex(0x0AU); i += 2; return true;
        case 't': emit_hex(0x09U); i += 2; return true;
        case 'r': emit_hex(0x0DU); i += 2; return true;
        case 'f': emit_hex(0x0CU); i += 2; return true;
        case 'v': emit_hex(0x0BU); i += 2; return true;
        case '/': out                += '/'; i += 2; return true; // an escaped delimiter -> a literal slash
        case '"': out                += '"'; i += 2; return true; // a literal quote
        default: break;
      }
      if (d >= '0' && d <= '7') {
        unsigned    val    {0};
        std::size_t k      {i + 1};
        std::size_t digits {0};
        while (k < n && digits < 3 && p[k] >= '0' && p[k] <= '7') {
          val = (val * 8U) + static_cast<unsigned>(p[k] - '0');
          ++k;
          ++digits;
        }
        if (val > 0xFFU) { return false; } // an octal overflow (> 0377) -> std
        emit_hex(val);
        i = k;
        return true;
      }
      return false; // `\d` `\w` `\s` … — no awk meaning; decline
    }

    /*!
     * \brief Whether \p p has an empty alternation branch (`(|`, `|)`, `||`, or a `|` at either end), which std
     *        rejects in the POSIX grammars. Conservative: a false positive costs only linear coverage, a false
     *        negative would be a silent over-accept.
     * \param[in] p The pattern text.
     * \return `true` if a branch is empty, so the translation declines.
     */
    [[nodiscard]] inline bool has_empty_alternation_branch(std::string_view p)
    {
      bool in_class {false};
      for (std::size_t i = 0; i < p.size(); ++i) {
        const char c {p[i]};
        if (c == '\\') { ++i; continue; } // skip the escaped character
        if (in_class) {
          if (c == ']') { in_class = false; }
          continue;
        }
        if (c == '[') { in_class = true; continue; }
        if (c == '|') {
          const bool left_empty  {i == 0 || p[i - 1] == '(' || p[i - 1] == '|'};
          const bool right_empty {i + 1 >= p.size() || p[i + 1] == ')' || p[i + 1] == '|'};
          if (left_empty || right_empty) { return true; }
        }
      }
      return false;
    }

    /*!
     * \brief Translates an ERE pattern (with \p awk, an awk one: \ref append_awk_escape) to REAL's syntax, or
     *        `nullopt` on a construct the grammars read differently: an ECMAScript shorthand, an ambiguous `{`, an
     *        unknown class, an empty branch (\ref has_empty_alternation_branch).
     * \param[in] p   The ERE pattern.
     * \param[in] awk Whether awk's extra escapes are in scope.
     * \return The ECMAScript equivalent, or `std::nullopt` when the pattern cannot be translated.
     */
    [[nodiscard]] inline std::optional<std::string> translate_ere(std::string_view p,
                                                                  bool             awk = false)
    {
      if (has_empty_alternation_branch(p)) { return std::nullopt; }
      std::string       out;
      std::size_t       i {0};
      const std::size_t n {p.size()};
      while (i < n) {
        const char c {p[i]};
        if (c == '\\') {
          if (i + 1 >= n) { return std::nullopt; } // trailing backslash
          const char d {p[i + 1]};
          // `\]` and `\}` outside a class are undefined in POSIX, and libc++ and libstdc++ disagree on them.
          if (d == ']' || d == '}') { return std::nullopt; }
          if (std::string_view {".[]{}()*+?|^$\\"}.find(d) != std::string_view::npos) {
            out += '\\';
            out += d;
            i   += 2;
            continue; // an escaped metacharacter is a literal in both grammars
          }
          if (awk && append_awk_escape(p, i, out)) { continue; } // awk C-escapes (\b=BS, \n, octal, …)
          return std::nullopt;                                 // \d, \w, \b, … — no ERE meaning; fall back to std
        }
        if (c == '[') {
          if (!translate_bracket(p, i, out)) { return std::nullopt; }
          continue;
        }
        if (c == '{') {
          // keep only a strict interval {n}/{n,}/{n,m}; an ambiguous `{` is literal in POSIX (it diverges).
          std::size_t       j  {i + 1};
          const std::size_t ds {j};
          while (j < n && p[j] >= '0' && p[j] <= '9') { ++j; }
          bool ok              {j > ds};
          if (ok && j < n && p[j] == ',') {
            ++j;
            while (j < n && p[j] >= '0' && p[j] <= '9') { ++j; }
          }
          ok = ok && j < n && p[j] == '}';
          if (!ok) { return std::nullopt; }
          out += p.substr(i, (j + 1) - i);
          i    = j + 1;
          continue;
        }
        out += c; // common production (. * + ? | ( ) ^ $ literal) — read alike
        i   += 1;
      }
      return out;
    }

    /*!
     * \brief Translates a BRE pattern to REAL's syntax, or `nullopt`: `\(`, `\)` and `\{n\}` group and quantify,
     *        a bare `( ) { } | + ?` is escaped as the literal it is. Declines on a backreference, an ECMAScript
     *        escape, a non-strict `\{`, an unknown class, a `*` opening an expression, or a `^` or `$` away from
     *        the pattern's ends.
     * \param[in] p The BRE pattern.
     * \return The ECMAScript equivalent, or `std::nullopt` when it cannot be translated.
     */
    [[nodiscard]] inline std::optional<std::string> translate_bre(std::string_view p)
    {
      std::string       out;
      std::size_t       i        {0};
      const std::size_t n        {p.size()};
      bool              at_start {true}; // pattern start or just after `\(`, where `*` would be a literal
      while (i < n) {
        const char c {p[i]};
        if (c == '\\') {
          if (i + 1 >= n) { return std::nullopt; }                          // trailing backslash
          const char d {p[i + 1]};
          if (d == '(') { out += '('; i += 2; at_start = true;  continue; } // \( -> group open
          if (d == ')') { out += ')'; i += 2; at_start = false; continue; } // \) -> group close
          if (d == '{') {                                                   // \{n\} / \{n,\} / \{n,m\} interval
            std::size_t       j  {i + 2};
            const std::size_t ds {j};
            while (j < n && p[j] >= '0' && p[j] <= '9') { ++j; }
            bool ok              {j > ds};
            if (ok && j < n && p[j] == ',') {
              ++j;
              while (j < n && p[j] >= '0' && p[j] <= '9') { ++j; }
            }
            if (!ok || j + 1 >= n || p[j] != '\\' || p[j + 1] != '}') { return std::nullopt; } // not a strict \{...\}
            out     += '{';
            out     += p.substr(i + 2, j - (i + 2));
            out     += '}';
            i        = j + 2;
            at_start = false;
            continue;
          }
          if (d == ']' || d == '}') { return std::nullopt; } // `\]` / `\}` outside a class: undefined, std-divergent
          if (std::string_view {".[]*\\^$"}.find(d) != std::string_view::npos) {
            out      += '\\'; // an escaped metacharacter is a literal in both grammars
            out      += d;
            i        += 2;
            at_start  = false;
            continue;
          }
          return std::nullopt; // \1-\9 backref, \d\w\s\b, \+ \? \| (not BRE), stray \} -> decline
        }
        if (c == '(' || c == ')' || c == '{' || c == '}' || c == '|' || c == '+' || c == '?') {
          out      += '\\';    // bare, these are literals in BRE -> escape for REAL
          out      += c;
          i        += 1;
          at_start  = false;
          continue;
        }
        if (c == '^') {
          if (i == 0) { out += '^'; i += 1; at_start = false; continue; }   // anchor at the pattern head (libs agree)
          return std::nullopt;                                              // a medial `^` is a POSIX literal, but libstdc++ reads it as an anchor while
                                                                            // libc++ reads it as a literal — decline so compat stays ≡ its own std
        }
        if (c == '$') {
          if (i + 1 == n) { out += '$'; i += 1; at_start = false; continue; } // anchor at the tail (libs agree)
          return std::nullopt;                                              // a medial `$` — the same libstdc++/libc++ disagreement; decline to std
        }
        if (c == '*') {
          if (at_start) { return std::nullopt; }                            // a leading `*` is a literal in POSIX BRE -> decline (rare)
          out += '*';                                                       // quantifier
          i   += 1;
          continue;
        }
        if (c == '[') {
          if (!translate_bracket(p, i, out)) { return std::nullopt; }
          at_start = false;
          continue;
        }
        out      += c; // `.` and ordinary literals — read alike
        i        += 1;
        at_start  = false;
      }
      return out;
    }

    /*!
     * \brief grep / egrep: each newline-separated line is translated by \p translate_line and the lines joined
     *        with `|`, the lowest precedence, so each line keeps its own anchors. A line that declines, or an
     *        empty one, declines the whole pattern.
     * \param[in] p              The pattern, whose newlines separate alternatives (grep/egrep).
     * \param[in] translate_line Applied to each line; its `std::nullopt` fails the whole translation.
     * \return The joined ECMAScript alternation, or `std::nullopt` if any line failed.
     */
    template <typename LineFn>
    [[nodiscard]] inline std::optional<std::string> translate_newline_alt(std::string_view p,
                                                                          LineFn           translate_line)
    {
      std::string out;
      std::size_t start {0};
      bool        first {true};
      while (true) {
        const std::size_t      nl          {p.find('\n', start)};
        const std::string_view line        {p.substr(start, (nl == std::string_view::npos ? p.size() : nl) - start)};
        if (line.empty()) { return std::nullopt; } // a blank branch -> std
        const std::optional<std::string> t {translate_line(line)};
        if (!t) { return std::nullopt; }
        if (!first) { out += '|'; }
        out  += *t;
        first = false;
        if (nl == std::string_view::npos) { break; }
        start = nl + 1;
      }
      return out;
    }

    /*!
     * \brief Dispatches a single POSIX grammar to its translator; `nullopt` for no grammar bit or several, or
     *        under `collate` or `nosubs`.
     * \param[in] p The pattern text.
     * \param[in] f The syntax options, which select the POSIX grammar to translate from.
     * \return The ECMAScript equivalent, or `std::nullopt` when the options or pattern decline.
     */
    [[nodiscard]] inline std::optional<std::string> translate_posix(std::string_view                    p,
                                                                    regex_constants::syntax_option_type f)
    {
      using namespace regex_constants;
      if ((f & (collate | nosubs)) != ECMAScript) { return std::nullopt; }
      int grammars {0};
      if ((f & basic) != ECMAScript) { ++grammars; }
      if ((f & extended) != ECMAScript) { ++grammars; }
      if ((f & awk) != ECMAScript) { ++grammars; }
      if ((f & grep) != ECMAScript) { ++grammars; }
      if ((f & egrep) != ECMAScript) { ++grammars; }
      if (grammars != 1) { return std::nullopt; } // ECMAScript (0), or a mix — not a single POSIX grammar
      if ((f & extended) != ECMAScript) { return translate_ere(p); }
      if ((f & basic) != ECMAScript) { return translate_bre(p); }
      if ((f & awk) != ECMAScript) { return translate_ere(p, /*awk=*/ true); }
      if ((f & grep) != ECMAScript) { return translate_newline_alt(p, [](std::string_view l) { return translate_bre(l); }); }
      return translate_newline_alt(p, [](std::string_view l) { return translate_ere(l); }); // egrep
    }

    /*!
     * \brief Maps compat options to REAL's flags, always with `bytes | ecma`: one REAL byte per `std::regex` char.
     * \param[in] f The compat syntax options.
     * \return The equivalent \ref real::flags.
     */
    inline real::flags to_real(regex_constants::syntax_option_type f) noexcept
    {
      real::flags r {real::flags::bytes | real::flags::ecma};
      if ((f & regex_constants::icase) != regex_constants::ECMAScript) { r = r | real::flags::icase; }
      if ((f & regex_constants::multiline) != regex_constants::ECMAScript) { r = r | real::flags::multiline; }
      return r;
    }

    /*!
     * \brief Maps compat options to std::regex syntax flags (the fallback path).
     * \param[in] f The compat syntax options.
     * \return The equivalent `std::regex_constants::syntax_option_type`.
     */
    inline std::regex_constants::syntax_option_type to_std(regex_constants::syntax_option_type f) noexcept
    {
      namespace sc = std::regex_constants;
      sc::syntax_option_type s {};
      using namespace regex_constants;
      if ((f & icase) != ECMAScript) { s |= sc::icase; }
      if ((f & nosubs) != ECMAScript) { s |= sc::nosubs; }
      if ((f & optimize) != ECMAScript) { s |= sc::optimize; }
      if ((f & collate) != ECMAScript) { s |= sc::collate; }
      if ((f & multiline) != ECMAScript) { s |= sc::multiline; }
      if ((f & basic) != ECMAScript) { s |= sc::basic; }
      else if ((f & extended) != ECMAScript) { s |= sc::extended; }
      else if ((f & awk) != ECMAScript) { s |= sc::awk; }
      else if ((f & grep) != ECMAScript) { s |= sc::grep; }
      else if ((f & egrep) != ECMAScript) { s |= sc::egrep; }
      else { s |= sc::ECMAScript; }
      return s;
    }

    /*!
     * \brief Runs \p call on the std backend and reports its errors as \ref regex_error, the
     *        type every error of this layer has. A `std::basic_regex` can fail while it matches
     *        (`error_complexity`, `error_stack`), long after it was built.
     * \tparam Call A callable taking no argument.
     * \param[in] call The std operation.
     * \return What \p call returns.
     * \throws real::compat::regex_error when \p call throws a `std::regex_error`.
     */
    template <typename Call>
    decltype(auto) std_call(Call && call)
    {
      try {
        return std::forward<Call>(call)();
      }
      catch (const regex_error&) {
        throw; // already the compat type (a lazy build's error)
      }
      catch (const std::regex_error& std_error) {
        throw regex_error(std_error);
      }
    }

    /*!
     * \brief An engine built on first use and published once: a reader after the build takes no lock, and a copy
     *        made while another thread builds sees the finished engine or none (and then builds its own).
     *        Copyable, so the regex that holds it stays copyable.
     * \tparam StdRegex The engine: a `std::basic_regex`, or the `end_variants` of a REAL pattern.
     */
    template <typename StdRegex>
    class lazy_std_engine
    {
    public:

      lazy_std_engine() = default;

      /*!
       * \brief Copies \p other's engine if it is published.
       * \param[in] other The source.
       */
      lazy_std_engine(const lazy_std_engine& other)
      {
        copy_from(other);
      }

      /*!
       * \brief Takes \p other's engine if it is published; \p other is left without one.
       * \param[in,out] other The source.
       */
      lazy_std_engine(lazy_std_engine&& other) noexcept
      {
        if (other.ready_.load(std::memory_order_acquire)) {
          value_ = std::move(other.value_);
          ready_.store(true, std::memory_order_relaxed);
          other.reset();
        }
      }

      /*!
       * \brief Replaces this engine with a copy of \p other's, if published.
       * \param[in] other The source.
       * \return This.
       */
      lazy_std_engine& operator=(const lazy_std_engine& other)
      {
        if (this != &other) {
          reset();
          copy_from(other);
        }
        return *this;
      }

      /*!
       * \brief Replaces this engine with \p other's, if published; \p other is left without one.
       * \param[in,out] other The source.
       * \return This.
       */
      lazy_std_engine& operator=(lazy_std_engine&& other) noexcept
      {
        if (this != &other) {
          reset();
          if (other.ready_.load(std::memory_order_acquire)) {
            value_ = std::move(other.value_);
            ready_.store(true, std::memory_order_relaxed);
            other.reset();
          }
        }
        return *this;
      }

      ~lazy_std_engine() = default;

      /*!
       * \brief The published engine.
       * \return It, or null before \ref publish.
       */
      [[nodiscard]] const StdRegex* get() const noexcept
      {
        return ready_.load(std::memory_order_acquire) ? &*value_ : nullptr;
      }

      /*!
       * \brief Publishes \p engine; the caller holds the build lock and has seen \ref get return null.
       * \param[in] engine The built engine.
       * \return The published engine.
       */
      const StdRegex& publish(StdRegex&& engine) const
      {
        value_.emplace(std::move(engine));
        ready_.store(true, std::memory_order_release);
        return *value_;
      }

      /*!
       * \brief Drops the engine; not concurrent with any other use of this object.
       */
      void reset() noexcept
      {
        ready_.store(false, std::memory_order_relaxed);
        value_.reset();
      }

      /*!
       * \brief Exchanges the engines; not concurrent with any other use of either object.
       * \param[in,out] other The other engine.
       */
      void swap(lazy_std_engine& other) noexcept
      {
        const bool mine {ready_.load(std::memory_order_relaxed)};
        ready_.store(other.ready_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.ready_.store(mine, std::memory_order_relaxed);
        value_.swap(other.value_);
      }

    private:

      /*!
       * \brief Copies \p other's engine if published; this one holds none.
       * \param[in] other The source.
       */
      void copy_from(const lazy_std_engine& other)
      {
        if (const StdRegex* engine {other.get()}; engine != nullptr) {
          value_.emplace(*engine);
          ready_.store(true, std::memory_order_relaxed);
        }
      }

      mutable std::optional<StdRegex> value_;         //!< The engine, set once under the build lock.
      mutable std::atomic<bool>       ready_ {false}; //!< Released after \ref value_ is set: acquire it before reading.
    };

    /*!
     * \brief \p pattern with `match_not_eol` and `match_not_eow` written into it, so a REAL search honors them
     *        without run-time context: under `not_eol` a `$` holds at no end of the sequence (in multiline only
     *        before a line terminator), under `not_eow` a `\b` holds at no end and a `\B` does.
     * \param[in] pattern   An ECMAScript pattern as REAL compiles it.
     * \param[in] not_eol   Rewrite `$`.
     * \param[in] not_eow   Rewrite `\b` and `\B`.
     * \param[in] multiline The pattern is multiline.
     * \return The rewritten pattern.
     */
    [[nodiscard]] inline std::string rewrite_end_context(std::string_view pattern,
                                                         bool             not_eol,
                                                         bool             not_eow,
                                                         bool             multiline)
    {
      std::string out;
      out.reserve(pattern.size() + 16U);
      bool in_class {false};
      for (std::size_t i {0}; i < pattern.size(); ++i) {
        const char c {pattern[i]};
        if (c == '\\' && i + 1 < pattern.size()) {
          const char next {pattern[i + 1]};
          ++i;
          if (!in_class && not_eow && next == 'b') {
            out += "(?:\\b(?=[\\s\\S]))";
          }
          else if (!in_class && not_eow && next == 'B') {
            out += "(?:\\B|(?![\\s\\S]))";
          }
          else {
            out += c;
            out += next;
          }
          continue;
        }
        if (in_class) {
          in_class = c != ']';
          out     += c;
          continue;
        }
        if (c == '[') {
          in_class = true;
          out     += c;
          if (i + 1 < pattern.size() && pattern[i + 1] == '^') {
            out += '^';
            ++i;
          }
          continue;
        }
        if (c == '$' && not_eol) {
          out += multiline ? "(?=[\\n\\r])" : "(?!)";
          continue;
        }
        out += c;
      }
      return out;
    }

    /*!
     * \brief One variant of a pattern for `match_not_eol` / `match_not_eow`: REAL's compiled rewrite, or the
     *        original's own engine when the rewrite changed nothing, or neither when REAL does not compile the
     *        rewrite (a `$` or `\b` inside a lookaround would nest one).
     */
    struct end_variant
    {
      std::optional<real::regex> engine;            //!< The rewritten pattern, compiled.
      bool                       unchanged {false}; //!< Nothing to rewrite: the original engine serves.
    };

    /*!
     * \brief The three variants a pattern may need: `not_eol`, `not_eow`, both.
     */
    struct end_variants
    {
      end_variant not_eol; //!< `match_not_eol` alone.
      end_variant not_eow; //!< `match_not_eow` alone.
      end_variant both;    //!< Both flags.
    };
  } // namespace detail

  /*!
   * \brief A `std::basic_regex`-compatible pattern, backed by REAL where it can serve the pattern, else by `std`.
   * \tparam CharT  Character type (`char`; other types route straight to `std`).
   * \tparam Traits Regex traits (std parity).
   */
  template <typename CharT = char, typename Traits = std::regex_traits<CharT>>
  class basic_regex
  {
  public:

    using value_type  = CharT;                                           //!< Character type.
    using traits_type = Traits;                                          //!< Regex traits (std parity).
    using string_type = std::basic_string<CharT>;                        //!< Pattern string type.
    using flag_type   = regex_constants::syntax_option_type;             //!< Option type.
    using locale_type = typename Traits::locale_type;                    //!< The traits' locale type (std parity).

    static constexpr flag_type icase      {regex_constants::icase};      //!< As `std::basic_regex::icase`.
    static constexpr flag_type nosubs     {regex_constants::nosubs};     //!< As `std::basic_regex::nosubs`.
    static constexpr flag_type optimize   {regex_constants::optimize};   //!< As `std::basic_regex::optimize`.
    static constexpr flag_type collate    {regex_constants::collate};    //!< As `std::basic_regex::collate`.
    static constexpr flag_type ECMAScript {regex_constants::ECMAScript}; //!< As `std::basic_regex::ECMAScript`.
    static constexpr flag_type basic      {regex_constants::basic};      //!< As `std::basic_regex::basic`.
    static constexpr flag_type extended   {regex_constants::extended};   //!< As `std::basic_regex::extended`.
    static constexpr flag_type awk        {regex_constants::awk};        //!< As `std::basic_regex::awk`.
    static constexpr flag_type grep       {regex_constants::grep};       //!< As `std::basic_regex::grep`.
    static constexpr flag_type egrep      {regex_constants::egrep};      //!< As `std::basic_regex::egrep`.
    static constexpr flag_type multiline  {regex_constants::multiline};  //!< As `std::basic_regex::multiline`.

    /*! \brief An empty pattern on the std backend — the variant's first alternative default-constructs. */
    basic_regex() = default;

    /*!
     * \brief Compiles \p pattern from a C string.
     * \param[in] pattern NUL-terminated pattern text.
     * \param[in] f       Syntax options; the grammar they select may route the pattern to `std`.
     * \param[in] pol     Strict rejects a pattern REAL cannot represent linearly; fallback routes it to `std`.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    explicit basic_regex(const CharT* pattern,
                         flag_type    f   = regex_constants::ECMAScript,
                         policy       pol = policy::strict)
    {
      policy_ = pol;
      compile(std::basic_string_view<CharT>(pattern), f);
    }

    /*!
     * \brief Compiles \p pattern from an owned string.
     * \param[in] pattern The pattern text.
     * \param[in] f       Syntax options.
     * \param[in] pol     Rejection policy; see the C-string overload.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    explicit basic_regex(const string_type& pattern,
                         flag_type          f   = regex_constants::ECMAScript,
                         policy             pol = policy::strict)
    {
      policy_ = pol;
      compile(std::basic_string_view<CharT>(pattern), f);
    }

    /*!
     * \brief Compiles the first \p len characters of \p pattern, which need not be NUL-terminated.
     * \param[in] pattern Pattern text.
     * \param[in] len     Its length in characters.
     * \param[in] f       Syntax options.
     * \param[in] pol     Rejection policy.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    basic_regex(const CharT* pattern,
                std::size_t  len,
                flag_type    f   = regex_constants::ECMAScript,
                policy       pol = policy::strict)
    {
      policy_ = pol;
      compile(std::basic_string_view<CharT>(pattern, len), f);
    }

    /*!
     * \brief Compiles the pattern in `[begin, end)`.
     * \param[in] begin Start of the pattern text.
     * \param[in] end   One past its end.
     * \param[in] f     Syntax options.
     * \param[in] pol   Rejection policy.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    template <typename It>
    basic_regex(It        begin,
                It        end,
                flag_type f   = regex_constants::ECMAScript,
                policy    pol = policy::strict)
    {
      policy_ = pol;
      const string_type pattern(begin, end);
      compile(std::basic_string_view<CharT>(pattern), f);
    }

    /*!
     * \brief Compiles the characters of \p pattern.
     * \param[in] pattern The pattern text.
     * \param[in] f       Syntax options.
     * \param[in] pol     Rejection policy.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    basic_regex(std::initializer_list<CharT> pattern,
                flag_type                    f   = regex_constants::ECMAScript,
                policy                       pol = policy::strict)
    {
      policy_ = pol;
      compile(std::basic_string_view<CharT>(pattern.begin(), pattern.size()), f);
    }

    /*!
     * \brief Replaces the pattern with \p pattern, as `assign(pattern)`.
     * \param[in] pattern NUL-terminated pattern text.
     * \return `*this`.
     * \throws real::compat::regex_error as \ref assign does; `*this` is then unchanged.
     */
    basic_regex& operator=(const CharT* pattern)
    {
      assign(pattern);
      return *this;
    }

    /*!
     * \brief Replaces the pattern with \p pattern, as `assign(pattern)`.
     * \param[in] pattern The pattern text.
     * \return `*this`.
     * \throws real::compat::regex_error as \ref assign does; `*this` is then unchanged.
     */
    basic_regex& operator=(std::initializer_list<CharT> pattern)
    {
      assign(pattern);
      return *this;
    }

    /*!
     * \brief Replaces the pattern with \p pattern, as `assign(pattern)`.
     * \tparam ST The string's traits.
     * \tparam SA The string's allocator.
     * \param[in] pattern The pattern text.
     * \return `*this`.
     * \throws real::compat::regex_error as \ref assign does; `*this` is then unchanged.
     */
    template <typename ST, typename SA>
    basic_regex& operator=(const std::basic_string<CharT, ST, SA>& pattern)
    {
      assign(pattern);
      return *this;
    }

    /*!
     * \brief Becomes a copy of \p other.
     * \param[in] other The regex to copy.
     * \return `*this`.
     */
    basic_regex& assign(const basic_regex& other)
    {
      *this = other;
      return *this;
    }

    /*!
     * \brief Takes \p other's pattern.
     * \param[in,out] other The regex to move from.
     * \return `*this`.
     */
    basic_regex& assign(basic_regex&& other) noexcept
    {
      *this = std::move(other);
      return *this;
    }

    /*!
     * \brief Compiles \p pattern in place of the current one, under this regex's policy. Every overload gives
     *        the strong guarantee std's does: on a throw `*this` is unchanged.
     * \param[in] pattern NUL-terminated pattern text.
     * \param[in] f       Syntax options.
     * \return `*this`.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    basic_regex& assign(const CharT* pattern,
                        flag_type    f = regex_constants::ECMAScript)
    {
      return recompile(std::basic_string_view<CharT>(pattern), f);
    }

    /*!
     * \brief Compiles the first \p len characters of \p pattern in place of the current pattern.
     * \param[in] pattern Pattern text.
     * \param[in] len     Its length in characters.
     * \param[in] f       Syntax options.
     * \return `*this`.
     * \throws real::compat::regex_error as the C-string overload does; `*this` is then unchanged.
     */
    basic_regex& assign(const CharT* pattern,
                        std::size_t  len,
                        flag_type    f = regex_constants::ECMAScript)
    {
      return recompile(std::basic_string_view<CharT>(pattern, len), f);
    }

    /*!
     * \brief Compiles \p pattern in place of the current pattern.
     * \tparam ST The string's traits.
     * \tparam SA The string's allocator.
     * \param[in] pattern The pattern text.
     * \param[in] f       Syntax options.
     * \return `*this`.
     * \throws real::compat::regex_error as the C-string overload does; `*this` is then unchanged.
     */
    template <typename ST, typename SA>
    basic_regex& assign(const std::basic_string<CharT, ST, SA>& pattern,
                        flag_type                               f = regex_constants::ECMAScript)
    {
      return recompile(std::basic_string_view<CharT>(pattern.data(), pattern.size()), f);
    }

    /*!
     * \brief Compiles the pattern in `[first, last)` in place of the current pattern.
     * \tparam InputIt An input iterator over characters.
     * \param[in] first Start of the pattern text.
     * \param[in] last  One past its end.
     * \param[in] f     Syntax options.
     * \return `*this`.
     * \throws real::compat::regex_error as the C-string overload does; `*this` is then unchanged.
     */
    template <typename InputIt>
    basic_regex& assign(InputIt   first,
                        InputIt   last,
                        flag_type f = regex_constants::ECMAScript)
    {
      const string_type pattern(first, last);
      return recompile(std::basic_string_view<CharT>(pattern), f);
    }

    /*!
     * \brief Compiles the characters of \p pattern in place of the current pattern.
     * \param[in] pattern The pattern text.
     * \param[in] f       Syntax options.
     * \return `*this`.
     * \throws real::compat::regex_error as the C-string overload does; `*this` is then unchanged.
     */
    basic_regex& assign(std::initializer_list<CharT> pattern,
                        flag_type                    f = regex_constants::ECMAScript)
    {
      return recompile(std::basic_string_view<CharT>(pattern.begin(), pattern.size()), f);
    }

    /*!
     * \brief Number of marked sub-expressions (excluding group 0), as `std::basic_regex`.
     * \return The group count.
     */
    [[nodiscard]] std::size_t mark_count() const noexcept
    {
      return mark_count_;
    }

    /*!
     * \brief The flags this regex was built with.
     * \return Those flags.
     */
    [[nodiscard]] flag_type flags() const noexcept
    {
      return flags_;
    }

    /*!
     * \brief Exchanges engines, flags, policy and cached state with \p other.
     * \param[in,out] other The regex to swap with.
     */
    void swap(basic_regex& other) noexcept
    {
      engine_.swap(other.engine_);
      std::swap(pattern_, other.pattern_);
      std::swap(flags_, other.flags_);
      std::swap(mark_count_, other.mark_count_);
      std::swap(nullable_, other.nullable_);
      std::swap(nullable_captured_repeat_, other.nullable_captured_repeat_);
      std::swap(posix_longest_, other.posix_longest_);
      lazy_std_.swap(other.lazy_std_);
      end_variants_.swap(other.end_variants_);
      std::swap(policy_, other.policy_);
    }

    /*!
     * \brief True if this regex is backed by the `real` engine (vs the std fallback).
     * \return `true` if REAL's linear engine holds it.
     */
    [[nodiscard]] bool uses_real() const noexcept
    {
      return std::holds_alternative<real::regex>(engine_);
    }

    /*!
     * \brief True if `std::regex` holds this pattern, which then has no linear-time guarantee: a pattern REAL
     *        cannot serve under `policy::fallback`, any non-`char` instantiation, a default-constructed regex.
     * \return `true` if `std::basic_regex` holds it.
     */
    [[nodiscard]] bool uses_fallback() const noexcept
    {
      return !uses_real();
    }

    /*!
     * \brief The drop-in policy this regex was constructed with.
     * \return Strict or fallback.
     */
    [[nodiscard]] compat::policy policy() const noexcept
    {
      return policy_;
    }

    /*!
     * \brief Access the active backend (engine-facing; used by the free functions).
     * \return The variant holding whichever backend compiled this pattern.
     */
    [[nodiscard]] const std::variant<std::basic_regex<CharT, Traits>, real::regex>& engine() const noexcept
    {
      return engine_;
    }

    /*!
     * \brief Whether the pattern can match the empty string; under a POSIX grammar a nullable pattern's replace
     *        and iteration run on std (\ref uses_real_traversal).
     * \return `true` if it is nullable.
     */
    [[nodiscard]] bool nullable() const noexcept
    {
      return nullable_;
    }

    /*!
     * \brief Whether a POSIX grammar was translated onto REAL (\ref detail::translate_posix): a search then
     *        takes leftmost-longest overall bounds, while captures stay the winning thread's rather than
     *        following POSIX subexpression rules. False on the std backend, which applies POSIX itself.
     * \return `true` under a POSIX grammar that REAL is running.
     */
    [[nodiscard]] bool posix_longest() const noexcept
    {
      return posix_longest_;
    }

    /*!
     * \brief Whether replace and iteration run on REAL: real-backed, not a nullable POSIX pattern (std's
     *        leftmost-longest empty-match traversal is not modelled), and no capturing group nullable under a
     *        quantifier (`(ab|)+a`): REAL captures its last consuming iteration, libstdc++ and libc++ an extra
     *        empty one. Search and match keep that one capture divergence, by design (docs/COMPATIBILITY.md).
     * \return `true` for a real-backed pattern whose traversal `real` models.
     */
    [[nodiscard]] bool uses_real_traversal() const noexcept
    {
      return uses_real() && !nullable_captured_repeat_ && !(nullable_ && posix_longest_);
    }

    /*!
     * \brief The `std::regex` for the std path, built once on demand for a real-backed pattern (a call REAL
     *        cannot honor, a traversal it does not model, a `$0` format).
     *
     * Thread-safe: a static mutex per instantiation serialises the build only; once published
     * (\ref detail::lazy_std_engine) every call reads it lock-free. Not a `std::once_flag`, which is not
     * copyable, as `basic_regex` must be.
     * \return The wrapped `std::basic_regex`; compiling one on demand if this pattern is real-backed.
     */
    [[nodiscard]] const std::basic_regex<CharT, Traits>& std_engine() const
    {
      if (std::holds_alternative<std::basic_regex<CharT, Traits>>(engine_)) {
        return std::get<std::basic_regex<CharT, Traits>>(engine_);
      }
      if (const auto* built {lazy_std_.get()}; built != nullptr) {
        return *built; // published: no lock once built
      }
      static std::mutex     build_mutex; // one per basic_regex<CharT, Traits> instantiation
      const std::lock_guard lock {build_mutex};
      if (const auto* built {lazy_std_.get()}; built == nullptr) {
        try {
          return lazy_std_.publish(std::basic_regex<CharT, Traits>(pattern_.data(), pattern_.size(),
                                                                   detail::to_std(flags_)));
        }
        catch (const std::regex_error& std_error) {
          // A pattern REAL accepts and std rejects: a compat::regex_error, as from the constructor.
          throw regex_error(std_error);
        }
      }
      return *lazy_std_.get();
    }

    /*!
     * \brief The REAL engine a search under `match_not_eol` / `match_not_eow` runs: this pattern's own when it has
     *        nothing those flags change, else its rewrite (see \ref detail::rewrite_end_context), built once on
     *        demand and thread-safely as \ref std_engine is.
     * \param[in] not_eol `match_not_eol` is set.
     * \param[in] not_eow `match_not_eow` is set.
     * \return The engine, or null when the call must go to std (a POSIX grammar, a rewrite REAL does not
     *         compile).
     */
    [[nodiscard]] const real::regex* end_engine(bool not_eol,
                                                bool not_eow) const
    requires(detail::real_eligible<CharT, Traits>)
    {
      if (!uses_real() || posix_longest_) {
        return nullptr;
      }
      const detail::end_variants* built {end_variants_.get()};
      if (built == nullptr) {
        static std::mutex     build_mutex; // one per basic_regex<CharT, Traits> instantiation
        const std::lock_guard lock {build_mutex};
        built = end_variants_.get();
        if (built == nullptr) {
          built = &end_variants_.publish(build_end_variants());
        }
      }
      const detail::end_variant* variant {&built->not_eow};
      if (not_eol && not_eow) {
        variant = &built->both;
      }
      else if (not_eol) {
        variant = &built->not_eol;
      }
      if (variant->unchanged) {
        return &std::get<real::regex>(engine_);
      }
      return variant->engine.has_value() ? &*variant->engine : nullptr;
    }

  private:

    /*!
     * \brief Builds the three `not_eol` / `not_eow` variants of this real-backed ECMAScript pattern.
     * \return The variants.
     */
    [[nodiscard]] detail::end_variants build_end_variants() const
    requires(detail::real_eligible<CharT, Traits>)
    {
      return detail::end_variants {.not_eol = build_end_variant(true, false),
                                   .not_eow = build_end_variant(false, true),
                                   .both    = build_end_variant(true, true)};
    }

    /*!
     * \brief Builds one `not_eol` / `not_eow` variant of this real-backed ECMAScript pattern.
     * \param[in] not_eol `match_not_eol` is set.
     * \param[in] not_eow `match_not_eow` is set.
     * \return The variant.
     */
    [[nodiscard]] detail::end_variant build_end_variant(bool not_eol,
                                                        bool not_eow) const
    requires(detail::real_eligible<CharT, Traits>)
    {
      detail::end_variant    made;
      const std::string_view sv        {pattern_.data(), pattern_.size()};
      const std::string      rewritten {
        detail::rewrite_end_context(sv, not_eol, not_eow, (flags_ & regex_constants::multiline) != 0U)};
      if (rewritten == sv) {
        made.unchanged = true;
        return made;
      }
      try {
        made.engine.emplace(rewritten, detail::to_real(flags_));
      }
      catch (const real::regex_error&) {
        made.engine.reset(); // REAL does not compile the rewrite: the call goes to std
      }
      return made;
    }

    // std backend first so the variant is default-constructible (real::regex has no default ctor).
    std::variant<std::basic_regex<CharT, Traits>, real::regex>           engine_;                                                 //!< Whichever backend compiled this pattern; see \ref uses_real and \ref uses_fallback.
    string_type                                                          pattern_;                                                //!< The pattern text, for the lazy builds.
    flag_type                                                            flags_                    {regex_constants::ECMAScript}; //!< Syntax options it was compiled with (\ref flags).
    std::size_t                                                          mark_count_               {};                            //!< Capturing groups excluding the whole match (\ref mark_count).
    bool                                                                 nullable_                 {};                            //!< empty_match_possible (real-backed).
    bool                                                                 nullable_captured_repeat_ {};                            //!< A capturing group nullable under a quantifier (real-backed); see \ref uses_real_traversal.
    bool                                                                 posix_longest_            {};                            //!< A POSIX grammar translated onto REAL: search uses leftmost-longest bounds.
    detail::lazy_std_engine<std::basic_regex<CharT, Traits>>             lazy_std_;                                               //!< The std engine, built on demand (\ref std_engine).
    detail::lazy_std_engine<detail::end_variants>                        end_variants_;                                           //!< Lazy REAL variants for match_not_eol / match_not_eow.
    compat::policy                                                       policy_                   {policy::strict};              //!< strict rejects ineligible, fallback delegates to std.

    /*!
     * \brief Compiles \p pattern into a fresh regex under this one's policy, then takes it: a throw leaves
     *        `*this` as it was.
     * \param[in] pattern The pattern text.
     * \param[in] f       Syntax options.
     * \return `*this`.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    basic_regex& recompile(std::basic_string_view<CharT> pattern,
                           flag_type                     f)
    {
      basic_regex fresh;
      fresh.policy_ = policy_;
      fresh.compile(pattern, f);
      swap(fresh);
      return *this;
    }

    /*!
     * \brief Compiles \p pattern into this object, replacing whatever it held.
     * \param[in] pattern The pattern text.
     * \param[in] f       Syntax options.
     * \throws real::compat::regex_error on an invalid pattern, or on a strict-policy rejection.
     */
    void compile(std::basic_string_view<CharT> pattern,
                 flag_type                     f)
    {
      flags_   = f;
      pattern_ = string_type(pattern);
      lazy_std_.reset();
      end_variants_.reset();
      nullable_                 = false;
      nullable_captured_repeat_ = false;
      posix_longest_            = false;
      if constexpr (detail::real_eligible<CharT, Traits>) {
        const std::string_view sv {pattern.data(), pattern.size()};
        // A single POSIX grammar that translates runs on REAL with leftmost-longest bounds; a decline or a REAL
        // reject falls through to the checks below.
        if (!detail::pattern_forces_std(sv)) {
          if (const std::optional<std::string> translated {detail::translate_posix(sv, f)}) {
            try {
              real::regex compiled(*translated, detail::to_real(f));
              mark_count_               = compiled.group_count();
              nullable_                 = compiled.raw_program().hints.empty_match_possible;
              nullable_captured_repeat_ = compiled.raw_program().hints.nullable_captured_repeat;
              posix_longest_            = true;
              engine_.template emplace<real::regex>(std::move(compiled));
              return;
            }
            catch (const real::regex_error&) {
              // translated, but REAL cannot represent it
            }
          }
        }
        if (detail::grammar_forces_std(f) || detail::pattern_forces_std(sv)) {
          reject_or_fallback(sv, f, "the pattern uses a construct the linear engine does not represent "
                             "(a backreference, an unbounded lookbehind, a POSIX class, or a "
                             "grammar that forces std)");
          return;
        }
        try {
          real::regex compiled(sv, detail::to_real(f));
          mark_count_               = compiled.group_count();
          nullable_                 = compiled.raw_program().hints.empty_match_possible;
          nullable_captured_repeat_ = compiled.raw_program().hints.nullable_captured_repeat;
          engine_.template emplace<real::regex>(std::move(compiled));
          // No eager std build: a pattern std rejects fails only when a call reaches std (std_engine).
        }
        catch (const real::regex_error& real_error) {
          // A non-ASCII class member never lands here: bytes mode makes it a byte class, kept on REAL.
          reject_or_fallback(sv, f, real_error.what());
        }
      }
      else {
        // Not `char` with default traits: always std, under either policy.
        try {
          engine_.template emplace<std::basic_regex<CharT, Traits>>(pattern.data(), pattern.size(),
                                                                    detail::to_std(f));
          mark_count_ = std::get<std::basic_regex<CharT, Traits>>(engine_).mark_count();
        }
        catch (const std::regex_error& std_error) {
          throw regex_error(std_error);
        }
      }
    }

    /*!
     * \brief The policy branch for a pattern REAL cannot serve: `strict` throws (see \ref regex_error),
     *        `fallback` compiles it on `std::regex`.
     * \param[in] sv     The pattern text.
     * \param[in] f      Syntax options.
     * \param[in] reason Message carried by the thrown \ref regex_error under strict policy.
     * \throws real::compat::regex_error under `policy::strict`; compiles on `std` under `policy::fallback`.
     */
    void reject_or_fallback(std::string_view   sv,
                            flag_type          f,
                            const std::string& reason)
    {
      if (policy_ == policy::strict) {
        // A syntax error keeps std's own code; only a pattern std accepts is an error_complexity rejection.
        if constexpr (detail::real_eligible<CharT, Traits>) {
          try {
            const std::basic_regex<CharT, Traits> probe(sv.data(), sv.size(), detail::to_std(f));
            static_cast<void>(probe);     // std accepts it: a REAL-only limit
          }
          catch (const std::regex_error& std_error) {
            throw regex_error(std_error); // invalid for both: std's code
          }
        }
        throw regex_error(std::regex_constants::error_complexity,
                          "real::compat (strict policy): this pattern requires a non-linear (backtracking) "
                          "engine — " + reason + ". Construct with policy::fallback to delegate it to "
                          "std::regex, forfeiting the linear-time / ReDoS-safe guarantee for it.");
      }
      emplace_std(sv, f);
    }

    /*!
     * \brief Compiles \p sv on the standard-library backend and stores it.
     * \param[in] sv The pattern text.
     * \param[in] f  Syntax options, translated by \ref detail::to_std.
     */
    void emplace_std(std::string_view sv,
                     flag_type        f)
    {
      try {
        auto& std_engine = engine_.template emplace<std::basic_regex<CharT, Traits>>(
          sv.data(), sv.size(), detail::to_std(f));
        mark_count_ = std_engine.mark_count();
      }
      catch (const std::regex_error& std_error) {
        throw regex_error(std_error); // never a raw std::regex_error out of this layer
      }
    }
  };

  using regex  = basic_regex<char>;    //!< The char-path compat regex (real-eligible).
  using wregex = basic_regex<wchar_t>; //!< The wide compat regex (always the std backend).
} // namespace real::compat

#endif // REAL_STD_REGEX_CORE_HPP
