/*!
 * \file std/regex_match.hpp
 * \brief std::regex-compatibility layer, part 2/3: `sub_match`, `match_results`, the shared runner,
 *        and the `regex_search` / `regex_match` / `regex_replace` free functions. Included via the
 *        `std/regex.hpp` umbrella.
 */
#ifndef REAL_STD_REGEX_MATCH_HPP
#define REAL_STD_REGEX_MATCH_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "regex_core.hpp"

#include <algorithm>
#include <iterator>

namespace real::compat {
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
} // namespace real::compat

#endif // REAL_STD_REGEX_MATCH_HPP
