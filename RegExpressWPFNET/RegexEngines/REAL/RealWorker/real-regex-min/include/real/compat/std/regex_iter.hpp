/*!
 * \file std/regex_iter.hpp
 * \brief std::regex compatibility, part 3/3: `regex_iterator` and `regex_token_iterator`.
 */
#ifndef REAL_STD_REGEX_ITER_HPP
#define REAL_STD_REGEX_ITER_HPP

// Internal — do not include directly; the entry point is <real/compat/std/regex.hpp>.

#include "regex_match.hpp"

#include <iterator>
#include <memory>

/*!
 * \brief Drop-in replacements for `<regex>`, backed by REAL's linear-time engine where it can serve the
 *        pattern and by `std::regex` otherwise, never by a silent divergence.
 */
namespace real::compat {
  namespace detail {

    /*!
     * \brief What a `regex_iterator` over contiguous storage keeps of its range for REAL: nothing, REAL
     *        searches the range where it lies.
     */
    struct in_place_subject
    {};

    /*!
     * \brief What a `regex_iterator` over a non-contiguous range keeps for REAL: one copy of the range, shared by
     *        the iterator's copies, and the cursor mapping its offsets back. On the heap because the walker views
     *        it: a short string held by value would move its characters with the iterator.
     * \tparam BidirIt The caller's iterator.
     */
    template <typename BidirIt>
    struct copied_subject
    {
      std::shared_ptr<const std::string> text; //!< The copy of `[begin_, end_)`.
      offset_cursor<BidirIt>             at;   //!< At the last offset mapped; never past the next match.
    };

    //! \brief The subject a `regex_iterator` over \p BidirIt keeps for REAL.
    template <typename BidirIt>
    using subject_for = std::conditional_t<std::contiguous_iterator<BidirIt>, in_place_subject, copied_subject<BidirIt>>;
  } // namespace detail

  /*!
   * \brief Iterates the non-overlapping matches of a pattern in a sequence (`std::regex_iterator`).
   *
   * REAL drives the traversal when `basic_regex::uses_real_traversal` holds and `detail::real_honors` accepts
   * the flags; otherwise this wraps a `std::regex_iterator`. Default-constructed, it is the end sentinel.
   * \tparam BidirIt A bidirectional iterator into the searched sequence.
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
     * \param[in] first Start of the character sequence.
     * \param[in] last  End of the character sequence.
     * \param[in] re    The pattern; it must outlive this iterator.
     * \param[in] flags Match flags; a constraining one routes the iteration to std.
     */
    regex_iterator(BidirIt                          first,
                   BidirIt                          last,
                   const regex_type&                re,
                   regex_constants::match_flag_type flags = regex_constants::match_default)
      : begin_(first), end_(last), re_(&re), flags_(flags)
    {
      // Compiled out for other CharT: next_real is char-only.
      if constexpr (detail::real_eligible<CharT, Traits>) {
        if (re.uses_real_traversal() && detail::real_honors(flags)) {
          real_path_ = true;
          if constexpr (!std::contiguous_iterator<BidirIt>) {
            subject_ = {.text = std::make_shared<const std::string>(first, last), .at = {first, 0}};
          }
          next_real();
          return;
        }
      }
      detail::std_call([&] { std_it_.emplace(first, last, re.std_engine(), detail::to_std_match(flags)); });
      sync_std();
    }

    /*!
     * \brief Copies the iteration position but not the walker (an accelerator over \ref real_pos_). Cloning its
     *        VM scratch, or sharing it copy-on-write, would cost every `it++`; a copy builds its own walker only
     *        if it advances.
     * \param[in] other The iterator to copy.
     */
    regex_iterator(const regex_iterator& other)
      : subject_(other.subject_), begin_(other.begin_), end_(other.end_), re_(other.re_), flags_(other.flags_),
        real_path_(other.real_path_), real_pos_(other.real_pos_), matched_(other.matched_),
        last_empty_(other.last_empty_), std_it_(other.std_it_), match_(other.match_), at_end_(other.at_end_)
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
        subject_    = other.subject_;
        begin_      = other.begin_;
        end_        = other.end_;
        re_         = other.re_;
        flags_      = other.flags_;
        real_path_  = other.real_path_;
        real_pos_   = other.real_pos_;
        matched_    = other.matched_;
        last_empty_ = other.last_empty_;
        std_it_     = other.std_it_;
        match_      = other.match_;
        at_end_     = other.at_end_;
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
      // Compiled out for other CharT, where real_path_ is always false.
      if constexpr (detail::real_eligible<CharT, Traits>) {
        if (real_path_) {
          next_real();
          return *this;
        }
      }
      detail::std_call([&] { ++(*std_it_); });
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
      return same_position(other);
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

