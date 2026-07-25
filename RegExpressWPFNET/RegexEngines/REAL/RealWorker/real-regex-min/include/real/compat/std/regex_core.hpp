/*!
 * \file std/regex_core.hpp
 * \brief std::regex-compatibility layer, part 1/3: the constants, the error type, the backend-routing
 *        screens, and `basic_regex`. Included via the `std/regex.hpp` umbrella — do not include the
 *        parts directly; `#include <real/compat/std/regex.hpp>` stays the one public entry point.
 */
#ifndef REAL_STD_REGEX_CORE_HPP
#define REAL_STD_REGEX_CORE_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>).

#include <real/version.hpp>

#include <cstddef>
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

    //! \brief Grammar / option flags (own bit values; mapped to real::flags or std at construction).
    enum syntax_option_type : unsigned
    {
      ECMAScript = 0,        //!< The default grammar.
      icase      = 1U << 0U, //!< Case-insensitive (ASCII).
      nosubs     = 1U << 1U, //!< Do not expose sub-expressions (groups still computed).
      optimize   = 1U << 2U, //!< Hint to favour matching speed; honoured as a no-op.
      collate    = 1U << 3U, //!< Locale-sensitive ranges; forces the std backend.
      multiline  = 1U << 4U, //!< `^`/`$` match at line boundaries.
      basic      = 1U << 5U, //!< POSIX basic — std backend.
      extended   = 1U << 6U, //!< POSIX extended — std backend.
      awk        = 1U << 7U, //!< awk grammar — std backend.
      grep       = 1U << 8U, //!< grep grammar — std backend.
      egrep      = 1U << 9U, //!< egrep grammar — std backend.
    };

    constexpr syntax_option_type operator|(syntax_option_type a,
                                           syntax_option_type b) noexcept
    {
      return static_cast<syntax_option_type>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    constexpr syntax_option_type operator&(syntax_option_type a,
                                           syntax_option_type b) noexcept
    {
      return static_cast<syntax_option_type>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }

    //! \brief Match-control flags: the common subset.
    enum match_flag_type : unsigned
    {
      match_default    = 0,
      match_not_bol    = 1U << 0U,   //!< `^` does not match the start of the sequence.
      match_not_eol    = 1U << 1U,   //!< `$` does not match the end of the sequence.
      match_not_bow    = 1U << 2U,   //!< `\b` does not match at the start.
      match_not_eow    = 1U << 3U,   //!< `\b` does not match at the end.
      match_any        = 1U << 4U,
      match_not_null   = 1U << 5U,   //!< Do not match an empty sequence.
      match_continuous = 1U << 6U,   //!< The match must start at the first character.
      match_prev_avail = 1U << 7U,
      format_default    = 0,
      format_sed        = 1U << 8U,  //!< sed/POSIX replacement syntax (routes to std).
      format_no_copy    = 1U << 9U,  //!< Do not copy the parts of the text that did not match.
      format_first_only = 1U << 10U, //!< Replace only the first match.
    };

    constexpr match_flag_type operator|(match_flag_type a,
                                        match_flag_type b) noexcept
    {
      return static_cast<match_flag_type>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    constexpr match_flag_type operator&(match_flag_type a,
                                        match_flag_type b) noexcept
    {
      return static_cast<match_flag_type>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }

    constexpr match_flag_type operator~(match_flag_type a) noexcept
    {
      return static_cast<match_flag_type>(~static_cast<unsigned>(a));
    }

    //! \brief Error categories. Aliased to std's so `regex_error::code()` is a true drop-in.
    using error_type = std::regex_constants::error_type;
  } // namespace regex_constants

  /*!
   * \brief `std::regex_error`-compatible exception.
   *
   * Thrown on two paths, both preserving the `std::regex_error` contract:
   * - **Invalid for both backends** (a syntax error): real rejects, and so does std — the exact std
   *   `.code()` is preserved and `what()` keeps std's message, so a syntax error is byte-for-byte `std`.
   * - **Strict-policy rejection** (\ref policy): a pattern real cannot represent linearly but std *could*
   *   (a backreference, an unbounded lookaround, a POSIX class) — `code()` is `error_complexity` and
   *   `what()` carries a REAL-identifiable message. Under `policy::fallback` this path delegates to std
   *   instead of throwing, so the only thrown case there is the invalid-for-both one above.
   */
  class regex_error : public std::regex_error
  {
  public:

    //! \brief From a std backend error (the fallback path); keeps std's exact code.
    explicit regex_error(const std::regex_error& error)
      : std::regex_error(error.code()),
        message_(error.what())
    {}

    //! \brief With an explicit code and message — the strict-policy rejection of a pattern REAL cannot
    //!        represent linearly (`error_complexity`), carrying a REAL-identifiable message.
    regex_error(std::regex_constants::error_type code,
                std::string                      message)
      : std::regex_error(code),
        message_(std::move(message))
    {}

    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

  private:

    std::string message_; //!< The originating error's detailed message.
  };

  //! \brief The drop-in policy for a pattern the linear engine cannot represent (backreferences, an
  //!        unbounded lookaround, a POSIX class, …). `strict` (the default) rejects it, so every accepted
  //!        pattern executes each `regex_search`/`regex_match` in time linear in the input — the ReDoS-safety
  //!        guarantee (replace/iterate compose O(n) such operations: quadratic worst-case on any linear
  //!        engine, never exponential); `fallback` delegates it to `std::regex`, which may accept it but
  //!        **forfeits the guarantee** for that pattern.
  enum class policy : std::uint8_t
  {
    strict,   //!< Reject an ineligible pattern (throws `regex_error` with `error_complexity`). The default.
    fallback, //!< Delegate an ineligible pattern to `std::regex` (backtracking — not ReDoS-safe).
  };

  namespace detail {

    //! \brief Whether `real` is even *eligible* for this `basic_regex` instantiation. `real` runs only
    //!        the `char` path with default traits; `wchar_t`/`char8_t`/… and custom traits are always
    //!        std. This is a compile-time gate: it must compile `real`'s char-only code (the byte
    //!        `string_view`, `fill_from_real`) *out* for other `CharT`, not merely skip it at runtime.
    template <typename CharT, typename Traits>
    inline constexpr bool real_eligible =
      std::is_same_v<CharT, char> && std::is_same_v<Traits, std::regex_traits<char>>;

    //! \brief Grammars/options that force the std backend (real implements default-traits ECMAScript,
    //!        reporting every group — so `nosubs`, which std answers by exposing only group 0, also
    //!        routes to std to avoid a structural both-accept divergence).
    inline bool grammar_forces_std(regex_constants::syntax_option_type f) noexcept
    {
      using namespace regex_constants;
      return (f & (basic | extended | awk | grep | egrep)) != ECMAScript
             || (f & collate) != ECMAScript
             || (f & nosubs) != ECMAScript;
    }

    //! \brief Pattern text that real *accepts* but matches differently from libstdc++ — a both-accept
    //!        silent divergence, so it must route to std up front (real's accept hides it otherwise).
    //!
    //! `\0` followed by a digit: `real` reads it as a legacy octal escape (Annex B, e.g. `\012` →
    //! newline) while libstdc++ reads `\0` as NUL then a literal digit. Strict ECMAScript makes `\0`+
    //! digit a syntax error (no valid production), so neither is "the" spec answer — routing to std
    //! keeps compat ≡ its secondary oracle and the contract (never a silent divergence). The fuzzer
    //! found this. (`\1`-`\9` already route to std via real's backreference rejection.)
    //!
    //! `\C` (RE2's raw-byte escape): `real` accepts it here because `real::compat` always
    //! compiles its internal engine with `flags::bytes` (for byte-per-`std::regex`-char alignment, see
    //! the file header), which is the ONLY gate `\C` itself checks — an internal implementation detail
    //! leaking through as accidental, unintended public surface. `\C` is not ECMAScript at all (an
    //! RE2/real-only extension outside this layer's std::regex contract), and libstdc++ does not accept
    //! it either — a genuine both-behavior mismatch (real: matches one byte; std: rejects, or accepts
    //! it as some other escape and matches differently), not a case where routing to std even yields
    //! agreement. Route to std up front so compat never exposes it. The fuzzer found this (the exact
    //! seed corpus added for `\C`'s own coverage, replayed here since `fuzz/corpus` is shared).
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
            return true; // \C -- see the doc comment above
          }
          ++i; // consume the escaped character (so `\\0` is an escaped backslash, not `\0`)
          continue;
        }
        // An inline-flags group `(?imsxaU:...)` / `(?-...:...)` / a bare `(?imsxaU)`: real accepts
        // these (Python semantics; `U` is the RE2 ungreedy swap), but ECMAScript has no inline flags,
        // so std rejects them. Route to std so compat stays ≡ std — the char after `(?` is a flag
        // letter or `-` only for a flag construct (`(?:` `(?=` `(?!` `(?<` `(?P` start with other
        // characters). Over-routing here is safe. Omitting `U` here would be a guaranteed divergence:
        // real would honor `(?U)` (swapped greediness) while std rejects it.
        if (p[i] == '(' && i + 2 < p.size() && p[i + 1] == '?' && is_flag_or_dash(p[i + 2])) {
          return true;
        }
      }
      return false;
    }

    //! \brief Replacement format text that must route to std: `$0`. `$0` is platform-variant
    //!        (libstdc++ = the whole match, strict-ECMAScript/MSVC = a literal), so real cannot
    //!        pick one without risking a silent divergence — route to std, which is authoritative
    //!        for its own platform. `$$` is skipped (an escaped literal `$`).
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

    //! \brief A POSIX bracket-class name to its ASCII (C-locale) range content, appended inside a `[...]` during
    //!        ERE translation. Empty for an unknown name (the caller then falls back to std).
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

    //! \brief Translates a POSIX bracket expression `[...]` — identical syntax in BRE and ERE, so shared by both
    //!        translators. POSIX classes (`[[:alpha:]]`) become ASCII ranges; other members pass through. \p i
    //!        must point at the opening `[`; on success it advances past the `]` and appends the class to \p out.
    //!        Returns false (the caller then declines to std) on an unterminated class or an unknown / collating
    //!        `[[:foo:]]` / `[.x.]` / `[=x=]`.
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

    //! \brief Appends the REAL translation of an awk C-escape at \p i (which points at the backslash),
    //!        advancing \p i; returns false to decline (→ std). awk's escapes beyond ERE: `\b` is BACKSPACE
    //!        (0x08) — **not** a word boundary, the inverse of the ERE decline — `\a`=BEL, `\n\t\r\f\v` the usual
    //!        controls, `\/` and `\"` literals, and a 1-to-3-digit octal `\ddd` (both std libraries agree; an
    //!        overflow > 0377 declines). Emitted as `\xHH` so REAL matches the exact byte.
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

    //! \brief Whether \p p has an **empty alternation branch** — a `|` with nothing on one side: `(|`, `|)`,
    //!        `||`, or a `|` at the pattern start or end. Two reasons this matters: (a) `std::regex` **rejects**
    //!        these in the POSIX grammars, so translating one would make compat over-accept vs std; (b) REAL's
    //!        leftmost-first star semantics over an empty-first branch (`(|a)*`) diverge from re / std on
    //!        repetition. Conservative — a `|` merely adjacent to a group boundary or the pattern edge counts,
    //!        an escaped `\|` or a `|` inside a class does not — because a false positive only costs linear
    //!        coverage (safe) while a false negative is a silent divergence (forbidden).
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

    //! \brief Translates a POSIX **extended** (ERE) — or, with \p awk, an **awk** — pattern to an equivalent REAL
    //!        pattern, or `nullopt` when it uses a construct the two grammars read differently (an ECMAScript
    //!        shorthand `\d\w\s` — undefined/literal in ERE; an ambiguous `{`; an unknown/collating `[[:…:]]`;
    //!        an empty alternation branch, which std rejects — see \ref has_empty_alternation_branch).
    //!        awk adds the C-escapes (see \ref append_awk_escape). POSIX classes become ASCII ranges (C locale);
    //!        the common productions pass through, since REAL reads them like ERE. Validated by a bounds differential.
    [[nodiscard]] inline std::optional<std::string> translate_ere(std::string_view p,
                                                                  bool             awk = false)
    {
      if (has_empty_alternation_branch(p)) { return std::nullopt; } // std rejects these in POSIX -> keep ≡ std
      std::string       out;
      std::size_t       i {0};
      const std::size_t n {p.size()};
      while (i < n) {
        const char c {p[i]};
        if (c == '\\') {
          if (i + 1 >= n) { return std::nullopt; } // trailing backslash
          const char d {p[i + 1]};
          // `\]` / `\}` OUTSIDE a class are undefined in POSIX and the std libraries disagree — libc++ rejects
          // `\]` but accepts `\}`, libstdc++ rejects both, AT&T matches — so decline (≡ its own std per platform,
          // the same rule as the medial `^`/`$`). Ironically REAL was AT&T-conformant here, but the drop-in
          // contract is ≡-std, not ≡-AT&T. (Inside a class, `\]` is handled by translate_bracket, unaffected.)
          if (d == ']' || d == '}') { return std::nullopt; }
          if (std::string_view {".[]{}()*+?|^$\\"}.find(d) != std::string_view::npos) {
            out += '\\';
            out += d;
            i   += 2;
            continue; // an escaped metacharacter is a literal in both grammars
          }
          if (awk && append_awk_escape(p, i, out)) { continue; } // awk C-escapes (\b=BS, \n, octal, …)
          return std::nullopt; // \d, \w, \b, … — no ERE meaning; fall back to std
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

    //! \brief Translates a POSIX **basic** (BRE) pattern to an equivalent REAL pattern, or `nullopt`. BRE differs
    //!        from ERE: `\(` `\)` group and `\{n\}` quantify, while bare `( ) { } | + ?` are LITERALS (escaped for
    //!        REAL); `*` at an expression start and `^`/`$` off the ends are literals too. Declines (→ std) on a
    //!        backreference `\1`-`\9` (std's residual value), an ECMAScript-ism, a non-strict `\{`, an unknown /
    //!        collating class, or a POSIX-undefined corner (`^`/`$`/`*` at a subexpression boundary).
    [[nodiscard]] inline std::optional<std::string> translate_bre(std::string_view p)
    {
      std::string       out;
      std::size_t       i        {0};
      const std::size_t n        {p.size()};
      bool              at_start {true}; // pattern start or just after `\(` — where `*` is literal and `^` anchors
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
          if (i == 0) { out += '^'; i += 1; at_start = false; continue; } // anchor at the pattern head (libs agree)
          return std::nullopt; // a medial `^` is a POSIX literal, but libstdc++ reads it as an anchor while
                               // libc++ reads it as a literal — decline so compat stays ≡ its own std
        }
        if (c == '$') {
          if (i + 1 == n) { out += '$'; i += 1; at_start = false; continue; } // anchor at the tail (libs agree)
          return std::nullopt; // a medial `$` — the same libstdc++/libc++ disagreement; decline to std
        }
        if (c == '*') {
          if (at_start) { return std::nullopt; } // a leading `*` is a literal in POSIX BRE -> decline (rare)
          out += '*';                            // quantifier
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

    //! \brief grep / egrep: a newline in the pattern is a top-level alternation of the lines (grep = BRE lines,
    //!        egrep = ERE lines). Each line is translated by \p translate_line and the results are joined with
    //!        `|` — correct precedence by construction, since `|` is the lowest, and each line's `^`/`$` stay
    //!        branch-relative. A line that declines, or an empty line (a blank branch — a std edge best left to
    //!        std), declines the whole pattern.
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

    //! \brief Dispatches a single POSIX grammar to its translator, or `nullopt` (→ std). Exactly one grammar bit
    //!        must be set, and neither `collate` nor `nosubs` (which force std). `extended` → ERE, `basic` → BRE,
    //!        `awk` → ERE + C-escapes, `grep` → BRE lines joined by `|`, `egrep` → ERE lines joined by `|`.
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

    //! \brief Maps compat options to real::flags (always with bytes|ecma for std-char alignment).
    inline real::flags to_real(regex_constants::syntax_option_type f) noexcept
    {
      real::flags r {real::flags::bytes | real::flags::ecma};
      if ((f & regex_constants::icase) != regex_constants::ECMAScript) { r = r | real::flags::icase; }
      if ((f & regex_constants::multiline) != regex_constants::ECMAScript) { r = r | real::flags::multiline; }
      return r;
    }

    //! \brief Maps compat options to std::regex syntax flags (the fallback path).
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
  } // namespace detail

  /*!
   * \brief A `std::basic_regex`-compatible pattern, backed by `real` where proven, else `std`.
   *
   * \tparam CharT  Character type (`char`; other types route straight to `std`).
   * \tparam Traits Regex traits (std parity).
   */
  template <typename CharT = char, typename Traits = std::regex_traits<CharT>>
  class basic_regex
  {
  public:

    using value_type  = CharT;                               //!< Character type.
    using flag_type   = regex_constants::syntax_option_type; //!< Option type.
    using string_type = std::basic_string<CharT>;            //!< Pattern string type.

    basic_regex() = default;                                 // the variant default-constructs to an empty std regex

    explicit basic_regex(const CharT* pattern,
                         flag_type    f   = regex_constants::ECMAScript,
                         policy       pol = policy::strict)
    {
      policy_ = pol;
      assign(std::basic_string_view<CharT>(pattern), f);
    }

    explicit basic_regex(const string_type& pattern,
                         flag_type          f   = regex_constants::ECMAScript,
                         policy             pol = policy::strict)
    {
      policy_ = pol;
      assign(std::basic_string_view<CharT>(pattern), f);
    }

    basic_regex(const CharT* pattern,
                std::size_t  len,
                flag_type    f   = regex_constants::ECMAScript,
                policy       pol = policy::strict)
    {
      policy_ = pol;
      assign(std::basic_string_view<CharT>(pattern, len), f);
    }

    template <typename It>
    basic_regex(It        begin,
                It        end,
                flag_type f   = regex_constants::ECMAScript,
                policy    pol = policy::strict)
    {
      policy_ = pol;
      const string_type pattern(begin, end);
      assign(std::basic_string_view<CharT>(pattern), f);
    }

    //! \brief Number of marked sub-expressions (excluding group 0), as `std::basic_regex`.
    [[nodiscard]] std::size_t mark_count() const noexcept
    {
      return mark_count_;
    }

    //! \brief The flags this regex was built with.
    [[nodiscard]] flag_type flags() const noexcept
    {
      return flags_;
    }

    void swap(basic_regex& other) noexcept
    {
      engine_.swap(other.engine_);
      std::swap(pattern_, other.pattern_);
      std::swap(flags_, other.flags_);
      std::swap(mark_count_, other.mark_count_);
      std::swap(nullable_, other.nullable_);
      std::swap(nullable_captured_repeat_, other.nullable_captured_repeat_);
      std::swap(posix_longest_, other.posix_longest_);
      std::swap(lazy_std_, other.lazy_std_);
      std::swap(policy_, other.policy_);
    }

    //! \brief True if this regex is backed by the `real` engine (vs the std fallback).
    [[nodiscard]] bool uses_real() const noexcept
    {
      return std::holds_alternative<real::regex>(engine_);
    }

    //! \brief True if this regex fell back to `std::regex` (a `policy::fallback` regex on an ineligible
    //!        pattern) — so this pattern is *not* linear-time / ReDoS-safe. Always false under `strict`.
    [[nodiscard]] bool uses_fallback() const noexcept
    {
      return !uses_real();
    }

    //! \brief The drop-in policy this regex was constructed with.
    [[nodiscard]] compat::policy policy() const noexcept
    {
      return policy_;
    }

    //! \brief Access the active backend (engine-facing; used by the free functions).
    [[nodiscard]] const std::variant<std::basic_regex<CharT, Traits>, real::regex>& engine() const noexcept
    {
      return engine_;
    }

    //! \brief Whether the pattern can match the empty string (real's `empty_match_possible` hint).
    //!
    //! Empty-match *traversal* (replace / iterate) follows Python's advance rules in `real`, which
    //! differ from ECMAScript. So a nullable real-backed pattern routes those operations to a lazily
    //! built `std::regex` (\ref std_engine) — per operation, not at construction, so `search`/`match`
    //! keep `real`'s linear-time guarantee even on nullable-ReDoS patterns like `(a*)*`.
    [[nodiscard]] bool nullable() const noexcept
    {
      return nullable_;
    }

    //! \brief Whether this is a POSIX-ERE pattern routed to REAL: `search`/`match` must use leftmost-**longest**
    //!        bounds (the POSIX semantics), not the default leftmost-first. Set only for a translated `extended`.
    [[nodiscard]] bool posix_longest() const noexcept
    {
      return posix_longest_;
    }

    //! \brief Whether replace/iterate run on the `real` traversal (real-backed AND non-nullable AND no
    //!        nullable captured-repeat group). A nullable pattern delegates replace/iterate to std (the
    //!        empty-match traversal differs; and iterating a nullable pattern whose per-position match
    //!        cost is O(n) is O(n²) on any linear engine, so routing it buys correctness but not a linear
    //!        guarantee — see the nullable note in COMPATIBILITY.md). A pattern with a capturing group
    //!        that is nullable under a quantifier (`(ab|)+a`) is itself non-nullable as a whole, but
    //!        real's last-consuming-iteration capture (RE2/Rust/Go lineage) diverges from an ECMAScript
    //!        backtracker's extra empty final iteration on that GROUP's span — so it routes too, for the
    //!        same reason: `regex_search`/`match` are unaffected (see the nullable-loop group-capture
    //!        section of COMPATIBILITY.md — the search residue is intentional, not an oversight).
    [[nodiscard]] bool uses_real_traversal() const noexcept
    {
      return uses_real() && !nullable_ && !nullable_captured_repeat_;
    }

    //! \brief The `std::regex` for the std / lazy-std path (built once on demand for a real-backed
    //!        pattern reached via a constraining flag / nullable replace-iterate / `$0`/sed replace).
    //!
    //! Thread-safe: `std::regex` guarantees concurrent `const` operations on one object are safe, but
    //! this builds `lazy_std_` (a `mutable` member) on demand. A function-local static build mutex
    //! serialises the build (and the read is taken under the same lock), so the guarantee holds for
    //! nullable AND non-nullable real-backed patterns. `std::once_flag` would be lighter but is
    //! non-copyable, and `basic_regex` must stay copyable (`std::regex` is); a static mutex keeps the
    //! value semantics defaulted. The build is per operation, cold relative to matching.
    [[nodiscard]] const std::basic_regex<CharT, Traits>& std_engine() const
    {
      if (std::holds_alternative<std::basic_regex<CharT, Traits>>(engine_)) {
        return std::get<std::basic_regex<CharT, Traits>>(engine_);
      }
      static std::mutex          build_mutex; // one per basic_regex<CharT, Traits> instantiation
      const std::lock_guard      lock {build_mutex};
      if (!lazy_std_.has_value()) {
        try {
          lazy_std_.emplace(pattern_.data(), pattern_.size(), detail::to_std(flags_));
        }
        catch (const std::regex_error& std_error) {
          // A pattern real accepted but std cannot build (a real superset) reaches std only via a
          // constraining flag / nullable replace-iterate. Surface it as a compat::regex_error, not a
          // raw std one: an error, homogeneous with the ctor path, never a silent result.
          throw regex_error(std_error);
        }
      }
      return *lazy_std_;
    }

  private:

    // std backend first so the variant is default-constructible (real::regex has no default ctor).
    std::variant<std::basic_regex<CharT, Traits>, real::regex>           engine_;
    string_type                                                          pattern_;                                   //!< Original pattern (for the lazy std build).
    flag_type                                                            flags_                    {regex_constants::ECMAScript};
    std::size_t                                                          mark_count_               {};
    bool                                                                 nullable_                 {};               //!< empty_match_possible (real-backed).
    bool                                                                 nullable_captured_repeat_ {};               //!< nullable_captured_repeat (real-backed) — a nullable capturing group under a quantifier; see \ref uses_real_traversal.
    bool                                                                 posix_longest_            {};               //!< POSIX ERE on REAL: search uses leftmost-longest bounds.
    mutable std::optional<std::basic_regex<CharT, Traits>>               lazy_std_;                                  //!< Lazy std for nullable replace/iterate.
    compat::policy                                                       policy_                   {policy::strict}; //!< strict rejects ineligible, fallback delegates to std.

    void assign(std::basic_string_view<CharT> pattern,
                flag_type                     f)
    {
      flags_   = f;
      pattern_ = string_type(pattern);
      lazy_std_.reset();
      nullable_                 = false;
      nullable_captured_repeat_ = false;
      posix_longest_            = false;
      if constexpr (detail::real_eligible<CharT, Traits>) {
        const std::string_view sv {pattern.data(), pattern.size()};
        // PX1a/PX1b: a single POSIX grammar (extended -> ERE, basic -> BRE; awk/grep/egrep next) on the linear
        // engine when the pattern translates — run it on REAL with leftmost-LONGEST bounds (the POSIX semantics,
        // via search_longest / find_iter_longest) instead of delegating to std's backtracker. A `nullopt` (a
        // wrong grammar mix, or an untranslatable construct) or a real reject falls through to the std path below;
        // the fallback lives, and everything that reaches std keeps reaching it.
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
              // translated but real cannot represent it -> fall through to the std path
            }
          }
        }
        if (detail::grammar_forces_std(f) || detail::pattern_forces_std(sv)) {
          reject_or_fallback(sv, f, "the pattern uses a construct the linear engine does not represent "
                             "(a backreference, an unbounded lookaround, a POSIX class, or a "
                             "grammar that forces std)");
          return;
        }
        try {
          real::regex compiled(sv, detail::to_real(f));
          mark_count_               = compiled.group_count();
          nullable_                 = compiled.raw_program().hints.empty_match_possible;
          nullable_captured_repeat_ = compiled.raw_program().hints.nullable_captured_repeat;
          engine_.template emplace<real::regex>(std::move(compiled));
          // The std engine for a real-backed pattern (needed for a constraining flag or nullable
          // replace/iterate) is built lazily and thread-safely by std_engine() under its build mutex,
          // so no eager build is needed here (a real superset that std rejects surfaces the wrapped
          // error only when the std-only operation is actually invoked; search/match stay on real).
        }
        catch (const real::regex_error& real_error) {
          // real cannot represent it (backref / unbounded lookaround / POSIX class / non-ASCII in a
          // class). strict rejects; fallback delegates to std (which may accept it). Invalid for both
          // throws compat::regex_error (emplace_std wraps).
          reject_or_fallback(sv, f, real_error.what());
        }
      }
      else {
        // wchar_t / char8/16/32 / custom traits: real is never eligible, so go straight to std. The
        // real::regex variant alternative stays dead for this CharT (never emplaced), and real's
        // char-only helpers are not instantiated. always-std => std parity by construction.
        try {
          engine_.template emplace<std::basic_regex<CharT, Traits>>(pattern.data(), pattern.size(),
                                                                    detail::to_std(f));
          mark_count_ = std::get<std::basic_regex<CharT, Traits>>(engine_).mark_count();
        }
        catch (const std::regex_error& std_error) {
          throw regex_error(std_error); // homogeneous compat::regex_error on the wide/custom-traits path
        }
      }
    }

    //! \brief The policy branch for a pattern the linear engine cannot represent: `strict` throws
    //!        `regex_error` with `error_complexity` and a REAL-identifiable message; `fallback` delegates
    //!        it to `std::regex`.
    void reject_or_fallback(std::string_view   sv,
                            flag_type          f,
                            const std::string& reason)
    {
      if (policy_ == policy::strict) {
        // Distinguish "real cannot represent it, but the pattern is valid" (std accepts) from "invalid for
        // both" (a syntax error). The first is a capability limit -> error_complexity; the second is a bad
        // pattern -> std's own code, so a syntax error still reports as a syntax error, exactly like std.
        if constexpr (detail::real_eligible<CharT, Traits>) {
          try {
            const std::basic_regex<CharT, Traits> probe(sv.data(), sv.size(), detail::to_std(f));
            static_cast<void>(probe);     // std accepts it: a real-only limitation, fall through to the throw below
          }
          catch (const std::regex_error& std_error) {
            throw regex_error(std_error); // invalid for both -> std's exact code (drop-in ≡ std)
          }
        }
        throw regex_error(std::regex_constants::error_complexity,
                          "real::compat (strict policy): this pattern requires a non-linear (backtracking) "
                          "engine — " + reason + ". Construct with policy::fallback to delegate it to "
                          "std::regex, forfeiting the linear-time / ReDoS-safe guarantee for it.");
      }
      emplace_std(sv, f);
    }

    void emplace_std(std::string_view sv,
                     flag_type        f)
    {
      try {
        auto& std_engine = engine_.template emplace<std::basic_regex<CharT, Traits>>(
          sv.data(), sv.size(), detail::to_std(f));
        mark_count_ = std_engine.mark_count();
      }
      catch (const std::regex_error& std_error) {
        // Every std-only build path throws a compat::regex_error, homogeneous with the rest of the
        // layer (never a raw std::regex_error leaking out of a compat entry point).
        throw regex_error(std_error);
      }
    }
  };

  using regex  = basic_regex<char>;    //!< The char-path compat regex (real-eligible).
  using wregex = basic_regex<wchar_t>; //!< The wide compat regex (always the std backend).
} // namespace real::compat

#endif // REAL_STD_REGEX_CORE_HPP
