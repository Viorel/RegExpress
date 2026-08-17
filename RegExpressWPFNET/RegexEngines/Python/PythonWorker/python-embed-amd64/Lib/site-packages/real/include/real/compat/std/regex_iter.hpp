/*!
 * \file std/regex_iter.hpp
 * \brief std::regex-compatibility layer, part 3/3: `regex_iterator` and `regex_token_iterator`.
 *        Included via the `std/regex.hpp` umbrella.
 */
#ifndef REAL_STD_REGEX_ITER_HPP
#define REAL_STD_REGEX_ITER_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "regex_match.hpp"

#include <iterator>
#include <memory>

/*!
 * \brief Drop-in replacements for `<regex>`: \c basic_regex, \c regex_search / \c regex_match /
 *        \c regex_replace and the iterator types -- backed by REAL's linear-time engine where it can
 *        serve the pattern, and by `std::regex` otherwise, never by a silent divergence.
 */
namespace real::compat {
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

    /*!
     * \brief Constructs the end sentinel.
     */
    regex_iterator() = default;

    /*!
     * \brief Constructs a begin iterator over `[first, last)` and finds the first match.
     *        A constraining match flag (see \ref detail::real_honors) routes to the std backend,
     *        which carries the flags through the wrapped `std::regex_iterator`.
     * \param[in] first Start of the character sequence.
     * \param[in] last  End of the character sequence.
     * \param[in] re    The pattern; it must outlive this iterator.
     * \param[in] flags Match flags, defaulting to \c regex_constants::match_default.
     */
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

    /*!
     * \brief Copies the iteration position, but NOT the walker.
     *
     * The walker is an accelerator over \ref real_pos_, never the state itself, and that is what
     * makes this cheap where the obvious designs are not. Copying it would clone the VM scratch it
     * embeds; sharing it copy-on-write would clone on the first advance, which `operator++(int)`
     * performs on every call -- buying `++it` at the price of `it++`. Leaving the copy without one
     * costs it a single walker construction IF it ever advances, and nothing at all if it does not,
     * which is what a post-increment's discarded result actually does.
     * \param[in] other The iterator to copy.
     */
    regex_iterator(const regex_iterator& other)
      : begin_(other.begin_), end_(other.end_), re_(other.re_), flags_(other.flags_),
        real_path_(other.real_path_), real_pos_(other.real_pos_), std_it_(other.std_it_),
        match_(other.match_), at_end_(other.at_end_)
    {}

    /*!
     * \brief Copy-assigns the iteration position, dropping any walker this iterator owned.
     * \param[in] other The iterator to copy.
     * \return `*this`.
     */
    regex_iterator& operator=(const regex_iterator& other)
    {
      if (this != &other) {
        walker_.reset(); // rebuilt on the next advance, from real_pos_
        begin_     = other.begin_;
        end_       = other.end_;
        re_        = other.re_;
        flags_     = other.flags_;
        real_path_ = other.real_path_;
        real_pos_  = other.real_pos_;
        std_it_    = other.std_it_;
        match_     = other.match_;
        at_end_    = other.at_end_;
      }
      return *this;
    }

    regex_iterator(regex_iterator&&) noexcept = default; //!< Moves the walker along with the position.

    /*!
     * \brief Move-assigns, carrying the walker along with the position.
     * \return `*this`.
     */
    regex_iterator& operator=(regex_iterator&&) noexcept = default;
    ~regex_iterator()                                    = default; //!< Releases the walker, if any.

    /*!
     * \brief Constructing from a temporary regex would dangle (std::regex_iterator parity).
     */
    regex_iterator(BidirIt                          first,
                   BidirIt                          last,
                   const regex_type&&               re,
                   regex_constants::match_flag_type flags = regex_constants::match_default) = delete;

    /*!
     * \brief The current match.
     * \return A reference to it, valid until the next increment.
     */
    [[nodiscard]] reference operator*() const
    {
      return match_;
    }

