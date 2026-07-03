/*!
 * \file std/regex_core.hpp
 * \brief std::regex-compatibility layer, part 1/3: the constants, the error type, the backend-routing
 *        screens, and `basic_regex`. Included via the `std/regex.hpp` umbrella — do not include the
 *        parts directly; `#include <real/std/regex.hpp>` stays the one public entry point.
 */
#ifndef REAL_STD_REGEX_CORE_HPP
#define REAL_STD_REGEX_CORE_HPP

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
   * A pattern reaches this only when it is invalid for **both** backends: real is always tried
   * first and, on rejection, falls back to std (which may accept what real cannot, e.g. a
   * backreference). So the throwing path is exactly "std also rejected" — the exact std
   * `.code()` is preserved, and `what()` keeps std's detailed message.
   *
   * (An alternative — mapping `real::regex_error::kind()` to a code directly — is intentionally
   * absent: the always-fall-back flow never propagates a real error directly — a real resource-limit
   * rejection
   * must still try std, which may accept it, to stay ≡ std. Mapping real kinds would require a
   * no-fallback path, which would diverge from std. Revisit only if such a path is introduced.)
   */
  class regex_error : public std::regex_error
  {
  public:

    //! \brief From a std backend error (the only reachable path); keeps std's exact code.
    explicit regex_error(const std::regex_error& error)
      : std::regex_error(error.code()),
        message_(error.what())
    {}

    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

  private:

    std::string message_; //!< The originating error's detailed message.
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
    [[nodiscard]] inline bool pattern_forces_std(std::string_view p) noexcept
    {
      const auto is_flag_or_dash = [](char c) {
                                     return c == '-' || c == 'i' || c == 'm' || c == 's' || c == 'x' || c == 'a';
                                   };
      for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '\\') {
          if (i + 2 < p.size() && p[i + 1] == '0' && p[i + 2] >= '0' && p[i + 2] <= '9') {
            return true;
          }
          ++i; // consume the escaped character (so `\\0` is an escaped backslash, not `\0`)
          continue;
        }
        // An inline-flags group `(?imsxa:...)` / `(?-...:...)` / a bare `(?imsxa)`: real accepts these
        // (Python semantics), but ECMAScript has no inline flags, so std rejects them. Route to std so
        // compat stays ≡ std — the char after `(?` is a flag letter or `-` only for a flag construct
        // (`(?:` `(?=` `(?!` `(?<` `(?P` start with other characters). Over-routing here is safe.
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
                         flag_type    f = regex_constants::ECMAScript)
    {
      assign(std::basic_string_view<CharT>(pattern), f);
    }

    explicit basic_regex(const string_type& pattern,
                         flag_type          f = regex_constants::ECMAScript)
    {
      assign(std::basic_string_view<CharT>(pattern), f);
    }

    basic_regex(const CharT* pattern,
                std::size_t  len,
                flag_type    f = regex_constants::ECMAScript)
    {
      assign(std::basic_string_view<CharT>(pattern, len), f);
    }

    template <typename It>
    basic_regex(It        begin,
                It        end,
                flag_type f = regex_constants::ECMAScript)
    {
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
      std::swap(lazy_std_, other.lazy_std_);
    }

    //! \brief True if this regex is backed by the `real` engine (vs the std fallback).
    [[nodiscard]] bool uses_real() const noexcept
    {
      return std::holds_alternative<real::regex>(engine_);
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

    //! \brief Whether replace/iterate run on the `real` traversal (real-backed AND non-nullable).
    [[nodiscard]] bool uses_real_traversal() const noexcept
    {
      return uses_real() && !nullable_;
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
    std::variant<std::basic_regex<CharT, Traits>, real::regex>          engine_;
    string_type                                                         pattern_;       //!< Original pattern (for the lazy std build).
    flag_type                                                           flags_      {regex_constants::ECMAScript};
    std::size_t                                                         mark_count_ {};
    bool                                                                nullable_   {}; //!< empty_match_possible (real-backed).
    mutable std::optional<std::basic_regex<CharT, Traits>>              lazy_std_;      //!< Lazy std for nullable replace/iterate.

    void assign(std::basic_string_view<CharT> pattern,
                flag_type                     f)
    {
      flags_   = f;
      pattern_ = string_type(pattern);
      lazy_std_.reset();
      nullable_ = false;
      if constexpr (detail::real_eligible<CharT, Traits>) {
        const std::string_view sv {pattern.data(), pattern.size()};
        if (detail::grammar_forces_std(f) || detail::pattern_forces_std(sv)) {
          emplace_std(sv, f);
          return;
        }
        try {
          real::regex compiled(sv, detail::to_real(f));
          mark_count_ = compiled.group_count();
          nullable_   = compiled.raw_program().hints.empty_match_possible;
          engine_.template emplace<real::regex>(std::move(compiled));
          // The std engine for a real-backed pattern (needed for a constraining flag or nullable
          // replace/iterate) is built lazily and thread-safely by std_engine() under its build mutex,
          // so no eager build is needed here (a real superset that std rejects surfaces the wrapped
          // error only when the std-only operation is actually invoked; search/match stay on real).
        }
        catch (const real::regex_error&) {
          // real cannot represent it (backref / unbounded lookaround / POSIX class / non-ASCII in a
          // class): fall back to std, which may accept it. Invalid for both throws compat::regex_error
          // (emplace_std wraps).
          emplace_std(sv, f);
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
