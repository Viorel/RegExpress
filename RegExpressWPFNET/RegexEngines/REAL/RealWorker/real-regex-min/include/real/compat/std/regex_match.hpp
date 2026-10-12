/*!
 * \file std/regex_match.hpp
 * \brief std::regex compatibility, part 2/3: `sub_match`, `match_results`, the backend runner and the
 *        `regex_search` / `regex_match` / `regex_replace` free functions.
 */
#ifndef REAL_STD_REGEX_MATCH_HPP
#define REAL_STD_REGEX_MATCH_HPP

// Internal — do not include directly; the entry point is <real/compat/std/regex.hpp>.

#include "regex_core.hpp"

#include <algorithm>
#include <iterator>
#include <ostream>

namespace real::compat {
  /*!
   * \brief A matched sub-expression: a `[first, second)` range into the searched sequence. Any
   *        bidirectional iterator, as `std::sub_match`; only \ref view needs contiguous storage.
   * \tparam BidirIt A bidirectional iterator into the searched sequence.
   */
  template <typename BidirIt>
  class sub_match
  {
  public:

    using iterator        = BidirIt;                                                 //!< The underlying iterator.
    using value_type      = typename std::iterator_traits<BidirIt>::value_type;      //!< The character type.
    using difference_type = typename std::iterator_traits<BidirIt>::difference_type; //!< Distance type.
    using string_type     = std::basic_string<value_type>;                           //!< The owning string type.

    BidirIt first   {};                                                              //!< Start of the sub-match.
    BidirIt second  {};                                                              //!< One past the end of the sub-match.
    bool    matched {false};                                                         //!< Whether this sub-expression participated.

    /*!
     * \brief Length of the sub-match (0 if it did not participate).
     * \return `second - first`, or 0 when \ref matched is `false`.
     */
    [[nodiscard]] difference_type length() const
    {
      return matched ? std::distance(first, second) : difference_type {0};
    }

    /*!
     * \brief The matched text as an owned string (empty if it did not participate).
     * \return A copy of `[first, second)`, or an empty string when \ref matched is `false`.
     */
    [[nodiscard]] string_type str() const
    {
      return matched ? string_type(first, second) : string_type {};
    }

    /*!
     * \brief Implicit conversion to the owned string (std::sub_match parity).
     */
    operator string_type() const // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
    {
      return str();
    }

    /*!
     * \brief A non-owning view of the matched text; a REAL extension, not in `std::sub_match`.
     *
     * Contiguous iterators only: an owning string returned under this name for a `std::list` would dangle
     * once bound to a `string_view`. \ref str serves every iterator.
     * \return A view over `[first, second)`, or an empty view when \ref matched is `false`.
     */
    [[nodiscard]] std::basic_string_view<value_type> view() const
    requires std::contiguous_iterator<BidirIt>
    {
      return matched ? std::basic_string_view<value_type>(std::to_address(first),
                                                          static_cast<std::size_t>(length()))
                     : std::basic_string_view<value_type> {};
    }

    /*!
     * \brief Three-way length/lexicographic comparison against a string (`std::sub_match::compare`).
     * \param[in] other The string to compare against.
     * \return Negative, zero or positive as `str()` orders before, equal to, or after \p other.
     */
    [[nodiscard]] int compare(const string_type& other) const
    {
      return str().compare(other);
    }

    /*!
     * \brief Three-way comparison against a C string (`std::sub_match::compare`).
     * \param[in] other The NUL-terminated string to compare against.
     * \return Negative, zero or positive as `str()` orders before, equal to, or after \p other.
     */
    [[nodiscard]] int compare(const value_type* other) const
    {
      return str().compare(other);
    }

    /*!
     * \brief Three-way comparison against another sub-match, by matched text.
     * \param[in] other The sub-match to compare against.
     * \return Negative, zero or positive as `str()` orders before, equal to, or after `other.str()`.
     */
    [[nodiscard]] int compare(const sub_match& other) const
    {
      return str().compare(other.str());
    }
  };

  /*!
   * \brief Equality against an owned string (the common `std::sub_match` comparison).
   * \param[in] lhs The sub-match.
   * \param[in] rhs The string to compare its text against.
   * \return `true` if they hold the same characters.
   */
  template <typename BidirIt>
  bool operator==(const sub_match<BidirIt>&                       lhs,
                  const typename sub_match<BidirIt>::string_type& rhs)
  {
    return lhs.str() == rhs;
  }

  /*!
   * \brief Equality with the string on the left, for `std::sub_match` parity.
   * \param[in] lhs The string.
   * \param[in] rhs The sub-match whose text is compared.
   * \return `true` if they hold the same characters.
   */
  template <typename BidirIt>
  bool operator==(const typename sub_match<BidirIt>::string_type& lhs,
                  const sub_match<BidirIt>&                       rhs)
  {
    return lhs == rhs.str();
  }

  /*!
   * \brief Equality between two sub-matches, by matched text.
   * \param[in] lhs The left sub-match.
   * \param[in] rhs The right sub-match.
   * \return `true` if they hold the same characters.
   */
  template <typename BidirIt>
  bool operator==(const sub_match<BidirIt>& lhs,
                  const sub_match<BidirIt>& rhs)
  {
    return lhs.str() == rhs.str();
  }

  /*!
   * \brief Ordering between two sub-matches, by matched text (`std::sub_match` parity).
   * \param[in] lhs The left sub-match.
   * \param[in] rhs The right sub-match.
   * \return How `lhs.str()` orders against `rhs.str()`.
   */
  template <typename BidirIt>
  auto operator<=>(const sub_match<BidirIt>& lhs,
                   const sub_match<BidirIt>& rhs)
  {
    return lhs.str() <=> rhs.str();
  }

  /*!
   * \brief Ordering against an owned string; the reversed and the `<`, `>` forms follow from it.
   * \param[in] lhs The sub-match.
   * \param[in] rhs The string.
   * \return How `lhs.str()` orders against \p rhs.
   */
  template <typename BidirIt>
  auto operator<=>(const sub_match<BidirIt>&                       lhs,
                   const typename sub_match<BidirIt>::string_type& rhs)
  {
    return lhs.str() <=> rhs;
  }

  /*!
   * \brief Equality against a C string.
   * \param[in] lhs The sub-match.
   * \param[in] rhs The NUL-terminated string.
   * \return `true` if they hold the same characters.
   */
  template <typename BidirIt>
  bool operator==(const sub_match<BidirIt>&                      lhs,
                  const typename sub_match<BidirIt>::value_type* rhs)
  {
    return lhs.compare(rhs) == 0;
  }

  /*!
   * \brief Ordering against a C string.
   * \param[in] lhs The sub-match.
   * \param[in] rhs The NUL-terminated string.
   * \return How `lhs.str()` orders against \p rhs.
   */
  template <typename BidirIt>
  auto operator<=>(const sub_match<BidirIt>&                      lhs,
                   const typename sub_match<BidirIt>::value_type* rhs)
  {
    return lhs.str() <=> typename sub_match<BidirIt>::string_type(rhs);
  }