    /*!
     * \brief The current match.
     * \return A pointer to it, valid until the next increment.
     */
    [[nodiscard]] pointer   operator->() const
    {
      return &match_;
    }

    /*!
     * \brief Advances to the next match, becoming the end sentinel when there is none.
     * \return `*this`.
     */
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

    /*!
     * \brief Advances to the next match, returning the previous position.
     * \return A copy of `*this` as it was before the increment.
     */
    regex_iterator operator++(int)
    {
      regex_iterator previous {*this};
      ++(*this);
      return previous;
    }

    /*!
     * \brief Equality. Two non-end iterators compare equal only for the same regex, sequence, flags
     *        and current match — not for a coincidental same position across different patterns.
     * \param[in] other The iterator to compare against.
     * \return Whether the two denote the same iteration position.
     */
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

    /*!
     * \brief Inequality, the negation of \ref operator==.
     * \param[in] other The iterator to compare against.
     * \return Whether the two denote different iteration positions.
     */
    [[nodiscard]] bool operator!=(const regex_iterator& other) const
    {
      return !(*this == other);
    }

  private:

    //! \brief The REAL walker, when this iterator owns one. ACCELERATION ONLY: \ref real_pos_ stays
    //!        the source of truth, so a null walker costs correctness nothing and only speed.
    using walker_type = real::basic_match_iterator<real::detail::dynamic_storage>;

    std::unique_ptr<walker_type>                walker_;                                      //!< Owned, never copied -- see the copy constructor.
    BidirIt                                     begin_     {};                                //!< Start of the sequence.
    BidirIt                                     end_       {};                                //!< End of the sequence.
    const regex_type*                           re_        {nullptr};                         //!< The pattern, borrowed.
    regex_constants::match_flag_type            flags_     {regex_constants::match_default};  //!< Match flags this iteration was built with.
    bool                                        real_path_ {false};                           //!< Whether the REAL engine drives the traversal rather than the std backend.
    std::size_t                                 real_pos_  {};                                //!< REAL path: byte offset the next region search starts at.
    std::optional<std::regex_iterator<BidirIt>> std_it_;                                      //!< std path: the wrapped iterator (engaged only off the REAL path).
    value_type                                  match_;                                       //!< The current match, refilled by each increment.
    bool                                        at_end_ {true};                               //!< Whether this is the end sentinel.

    /*!
     * \brief Advances the real path: one step of the walker, built on first use at \ref real_pos_.
     *
     * \note ONE walker serves the whole iteration, and that is the point rather than an allocation
     *       detail. A match iterator carries the VM state where every per-haystack decision lives --
     *       the Aho-Corasick density verdict, the inner-literal density gate, the DFA warmup -- so
     *       advancing by a fresh `search()` instead would re-derive all of them once per match, worst
     *       on exactly the families whose routing gate is sticky per haystack.
     *
     * \note The walker is held behind a pointer and never copied. `std::regex_iterator` requires
     *       copies to be independent and its `operator++(int)` returns one, so embedding the walker
     *       would clone the VM scratch -- which is what makes a match iterator an order of magnitude
     *       larger than this one -- on every post-increment, buying `++it` at the price of `it++`. A
     *       copy starts without a walker and builds one only if it advances; see the copy constructor.
     */
    void next_real()
    {
      const std::string_view sv     {std::to_address(begin_),
                                     static_cast<std::size_t>(std::distance(begin_, end_))};
      // A POSIX grammar on REAL drives the iteration with leftmost-longest bounds; the ECMAScript
      // default keeps leftmost-first.
      const real::regex&     engine {std::get<real::regex>(re_->engine())};
      if (walker_ == nullptr) {
        // First advance, or the first after a copy: build the walker at the current position. It is
        // self-contained (program view and text are values/views), so it outlives the range it came
        // from. `find_iter`'s empty-match rules never come into play here: this path is taken only
        // for a NON-NULLABLE pattern (see this file's header), which is also why repeated region
        // searches agreed with it before.
        auto range {re_->posix_longest() ? engine.find_iter_longest(sv, real_pos_)
                                         : engine.find_iter(sv, real_pos_)};
        walker_ = std::make_unique<walker_type>(range.begin());
      }
      else {
        ++(*walker_);
      }
      if (walker_->exhausted()) {
        at_end_ = true;
        return;
      }
      const auto& result {**walker_};
      match_.reset(begin_, end_);
      match_.fill_from_real(result);
      // Iteration: the prefix runs from the previous match end (== real_pos_ here), not the start.
      match_.rebase_prefix(begin_ + static_cast<difference_type>(real_pos_));
      real_pos_ = result.end(0); // non-nullable: end > start >= pos, so this always advances
      at_end_   = false;
    }

