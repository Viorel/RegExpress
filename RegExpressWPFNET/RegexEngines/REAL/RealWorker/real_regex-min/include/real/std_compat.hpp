/*!
 * \file std_compat.hpp
 * \brief `real::compat` — a `std::regex`-compatible drop-in (`<regex>` surface), char path.
 *
 * Contract: behave identically to `std::regex` (ECMAScript) where `real` can prove it, and
 * fall back to `std::regex` everywhere else — never a silent divergence. A pattern is run on
 * `real` (linear-time, ReDoS-safe) when possible; backreferences, unbounded/oversized
 * lookarounds, POSIX classes, non-ASCII inside `[...]`, and the non-ECMAScript grammars route
 * to `std::regex` via a compile-time screen plus a compile-failure catch.
 *
 * `real` is always built with `flags::bytes | flags::ecma` so its byte-oriented, ECMAScript-`$`,
 * ECMAScript-`.` semantics align with `std::basic_regex<char>` (validated by a differential).
 *
 * Surface: `basic_regex` / `sub_match` / `match_results` / `regex_error`, `regex_search`,
 * `regex_match` (S1), `regex_replace` (S2a), `regex_iterator` / `regex_token_iterator` (S2b),
 * the full `match_flag_type` (S3), and `wregex` + POSIX grammars + `nosubs` (S4). `real` runs only
 * the `char` / default-traits / ECMAScript / every-group path (see `detail::real_eligible`); wide
 * `CharT`, custom traits, POSIX/`collate`, and `nosubs` are always `std`. `regex_replace`/iterators
 * route nullable patterns to `std::regex` (the empty-match traversal differs from ECMAScript, see
 * `basic_regex::nullable`), and a constraining `match_flag` routes that operation to `std`.
 */
#ifndef REAL_STD_COMPAT_HPP
#define REAL_STD_COMPAT_HPP