  /*!
   * \brief Equality against one character: the sub-match holds exactly it.
   * \param[in] lhs The sub-match.
   * \param[in] rhs The character.
   * \return `true` if the matched text is that one character.
   */
  template <typename BidirIt>
  bool operator==(const sub_match<BidirIt>&                      lhs,
                  const typename sub_match<BidirIt>::value_type& rhs)
  {
    return lhs.str() == typename sub_match<BidirIt>::string_type(1, rhs);
  }

  /*!
   * \brief Ordering against one character, as against the string holding only it.
   * \param[in] lhs The sub-match.
   * \param[in] rhs The character.
   * \return How `lhs.str()` orders against that string.
   */
  template <typename BidirIt>
  auto operator<=>(const sub_match<BidirIt>&                      lhs,
                   const typename sub_match<BidirIt>::value_type& rhs)
  {
    return lhs.str() <=> typename sub_match<BidirIt>::string_type(1, rhs);
  }

  /*!
   * \brief Streams `m.str()`, empty when the group did not participate (`std::sub_match` parity).
   * \param[in,out] os The stream.
   * \param[in]     m  The sub-match whose text is written.
   * \return \p os, after writing.
   */
  template <typename CharT, typename Traits, typename BidirIt>
  std::basic_ostream<CharT, Traits>&
  operator<<(std::basic_ostream<CharT, Traits>& os,
             const sub_match<BidirIt>&          m)
  {
    return os << m.str();
  }

  /*!
   * \brief The result of a match: group sub-matches plus the prefix and suffix.
   *
   * Holds both ends of the searched sequence: the suffix's end is not derivable from a base iterator.
   * Filled from REAL's byte offsets, or copied from a `std::match_results` on the std path.
   * \tparam BidirIt A bidirectional iterator into the searched sequence.
   * \tparam Alloc   Allocator for the sub-match vector.
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
    using allocator_type  = Alloc;                                                   //!< Allocator type.

    /*!
     * \brief Whether a successful match has been stored.
     * \return `true` once a search or match has filled this object.
     */
    [[nodiscard]] bool ready() const noexcept
    {
      return ready_;
    }

    /*!
     * \brief Number of marks (groups), including group 0; 0 when there was no match.
     * \return The sub-match count.
     */
    [[nodiscard]] size_type size() const noexcept
    {
      return groups_.size();
    }

    /*!
     * \brief The most marks this object could hold.
     * \return The sub-match vector's limit.
     */
    [[nodiscard]] size_type max_size() const noexcept
    {
      return groups_.max_size();
    }

    /*!
     * \brief Whether there are no marks at all.
     * \return `true` when \ref size is 0, i.e. no match was stored.
     */
    [[nodiscard]] bool      empty() const noexcept
    {
      return groups_.empty();
    }

    /*!
     * \brief The sub-match for group \p n. An out-of-range \p n yields an unmatched sub-match anchored at the
     *        sequence end, as libstdc++ and libc++ do; a token iterator's out-of-range selector relies on it.
     * \param[in] n Group index; 0 is the whole match.
     * \return That group's sub-match, or the end-anchored unmatched one when \p n is out of range.
     */
    const_reference operator[](size_type n) const
    {
      return n < groups_.size() ? groups_[n] : unmatched_;
    }

    /*!
     * \brief Start offset of group \p n; an out-of-range group sits at the end, as in `std`.
     * \param[in] n Group index; 0 is the whole match.
     * \return Its start offset from the sequence start.
     */
    [[nodiscard]] difference_type position(size_type n = 0) const
    {
      return n < groups_.size() ? std::distance(first_, groups_[n].first)
                                : std::distance(first_, last_);
    }

    /*!
     * \brief Length of group \p n (0 if out of range or unmatched).
     * \param[in] n Group index; 0 is the whole match.
     * \return Its length in characters.
     */
    [[nodiscard]] difference_type length(size_type n = 0) const
    {
      return n < groups_.size() ? groups_[n].length() : difference_type {0};
    }

    /*!
     * \brief Matched text of group \p n (empty if out of range or unmatched).
     * \param[in] n Group index; 0 is the whole match.
     * \return An owned copy of its text.
     */
    [[nodiscard]] string_type str(size_type n = 0) const
    {
      return (*this)[n].str();
    }

    /*!
     * \brief The unmatched prefix (sequence start up to the whole match).
     * \return The prefix sub-match; see \ref rebase_prefix for what it means during iteration.
     */
    [[nodiscard]] const value_type& prefix() const
    {
      return prefix_;
    }

    /*!
     * \brief The unmatched suffix (whole match end to sequence end).
     * \return The suffix sub-match.
     */
    [[nodiscard]] const value_type& suffix() const
    {
      return suffix_;
    }

    /*!
     * \brief Iteration over the marks, group 0 first.
     * \return A const iterator to the first sub-match, as in `std::match_results`.
     */
    [[nodiscard]] const_iterator begin() const
    {
      return groups_.begin();
    }

    /*!
     * \brief End of the mark range.
     * \return One past the last sub-match.
     */
    [[nodiscard]] const_iterator end() const
    {
      return groups_.end();
    }

    /*!
     * \brief Same as \ref begin; the marks are const either way.
     * \return An iterator to the first sub-match.
     */
    [[nodiscard]] const_iterator cbegin() const
    {
      return groups_.begin();
    }

    /*!
     * \brief Same as \ref end.
     * \return One past the last sub-match.
     */
    [[nodiscard]] const_iterator cend() const
    {
      return groups_.end();
    }

    /*!
     * \brief The allocator of the sub-match vector.
     * \return A copy of it.
     */
    [[nodiscard]] allocator_type get_allocator() const
    {
      return groups_.get_allocator();
    }

    /*!
     * \brief Exchanges the whole state with \p other.
     * \param[in,out] other The result to swap with.
     */
    void swap(match_results& other) noexcept
    {
      using std::swap;
      swap(first_, other.first_);
      swap(last_, other.last_);
      groups_.swap(other.groups_);
      swap(prefix_, other.prefix_);
      swap(suffix_, other.suffix_);
      swap(unmatched_, other.unmatched_);
      swap(ready_, other.ready_);
    }

    /*!
     * \brief Writes `[fmt_first, fmt_last)` with its references replaced by this match's text
     *        (`std::match_results::format`).
     *
     * ECMAScript rules: a dollar followed by a dollar, an ampersand, a backtick, a quote or one or two digits
     * inserts a dollar, the match, the prefix, the suffix or that group (nothing for a group that does not
     * exist); any other dollar is itself. `$0` reads as the native std reads it. `format_sed` selects sed's.
     * \tparam OutputIter An output iterator over characters.
     * \param[out] out       Where the result is written.
     * \param[in]  fmt_first Start of the format.
     * \param[in]  fmt_last  One past its end.
     * \param[in]  flags     `format_sed` selects sed's rules; the other bits are ignored.
     * \return \p out advanced past what was written.
     * \pre ready()
     */
    template <typename OutputIter>
    OutputIter format(OutputIter                       out,
                      const char_type*                 fmt_first,
                      const char_type*                 fmt_last,
                      regex_constants::match_flag_type flags = regex_constants::format_default) const
    {
      const auto expand = [&]<bool Sed>() {
                            detail::expand_replacement<Sed>(
                              fmt_first, fmt_last, [&](char_type c) { *out++ = c; },
                              [&](std::size_t g) { out = copy_group(out, static_cast<size_type>(g)); },
                              [&] { out = std::copy(prefix_.first, prefix_.second, out); },
                              [&] { out = std::copy(suffix_.first, suffix_.second, out); });
                          };
      if ((flags & regex_constants::format_sed) != 0U) {
        expand.template operator()<true>();
      }
      else {
        expand.template operator()<false>();
      }
      return out;
    }