    /*!
     * \brief \ref operator== between two live iterators.
     *
     * Out of line so that `operator==` itself inlines: `it != std::sregex_iterator()` builds and compares an end
     * sentinel at every step, and with this comparison inline clang kept the whole operator out of line.
     * \param[in] other A live iterator.
     * \return Whether both denote the same match of the same iteration.
     */
    REAL_NOINLINE bool same_position(const regex_iterator& other) const
    {
      return re_ == other.re_ && begin_ == other.begin_ && end_ == other.end_ && flags_ == other.flags_
             && match_.position(0) == other.match_.position(0) && match_.length(0) == other.match_.length(0);
    }

    //! \brief The REAL walker: an accelerator only, \ref real_pos_ stays the source of truth.
    using walker_type = real::basic_match_iterator<real::detail::dynamic_storage>;

    [[no_unique_address]] detail::subject_for<BidirIt> subject_;                                     //!< Non-contiguous range: the copy REAL walks; declared before the walker, which views it.
    std::unique_ptr<walker_type>                       walker_;                                      //!< Owned, never copied -- see the copy constructor.
    BidirIt                                            begin_      {};                               //!< Start of the sequence.
    BidirIt                                            end_        {};                               //!< End of the sequence.
    const regex_type       *                           re_         {nullptr};                        //!< The pattern, borrowed.
    regex_constants::match_flag_type                   flags_      {regex_constants::match_default}; //!< Match flags this iteration was built with.
    bool                                               real_path_  {false};                          //!< Whether the REAL engine drives the traversal rather than the std backend.
    std::size_t                                        real_pos_   {};                               //!< REAL path: byte offset the next region search starts at.
    std::size_t                                        matched_    {};                               //!< REAL path: matches made so far, saturating at 2 (only the first is retried without context).
    bool                                               last_empty_ {};                               //!< REAL path: the current match is empty.
    std::optional<std::regex_iterator<BidirIt>>        std_it_;                                      //!< std path: the wrapped iterator (engaged only off the REAL path).
    value_type                                         match_;                                       //!< The current match, refilled by each increment.
    bool                                               at_end_ {true};                               //!< Whether this is the end sentinel.

    /*!
     * \brief Advances the real path: one step of the walker, built on first use at \ref real_pos_.
     *
     * \note One walker serves the whole iteration: it carries the per-haystack routing state (prefilter
     *       density verdicts, DFA warmup), which a fresh `search()` per match would re-derive every time.
     *       It is never copied; see the copy constructor.
     */
    void next_real()
    {
      std::string_view       sv;
      if constexpr (std::contiguous_iterator<BidirIt>) {
        sv = {std::to_address(begin_), static_cast<std::size_t>(std::distance(begin_, end_))};
      }
      else {
        sv = *subject_.text;
      }
      const real::regex&     engine {std::get<real::regex>(re_->engine())};
      if (matched_ == 1 && last_empty_) {
        // The standard's retry after a first match that came out empty (detail::nonempty_at_without_context).
        const std::size_t at {real_pos_};
        walker_.reset();
        if (const auto retry {detail::nonempty_at_without_context(engine, sv, at)}; retry.has_value()) {
          emit(detail::offset_match<real::regex::result_type> {*retry, at});
          return;
        }
        if (at >= sv.size()) {
          at_end_ = true;
          return;
        }
        // No non-empty match there: search on past it, with its context. real_pos_ stays at the empty
        // match's end, where the next match's prefix starts.
        auto range {re_->posix_longest() ? engine.find_iter_longest(sv, at + 1) : engine.find_iter(sv, at + 1)};
        walker_ = std::make_unique<walker_type>(range.begin());
        if (walker_->exhausted()) {
          at_end_ = true;
          return;
        }
        emit(**walker_);
        return;
      }
      if (walker_ == nullptr) {
        // First advance, or the first after a copy. The walker is self-contained: it outlives `range`.
        auto range {re_->posix_longest() ? engine.find_iter_longest(sv, real_pos_)
                                         : engine.find_iter(sv, real_pos_)};
        walker_ = std::make_unique<walker_type>(range.begin());
        if (last_empty_ && !walker_->exhausted()) {
          ++(*walker_); // a copy resuming after an empty match: the walker's first match is that one again
        }
      }
      else {
        ++(*walker_);
      }
      if (walker_->exhausted()) {
        at_end_ = true;
        return;
      }
      emit(**walker_);
    }