#include "version.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "real.hpp"

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

    //! \brief Match-control flags. S1 carries the common subset; the rest arrive in a later slice.
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
   * (The fiche's §4 "map real::regex_error::kind() to a code" path is intentionally absent: the
   * always-fall-back flow never propagates a real error directly — a real resource-limit rejection
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

  /*!
   * \brief A matched sub-expression: a `[first, second)` range into the searched sequence.
   *
   * Contiguous iterators only — `sub_match` is built from byte offsets, which requires the
   * underlying storage to be contiguous (a `std::deque::iterator` is random-access but not
   * contiguous, so it is rejected).
   *
   * \tparam BidirIt A contiguous iterator into the searched sequence.
   */
  template <typename BidirIt>
  class sub_match
  {
    static_assert(std::contiguous_iterator<BidirIt>,
                  "real::compat::sub_match requires a contiguous iterator");

  public:

    using iterator        = BidirIt;                                                 //!< The underlying iterator.
    using value_type      = typename std::iterator_traits<BidirIt>::value_type;      //!< The character type.
    using difference_type = typename std::iterator_traits<BidirIt>::difference_type; //!< Distance type.
    using string_type     = std::basic_string<value_type>;                           //!< The owning string type.

    BidirIt first   {};                                                              //!< Start of the sub-match.
    BidirIt second  {};                                                              //!< One past the end of the sub-match.
    bool    matched {false};                                                         //!< Whether this sub-expression participated.

    //! \brief Length of the sub-match (0 if it did not participate).
    [[nodiscard]] difference_type length() const
    {
      return matched ? std::distance(first, second) : difference_type {0};
    }

    //! \brief The matched text as an owned string (empty if it did not participate).
    [[nodiscard]] string_type str() const
    {
      return matched ? string_type(first, second) : string_type {};
    }

    //! \brief Implicit conversion to the owned string (std::sub_match parity).
    operator string_type() const // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
    {
      return str();
    }

    //! \brief A non-owning view of the matched text.
    [[nodiscard]] std::basic_string_view<value_type> view() const
    {
      return matched ? std::basic_string_view<value_type>(std::to_address(first),
                                                          static_cast<std::size_t>(length()))
                     : std::basic_string_view<value_type> {};
    }

    //! \brief Three-way length/lexicographic comparison against a string (std::sub_match::compare).
    [[nodiscard]] int compare(const string_type& other) const
    {
      return str().compare(other);
    }

    [[nodiscard]] int compare(const sub_match& other) const
    {
      return str().compare(other.str());
    }
  };

  //! \brief Equality against an owned string (the common std::sub_match comparison).
  template <typename BidirIt>
  bool operator==(const sub_match<BidirIt>&                       lhs,
                  const typename sub_match<BidirIt>::string_type& rhs)
  {
    return lhs.str() == rhs;
  }

  template <typename BidirIt>
  bool operator==(const typename sub_match<BidirIt>::string_type& lhs,
                  const sub_match<BidirIt>&                       rhs)
  {
    return lhs == rhs.str();
  }

  template <typename BidirIt>
  bool operator==(const sub_match<BidirIt>& lhs,
                  const sub_match<BidirIt>& rhs)
  {
    return lhs.str() == rhs.str();
  }

  /*!
   * \brief The result of a match: group sub-matches plus the prefix and suffix.
   *
   * Stores both ends of the searched sequence (`first_`, `last_`) so `suffix()` and lengths are
   * exact (the end is not derivable from a base pointer alone). Filled either from `real`'s byte
   * offsets or copied from a `std::match_results` on the fallback path.
   *
   * \tparam BidirIt A contiguous iterator into the searched sequence.
   * \tparam Alloc   Allocator for the sub-match vector (std parity; default suffices).
   */
  template <typename BidirIt, typename Alloc = std::allocator<sub_match<BidirIt>>>
  class match_results
  {
  public:

    using value_type      = sub_match<BidirIt>;                                      //!< Element type.
    using const_reference = const value_type&;                                       //!< Reference type.
    using reference       = value_type&;                                             //!< Reference type.
    using const_iterator  = typename std::vector<value_type, Alloc>::const_iterator; //!< Iterator.
    using iterator        = const_iterator;                                          //!< Iterators are const (std parity).
    using difference_type = typename std::iterator_traits<BidirIt>::difference_type; //!< Distance type.
    using size_type       = std::size_t;                                             //!< Size type.
    using char_type       = typename std::iterator_traits<BidirIt>::value_type;      //!< Character type.
    using string_type     = std::basic_string<char_type>;                            //!< Owning string type.

    //! \brief Whether a successful match has been stored.
    [[nodiscard]] bool ready() const noexcept
    {
      return ready_;
    }

    //! \brief Number of marks (groups), including group 0; 0 when there was no match.
    [[nodiscard]] size_type size() const noexcept
    {
      return groups_.size();
    }

    [[nodiscard]] bool      empty() const noexcept
    {
      return groups_.empty();
    }

    //! \brief The sub-match for group \p n (group 0 is the whole match). Out-of-range `n` returns a
    //!        reference to an unmatched sub_match anchored at the sequence end `{last_, last_, false}`,
    //!        exactly like `std::match_results::operator[]` (verified on libc++ and libstdc++) — never
    //!        out-of-bounds. A token selector `{2}`/`{5}` or a negative field relies on this.
    const_reference operator[](size_type n) const
    {
      return n < groups_.size() ? groups_[n] : unmatched_;
    }

    //! \brief Start offset of group \p n from the sequence start. For an out-of-range group `std`
    //!        anchors the sub_match at the end, so the offset is the full sequence length.
    [[nodiscard]] difference_type position(size_type n = 0) const
    {
      return n < groups_.size() ? std::distance(first_, groups_[n].first)
                                : std::distance(first_, last_);
    }

    //! \brief Length of group \p n (0 if out of range or unmatched).
    [[nodiscard]] difference_type length(size_type n = 0) const
    {
      return n < groups_.size() ? groups_[n].length() : difference_type {0};
    }

    //! \brief Matched text of group \p n (empty if out of range or unmatched).
    [[nodiscard]] string_type str(size_type n = 0) const
    {
      return (*this)[n].str();
    }

    //! \brief The unmatched prefix (sequence start up to the whole match).
    [[nodiscard]] const value_type& prefix() const
    {
      return prefix_;
    }

    //! \brief The unmatched suffix (whole match end to sequence end).
    [[nodiscard]] const value_type& suffix() const
    {
      return suffix_;
    }

    [[nodiscard]] const_iterator begin() const
    {
      return groups_.begin();
    }

    [[nodiscard]] const_iterator end() const
    {
      return groups_.end();
    }

    [[nodiscard]] const_iterator cbegin() const
    {
      return groups_.begin();
    }

    [[nodiscard]] const_iterator cend() const
    {
      return groups_.end();
    }

    // --- engine-facing fill helpers (used by the free functions) ---------------------------

    //! \brief Resets to the not-ready (no-match) state over the sequence `[first, last)`.
    void reset(BidirIt first,
               BidirIt last)
    {
      first_     = first;
      last_      = last;
      groups_.clear();
      prefix_           = suffix_ = value_type {.first = last, .second = last, .matched = false};
      unmatched_        = value_type {.first = last, .second = last, .matched = false};
      ready_            = false;
    }

    //! \brief Marks a *ready but unmatched* result — after a failed search/match `std` leaves
    //!        `ready() == true` with `size() == 0` (a not-ready result would be a divergence).
    void set_ready_no_match()
    {
      groups_.clear();
      ready_ = true;
    }

    //! \brief Re-bases the unmatched prefix to start at `first` — for iteration, where a match's
    //!        prefix runs from the *previous* match's end (not the sequence start). The std path
    //!        already gets this from the wrapped `std::regex_iterator`; the real path needs it.
    void rebase_prefix(BidirIt first)
    {
      prefix_.first   = first;
      prefix_.matched = first != prefix_.second;
    }

    //! \brief Fills from real's byte offsets over the sequence `[first_, last_)`.
    //!        Templated on the match type — `real::regex::search` returns an SBO-backed result,
    //!        not the `std::vector`-backed `real::match_result` alias.
    template <typename RealMatch>
    void fill_from_real(const RealMatch& match)
    {
      groups_.clear();
      const std::size_t count {match.size()};
      groups_.reserve(count);
      for (std::size_t g = 0; g < count; ++g) {
        const std::size_t start {match.start(g)};
        const std::size_t fin   {match.end(g)};
        if (start == real::npos || fin == real::npos) {
          groups_.push_back(value_type {.first = last_, .second = last_, .matched = false});
        }
        else {
          groups_.push_back(value_type {.first   = first_ + static_cast<difference_type>(start),
                                        .second  = first_ + static_cast<difference_type>(fin),
                                        .matched = true});
        }
      }
      const std::size_t whole_start {match.start(0)};
      const std::size_t whole_end   {match.end(0)};
      prefix_ = value_type {.first   = first_,
                            .second  = first_ + static_cast<difference_type>(whole_start),
                            .matched = whole_start > 0};
      suffix_ = value_type {.first   = first_ + static_cast<difference_type>(whole_end),
                            .second  = last_,
                            .matched = (first_ + static_cast<difference_type>(whole_end)) != last_};
      ready_ = true;
    }

    //! \brief Copies from a std::match_results (the fallback path) over the same sequence.
    template <typename StdMatch>
    void fill_from_std(const StdMatch& match)
    {
      groups_.clear();
      groups_.reserve(match.size());
      for (const auto& sub : match) {
        groups_.push_back(value_type {.first = sub.first, .second = sub.second, .matched = sub.matched});
      }
      const auto& pre {match.prefix()};
      const auto& suf {match.suffix()};
      prefix_ = value_type {.first = pre.first, .second = pre.second, .matched = pre.matched};
      suffix_ = value_type {.first = suf.first, .second = suf.second, .matched = suf.matched};
      ready_  = true;
    }

  private:

    BidirIt                            first_     {};      //!< Start of the searched sequence.
    BidirIt                            last_      {};      //!< End of the searched sequence.
    std::vector<value_type, Alloc>     groups_;            //!< Group sub-matches (0 = whole match).
    value_type                         prefix_    {};      //!< Unmatched prefix.
    value_type                         suffix_    {};      //!< Unmatched suffix.
    value_type                         unmatched_ {};      //!< Sentinel for out-of-range operator[] (anchored at last_).
    bool                               ready_     {false}; //!< Whether a match is stored.
  };

  using ssub_match  = sub_match<std::string::const_iterator>;  //!< Sub-match over a std::string.
  using csub_match  = sub_match<const char*>;                  //!< Sub-match over a C string.
  using wssub_match = sub_match<std::wstring::const_iterator>; //!< Sub-match over a std::wstring.
  using wcsub_match = sub_match<const wchar_t*>;               //!< Sub-match over a wide C string.

  using smatch  = match_results<std::string::const_iterator>;  //!< Match over a std::string.
  using cmatch  = match_results<const char*>;                  //!< Match over a C string.
  using wsmatch = match_results<std::wstring::const_iterator>; //!< Match over a std::wstring (always std).
  using wcmatch = match_results<const wchar_t*>;               //!< Match over a wide C string (always std).

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
      for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '\\') {
          if (i + 2 < p.size() && p[i + 1] == '0' && p[i + 2] >= '0' && p[i + 2] <= '9') {
            return true;
          }
          ++i; // consume the escaped character (so `\\0` is an escaped backslash, not `\0`)
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
   * \tparam CharT  Character type (S1: `char`; other types route straight to `std`).
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
          // raw std one — R4-honest: an error, homogeneous with the ctor path, never a silent result.
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
        // char-only helpers are not instantiated. always-std => std parity by construction (R4).
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

  // --- free functions ----------------------------------------------------------------------

  namespace detail {

    //! \brief Whether `real` can honor the requested match flags, so the operation may stay on it.
    //!
    //! Only `match_default` and the non-constraining `match_any` hint stay on `real` (which satisfies
    //! `match_any` by returning the leftmost match, so ignoring it is sound). *Any* constraining bit —
    //! `not_bol`, `not_eol`, `not_bow`, `not_eow`, `not_null`, `match_continuous`, `match_prev_avail` —
    //! is not expressible through `real`'s API, so the operation routes to `std` (§0: a constraining
    //! flag is never accepted-then-ignored). Affining this (e.g. `continuous`→`real.match(pos)`) is a
    //! measured optimization for later, not a hand-coded partition the fuzzer would have to police.
    [[nodiscard]] inline bool real_honors(regex_constants::match_flag_type mf) noexcept
    {
      constexpr unsigned non_constraining {static_cast<unsigned>(regex_constants::match_default)
                                           | static_cast<unsigned>(regex_constants::match_any)};
      return (static_cast<unsigned>(mf) & ~non_constraining) == 0U;
    }

    //! \brief Whether `regex_replace` can run its substitution on `real`. The real expander honors
    //!        only `format_first_only` / `format_no_copy` (plus the `match_any` hint); ANY other bit —
    //!        a constraining match flag (`not_bol`, `continuous`, …) OR `format_sed` (POSIX syntax) —
    //!        would be silently ignored by the ECMAScript expander, so the whole substitution routes
    //!        to `std`. (This subsumes the explicit `format_sed` screen; `$0` stays content-based.)
    [[nodiscard]] inline bool replace_stays_real(regex_constants::match_flag_type f) noexcept
    {
      using namespace regex_constants;
      constexpr unsigned honored {static_cast<unsigned>(match_default) | static_cast<unsigned>(match_any)
                                  | static_cast<unsigned>(format_first_only)
                                  | static_cast<unsigned>(format_no_copy)};
      return (static_cast<unsigned>(f) & ~honored) == 0U;
    }

    //! \brief Maps compat match/format flags to `std::regex_constants` — exhaustive (the std path).
    //!
    //! Every compat bit has an entry: a forgotten bit would be silently lost on the std path, which
    //! is exactly the divergence §0 forbids. Both the match-control flags (search/match/iterate) and
    //! the format flags (replace) are mapped here.
    [[nodiscard]] inline std::regex_constants::match_flag_type
    to_std_match(regex_constants::match_flag_type f) noexcept
    {
      namespace sc = std::regex_constants;
      using namespace regex_constants;
      auto s {sc::match_default};
      if ((f & match_not_bol) != 0U) { s |= sc::match_not_bol; }
      if ((f & match_not_eol) != 0U) { s |= sc::match_not_eol; }
      if ((f & match_not_bow) != 0U) { s |= sc::match_not_bow; }
      if ((f & match_not_eow) != 0U) { s |= sc::match_not_eow; }
      if ((f & match_any) != 0U) { s |= sc::match_any; }
      if ((f & match_not_null) != 0U) { s |= sc::match_not_null; }
      if ((f & match_continuous) != 0U) { s |= sc::match_continuous; }
      if ((f & match_prev_avail) != 0U) { s |= sc::match_prev_avail; }
      if ((f & format_sed) != 0U) { s |= sc::format_sed; }
      if ((f & format_no_copy) != 0U) { s |= sc::format_no_copy; }
      if ((f & format_first_only) != 0U) { s |= sc::format_first_only; }
      return s;
    }

    //! \brief Runs the active backend over `[first, last)` and fills \p m. \p anchored selects
    //!        whole-sequence match (regex_match) vs leftmost search (regex_search). A constraining
    //!        match flag (see \ref real_honors) routes to `std` even for a real-backed pattern.
    template <typename BidirIt, typename CharT, typename Traits>
    bool run(BidirIt                           first,
             BidirIt                           last,
             match_results<BidirIt>&           m,
             const basic_regex<CharT, Traits>& re,
             bool                              anchored,
             regex_constants::match_flag_type  mf)
    {
      m.reset(first, last);
      if constexpr (real_eligible<CharT, Traits>) {
        if (re.uses_real() && real_honors(mf)) {
          const std::string_view sv     {std::to_address(first),
                                         static_cast<std::size_t>(std::distance(first, last))};
          const real::regex&     engine {std::get<real::regex>(re.engine())};
          const auto             result {anchored ? engine.fullmatch(sv) : engine.search(sv)};
          if (!result.matched()) {
            m.set_ready_no_match(); // std leaves ready()==true, size()==0 on a failed match
            return false;
          }
          m.fill_from_real(result);
          return true;
        }
      }
      const std::basic_regex<CharT, Traits>& std_engine {re.std_engine()}; // lazy-built if real-backed
      std::match_results<BidirIt>            std_m;
      const auto                             sf         {to_std_match(mf)};
      const bool                             ok         {anchored ? std::regex_match(first, last, std_m, std_engine, sf)
                              : std::regex_search(first, last, std_m, std_engine, sf)};
      if (!ok) {
        m.set_ready_no_match();
        return false;
      }
      m.fill_from_std(std_m);
      return true;
    }

    //! \brief Backend run without capturing (no match_results to fill).
    template <typename BidirIt, typename CharT, typename Traits>
    bool run_nocapture(BidirIt                           first,
                       BidirIt                           last,
                       const basic_regex<CharT, Traits>& re,
                       bool                              anchored,
                       regex_constants::match_flag_type  mf)
    {
      if constexpr (real_eligible<CharT, Traits>) {
        if (re.uses_real() && real_honors(mf)) {
          const std::string_view sv     {std::to_address(first),
                                         static_cast<std::size_t>(std::distance(first, last))};
          const real::regex&     engine {std::get<real::regex>(re.engine())};
          return anchored ? engine.fullmatch(sv).matched() : engine.search(sv).matched();
        }
      }
      const std::basic_regex<CharT, Traits>& std_engine {re.std_engine()};
      const auto                             sf         {to_std_match(mf)};
      return anchored ? std::regex_match(first, last, std_engine, sf)
                      : std::regex_search(first, last, std_engine, sf);
    }
  } // namespace detail

  //! \brief Leftmost search of `[first, last)` (Python `re.search` / `std::regex_search`).
  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_search(BidirIt                           first,
                    BidirIt                           last,
                    match_results<BidirIt>&           m,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run(first, last, m, re, /*anchored=*/ false, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_search(const std::basic_string<CharT>&                                   s,
                    match_results<typename std::basic_string<CharT>::const_iterator>& m,
                    const basic_regex<CharT, Traits>&                                 re,
                    regex_constants::match_flag_type                                  flags = regex_constants::match_default)
  {
    return detail::run(s.begin(), s.end(), m, re, false, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_search(const CharT                     * s,
                    match_results<const CharT*>&      m,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run(s, s + std::char_traits<CharT>::length(s), m, re, false, flags);
  }

  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_search(BidirIt                           first,
                    BidirIt                           last,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_nocapture(first, last, re, false, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_search(const std::basic_string<CharT>&   s,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_nocapture(s.begin(), s.end(), re, false, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_search(const CharT                     * s,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_nocapture(s, s + std::char_traits<CharT>::length(s), re, false, flags);
  }

  //! \brief Match of the entire `[first, last)` (Python `re.fullmatch` / `std::regex_match`).
  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_match(BidirIt                           first,
                   BidirIt                           last,
                   match_results<BidirIt>&           m,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run(first, last, m, re, /*anchored=*/ true, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_match(const std::basic_string<CharT>&                                   s,
                   match_results<typename std::basic_string<CharT>::const_iterator>& m,
                   const basic_regex<CharT, Traits>&                                 re,
                   regex_constants::match_flag_type                                  flags = regex_constants::match_default)
  {
    return detail::run(s.begin(), s.end(), m, re, true, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_match(const CharT                     * s,
                   match_results<const CharT*>&      m,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run(s, s + std::char_traits<CharT>::length(s), m, re, true, flags);
  }

  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_match(BidirIt                           first,
                   BidirIt                           last,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_nocapture(first, last, re, true, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_match(const std::basic_string<CharT>&   s,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_nocapture(s.begin(), s.end(), re, true, flags);
  }

  template <typename CharT, typename Traits>
  bool regex_match(const CharT                     * s,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_nocapture(s, s + std::char_traits<CharT>::length(s), re, true, flags);
  }

  // Reject matching against an rvalue string (the result would dangle), mirroring real/std. Both the
  // 3-arg and the 4-arg (with match flags) forms must be deleted — otherwise the temporary binds to
  // the const-ref overload and the filled match_results dangles into freed storage.
  template <typename CharT, typename Traits>
  bool regex_search(const std::basic_string<CharT>&&,
                    match_results<typename std::basic_string<CharT>::const_iterator>&,
                    const basic_regex<CharT, Traits>&) = delete;
  template <typename CharT, typename Traits>
  bool regex_search(const std::basic_string<CharT>&&,
                    match_results<typename std::basic_string<CharT>::const_iterator>&,
                    const basic_regex<CharT, Traits>&,
                    regex_constants::match_flag_type) = delete;
  template <typename CharT, typename Traits>
  bool regex_match(const std::basic_string<CharT>&&,
                   match_results<typename std::basic_string<CharT>::const_iterator>&,
                   const basic_regex<CharT, Traits>&) = delete;
  template <typename CharT, typename Traits>
  bool regex_match(const std::basic_string<CharT>&&,
                   match_results<typename std::basic_string<CharT>::const_iterator>&,
                   const basic_regex<CharT, Traits>&,
                   regex_constants::match_flag_type) = delete;

  // --- regex_replace -----------------------------------------------------------------------

  namespace detail {

    //! \brief Appends one match's ECMAScript-expanded replacement.
    //!
    //! The ECMAScript replacement references: dollar-dollar to a literal `$`, dollar-ampersand to the
    //! whole match, dollar-backtick to the prefix, dollar-quote to the suffix, and `$N`/`$NN` to a
    //! group. Offsets come from the match's group spans relative to \p text. The prefix is the
    //! unmatched text *since the previous match* (`[prefix_start, start)`) and the suffix runs to the
    //! end — matching `std::regex_replace` (which uses `match_results` prefix/suffix), the parity
    //! oracle. A `$N`/`$NN` for a non-participating group inserts nothing; an invalid `$` is literal.
    template <typename RealMatch>
    void expand_format(std::string&     out,
                       const RealMatch& m,
                       std::string_view fmt,
                       std::string_view text,
                       std::size_t      prefix_start)
    {
      const std::size_t group_count {m.size()};   // includes group 0
      const std::size_t whole_start {m.start(0)};
      const std::size_t whole_end   {m.end(0)};
      for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '$') {
          out.push_back(fmt[i]);
          continue;
        }
        if (i + 1 >= fmt.size()) {
          out.push_back('$');
          break;
        }
        const char next {fmt[i + 1]};
        if (next == '$') {
          out.push_back('$');
          ++i;
        }
        else if (next == '&') {
          out.append(text.substr(whole_start, whole_end - whole_start));
          ++i;
        }
        else if (next == '`') {
          out.append(text.substr(prefix_start, whole_start - prefix_start));
          ++i;
        }
        else if (next == '\'') {
          out.append(text.substr(whole_end));
          ++i;
        }
        else if (next >= '0' && next <= '9') {
          // ECMAScript / std: greedily take a second digit when present (`$12` -> group 12; `$015`
          // -> group 01 == 1, then a literal '5'). The 2-digit value is used as-is; a reference to a
          // group that does not exist expands to nothing (the digits are still consumed). ($0… is
          // screened to std up front, so `next` here is 1-9.)
          std::size_t group    {static_cast<std::size_t>(next - '0')};
          std::size_t consumed {1};
          if (i + 2 < fmt.size() && fmt[i + 2] >= '0' && fmt[i + 2] <= '9') {
            group    = (group * 10) + static_cast<std::size_t>(fmt[i + 2] - '0');
            consumed = 2;
          }
          if (group >= 1 && group < group_count && m.start(group) != real::npos) {
            out.append(text.substr(m.start(group), m.end(group) - m.start(group)));
          }
          i += consumed;
        }
        else {
          out.push_back('$'); // a `$` not forming a valid reference is literal
        }
      }
    }
  } // namespace detail

  /*!
   * \brief Replaces matches of \p re in \p s with the ECMAScript-formatted \p fmt.
   *
   * Real-backed, non-nullable patterns run the substitution on `real` (linear, ReDoS-safe); the
   * std backend and nullable real-backed patterns route to `std::regex_replace` (the empty-match
   * traversal differs between Python `real` and ECMAScript, see \ref basic_regex::nullable).
   */
  template <typename CharT, typename Traits>
  std::basic_string<CharT> regex_replace(const std::basic_string<CharT>&   s,
                                         const basic_regex<CharT, Traits>& re,
                                         const std::basic_string<CharT>&   fmt,
                                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    if constexpr (!detail::real_eligible<CharT, Traits>) {
      // wide / custom-traits: always std (real is not eligible for this CharT).
      return std::regex_replace(s, re.std_engine(), fmt, detail::to_std_match(flags));
    }
    else {
      // Route to std when: the pattern is not real-traversable (std/nullable), OR a flag the real
      // expander cannot honor is set (any constraining match flag or format_sed — see
      // detail::replace_stays_real), OR the format uses `$0` (platform-variant, format_forces_std).
      // Only then does the real expander run.
      if (!re.uses_real_traversal() || !detail::replace_stays_real(flags)
          || detail::format_forces_std(std::string_view {fmt})) {
        return std::regex_replace(s, re.std_engine(), fmt, detail::to_std_match(flags));
      }
      const real::regex&     engine     {std::get<real::regex>(re.engine())};
      const std::string_view text       {s};
      std::string            out;
      const bool             first_only {(flags & regex_constants::format_first_only) != 0U};
      const bool             no_copy    {(flags & regex_constants::format_no_copy) != 0U};
      std::size_t            last_end   {0};
      bool                   done       {false};
      for (const auto& match : engine.find_iter(s)) {
        if (done) {
          break;
        }
        const std::size_t prefix_start {last_end};
        if (!no_copy) {
          out.append(text.substr(last_end, match.start() - last_end));
        }
        detail::expand_format(out, match, std::string_view {fmt}, text, prefix_start);
        last_end = match.end();
        if (first_only) {
          done = true;
        }
      }
      if (!no_copy) {
        out.append(text.substr(last_end));
      }
      return out;
    }
  }

  //! \brief `regex_replace` overload for a C-string format.
  template <typename CharT, typename Traits>
  std::basic_string<CharT> regex_replace(const std::basic_string<CharT>&   s,
                                         const basic_regex<CharT, Traits>& re,
                                         const CharT                     * fmt,
                                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    return regex_replace(s, re, std::basic_string<CharT>(fmt), flags);
  }

  //! \brief `regex_replace` writing to an output iterator (std parity).
  template <typename OutputIt, typename BidirIt, typename CharT, typename Traits>
  OutputIt regex_replace(OutputIt                          out,
                         BidirIt                           first,
                         BidirIt                           last,
                         const basic_regex<CharT, Traits>& re,
                         const std::basic_string<CharT>&   fmt,
                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    const std::basic_string<CharT> result {regex_replace(std::basic_string<CharT>(first, last), re, fmt, flags)};
    return std::copy(result.begin(), result.end(), out);
  }

  // --- regex_iterator ----------------------------------------------------------------------

  /*!
   * \brief Iterates the non-overlapping matches of a pattern in a sequence (`std::regex_iterator`).
   *
   * Same per-operation routing as `regex_replace` — a real-backed, non-nullable pattern drives
   * `real`'s linear traversal (repeated region search — a non-nullable pattern never matches empty,
   * so the position always advances past the match and the ECMAScript and `real` sequences agree);
   * the std backend and nullable patterns wrap `std::regex_iterator` (whose empty-match advance is
   * ECMAScript's). The default-constructed iterator is the end sentinel.
   *
   * \tparam BidirIt A contiguous iterator into the searched sequence.
   */
  template <typename BidirIt,
            typename CharT  = typename std::iterator_traits<BidirIt>::value_type,
            typename Traits = std::regex_traits<CharT>>
  class regex_iterator
  {
  public:

    using value_type        = match_results<BidirIt>;     //!< Yielded match.
    using difference_type   = std::ptrdiff_t;             //!< Iterator traits.
    using pointer           = const value_type*;          //!< Arrow type.
    using reference         = const value_type&;          //!< Dereference type.
    using iterator_category = std::forward_iterator_tag;  //!< std::regex_iterator parity.
    using regex_type        = basic_regex<CharT, Traits>; //!< The pattern type.

    //! \brief Constructs the end sentinel.
    regex_iterator() = default;

    //! \brief Constructs a begin iterator over `[first, last)` and finds the first match.
    //!        A constraining match flag (see \ref detail::real_honors) routes to the std backend,
    //!        which carries the flags through the wrapped `std::regex_iterator`.
    regex_iterator(BidirIt                          first,
                   BidirIt                          last,
                   const regex_type&                re,
                   regex_constants::match_flag_type flags = regex_constants::match_default)
      : begin_(first), end_(last), re_(&re), flags_(flags)
    {
      // The real traversal exists only for the char/default-traits path; for wide/custom-traits
      // CharT the branch is compiled out, so next_real() (char-only) is never instantiated.
      if constexpr (detail::real_eligible<CharT, Traits>) {
        if (re.uses_real_traversal() && detail::real_honors(flags)) {
          real_path_ = true;
          next_real();
          return;
        }
      }
      std_it_.emplace(first, last, re.std_engine(), detail::to_std_match(flags));
      sync_std();
    }

    //! \brief Constructing from a temporary regex would dangle (std::regex_iterator parity).
    regex_iterator(BidirIt                          first,
                   BidirIt                          last,
                   const regex_type&&               re,
                   regex_constants::match_flag_type flags = regex_constants::match_default) = delete;

    [[nodiscard]] reference operator*() const
    {
      return match_;
    }

    [[nodiscard]] pointer   operator->() const
    {
      return &match_;
    }

    regex_iterator& operator++()
    {
      if (at_end_) {
        return *this;
      }
      // Guarded so next_real() (char-only) is not instantiated for wide/custom-traits CharT, where
      // real_path_ is always false anyway (the ctor's real branch is compiled out).
      if constexpr (detail::real_eligible<CharT, Traits>) {
        if (real_path_) {
          next_real();
          return *this;
        }
      }
      ++(*std_it_);
      sync_std();
      return *this;
    }

    regex_iterator operator++(int)
    {
      regex_iterator previous {*this};
      ++(*this);
      return previous;
    }

    [[nodiscard]] bool operator==(const regex_iterator& other) const
    {
      if (at_end_ || other.at_end_) {
        return at_end_ == other.at_end_;
      }
      // std-conformant: two non-end iterators are equal only for the same regex + sequence at the
      // same current match (not just a coincidental same-position/length across different regexes).
      return re_ == other.re_ && begin_ == other.begin_ && end_ == other.end_ && flags_ == other.flags_
             && match_.position(0) == other.match_.position(0)
             && match_.length(0) == other.match_.length(0);
    }

    [[nodiscard]] bool operator!=(const regex_iterator& other) const
    {
      return !(*this == other);
    }

  private:

    BidirIt                                     begin_     {};
    BidirIt                                     end_       {};
    const regex_type*                           re_        {nullptr};
    regex_constants::match_flag_type            flags_     {regex_constants::match_default};
    bool                                        real_path_ {false};
    std::size_t                                 real_pos_  {};
    std::optional<std::regex_iterator<BidirIt>> std_it_;
    value_type                                  match_;
    bool                                        at_end_ {true};

    //! \brief Advances the real path: next region search from \ref real_pos_.
    void next_real()
    {
      const std::string_view sv     {std::to_address(begin_),
                                     static_cast<std::size_t>(std::distance(begin_, end_))};
      const auto             result {std::get<real::regex>(re_->engine()).search(sv, real_pos_)};
      if (!result.matched()) {
        at_end_ = true;
        return;
      }
      match_.reset(begin_, end_);
      match_.fill_from_real(result);
      // Iteration: the prefix runs from the previous match end (== real_pos_ here), not the start.
      match_.rebase_prefix(begin_ + static_cast<difference_type>(real_pos_));
      real_pos_ = result.end(0); // non-nullable: end > start >= pos, so this always advances
      at_end_   = false;
    }

    //! \brief Syncs the std path from the wrapped std::regex_iterator.
    void sync_std()
    {
      if (*std_it_ == std::regex_iterator<BidirIt> {}) {
        at_end_ = true;
        return;
      }
      match_.reset(begin_, end_);
      match_.fill_from_std(**std_it_);
      at_end_ = false;
    }
  };

  using sregex_iterator  = regex_iterator<std::string::const_iterator>;  //!< Over a std::string.
  using cregex_iterator  = regex_iterator<const char*>;                  //!< Over a C string.
  using wsregex_iterator = regex_iterator<std::wstring::const_iterator>; //!< Over a std::wstring (std).
  using wcregex_iterator = regex_iterator<const wchar_t*>;               //!< Over a wide C string (std).

  // --- regex_token_iterator ----------------------------------------------------------------------

  /*!
   * \brief Enumerates selected sub-matches (or the text *between* matches) — `std::regex_token_iterator`.
   *
   * Wraps `regex_iterator`, so it inherits the per-operation nullable routing untouched (it never
   * replays the engine choice). For each match it yields the requested fields in order: a field `N >= 0`
   * is capture group `N` (a non-participating group yields an empty `matched == false` token); the field
   * `-1` is the text *before* this match since the previous one — i.e. the match's `prefix()` — which
   * turns `-1` into a splitter. After the last match, a trailing `-1` field yields the final suffix
   * **iff it is non-empty** (std's rule; an empty field *between* adjacent matches is still produced,
   * the asymmetry std pins). With `-1` and no match at all, the whole sequence is the single token.
   *
   * \tparam BidirIt A contiguous iterator into the searched sequence.
   */
  template <typename BidirIt,
            typename CharT  = typename std::iterator_traits<BidirIt>::value_type,
            typename Traits = std::regex_traits<CharT>>
  class regex_token_iterator
  {
  public:

    using regex_type        = basic_regex<CharT, Traits>; //!< The pattern type.
    using value_type        = sub_match<BidirIt>;         //!< Yielded token.
    using difference_type   = std::ptrdiff_t;             //!< Iterator traits.
    using pointer           = const value_type*;          //!< Arrow type.
    using reference         = const value_type&;          //!< Dereference type.
    using iterator_category = std::forward_iterator_tag;  //!< std::regex_token_iterator parity.

    //! \brief Constructs the end sentinel.
    regex_token_iterator() = default;

    //! \brief Selects a single sub-match field (`0` = whole match, `N` = group N, `-1` = split).
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&                re,
                         int                              submatch = 0,
                         regex_constants::match_flag_type flags    = regex_constants::match_default)
      : regex_token_iterator(first,
                             last,
                             re,
                             std::vector<int> {submatch},
                             flags)
    {}

    //! \brief Selects a list of fields, cycled per match (e.g. `{1, 2}`, `{-1}`). The match flags are
    //!        forwarded to the wrapped `regex_iterator`, so the nullable/honors routing is inherited.
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&                re,
                         const std::vector<int>&          submatches,
                         regex_constants::match_flag_type flags = regex_constants::match_default)
      : position_(first, last, re, flags), subs_(submatches)
    {
      if (subs_.empty()) {
        subs_.push_back(0);
      }
      for (const int s : subs_) {
        if (s == -1) {
          has_m1_ = true;
          break;
        }
      }
      init(first, last);
    }

    //! \brief Selects a list of fields from a braced list (e.g. `{-1}`).
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&                re,
                         std::initializer_list<int>       submatches,
                         regex_constants::match_flag_type flags = regex_constants::match_default)
      : regex_token_iterator(first,
                             last,
                             re,
                             std::vector<int>(submatches),
                             flags)
    {}

    //! \brief Constructing from a temporary regex would dangle (std::regex_token_iterator parity).
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&&               re,
                         int                              submatch = 0,
                         regex_constants::match_flag_type flags    = regex_constants::match_default) = delete;
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&&               re,
                         const std::vector<int>&          submatches,
                         regex_constants::match_flag_type flags = regex_constants::match_default) = delete; //!< \overload

    [[nodiscard]] reference operator*() const
    {
      return current_;
    }

    [[nodiscard]] pointer   operator->() const
    {
      return &current_;
    }

    regex_token_iterator& operator++()
    {
      if (at_end_) {
        return *this;
      }
      if (suffix_mode_) { // the trailing -1 field was the last token
        *this = regex_token_iterator {};
        return *this;
      }
      const regex_iterator<BidirIt, CharT, Traits> prev {position_};
      if (n_ + 1 < subs_.size()) {
        ++n_; // more fields for the same match
        set_field();
      }
      else {
        n_ = 0;
        ++position_;
        if (position_ != regex_iterator<BidirIt, CharT, Traits> {}) {
          set_field();                   // first field of the next match
        }
        else if (has_m1_ && prev->suffix().length() != 0) {
          current_     = prev->suffix(); // trailing split field, only when non-empty
          suffix_mode_ = true;
        }
        else {
          at_end_ = true;
        }
      }
      return *this;
    }

    regex_token_iterator operator++(int)
    {
      regex_token_iterator previous {*this};
      ++(*this);
      return previous;
    }

    [[nodiscard]] bool operator==(const regex_token_iterator& other) const
    {
      if (at_end_ || other.at_end_) {
        return at_end_ == other.at_end_;
      }
      // std-conformant: same underlying match walk, same field selectors, same field index / suffix
      // state, same current token — not just a coincidental same current token across different lists.
      return position_ == other.position_ && subs_ == other.subs_ && n_ == other.n_
             && suffix_mode_ == other.suffix_mode_ && current_.first == other.current_.first
             && current_.second == other.current_.second;
    }

    [[nodiscard]] bool operator!=(const regex_token_iterator& other) const
    {
      return !(*this == other);
    }

  private:

    regex_iterator<BidirIt, CharT, Traits> position_;            //!< The underlying match walk.
    std::vector<int>                       subs_;                //!< Field selectors, cycled per match.
    std::size_t                            n_           {0};     //!< Current field index into \ref subs_.
    value_type                             current_;             //!< Current token (by value — no aliasing).
    bool                                   has_m1_      {false}; //!< Whether a `-1` (split) field is present.
    bool                                   suffix_mode_ {false}; //!< Emitting the trailing split suffix.
    bool                                   at_end_      {true};  //!< End-of-sequence.

    //! \brief Computes the current token from the current match and `subs_[n_]`.
    void set_field()
    {
      current_ = (subs_[n_] == -1) ? position_->prefix() : (*position_)[subs_[n_]];
    }

    //! \brief Establishes the first token (or the whole-sequence token when there is no match).
    void init(BidirIt first,
              BidirIt last)
    {
      if (position_ != regex_iterator<BidirIt, CharT, Traits> {}) {
        at_end_ = false;
        set_field();
      }
      else if (has_m1_) {    // no match at all: the whole sequence is ONE split token, then end
        at_end_      = false;
        suffix_mode_ = true; // terminal — the standard yields exactly one token here, no field cycling
        // std marks this whole-sequence suffix token as participating even when empty (matched=true),
        // unlike an empty field *between* matches (a prefix, matched=false). The fuzzer pinned this.
        // (Per [re.tokiter.cnstr] "one of the elements of subs is -1" — has_m1; libstdc++ conforms,
        // libc++ has a bug here that checks only subs[0], so it drops the token for e.g. {1,-1}.)
        current_ = value_type {.first = first, .second = last, .matched = true};
      }
    }
  };

  using sregex_token_iterator  = regex_token_iterator<std::string::const_iterator>;  //!< Over a std::string.
  using cregex_token_iterator  = regex_token_iterator<const char*>;                  //!< Over a C string.
  using wsregex_token_iterator = regex_token_iterator<std::wstring::const_iterator>; //!< Over a std::wstring (std).
  using wcregex_token_iterator = regex_token_iterator<const wchar_t*>;               //!< Over a wide C string (std).
} // namespace real::compat

#endif // REAL_STD_COMPAT_HPP