    /*!
     * \brief \ref format over a string format.
     * \tparam OutputIter An output iterator over characters.
     * \tparam ST         The format's traits.
     * \tparam SA         The format's allocator.
     * \param[out] out   Where the result is written.
     * \param[in]  fmt   The format.
     * \param[in]  flags `format_sed` selects sed's rules.
     * \return \p out advanced past what was written.
     * \pre ready()
     */
    template <typename OutputIter, typename ST, typename SA>
    OutputIter format(OutputIter                                  out,
                      const std::basic_string<char_type, ST, SA>& fmt,
                      regex_constants::match_flag_type            flags = regex_constants::format_default) const
    {
      return format(out, fmt.data(), fmt.data() + fmt.size(), flags);
    }

    /*!
     * \brief \ref format into a new string.
     * \tparam ST The format's traits.
     * \tparam SA The format's allocator.
     * \param[in] fmt   The format.
     * \param[in] flags `format_sed` selects sed's rules.
     * \return The expanded format.
     * \pre ready()
     */
    template <typename ST, typename SA>
    std::basic_string<char_type, ST, SA> format(const std::basic_string<char_type, ST, SA>& fmt,
                                                regex_constants::match_flag_type            flags = regex_constants::format_default) const
    {
      std::basic_string<char_type, ST, SA> result;
      format(std::back_inserter(result), fmt.data(), fmt.data() + fmt.size(), flags);
      return result;
    }

    /*!
     * \brief \ref format of a C-string format into a new string.
     * \param[in] fmt   The NUL-terminated format.
     * \param[in] flags `format_sed` selects sed's rules.
     * \return The expanded format.
     * \pre ready()
     */
    string_type format(const char_type*                 fmt,
                       regex_constants::match_flag_type flags = regex_constants::format_default) const
    {
      string_type result;
      format(std::back_inserter(result), fmt, fmt + std::char_traits<char_type>::length(fmt), flags);
      return result;
    }

    // --- engine-facing fill helpers (used by the free functions) ---------------------------

    /*!
     * \brief Resets to the not-ready (no-match) state over the sequence `[first, last)`.
     * \param[in] first Start of the sequence this result will describe.
     * \param[in] last  One past its end; kept so \ref suffix and lengths stay exact.
     */
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

    /*!
     * \brief Marks the result ready but empty, as `std` leaves it after a failed search or match.
     */
    void set_ready_no_match()
    {
      groups_.clear();
      ready_ = true;
    }

    /*!
     * \brief Starts the prefix at \p first, as iteration requires; the std path gets this from
     *        `std::regex_iterator`.
     * \param[in] first The previous match's end.
     */
    void rebase_prefix(BidirIt first)
    {
      prefix_.first   = first;
      prefix_.matched = first != prefix_.second;
    }