    /*!
     * \brief Makes \p result the current match, and the place the next one is searched from.
     * \tparam RealMatch A match with the subject's offsets.
     * \param[in] result The match.
     */
    template <typename RealMatch>
    void emit(const RealMatch& result)
    {
      match_.reset(begin_, end_);
      // Iteration: the prefix runs from the previous match end (== real_pos_ here), not the start.
      if constexpr (std::contiguous_iterator<BidirIt>) {
        match_.fill_from_real(result);
        match_.rebase_prefix(begin_ + static_cast<difference_type>(real_pos_));
      }
      else {
        // real_pos_ is the smallest offset this match maps: the cursor walks forward from it, once.
        const BidirIt prefix_first {subject_.at.to(real_pos_)};
        match_.fill_from_real_at(result, subject_.at);
        match_.rebase_prefix(prefix_first);
      }
      real_pos_   = result.end(0);
      last_empty_ = result.start(0) == result.end(0);
      matched_    = matched_ < 2 ? matched_ + 1 : matched_;
      at_end_     = false;
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
   * \brief Enumerates selected sub-matches, or the text between matches (`std::regex_token_iterator`).
   *
   * Wraps `regex_iterator`, whose routing it inherits. Per match it yields the requested fields in order:
   * `N >= 0` is group `N` (unmatched when the group took no part), `-1` the match's prefix, the text since
   * the previous match. As in std, a final `-1` field yields the last suffix only if it is non-empty, while
   * an empty field between adjacent matches is produced; with no match at all, `-1` yields the whole
   * sequence as one token.
   * \tparam BidirIt A bidirectional iterator into the searched sequence.
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
     * \brief Selects a list of fields, cycled per match (e.g. `{1, 2}`, `{-1}`).
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
     * \brief Selects the fields of a C array (e.g. `const int fields[] {1, 2}`).
     * \tparam N The number of fields.
     * \param[in] first      Start of the character sequence.
     * \param[in] last       End of the character sequence.
     * \param[in] re         The pattern; it must outlive this iterator.
     * \param[in] submatches The fields to cycle through.
     * \param[in] flags      Match flags, defaulting to \c regex_constants::match_default.
     */
    template <std::size_t N>
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&                re,
                         const int (&submatches)[N],
                         regex_constants::match_flag_type flags = regex_constants::match_default)
      : regex_token_iterator(first,
                             last,
                             re,
                             std::vector<int>(std::begin(submatches), std::end(submatches)),
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
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&&               re,
                         std::initializer_list<int>       submatches,
                         regex_constants::match_flag_type flags = regex_constants::match_default) = delete; //!< \overload
    template <std::size_t N>
    regex_token_iterator(BidirIt                          first,
                         BidirIt                          last,
                         const regex_type&&               re,
                         const int (&submatches)[N],
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
      if (n_ + 1 < subs_.size()) {
        ++n_; // more fields for the same match
        set_field();
      }
      else {
        n_ = 0;
        // The suffix is all of this match that the step can still need, and only with a -1 selector: keeping
        // it, not a copy of the whole iterator (its match vector and shared walk), spares an allocation a step.
        const value_type suffix {has_m1_ ? position_->suffix() : value_type {}};
        ++position_;
        if (position_ != regex_iterator<BidirIt, CharT, Traits> {}) {
          set_field();           // first field of the next match
        }
        else if (has_m1_ && suffix.length() != 0) {
          current_     = suffix; // trailing split field, only when non-empty
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
      else if (has_m1_) {    // no match at all: the whole sequence is one split token, then end
        at_end_      = false;
        suffix_mode_ = true; // terminal: exactly one token, no field cycling
        // Matched even when empty, unlike an empty field between matches. Any `-1` in the list counts
        // ([re.tokiter.cnstr]); libc++ checks only the first field and drops this token for `{1, -1}`.
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