    /*!
     * \brief Syncs the std path from the wrapped std::regex_iterator.
     */
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

    /*!
     * \brief Constructs the end sentinel.
     */
    regex_token_iterator() = default;

    /*!
     * \brief Selects a single sub-match field (`0` = whole match, `N` = group N, `-1` = split).
     * \param[in] first    Start of the character sequence.
     * \param[in] last     End of the character sequence.
     * \param[in] re       The pattern; it must outlive this iterator.
     * \param[in] submatch The field to yield per match.
     * \param[in] flags    Match flags, defaulting to \c regex_constants::match_default.
     */
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

    /*!
     * \brief Selects a list of fields, cycled per match (e.g. `{1, 2}`, `{-1}`). The match flags are
     *        forwarded to the wrapped `regex_iterator`, so the nullable/honors routing is inherited.
     * \param[in] first      Start of the character sequence.
     * \param[in] last       End of the character sequence.
     * \param[in] re         The pattern; it must outlive this iterator.
     * \param[in] submatches The fields to cycle through; an empty list is treated as `{0}`.
     * \param[in] flags      Match flags, defaulting to \c regex_constants::match_default.
     */
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

    /*!
     * \brief Selects a list of fields from a braced list (e.g. `{-1}`).
     * \param[in] first      Start of the character sequence.
     * \param[in] last       End of the character sequence.
     * \param[in] re         The pattern; it must outlive this iterator.
     * \param[in] submatches The fields to cycle through.
     * \param[in] flags      Match flags, defaulting to \c regex_constants::match_default.
     */
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

    /*!
     * \brief Constructing from a temporary regex would dangle (std::regex_token_iterator parity).
     */
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

    /*!
     * \brief The current token.
     * \return A reference to it, valid until the next increment.
     */
    [[nodiscard]] reference operator*() const
    {
      return current_;
    }

    /*!
     * \brief The current token.
     * \return A pointer to it, valid until the next increment.
     */
    [[nodiscard]] pointer   operator->() const
    {
      return &current_;
    }

    /*!
     * \brief Advances to the next token, becoming the end sentinel when the fields are exhausted.
     * \return `*this`.
     */
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

    /*!
     * \brief Advances to the next token, returning the previous position.
     * \return A copy of `*this` as it was before the increment.
     */
    regex_token_iterator operator++(int)
    {
      regex_token_iterator previous {*this};
      ++(*this);
      return previous;
    }

    /*!
     * \brief Equality. Two non-end iterators compare equal only for the same underlying walk, field
     *        selectors, field index, suffix state and current token.
     * \param[in] other The iterator to compare against.
     * \return Whether the two denote the same iteration position.
     */
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

    /*!
     * \brief Inequality, the negation of \ref operator==.
     * \param[in] other The iterator to compare against.
     * \return Whether the two denote different iteration positions.
     */
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

    /*!
     * \brief Computes the current token from the current match and `subs_[n_]`.
     */
    void set_field()
    {
      current_ = (subs_[n_] == -1) ? position_->prefix() : (*position_)[subs_[n_]];
    }

    /*!
     * \brief Establishes the first token (or the whole-sequence token when there is no match).
     * \param[in] first Start of the character sequence.
     * \param[in] last  End of the character sequence.
     */
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

#endif // REAL_STD_REGEX_ITER_HPP