    /*!
     * \brief Fills from REAL's byte offsets over `[first_, last_)`, from any match exposing `size`, `start` and
     *        `end` (an engine result, a `detail::offset_match`).
     * \param[in] match The engine's result, whose group offsets are byte offsets into the searched view.
     * \param[in] lead  How many characters the view holds before `first_` (1 under `match_prev_avail`, the
     *                  context it searched with); no match starts among them.
     */
    template <typename RealMatch>
    void fill_from_real(const RealMatch& match,
                        std::size_t      lead = 0)
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
          groups_.push_back(value_type {.first   = first_ + static_cast<difference_type>(start - lead),
                                        .second  = first_ + static_cast<difference_type>(fin - lead),
                                        .matched = true});
        }
      }
      const std::size_t whole_start {match.start(0) - lead};
      const std::size_t whole_end   {match.end(0) - lead};
      prefix_ = value_type {.first   = first_,
                            .second  = first_ + static_cast<difference_type>(whole_start),
                            .matched = whole_start > 0};
      suffix_ = value_type {.first   = first_ + static_cast<difference_type>(whole_end),
                            .second  = last_,
                            .matched = (first_ + static_cast<difference_type>(whole_end)) != last_};
      ready_ = true;
    }

    /*!
     * \brief Fills from REAL's offsets into a copy of a non-contiguous `[first_, last_)`: marks are mapped through
     *        \p at in increasing offset order, so the walk covers their span once whatever the group order.
     * \tparam Cursor A `detail::offset_cursor` over the copied sequence.
     * \param[in]     match The engine's result, whose offsets index the copy.
     * \param[in,out] at    The cursor; at or before the match's start, left at its last mark.
     * \param[in]     lead  How many characters the copy holds before `first_` (1 under `match_prev_avail`).
     */
    template <typename RealMatch, typename Cursor>
    void fill_from_real_at(const RealMatch& match,
                           Cursor&          at,
                           std::size_t      lead = 0)
    {
      const std::size_t count {match.size()};
      groups_.assign(count, value_type {.first = last_, .second = last_, .matched = false});
      std::vector<std::pair<std::size_t, std::size_t>> marks; // (offset, 2 * group + 1 for an end)
      marks.reserve(2 * count);
      for (std::size_t g = 0; g < count; ++g) {
        if (match.start(g) != real::npos && match.end(g) != real::npos) {
          marks.emplace_back(match.start(g), 2 * g);
          marks.emplace_back(match.end(g), (2 * g) + 1);
        }
      }
      std::sort(marks.begin(), marks.end());
      for (const auto& [offset, slot] : marks) {
        value_type& sub {groups_[slot / 2]};
        ((slot % 2) == 0 ? sub.first : sub.second)  = at.to(offset);
        sub.matched                                 = true;
      }
      prefix_ = value_type {.first = first_, .second = groups_[0].first, .matched = match.start(0) > lead};
      suffix_ = value_type {.first = groups_[0].second, .second = last_, .matched = groups_[0].second != last_};
      ready_  = true;
    }

    /*!
     * \brief Copies from a `std::match_results` (the fallback path) over the same sequence.
     * \param[in] match The standard library's result to copy marks, prefix and suffix from.
     */
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

    /*!
     * \brief Copies group \p g's text to \p out, nothing when it did not take part or does not exist.
     * \tparam OutputIter An output iterator over characters.
     * \param[out] out Where the text is written.
     * \param[in]  g   The group number.
     * \return \p out advanced past it.
     */
    template <typename OutputIter>
    OutputIter copy_group(OutputIter out,
                          size_type  g) const
    {
      const value_type& sub {(*this)[g]};
      return sub.matched ? std::copy(sub.first, sub.second, out) : out;
    }

    BidirIt                            first_     {};      //!< Start of the searched sequence.
    BidirIt                            last_      {};      //!< End of the searched sequence.
    std::vector<value_type, Alloc>     groups_;            //!< Group sub-matches (0 = whole match).
    value_type                         prefix_    {};      //!< Unmatched prefix.
    value_type                         suffix_    {};      //!< Unmatched suffix.
    value_type                         unmatched_ {};      //!< Sentinel for out-of-range operator[] (anchored at last_).
    bool                               ready_     {false}; //!< Whether a match is stored.
  };

  /*!
   * \brief Equality of two results (`std::match_results` parity): both not ready, or both ready and both
   *        empty, or both holding the same text in the prefix, every group and the suffix.
   * \param[in] lhs The left result.
   * \param[in] rhs The right result.
   * \return `true` when they are equal in that sense.
   */
  template <typename BidirIt, typename Alloc>
  bool operator==(const match_results<BidirIt, Alloc>& lhs,
                  const match_results<BidirIt, Alloc>& rhs)
  {
    if (!lhs.ready() || !rhs.ready()) {
      return !lhs.ready() && !rhs.ready();
    }
    if (lhs.empty() || rhs.empty()) {
      return lhs.empty() && rhs.empty();
    }
    return lhs.prefix() == rhs.prefix() && lhs.size() == rhs.size()
           && std::equal(lhs.begin(), lhs.end(), rhs.begin()) && lhs.suffix() == rhs.suffix();
  }

  /*!
   * \brief Exchanges two results (`std::swap` parity).
   * \param[in,out] lhs One result.
   * \param[in,out] rhs The other.
   */
  template <typename BidirIt, typename Alloc>
  void swap(match_results<BidirIt, Alloc>& lhs,
            match_results<BidirIt, Alloc>& rhs) noexcept
  {
    lhs.swap(rhs);
  }

  using ssub_match  = sub_match<std::string::const_iterator>;  //!< Sub-match over a std::string.
  using csub_match  = sub_match<const char*>;                  //!< Sub-match over a C string.
  using wssub_match = sub_match<std::wstring::const_iterator>; //!< Sub-match over a std::wstring.
  using wcsub_match = sub_match<const wchar_t*>;               //!< Sub-match over a wide C string.

  using smatch  = match_results<std::string::const_iterator>;  //!< Match over a std::string.
  using cmatch  = match_results<const char*>;                  //!< Match over a C string.
  using wsmatch = match_results<std::wstring::const_iterator>; //!< Match over a std::wstring (always std).
  using wcmatch = match_results<const wchar_t*>;               //!< Match over a wide C string (always std).

  // --- free functions ----------------------------------------------------------------------

  /*! \brief Backend routing and format expansion for the compat layer. Not a stable API. */
  namespace detail {

    /*!
     * \brief Maps offsets into a non-contiguous range's copy back to the caller's iterators by moving one iterator
     *        from the last offset mapped; `std::next(first, offset)` per mark would rewalk a `std::list`.
     * \tparam BidirIt The caller's iterator.
     */
    template <typename BidirIt>
    class offset_cursor
    {
    public:

      offset_cursor() = default; //!< Unbound; assigned before use.

      /*!
       * \brief Binds the cursor to \p it, which sits at \p offset of the sequence.
       * \param[in] it     An iterator into the caller's sequence.
       * \param[in] offset Its offset from the sequence start.
       */
      offset_cursor(BidirIt     it,
                    std::size_t offset)
        : it_ {it}, offset_ {offset}
      {}

      /*!
       * \brief Moves to \p offset, either way.
       * \param[in] offset An offset of the sequence, at most its length.
       * \return The caller's iterator at \p offset.
       */
      BidirIt to(std::size_t offset)
      {
        using difference = typename std::iterator_traits<BidirIt>::difference_type;
        std::advance(it_, static_cast<difference>(offset) - static_cast<difference>(offset_));
        offset_ = offset;
        return it_;
      }

    private:

      BidirIt     it_     {}; //!< The caller's iterator at \ref offset_.
      std::size_t offset_ {}; //!< Where \ref it_ sits.
    };

    /*!
     * \brief What a `regex_search` or `regex_match` call asks of REAL, its match flags included.
     */
    struct call_shape
    {
      bool               anchored   {}; //!< `regex_match`: the whole sequence, rather than the leftmost match.
      bool               continuous {}; //!< `match_continuous`: the match starts at `first`.
      std::size_t        lead       {}; //!< 1 under `match_prev_avail`: the view starts at `--first`, its context.
      bool               non_empty  {}; //!< `match_not_null` on a pattern that can match empty: no empty match.
      const real::regex* engine     {}; //!< The engine that runs: the pattern's, or its `not_eol` / `not_eow` variant.
    };

    /*!
     * \brief The REAL call for \p shape over \p view, whose first `shape.lead` characters are context only: `^`,
     *        `\b` and a lookbehind see them, and a non-multiline `^` does not hold after them ([re.matchflag]).
     * \param[in] re    The pattern; real-backed.
     * \param[in] view  The sequence, behind its context.
     * \param[in] shape The call.
     * \return The engine's result, offsets into \p view.
     */
    template <typename CharT, typename Traits>
    [[nodiscard]] auto find_real(const basic_regex<CharT, Traits>& re,
                                 std::string_view                  view,
                                 call_shape                        shape)
    {
      const real::regex& engine {*shape.engine};
      if (shape.non_empty) {
        if (shape.anchored) {
          // A whole-sequence match is empty only over an empty sequence.
          return view.size() == shape.lead ? real::regex::result_type {} : engine.fullmatch(view, shape.lead);
        }
        return shape.continuous ? real::detail::non_empty_access::match(engine, view, shape.lead)
                                : real::detail::non_empty_access::search(engine, view, shape.lead);
      }
      // A whole-sequence match has one candidate span, so a POSIX grammar needs no longest form here.
      if (shape.anchored) {
        return shape.lead == 0 ? engine.fullmatch(view) : engine.fullmatch(view, shape.lead);
      }
      if (shape.continuous) {
        return shape.lead == 0 ? engine.match(view) : engine.match(view, shape.lead);
      }
      if (re.posix_longest()) {
        return shape.lead == 0 ? engine.search_longest(view) : engine.search_longest(view, shape.lead);
      }
      return shape.lead == 0 ? engine.search(view) : engine.search(view, shape.lead);
    }

    /*!
     * \brief \ref run_impl over a non-contiguous range on REAL: the range is copied once, searched, and every offset
     *        of the result mapped back to the caller's iterators in one forward walk.
     * \param[in]  first    Start of the sequence.
     * \param[in]  last     One past its end.
     * \param[out] m        Result; reset over `[first, last)` by the caller.
     * \param[in]  re       The pattern; real-backed.
     * \param[in]  shape    The call; under `match_prev_avail` the copy starts at `--first`.
     * \return `true` if a match was found and \p m filled.
     */
    template <typename BidirIt, typename CharT, typename Traits>
    bool run_copied(BidirIt                           first,
                    BidirIt                           last,
                    match_results<BidirIt>&           m,
                    const basic_regex<CharT, Traits>& re,
                    call_shape                        shape)
    {
      const BidirIt     from   {shape.lead == 0 ? first : std::prev(first)};
      const std::string text(from, last);
      const auto        result {find_real(re, text, shape)};
      if (!result.matched()) {
        m.set_ready_no_match();
        return false;
      }
      offset_cursor<BidirIt> at {from, 0};
      m.fill_from_real_at(result, at, shape.lead);
      return true;
    }

    /*!
     * \brief Whether an iteration may stay on `real` under \p mf: only `match_default` and `match_any` (met by the
     *        leftmost match) qualify; a constraining bit routes it to `std` rather than be ignored. A single search
     *        or match honors more (\ref call_stays_real).
     * \param[in] mf The match flags the caller passed.
     * \return `true` if the iteration may stay on REAL.
     */
    [[nodiscard]] inline bool real_honors(regex_constants::match_flag_type mf) noexcept
    {
      constexpr unsigned non_constraining {static_cast<unsigned>(regex_constants::match_default)
                                           | static_cast<unsigned>(regex_constants::match_any)};
      return (static_cast<unsigned>(mf) & ~non_constraining) == 0U;
    }

    /*!
     * \brief Whether one `regex_search` or `regex_match` call can honor \p mf on `real`.
     *
     * Beyond what \ref real_honors accepts, `match_continuous` is a match anchored at `first` (a POSIX
     * leftmost-longest search has no anchored form, so it stays on `std`); `match_prev_avail` is a region search
     * over a view starting one character earlier, under which the standard ignores `match_not_bol` and
     * `match_not_bow` ([re.matchflag]); `match_not_null` needs \p not_null_ok; `match_not_eol` and
     * `match_not_eow` run a rewrite of the pattern (`basic_regex::end_engine`), which may still decline.
     * `not_bol` / `not_bow` alone route to `std`.
     * \param[in] mf          The match flags the caller passed.
     * \param[in] anchored    A whole-sequence match (`regex_match`).
     * \param[in] longest     The pattern searches leftmost-longest (a POSIX grammar on REAL).
     * \param[in] not_null_ok REAL honors `match_not_null` for this pattern (see \ref not_null_stays_real).
     * \return `true` if the call may stay on the real backend.
     */
    [[nodiscard]] inline bool call_stays_real(regex_constants::match_flag_type mf,
                                              bool                             anchored,
                                              bool                             longest,
                                              bool                             not_null_ok) noexcept
    {
      using namespace regex_constants;
      const auto bits    {static_cast<unsigned>(mf)};
      unsigned   honored {static_cast<unsigned>(match_default) | static_cast<unsigned>(match_any)
                          | static_cast<unsigned>(match_prev_avail)};
      if (not_null_ok) {
        honored |= static_cast<unsigned>(match_not_null);
      }
      honored |= static_cast<unsigned>(match_not_eol) | static_cast<unsigned>(match_not_eow);
      if (anchored || !longest) {
        honored |= static_cast<unsigned>(match_continuous);
      }
      if ((bits & static_cast<unsigned>(match_prev_avail)) != 0U) {
        honored |= static_cast<unsigned>(match_not_bol) | static_cast<unsigned>(match_not_bow);
      }
      return (bits & ~honored) == 0U;
    }

    /*!
     * \brief Whether REAL honors `match_not_null` for \p re: always when the pattern cannot match empty (the flag
     *        then changes nothing); otherwise when its search is leftmost-first and its traversal REAL's (a
     *        nullable capturing group under a quantifier takes another last iteration than std's).
     * \param[in] re The pattern; real-backed.
     * \return `true` if a call under `match_not_null` may stay on REAL.
     */
    template <typename CharT, typename Traits>
    [[nodiscard]] bool not_null_stays_real(const basic_regex<CharT, Traits>& re) noexcept
    {
      return !re.nullable() || (re.uses_real_traversal() && !re.posix_longest());
    }

    /*!
     * \brief The REAL call a `regex_search` or `regex_match` with \p mf makes; see \ref call_stays_real.
     * \param[in] mf       The match flags, which \ref call_stays_real accepted.
     * \param[in] anchored A whole-sequence match (`regex_match`).
     * \param[in] nullable The pattern can match empty, so `match_not_null` changes what it finds.
     * \return The call's shape.
     */
    [[nodiscard]] inline call_shape shape_of(regex_constants::match_flag_type mf,
                                             bool                             anchored,
                                             bool                             nullable) noexcept
    {
      return call_shape {.anchored   = anchored,
                         .continuous = (mf & regex_constants::match_continuous) != 0U,
                         .lead       = (mf & regex_constants::match_prev_avail) != 0U ? 1U : 0U,
                         .non_empty  = nullable && (mf & regex_constants::match_not_null) != 0U};
    }

    /*!
     * \brief The REAL engine a `regex_search` or `regex_match` with \p mf runs on, or null for `std`.
     * \param[in] re       The pattern.
     * \param[in] mf       The match flags.
     * \param[in] anchored A whole-sequence match (`regex_match`).
     * \return The pattern's engine, its `not_eol` / `not_eow` variant, or null.
     */
    template <typename CharT, typename Traits>
    [[nodiscard]] const real::regex* real_engine_for(const basic_regex<CharT, Traits>& re,
                                                     regex_constants::match_flag_type  mf,
                                                     bool                              anchored)
    {
      if (!re.uses_real()
          || !call_stays_real(mf, anchored, re.posix_longest(),
                              (mf & regex_constants::match_not_null) == 0U || not_null_stays_real(re))) {
        return nullptr;
      }
      const bool not_eol {(mf & regex_constants::match_not_eol) != 0U};
      const bool not_eow {(mf & regex_constants::match_not_eow) != 0U};
      return not_eol || not_eow ? re.end_engine(not_eol, not_eow) : &std::get<real::regex>(re.engine());
    }

    /*!
     * \brief Whether `regex_replace` may run on `real` under \p f: the real expanders honor `format_first_only`,
     *        `format_no_copy`, `format_sed` and `match_any`; the traversal would ignore a constraining match flag,
     *        so any routes the whole substitution to `std`.
     * \param[in] f The match/format flags the caller passed to `regex_replace`.
     * \return `true` if the real expander honors all of them, so the replace may stay on the real backend.
     */
    [[nodiscard]] inline bool replace_stays_real(regex_constants::match_flag_type f) noexcept
    {
      using namespace regex_constants;
      constexpr unsigned honored {static_cast<unsigned>(match_default) | static_cast<unsigned>(match_any)
                                  | static_cast<unsigned>(format_first_only)
                                  | static_cast<unsigned>(format_no_copy) | static_cast<unsigned>(format_sed)};
      return (static_cast<unsigned>(f) & ~honored) == 0U;
    }

    /*!
     * \brief Maps every compat match and format flag to `std::regex_constants`; a bit missing here would be
     *        silently dropped on the std path.
     * \param[in] f The compat flags to translate.
     * \return The equivalent `std::regex_constants::match_flag_type`.
     */
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

    /*!
     * \brief Runs the backend over `[first, last)`, filling \p m under \p Capture; a flag REAL cannot honor
     *        routes to `std`.
     * \tparam Capture Fill \p m (`regex_search` / `regex_match` with results); without it no result is built.
     * \param[in]  first    Start of the sequence to run over.
     * \param[in]  last     One past its end.
     * \param[out] m        Under \p Capture, filled on success and left ready-but-unmatched on failure; else
     *                      unused (null).
     * \param[in]  re       The pattern, whose backend decides which engine runs.
     * \param[in]  anchored Whole-sequence match (`regex_match`) rather than leftmost search.
     * \param[in]  mf       Match flags (\ref call_stays_real).
     * \return `true` if a match was found.
     */
    template <bool Capture, typename BidirIt, typename CharT, typename Traits>
    bool run_impl(BidirIt                                       first,
                  BidirIt                                       last,
                  std::type_identity_t<match_results<BidirIt>>* m,
                  const basic_regex<CharT, Traits>&             re,
                  bool                                          anchored,
                  regex_constants::match_flag_type              mf)
    {
      if constexpr (Capture) {
        m->reset(first, last);
      }
      if constexpr (real_eligible<CharT, Traits>) {
        if (const real::regex* const engine {real_engine_for(re, mf, anchored)}; engine != nullptr) {
          call_shape shape {shape_of(mf, anchored, re.nullable())};
          shape.engine = engine;
          if constexpr (!std::contiguous_iterator<BidirIt>) {
            // A deque, a list, a reverse iterator: no byte view covers the range, so REAL searches a copy.
            if constexpr (Capture) {
              return run_copied(first, last, *m, re, shape);
            }
            else {
              return find_real(re, std::string(shape.lead == 0 ? first : std::prev(first), last), shape).matched();
            }
          }
          else {
            const std::string_view view   {std::to_address(first) - shape.lead,
                                           static_cast<std::size_t>(std::distance(first, last)) + shape.lead};
            const auto             result {find_real(re, view, shape)};
            if constexpr (Capture) {
              if (!result.matched()) {
                m->set_ready_no_match();
                return false;
              }
              m->fill_from_real(result, shape.lead);
              return true;
            }
            else {
              return result.matched();
            }
          }
        }
      }
      const std::basic_regex<CharT, Traits>& std_engine {re.std_engine()}; // lazy-built if real-backed
      const auto                             sf         {to_std_match(mf)};
      // With results even without Capture: libc++'s overloads without them search a basic_string COPY of a range
      // that is not a pointer pair, and match_prev_avail then reads the byte before that copy.
      std::match_results<BidirIt>            std_m;
      const bool                             ok         {std_call([&] {
                                                                    return anchored ? std::regex_match(first, last, std_m, std_engine, sf)
                                                                                : std::regex_search(first, last, std_m, std_engine, sf);
                                                                  })};
      if constexpr (Capture) {
        if (!ok) {
          m->set_ready_no_match();
          return false;
        }
        m->fill_from_std(std_m);
      }
      return ok;
    }
  } // namespace detail

  /*!
   * \brief Leftmost search of `[first, last)`, as `std::regex_search`; the overloads taking no
   *        \ref match_results skip capture filling.
   * \param[in]  first Start of the sequence to search.
   * \param[in]  last  One past its end.
   * \param[out] m     Result filled on success; ready-but-unmatched on failure, as `std` leaves it.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags; a constraining one routes to `std`.
   * \return `true` if a match was found.
   */
  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_search(BidirIt                           first,
                    BidirIt                           last,
                    match_results<BidirIt>&           m,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<true>(first, last, &m, re, /*anchored=*/ false, flags);
  }

  /*!
   * \brief Leftmost search over a `std::basic_string`, as the primary overload.
   * \param[in]  s     The subject.
   * \param[out] m     Result filled on success.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if a match was found.
   */
  template <typename CharT, typename Traits>
  bool regex_search(const std::basic_string<CharT>&                                   s,
                    match_results<typename std::basic_string<CharT>::const_iterator>& m,
                    const basic_regex<CharT, Traits>&                                 re,
                    regex_constants::match_flag_type                                  flags = regex_constants::match_default)
  {
    return detail::run_impl<true>(s.begin(), s.end(), &m, re, false, flags);
  }

  /*!
   * \brief Leftmost search over a C string, as the primary overload.
   * \param[in]  s     The subject.
   * \param[out] m     Result filled on success.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if a match was found.
   */
  template <typename CharT, typename Traits>
  bool regex_search(const CharT                     * s,
                    match_results<const CharT*>&      m,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<true>(s, s + std::char_traits<CharT>::length(s), &m, re, false, flags);
  }

  /*!
   * \brief Leftmost search over `[first, last)`, without capturing, as the primary overload.
   * \param[in]  first Start of the sequence.
   * \param[in]  last  One past its end.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if a match was found.
   */
  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_search(BidirIt                           first,
                    BidirIt                           last,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<false>(first, last, nullptr, re, false, flags);
  }

  /*!
   * \brief Leftmost search over a `std::basic_string`, without capturing, as the primary overload.
   * \param[in]  s     The subject.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if a match was found.
   */
  template <typename CharT, typename Traits>
  bool regex_search(const std::basic_string<CharT>&   s,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<false>(s.begin(), s.end(), nullptr, re, false, flags);
  }

  /*!
   * \brief Leftmost search over a C string, without capturing, as the primary overload.
   * \param[in]  s     The subject.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if a match was found.
   */
  template <typename CharT, typename Traits>
  bool regex_search(const CharT                     * s,
                    const basic_regex<CharT, Traits>& re,
                    regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<false>(s, s + std::char_traits<CharT>::length(s), nullptr, re, false, flags);
  }

  /*!
   * \brief Match of the entire `[first, last)`, as `std::regex_match`; the overloads taking no
   *        \ref match_results skip capture filling.
   * \param[in]  first Start of the sequence that must match in full.
   * \param[in]  last  One past its end.
   * \param[out] m     Result filled on success; ready-but-unmatched on failure.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags; a constraining one routes to `std`.
   * \return `true` if the whole sequence matched.
   */
  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_match(BidirIt                           first,
                   BidirIt                           last,
                   match_results<BidirIt>&           m,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<true>(first, last, &m, re, /*anchored=*/ true, flags);
  }

  /*!
   * \brief Whole-sequence match over a `std::basic_string`, as the primary overload.
   * \param[in]  s     The subject.
   * \param[out] m     Result filled on success.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if the whole sequence matched.
   */
  template <typename CharT, typename Traits>
  bool regex_match(const std::basic_string<CharT>&                                   s,
                   match_results<typename std::basic_string<CharT>::const_iterator>& m,
                   const basic_regex<CharT, Traits>&                                 re,
                   regex_constants::match_flag_type                                  flags = regex_constants::match_default)
  {
    return detail::run_impl<true>(s.begin(), s.end(), &m, re, true, flags);
  }

  /*!
   * \brief Whole-sequence match over a C string, as the primary overload.
   * \param[in]  s     The subject.
   * \param[out] m     Result filled on success.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if the whole sequence matched.
   */
  template <typename CharT, typename Traits>
  bool regex_match(const CharT                     * s,
                   match_results<const CharT*>&      m,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<true>(s, s + std::char_traits<CharT>::length(s), &m, re, true, flags);
  }

  /*!
   * \brief Whole-sequence match over `[first, last)`, without capturing, as the primary overload.
   * \param[in]  first Start of the sequence.
   * \param[in]  last  One past its end.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if the whole sequence matched.
   */
  template <typename BidirIt, typename CharT, typename Traits>
  bool regex_match(BidirIt                           first,
                   BidirIt                           last,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<false>(first, last, nullptr, re, true, flags);
  }

  /*!
   * \brief Whole-sequence match over a `std::basic_string`, without capturing, as the primary overload.
   * \param[in]  s     The subject.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if the whole sequence matched.
   */
  template <typename CharT, typename Traits>
  bool regex_match(const std::basic_string<CharT>&   s,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<false>(s.begin(), s.end(), nullptr, re, true, flags);
  }

  /*!
   * \brief Whole-sequence match over a C string, without capturing, as the primary overload.
   * \param[in]  s     The subject.
   * \param[in]  re    The pattern.
   * \param[in]  flags Match flags.
   * \return `true` if the whole sequence matched.
   */
  template <typename CharT, typename Traits>
  bool regex_match(const CharT                     * s,
                   const basic_regex<CharT, Traits>& re,
                   regex_constants::match_flag_type  flags = regex_constants::match_default)
  {
    return detail::run_impl<false>(s, s + std::char_traits<CharT>::length(s), nullptr, re, true, flags);
  }

  // An rvalue string is rejected, as in std; the forms with match flags too, or a temporary binds to the
  // const-ref overload and the results dangle.
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

    /*!
     * \brief Appends one match's expanded replacement (\ref expand_replacement), the prefix running from
     *        \p prefix_start as in `std::regex_replace`. A `$0` never reaches here (\ref format_forces_std
     *        routes it to std).
     * \param[in,out] out          Destination the expansion is appended to.
     * \param[in]     m            The match whose groups the format refers to.
     * \param[in]     fmt          The replacement format string.
     * \param[in]     text         The full subject the match's offsets index into.
     * \param[in]     prefix_start Where the unmatched prefix begins — the previous match's end.
     * \param[in]     sed          Select the `format_sed` rules.
     */
    template <typename RealMatch>
    void append_replacement(std::string&     out,
                            const RealMatch& m,
                            std::string_view fmt,
                            std::string_view text,
                            std::size_t      prefix_start,
                            bool             sed)
    {
      const std::size_t group_count {m.size()}; // includes group 0
      const std::size_t whole_start {m.start(0)};
      const std::size_t whole_end   {m.end(0)};
      const auto        expand = [&]<bool Sed>() {
                                   expand_replacement<Sed>(
                                     fmt.data(), fmt.data() + fmt.size(), [&](char c) { out.push_back(c); },
                                     [&](std::size_t g) {
                                       if (g == 0) {
                                         out.append(text.substr(whole_start, whole_end - whole_start));
                                       }
                                       else if (g < group_count && m.start(g) != real::npos) {
                                         out.append(text.substr(m.start(g), m.end(g) - m.start(g)));
                                       }
                                     },
                                     [&] { out.append(text.substr(prefix_start, whole_start - prefix_start)); },
                                     [&] { out.append(text.substr(whole_end)); });
                                 };
      if (sed) {
        expand.template operator()<true>();
      }
      else {
        expand.template operator()<false>();
      }
    }

    /*!
     * \brief A REAL match found on a suffix of the subject, seen with the subject's offsets: what the
     *        format expander and the match-results fill read (`size`, `start`, `end`).
     * \tparam RealMatch The engine's match type.
     */
    template <typename RealMatch>
    class offset_match
    {
    public:

      /*!
       * \brief Sees \p match, found on the suffix starting at \p offset, with the subject's offsets.
       * \param[in] match  The match; it must outlive this view.
       * \param[in] offset Where the suffix starts in the subject.
       */
      offset_match(const RealMatch& match,
                   std::size_t      offset) noexcept
        : match_ {&match}, offset_ {offset}
      {}

      /*!
       * \brief The group count, whole match included.
       * \return The count.
       */
      [[nodiscard]] std::size_t size() const noexcept
      {
        return match_->size();
      }

      /*!
       * \brief Group \p g's start in the subject.
       * \param[in] g The group.
       * \return The offset, or `real::npos` when the group took no part.
       */
      [[nodiscard]] std::size_t start(std::size_t g) const noexcept
      {
        const std::size_t at {match_->start(g)};
        return at == real::npos ? at : at + offset_;
      }

      /*!
       * \brief Group \p g's end in the subject.
       * \param[in] g The group.
       * \return The offset, or `real::npos` when the group took no part.
       */
      [[nodiscard]] std::size_t end(std::size_t g) const noexcept
      {
        const std::size_t at {match_->end(g)};
        return at == real::npos ? at : at + offset_;
      }

    private:

      const RealMatch* match_;  //!< The match, found on the suffix.
      std::size_t      offset_; //!< Where the suffix starts in the subject.
    };

    /*!
     * \brief Appends one match's replacement: the text since the previous match (unless `format_no_copy`),
     *        then the expanded format.
     * \tparam RealMatch A match with the subject's offsets.
     * \param[in,out] out      The replacement being built.
     * \param[in]     match    The match.
     * \param[in]     fmt      The format.
     * \param[in]     text     The subject.
     * \param[in,out] last_end The previous match's end; left at this one's.
     * \param[in]     no_copy  `format_no_copy`: the text between matches is dropped.
     * \param[in]     sed      `format_sed`: the format follows sed's rules rather than ECMAScript's.
     */
    template <typename RealMatch>
    void append_replacement(std::string&      out,
                            const RealMatch&  match,
                            std::string_view  fmt,
                            std::string_view  text,
                            std::size_t&      last_end,
                            bool              no_copy,
                            bool              sed)
    {
      const std::size_t prefix_start {last_end};
      if (!no_copy) {
        out.append(text.substr(last_end, match.start(0) - last_end));
      }
      append_replacement(out, match, fmt, text, prefix_start, sed);
      last_end = match.end(0);
    }

    /*!
     * \brief The retry [re.regiter.incr] makes after the iteration's first match came out empty at \p at: a
     *        non-empty match starting there, searched without `match_prev_avail`, so `\b`, `^` and a lookbehind
     *        read \p at as the start. Later empty matches need no retry: `real`'s own advance is the standard's.
     * \param[in] engine The pattern.
     * \param[in] text   The subject.
     * \param[in] at     Where the empty match was.
     * \return The match on `text.substr(at)`, offsets relative to \p at; empty when there is none.
     */
    inline std::optional<real::regex::result_type> nonempty_at_without_context(const real::regex& engine,
                                                                               std::string_view   text,
                                                                               std::size_t        at)
    {
      // The first match on the suffix may be the empty one at its start; the next one then forbids an
      // empty match there, so it starts there only if it is the non-empty match sought.
      for (auto walk {engine.find_iter(text.substr(at)).begin()}; !walk.exhausted(); ++walk) {
        const auto& match {*walk};
        if (match.start(0) != 0) {
          return std::nullopt;
        }
        if (match.end(0) != 0) {
          return match;
        }
      }
      return std::nullopt;
    }
  } // namespace detail

  /*!
   * \brief Replaces matches of \p re in \p s with the formatted \p fmt; the other overloads forward here.
   *
   * Runs on `real` when `basic_regex::uses_real_traversal` holds; a constraining flag or an ECMAScript `$0`
   * routes to `std::regex_replace`.
   * \param[in] s     The subject.
   * \param[in] re    The pattern whose matches are replaced.
   * \param[in] fmt   ECMAScript replacement format; `$N` refers to a group, `$&` to the whole match.
   * \param[in] flags Match/format flags; one the real expander does not honor routes to `std`.
   * \return The subject with every match replaced.
   */
  template <typename CharT, typename Traits>
  std::basic_string<CharT> regex_replace(const std::basic_string<CharT>&   s,
                                         const basic_regex<CharT, Traits>& re,
                                         const std::basic_string<CharT>&   fmt,
                                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    if constexpr (!detail::real_eligible<CharT, Traits>) {
      return detail::std_call([&] { return std::regex_replace(s, re.std_engine(), fmt, detail::to_std_match(flags)); });
    }
    else {
      // Under sed a `$` is an ordinary character, so only an ECMAScript `$0` forces std.
      const bool sed {(flags & regex_constants::format_sed) != 0U};
      if (!re.uses_real_traversal() || !detail::replace_stays_real(flags)
          || (!sed && detail::format_forces_std(std::string_view {fmt}))) {
        return detail::std_call([&] { return std::regex_replace(s, re.std_engine(), fmt, detail::to_std_match(flags)); });
      }
      const real::regex&     engine     {std::get<real::regex>(re.engine())};
      const std::string_view text       {s};
      std::string            out;
      const bool             first_only {(flags & regex_constants::format_first_only) != 0U};
      const bool             no_copy    {(flags & regex_constants::format_no_copy) != 0U};
      std::size_t            last_end   {0};
      bool                   done       {false};
      // The walk restarts at most once: after a first match that came out empty, whose retry the standard
      // makes without the text before it (detail::nonempty_at_without_context).
      std::size_t from    {0};
      bool        first   {true};
      bool        restart {true};
      while (restart && !done) {
        restart = false;
        const auto matches {re.posix_longest() ? engine.find_iter_longest(text, from) : engine.find_iter(text, from)};
        for (auto walk {matches.begin()}; !walk.exhausted(); ++walk) {
          const auto& match {*walk};
          const bool  retry_first {first && match.start(0) == match.end(0)};
          first = false;
          detail::append_replacement(out, match, std::string_view {fmt}, text, last_end, no_copy, sed);
          done = first_only;
          if (done) {
            break;
          }
          if (retry_first) {
            const std::size_t at {match.start(0)};
            if (const auto retry {detail::nonempty_at_without_context(engine, text, at)}; retry.has_value()) {
              detail::append_replacement(out, detail::offset_match<real::regex::result_type> {*retry, at},
                                         std::string_view {fmt}, text, last_end, no_copy, sed);
              done = first_only;
              if (done) {
                break;
              }
              from = at + retry->end(0);
            }
            else if (at < text.size()) {
              from = at + 1; // no non-empty match there: the search goes on past it, with its context
            }
            else {
              break;         // the empty match was at the end: nothing follows it
            }
            restart = true;
            break;
          }
        }
      }
      if (!no_copy) {
        out.append(text.substr(last_end));
      }
      return out;
    }
  }

  /*!
   * \brief `regex_replace` overload for a C-string format.
   * \param[in] s     The subject.
   * \param[in] re    The pattern whose matches are replaced.
   * \param[in] fmt   ECMAScript replacement format, as a C string.
   * \param[in] flags Match/format flags.
   * \return The subject with every match replaced.
   */
  template <typename CharT, typename Traits>
  std::basic_string<CharT> regex_replace(const std::basic_string<CharT>&   s,
                                         const basic_regex<CharT, Traits>& re,
                                         const CharT                     * fmt,
                                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    return regex_replace(s, re, std::basic_string<CharT>(fmt), flags);
  }

  /*!
   * \brief `regex_replace` writing to an output iterator (std parity).
   * \param[out] out   Destination the result is written through.
   * \param[in]  first Start of the subject sequence.
   * \param[in]  last  One past its end.
   * \param[in]  re    The pattern whose matches are replaced.
   * \param[in]  fmt   ECMAScript replacement format.
   * \param[in]  flags Match/format flags.
   * \return \p out advanced past what was written.
   */
  template <typename OutputIt, typename BidirIt, typename CharT, typename Traits>
  OutputIt regex_replace(OutputIt                          out,
                         BidirIt                           first,
                         BidirIt                           last,
                         const basic_regex<CharT, Traits>& re,
                         const std::basic_string<CharT>&   fmt,
                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    if ((flags & regex_constants::match_prev_avail) != 0U) {
      // `--first` is readable only through the caller's iterators, never in the copy below; the flag routes
      // to std in any case.
      return detail::std_call([&] {
                                return std::regex_replace(out, first, last, re.std_engine(), fmt, detail::to_std_match(flags));
                              });
    }
    const std::basic_string<CharT> result {regex_replace(std::basic_string<CharT>(first, last), re, fmt, flags)};
    return std::copy(result.begin(), result.end(), out);
  }

  /*!
   * \brief `regex_replace` to an output iterator with a C-string format, so a string literal binds (std parity).
   * \param[out] out   Destination the result is written through.
   * \param[in]  first Start of the subject sequence.
   * \param[in]  last  One past its end.
   * \param[in]  re    The pattern whose matches are replaced.
   * \param[in]  fmt   ECMAScript replacement format, as a C string.
   * \param[in]  flags Match/format flags.
   * \return \p out advanced past what was written.
   */
  template <typename OutputIt, typename BidirIt, typename CharT, typename Traits>
  OutputIt regex_replace(OutputIt                          out,
                         BidirIt                           first,
                         BidirIt                           last,
                         const basic_regex<CharT, Traits>& re,
                         const CharT                     * fmt,
                         regex_constants::match_flag_type  flags = regex_constants::format_default)
  {
    return regex_replace(out, first, last, re, std::basic_string<CharT>(fmt), flags);
  }
} // namespace real::compat

#endif // REAL_STD_REGEX_MATCH_HPP
