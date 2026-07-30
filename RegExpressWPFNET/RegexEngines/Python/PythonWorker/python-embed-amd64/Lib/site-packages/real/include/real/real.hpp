/*!
 * \file real.hpp
 * \brief The public API: `real::regex`, `real::static_regex` and results.
 *
 * Header-only, C++20, constexpr from end to end. Include this one header.
 */
#ifndef REAL_REAL_HPP
#define REAL_REAL_HPP

#include "real/version.hpp"

#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "real/engine/pike.hpp"
#include "real/core/program.hpp"
#include "real/storage.hpp"
#include "real/unicode/utf8.hpp"

namespace real {

  /*!
   * \brief The result of a match attempt: success, spans and captures.
   *
   * Group views point into the searched text, which must outlive the result —
   * the rvalue `std::string` overloads on the regex are deleted to catch the
   * common dangling mistake at compile time. Named-group lookups reference the
   * regex's name table, so the regex must outlive the result too.
   *
   * \tparam SlotStorage The capture-slot container (vector- or static-backed),
   *         supplied by the storage policy.
   */
  template <typename SlotStorage>
  class basic_match_result
  {
  public:

    /*!
     * \brief Constructs an empty (non-matched) result.
     */
    constexpr basic_match_result() = default;

    /*!
     * \brief Constructs a result from raw slots (used internally by the engine).
     * \param[in] text    The searched text (borrowed; must outlive the result).
     * \param[in] slots   Flattened capture slots (byte offsets, npos for unset).
     * \param[in] matched Whether a match occurred.
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] names   The regex's named-group table (borrowed).
     */
    constexpr basic_match_result(std::string_view                     text,
                                 SlotStorage                          slots,
                                 bool                                 matched,
                                 std::string_view                     pattern,
                                 std::span<const detail::named_group> names)
      : text_(text),
        slots_(std::move(slots)),
        matched_(matched),
        pattern_(pattern),
        names_(names)
    {}

    /*!
     * \brief Engine-internal: re-run the search into this result's OWN slot buffer, reusing its
     *        capacity. Not part of the public API.
     *
     * The match iterator holds one result and refreshes it in place each step. `vm.run` fills the slots
     * via `assign`, which reuses the existing capacity, so a match-dense iteration allocates the slot
     * vector once instead of once per match — the measured per-match cost (a fresh allocation is ~5x a
     * reused one). A user-held copy of a previous match stays independent: copying a result deep-copies
     * its slots, so refilling this one never disturbs it.
     *
     * \tparam Cascade Select the OPT-C memchr-cascade class-run variant (chosen once per walk).
     * \tparam Vm      The Pike VM type (kept a template to avoid a header cycle).
     * \param[in] vm      The VM to run.
     * \param[in] text    The searched text (borrowed).
     * \param[in] pos     Start offset for the search.
     * \param[in] mode    The run mode.
     * \param[in] forbid  The empty-match forbid-until offset.
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] names   The regex's named-group table (borrowed).
     * \return Whether a match occurred.
     */
    template <bool Cascade, typename Vm>
    constexpr bool engine_refill(Vm&                                  vm,
                                 std::string_view                     text,
                                 std::size_t                          pos,
                                 detail::run_mode                     mode,
                                 std::size_t                          forbid,
                                 std::string_view                     pattern,
                                 std::span<const detail::named_group> names)
    {
      const bool ok {vm.template run<Cascade>(text, pos, mode, slots_, forbid)};
      text_    = text;
      matched_ = ok;
      pattern_ = pattern;
      names_   = names;
      return ok;
    }

    //! \brief Binds the invariant context (subject, pattern, named groups) once. For an iterator that refills
    //!        the same result many times, these never change within a walk — set them here, not per match.
    constexpr void bind_context(std::string_view                     text,
                                std::string_view                     pattern,
                                std::span<const detail::named_group> names)
    {
      text_    = text;
      pattern_ = pattern;
      names_   = names;
    }

    //! \brief Per-match refill for an iterator whose context is already bound via \ref bind_context runs the
    //!        VM and records only the outcome (the invariant fields are already set), the find_iter hot path.
    template <bool Cascade, typename Vm>
    constexpr bool engine_refill_hot(Vm&              vm,
                                     std::string_view text,
                                     std::size_t      pos,
                                     detail::run_mode mode,
                                     std::size_t      forbid,
                                     match_semantics  sem = match_semantics::first)
    {
      matched_ = vm.template run<Cascade>(text, pos, mode, slots_, forbid, sem);
      return matched_;
    }

    //! \brief P3c cold path for TrailingLA walks only (never referenced from pure walks).
    template <bool Cascade, typename Vm>
    constexpr bool engine_refill_trailing_la(Vm&              vm,
                                             std::string_view text,
                                             std::size_t      pos)
    {
      detail::prof::tick_route(detail::prof::route::trailing_la);
      matched_ = vm.template run_class_loop_trailing_la<Cascade>(text, pos, detail::run_mode::search, slots_);
      return matched_;
    }

    /*!
     * \brief Returns `true` if the attempt matched.
     */
    [[nodiscard]] constexpr bool matched() const
    {
      return matched_;
    }

    /*!
     * \brief Returns `true` if the attempt matched (explicit bool conversion).
     */
    constexpr explicit operator bool() const {
      return matched_;
    }

    /*!
     * \brief Returns the number of groups, including group 0 (the whole match).
     */
    [[nodiscard]] constexpr std::size_t size() const
    {
      return slots_.size() / 2;
    }

    /*!
     * \brief Start byte offset of a group.
     * \param[in] group Group number (0 = whole match).
     * \return The offset, or \ref real::npos if the group did not participate.
     */
    [[nodiscard]] constexpr std::size_t start(std::size_t group = 0) const
    {
      return matched_ && group < size() ? slots_[2 * group] : npos;
    }

    /*!
     * \brief End byte offset (exclusive) of a group.
     * \param[in] group Group number (0 = whole match).
     * \return The offset, or \ref real::npos if the group did not participate.
     */
    [[nodiscard]] constexpr std::size_t end(std::size_t group = 0) const
    {
      return matched_ && group < size() ? slots_[(2 * group) + 1] : npos;
    }

    /*!
     * \brief View of a group's matched text.
     * \param[in] group Group number (0 = whole match).
     * \return A view into the searched text, empty if the group is unset.
     */
    [[nodiscard]] constexpr std::string_view operator[](std::size_t group) const
    {
      const std::size_t s {start(group)};
      return s == npos ? std::string_view {} : text_.substr(s, end(group) - s);
    }

    /*!
     * \brief Resolves a group name to its number.
     * \param[in] name The group name.
     * \return The group number, or \ref real::npos if unknown.
     */
    [[nodiscard]] constexpr std::size_t group_index(std::string_view name) const
    {
      for (const detail::named_group& named_group : names_) {
        const auto begin  {static_cast<std::size_t>(named_group.begin)};
        const auto length {static_cast<std::size_t>(named_group.end - named_group.begin)};
        if (pattern_.substr(begin, length) == name) {
          return static_cast<std::size_t>(named_group.group);
        }
      }
      return npos;
    }

    /*!
     * \brief Returns its start offset, or npos if unknown.
     * \param[in] name Group name.
     * \return Its start offset, or npos if unknown.
     */
    [[nodiscard]] constexpr std::size_t start(std::string_view name) const
    {
      const std::size_t g {group_index(name)};
      return g == npos ? npos : start(g);
    }

    /*!
     * \brief Returns its end offset, or npos if unknown.
     * \param[in] name Group name.
     * \return Its end offset, or npos if unknown.
     */
    [[nodiscard]] constexpr std::size_t end(std::string_view name) const
    {
      const std::size_t g {group_index(name)};
      return g == npos ? npos : end(g);
    }

    /*!
     * \brief Returns its matched text, empty if unknown/unset.
     * \param[in] name Group name.
     * \return Its matched text, empty if unknown/unset.
     */
    [[nodiscard]] constexpr std::string_view operator[](std::string_view name) const
    {
      const std::size_t g {group_index(name)};
      return g == npos ? std::string_view {} : (*this)[g];
    }

    /*!
     * \brief The capture slots as one flat `[start0, end0, start1, end1, …]` view — the raw storage,
     *        for a caller that copies every group out in one pass.
     *
     * Same layout and length as the C ABI's `spans` buffer, so the shim fills it by reading straight
     * across instead of calling \ref start / \ref end per group — each of those re-tests
     * `matched()` and the group bound, which a caller that has *already* established a match (the
     * ABI checks the return code first) pays for nothing. Measured on the C shim: `\b\w+\b`
     * −20.7 % per match, `[a-z]+` −7.1 %, multi-group patterns neutral.
     *
     * This is the raw storage, so it is only meaningful on a matched result — an unmatched one
     * carries whatever the last fill left (\ref start / \ref end are the safe accessors, and return
     * \ref real::npos there). Copy it **pairwise** (`spans[2g]`, `spans[2g+1]`) rather than with one
     * `memcpy`: the length is a runtime value, so `memcpy` compiles to a real libc call that costs
     * more than the stores for the 1–2 slot shapes that dominate.
     *
     * \return A view of `2 * size()` slot values; empty when there are no slots.
     */
    [[nodiscard]] constexpr std::span<const std::size_t> spans() const noexcept
    {
      if (slots_.empty()) {
        return {};
      }
      return std::span<const std::size_t> {&slots_[0], slots_.size()};
    }

  private:

    std::string_view                     text_;       //!< The searched text.
    SlotStorage                          slots_;      //!< Flattened capture slots.
    bool                                 matched_ {}; //!< Whether a match occurred.
    std::string_view                     pattern_;    //!< Pattern text (for named lookups).
    std::span<const detail::named_group> names_;      //!< Borrowed named-group table.
  };

  /*!
   * \brief The result type of the default, runtime-compiled `real::regex`.
   */
  using match_result = basic_match_result<std::vector<std::size_t>>;

  /*!
   * \brief Forward iterator over the non-overlapping matches in a text.
   *
   * Follows Python's empty-match rules: an empty match is yielded (even right
   * after a non-empty one), then the scan advances by one codepoint. The regex
   * and the text must outlive the iterator. Obtained from \ref basic_match_range.
   *
   * \tparam Storage    The regex's storage policy (selects the result/scratch types).
   * \tparam TrailingLA When `true`, this walk is the P3c trailing-LA class+ path only
   *                    (once-per-walk choice, like \ref cascade_). Pure walks use
   *                    `TrailingLA = false` so their `advance` has zero LA code — a per-step
   *                    runtime branch on trailing_lookaround regressed pure `[a-z]+` ~16 % on x86.
   */
  template <typename Storage, bool TrailingLA = false>
  class basic_match_iterator
  {
  public:

    using value_type        = basic_match_result<typename Storage::slot_storage>; //!< Yielded match type.
    using difference_type   = std::ptrdiff_t;                                     //!< Iterator traits.
    using reference         = const value_type&;                                  //!< Dereference type.
    using pointer           = const value_type*;                                  //!< Arrow type.
    using iterator_category = std::forward_iterator_tag;                          //!< Multipass: copies are independent.

    /*!
     * \brief Constructs the end sentinel.
     */
    constexpr basic_match_iterator() = default;

    /*!
     * \brief Constructs a begin iterator and finds the first match.
     * \param[in] prog    The compiled program to run.
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] text    The text to iterate over (borrowed).
     * \param[in] start   Byte offset to begin iterating from (0 = the whole text).
     * \param[in] sem     Match semantics: leftmost-first (default) or the experimental leftmost-longest.
     */
    constexpr basic_match_iterator(detail::program_view prog,
                                   std::string_view     pattern,
                                   std::string_view     text,
                                   std::size_t          start = 0,
                                   match_semantics      sem   = match_semantics::first)
      : prog_(prog),
        pattern_(pattern),
        text_(text),
        pos_(start),
        done_(false),
        // decided ONCE per walk, never per match. Longest forces the non-cascade variant: like every other
        // fast path, the memchr-cascade class-run is guarded to first mode (the PROTO-kLongest lesson), so a
        // longest walk must run the general loop.
        cascade_(sem == match_semantics::first && prog.hints.stop_set_size >= 1),
        sem_(sem)
    {
      current_.bind_context(text_, pattern_, prog_.names); // invariant across the walk — set once, not per match
      advance();
    }

    /*!
     * \brief Returns the current match.
     */
    [[nodiscard]] constexpr const value_type& operator*() const
    {
      return current_;
    }

    /*!
     * \brief Returns pointer to the current match.
     */
    [[nodiscard]] constexpr const value_type* operator->() const
    {
      return &current_;
    }

    /*!
     * \brief Advances to the next match.
     * \return *this.
     */
    constexpr basic_match_iterator& operator++()
    {
      advance();
      return *this;
    }

    /*!
     * \brief Advances to the next match (post-increment).
     * \return A copy of the iterator at its pre-increment position.
     */
    constexpr basic_match_iterator operator++(int)
    {
      basic_match_iterator previous {*this};
      advance();
      return previous;
    }

    /*!
     * \brief Returns `true` if both denote the same position/end.
     * \param[in] other Another iterator.
     * \return `true` if both denote the same position/end.
     */
    [[nodiscard]] constexpr bool operator==(const basic_match_iterator& other) const
    {
      return done_ == other.done_ && (done_ || pos_ == other.pos_);
    }

  private:

    detail::program_view         prog_;                                        //!< The program being run.
    std::string_view             pattern_;                                     //!< Pattern text (named lookups).
    std::string_view             text_;                                        //!< The text being scanned.
    std::size_t                  pos_                {};                       //!< Current scan offset.
    std::size_t                  forbid_empty_until_ {};                       //!< Empty-match guard (see pike.hpp).
    bool                         done_               {true};                   //!< True once exhausted.
    bool                         cascade_            {};                       //!< OPT-C: chosen once — run the memchr-cascade class-run variant for this whole walk.
    match_semantics              sem_                {match_semantics::first}; //!< leftmost-first (default) or longest (find_iter_longest).
    value_type                   current_;                                     //!< The current match.
    typename Storage::state_type state_;                                       //!< VM scratch, reused across the walk.

    /*!
     * \brief Finds the next match, applying the empty-match advance rules.
     *
     * \c TrailingLA is fixed for the whole walk (constructor / range). Pure walks
     * (`TrailingLA = false`) contain zero trailing-LA symbols — required for x86
     * class-loop codegen (see pattern_hints::trailing_lookaround).
     */
    constexpr void advance()
    {
      if (done_ || pos_ > text_.size()) {
        done_ = true;
        return;
      }
      // `state_` is this iterator's own member and `prog_` is fixed for the walk, so the state never
      // meets a second program: the VM can drop its per-`run()` program-identity compare.
      detail::pike_vm<typename Storage::state_type, true> vm(prog_, state_);
      bool                                                ok {};
      if constexpr (TrailingLA) {
        // P3c cold path only — this specialization is never mixed into pure walks.
        ok = cascade_ ? current_.template engine_refill_trailing_la<true>(vm, text_, pos_)
                      : current_.template engine_refill_trailing_la<false>(vm, text_, pos_);
      }
      else {
        // Pure walk — pre-P3c shape. Cascade chosen once; both arms are the same hot family.
        ok = cascade_ ? current_.template engine_refill_hot<true>(vm, text_, pos_, detail::run_mode::search,
                                                                  forbid_empty_until_, sem_)
                      : current_.template engine_refill_hot<false>(vm, text_, pos_, detail::run_mode::search,
                                                                   forbid_empty_until_, sem_);
      }
      if (!ok) {
        done_ = true;
        return;
      }
      const std::size_t start {current_.start(0)};
      const std::size_t end   {current_.end(0)};
      pos_ = end;
      if (end == start) {
        // CPython 3.7+: after an empty match, the next match may start at the
        // same position only if it is non-empty; another empty match there is
        // skipped. Forbid empty matches up to the next character boundary (a
        // codepoint, or one raw byte in binary mode) so the skip stays aligned.
        forbid_empty_until_ = end >= text_.size()
                              ? text_.size() + 1
                              : end + (prog_.byte_mode ? 1 : detail::codepoint_advance(text_, end));
      }
      else {
        forbid_empty_until_ = 0; // non-empty match: no restriction next time
      }
    }
  };

  /*!
   * \brief A range of matches, returned by `find_iter()` and usable in range-for.
   * \tparam Storage    The regex's storage policy.
   * \tparam TrailingLA Once-per-walk P3c path (see \ref basic_match_iterator).
   */
  template <typename Storage, bool TrailingLA = false>
  class basic_match_range
  {
  public:

    /*!
     * \brief Binds the range to a program and text.
     * \param[in] prog    The compiled program.
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] text    The text to iterate over (borrowed).
     * \param[in] start   Byte offset to begin iterating from (0 = the whole text).
     * \param[in] sem     Match semantics: leftmost-first (default) or the experimental leftmost-longest.
     */
    constexpr basic_match_range(detail::program_view prog,
                                std::string_view     pattern,
                                std::string_view     text,
                                std::size_t          start = 0,
                                match_semantics      sem   = match_semantics::first)
      : prog_(prog),
        pattern_(pattern),
        text_(text),
        start_(start),
        sem_(sem)
    {}

    /*!
     * \brief Returns an iterator to the first match.
     */
    [[nodiscard]] constexpr basic_match_iterator<Storage, TrailingLA> begin() const
    {
      return {prog_, pattern_, text_, start_, sem_};
    }

    /*!
     * \brief Returns the end sentinel.
     */
    [[nodiscard]] constexpr basic_match_iterator<Storage, TrailingLA> end() const
    {
      return {};
    }

  private:

    detail::program_view prog_;                           //!< The program being run.
    std::string_view     pattern_;                        //!< Pattern text (named lookups).
    std::string_view     text_;                           //!< The text to iterate.
    std::size_t          start_ {};                       //!< Byte offset to begin iterating from (region support).
    match_semantics      sem_   {match_semantics::first}; //!< leftmost-first (default) or longest.
  };

  /*!
   * \brief A compiled regular expression, parameterized on its storage policy.
   *
   * `Storage` owns the program; matching allocates only per-run scratch — and
   * nothing at all when the storage is compile-time. Use the \ref real::regex
   * and \ref real::static_regex aliases rather than this template directly.
   *
   * \tparam Storage \ref real::detail::dynamic_storage or
   *         \ref real::detail::static_storage.
   */
  template <typename Storage>
  class basic_regex
  {
  public:

    using result_type = basic_match_result<typename Storage::slot_storage>; //!< This regex's match-result type.

    /*!
     * \brief Compiles \p pattern at run time (the `real::regex` constructor).
     * \param[in] pattern      The pattern text.
     * \param[in] compile_flags Optional flags (merged with a leading (?ims)).
     * \throws real::regex_error on an invalid or over-limit pattern.
     */
    constexpr explicit basic_regex(std::string_view pattern,
                                   flags            compile_flags = flags::none)
    requires(!Storage::is_compile_time)
      : program_(Storage::compile(pattern, compile_flags))
    {}

    /*!
     * \brief Default constructor for the stateless compile-time storage (static_regex).
     */
    constexpr basic_regex()
    requires(Storage::is_compile_time)
    = default;

    /*!
     * \brief Match anchored at the start of \p text (Python `re.match`).
     * \param[in] text The subject text (must outlive the result).
     * \return The match result (test with `matched()` / `operator` bool).
     */
    [[nodiscard]] constexpr result_type match(std::string_view text) const
    {
      return run(text, detail::run_mode::prefix);
    }

    /*!
     * \brief Match the entire \p text (Python `re.fullmatch`).
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]] constexpr result_type fullmatch(std::string_view text) const
    {
      return run(text, detail::run_mode::full);
    }

    /*!
     * \brief Leftmost match anywhere in \p text (Python `re.search`).
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]] constexpr result_type search(std::string_view text) const
    {
      return run(text, detail::run_mode::search);
    }

    /*!
     * \brief Region-aware `match`: anchored at \p pos within `text[0:endpos]` (Python
     *        `re.match` with `pos` / `endpos`). Byte offsets; \p pos is not a slice (see
     *        \ref run — `\A` fails at `pos > 0`); \p endpos defaults to the end of \p text.
     */
    [[nodiscard]] constexpr result_type match(std::string_view text,
                                              std::size_t      pos,
                                              std::size_t      endpos = npos) const
    {
      return run(text, pos, endpos, detail::run_mode::prefix);
    }

    /*!
     * \brief Region-aware `fullmatch`: the whole region `[pos, endpos)` must match.
     */
    [[nodiscard]] constexpr result_type fullmatch(std::string_view text,
                                                  std::size_t      pos,
                                                  std::size_t      endpos = npos) const
    {
      return run(text, pos, endpos, detail::run_mode::full);
    }

    /*!
     * \brief Region-aware `search`: leftmost match within `[pos, endpos)`.
     */
    [[nodiscard]] constexpr result_type search(std::string_view text,
                                               std::size_t      pos,
                                               std::size_t      endpos = npos) const
    {
      return run(text, pos, endpos, detail::run_mode::search);
    }

    /*!
     * \brief `match` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type match(const char* text) const
    {
      return match(std::string_view(text));
    }

    /*!
     * \brief `fullmatch` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type fullmatch(const char* text) const
    {
      return fullmatch(std::string_view(text));
    }

    /*!
     * \brief `search` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type search(const char* text) const
    {
      return search(std::string_view(text));
    }

    /*!
     * \brief Lazy range over all non-overlapping matches (Python `re.finditer`).
     *
     * Only callable on an lvalue regex: calling on a temporary would dangle in a
     * C++20 range-for (the range initializer's temporaries die before the loop
     * body), so that misuse is a compile error (deleted rvalue overloads).
     *
     * \note Pure monomorphic walk (`TrailingLA = false`). The trailing-LA class+
     *       fast path is intentionally not taken here — the range's return type is
     *       fixed at compile time so pure `[a-z]+` codegen stays pristine. For the
     *       LA-fast route use \ref count_matches, \ref find_all, \ref search,
     *       \ref match, or \ref replace (once-per-walk dispatch). Correctness is
     *       identical; only throughput differs on eligible patterns.
     *
     * \param[in] text The subject text (must outlive the range).
     * \return A \ref basic_match_range usable directly in a range-for.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter(std::string_view text) const&
    {
      return {program_.view(), pattern(), text};
    }

    /*!
     * \brief `find_iter` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The range.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter(const char* text) const&
    {
      return find_iter(std::string_view(text));
    }

    /*!
     * \brief Region-aware `find_iter`: iterate matches within `[pos, endpos)` (Python
     *        `finditer` with `pos` / `endpos`). \p endpos truncates the subject to a view
     *        so iteration stops at it; \p pos is the start, not a slice (see \ref run).
     *        Byte offsets; \p endpos defaults to the end of \p text.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter(std::string_view text,
                                                                 std::size_t      pos,
                                                                 std::size_t      endpos = npos) const&
    {
      const std::size_t end {endpos < text.size() ? endpos : text.size()};
      return {program_.view(), pattern(), text.substr(0, end), pos};
    }

    /*!
     * \brief Experimental leftmost-**longest** `find_iter`: iterate matches with POSIX (leftmost-longest)
     *        bounds rather than the default leftmost-first — the iterator twin of \ref search_longest, sharing
     *        its prototype status (the `match_semantics` arc is not yet stable). Region semantics match \ref
     *        find_iter — \p endpos truncates the subject to a view, \p pos is the start (not a slice). Byte
     *        offsets; captures are the winning thread's, not POSIX submatch. Every fast path is bypassed.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter_longest(std::string_view text,
                                                                         std::size_t      pos    = 0,
                                                                         std::size_t      endpos = npos) const&
    {
      const std::size_t end {endpos < text.size() ? endpos : text.size()};
      return {program_.view(), pattern(), text.substr(0, end), pos, match_semantics::longest};
    }

    /*!
     * \brief Deleted: `find_iter_longest` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter_longest(std::string_view, std::size_t,
                                                               std::size_t) const&& = delete;

    /*!
     * \brief Deleted: `find_iter` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(std::string_view text) const&& = delete;
    /*!
     * \brief Deleted: `find_iter` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(const char* text) const&& = delete;
    /*!
     * \brief Deleted: region `find_iter` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(std::string_view text, std::size_t,
                                                       std::size_t = npos) const&& = delete;

    /*!
     * \brief Count non-overlapping matches without allocating result objects.
     *
     * Matching-only counter: once-per-walk dispatch (cascade_ model) — pure
     * monomorphic walk for ordinary patterns; TrailingLA monomorphic walk when
     * the trailing-lookaround class+ hint is set. Fair for multi-engine benches
     * (unlike \ref find_all, which builds a vector of Match objects and can
     * dominate high-cardinality scans). Prefer this over counting \ref find_iter
     * when measuring trailing-LA throughput — \ref find_iter stays pure by design.
     *
     * Region semantics match \ref find_iter — \p endpos truncates the subject to a
     * view; \p pos is the start offset (not a slice — \c \\A / \c ^ still see the
     * absolute position).
     *
     * \param[in] text   The subject text.
     * \param[in] pos    Byte offset to begin counting from (0 = start of text).
     * \param[in] endpos Exclusive end of the region; \ref npos = end of text.
     * \return The number of non-overlapping matches in the region.
     */
    [[nodiscard]] constexpr std::size_t count_matches(std::string_view text,
                                                      std::size_t      pos    = 0,
                                                      std::size_t      endpos = npos) const
    {
      const std::size_t      end    {endpos < text.size() ? endpos : text.size()};
      const std::string_view region {text.substr(0, end)};
      std::size_t            n      {};
      if constexpr (requires(typename Storage::state_type & st) {
        st.lookaround;
      }) {
        const auto& prog {program_.view()};
        if (prog.hints.trailing_lookaround >= 0
            && (std::is_constant_evaluated() || !detail::trailing_la_route_disabled())) {
          for (const result_type& match :
               basic_match_range<Storage, /*TrailingLA=*/ true> {prog, pattern(), region, pos}) {
            (void) match;
            ++n;
          }
          return n;
        }
      }
      for (const result_type& match : find_iter(text, pos, endpos)) {
        (void) match;
        ++n;
      }
      return n;
    }

    /*!
     * \brief All matches, eagerly (like Python `re.findall` but full results).
     *
     * Lvalue-only for the same reason as \ref find_iter (results reference this
     * regex's name table). Once-per-walk TrailingLA dispatch when eligible (same
     * route as \ref count_matches); vector construction cost is on top of the
     * scan — high match counts can dominate ns/B.
     *
     * \param[in] text The subject text (must outlive the results).
     * \return A vector of match results.
     */
    [[nodiscard]] constexpr std::vector<result_type> find_all(std::string_view text) const&
    {
      std::vector<result_type> result;
      // Same once-per-walk dispatch as count_matches (vector cost is on top of the scan).
      if constexpr (requires(typename Storage::state_type & st) {
        st.lookaround;
      }) {
        const auto& prog {program_.view()};
        if (prog.hints.trailing_lookaround >= 0
            && (std::is_constant_evaluated() || !detail::trailing_la_route_disabled())) {
          for (const result_type& match :
               basic_match_range<Storage, /*TrailingLA=*/ true> {prog, pattern(), text}) {
            result.push_back(match);
          }
          return result;
        }
      }
      for (const result_type& match : find_iter(text)) {
        result.push_back(match);
      }
      return result;
    }

    /*!
     * \brief `find_all` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The results.
     */
    [[nodiscard]] constexpr std::vector<result_type> find_all(const char* text) const&
    {
      return find_all(std::string_view(text));
    }

    /*!
     * \brief Deleted: `find_all` on a temporary regex would dangle.
     */
    [[nodiscard]] std::vector<result_type> find_all(std::string_view text) const&& = delete;
    /*!
     * \brief Deleted: `find_all` on a temporary regex would dangle.
     */
    [[nodiscard]] std::vector<result_type> find_all(const char* text) const&& = delete;

    /*!
     * \brief Replaces matches in \p text (Python `re.sub`).
     *
     * The \p replacement may reference groups: `$$` → '$', `$&` or `$0` →
     * whole match, `$1` …, and `${name}`. Returns an owning string, so a
     * temporary \p text is fine here.
     *
     * \param[in] text        The subject text.
     * \param[in] replacement The replacement template.
     * \param[in] max_count   Maximum replacements (0 = all).
     * \return The resulting string.
     * \throws real::regex_error on a malformed group reference in \p replacement.
     */
    [[nodiscard]] constexpr std::string replace(std::string_view text,
                                                std::string_view replacement,
                                                std::size_t      max_count = 0) const
    {
      std::string result;
      std::size_t last {};
      std::size_t done {};
      for (const result_type& match : find_iter(text)) {
        if (max_count != 0 && done == max_count) {
          break;
        }
        result.append(text.substr(last, match.start() - last));
        expand_replacement(result, match, replacement);
        last = match.end();
        ++done;
      }
      result.append(text.substr(last));
      return result;
    }

    /*!
     * \brief Splits \p text on matches (Python `re.split`).
     *
     * Each capturing group's text is inserted after its split (an unset group
     * yields an empty view, where Python would use `None`).
     *
     * \param[in] text       The subject text (must outlive the returned views).
     * \param[in] max_splits Maximum splits (0 = split everywhere).
     * \return The pieces, with captured separators interleaved.
     */
    [[nodiscard]] constexpr std::vector<std::string_view> split(std::string_view text,
                                                                std::size_t      max_splits = 0) const
    {
      std::vector<std::string_view> result;
      std::size_t                   last {};
      std::size_t                   done {};
      for (const result_type& match : find_iter(text)) {
        if (max_splits != 0 && done == max_splits) {
          break;
        }
        result.push_back(text.substr(last, match.start() - last));
        for (std::size_t group = 1; group < match.size(); ++group) {
          result.push_back(match[group]);
        }
        last = match.end();
        ++done;
      }
      result.push_back(text.substr(last));
      return result;
    }

    /*!
     * \brief `split` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \param[in] max_splits Max splits.
     * \return The pieces.
     */
    [[nodiscard]] constexpr std::vector<std::string_view> split(const char* text,
                                                                std::size_t max_splits = 0) const
    {
      return split(std::string_view(text), max_splits);
    }

    // Searched text must outlive the result: reject temporary std::string.
    [[nodiscard]] result_type                   match(const std::string&& text) const           = delete;                            //!< Deleted: temporary text would dangle.
    [[nodiscard]] result_type                   fullmatch(const std::string&& text) const       = delete;                            //!< Deleted: temporary text would dangle.
    [[nodiscard]] result_type                   search(const std::string&& text) const          = delete;                            //!< Deleted: temporary text would dangle.
    [[nodiscard]] result_type match(const std::string && text, std::size_t, std::size_t     = npos) const     = delete;              //!< Deleted: temporary text would dangle.
    [[nodiscard]] result_type fullmatch(const std::string && text, std::size_t, std::size_t = npos) const = delete;                  //!< Deleted: temporary text would dangle.
    [[nodiscard]] result_type search(const std::string && text, std::size_t, std::size_t    = npos) const    = delete;               //!< Deleted: temporary text would dangle.
    [[nodiscard]] basic_match_range<Storage>    find_iter(const std::string&& text) const&      = delete;                            //!< Deleted: temporary text would dangle.
    [[nodiscard]] basic_match_range<Storage>    find_iter(const std::string && text, std::size_t,
                                                          std::size_t                           = npos) const&             = delete; //!< Deleted: temporary text would dangle.
    [[nodiscard]] std::vector<result_type>      find_all(const std::string&& text) const&       = delete;                            //!< Deleted: temporary text would dangle.
    [[nodiscard]] std::vector<std::string_view> split(const std::string&& text,
                                                      std::size_t         max_splits = 0) const = delete;                            //!< Deleted: temporary text would dangle.

    /*!
     * \brief Returns the pattern text this regex was compiled from.
     */
    [[nodiscard]] constexpr std::string_view pattern() const
    {
      return program_.pattern();
    }

    /*!
     * \brief Returns the effective flags (constructor flags merged with a (?ims) prefix).
     */
    [[nodiscard]] constexpr flags compile_flags() const
    {
      return program_.compiled_flags();
    }

    /*!
     * \brief Returns the number of capturing groups (excluding group 0).
     */
    [[nodiscard]] constexpr std::size_t group_count() const
    {
      return (program_.view().slot_count / 2) - 1;
    }

    /*!
     * \brief The raw compiled program, for embedders (advanced).
     *
     * Lets an embedder (e.g. the Python binding) drive `detail::pike_vm` with
     * caller-owned reusable scratch. Valid as long as this regex is alive.
     *
     * \return A non-owning \ref detail::program_view.
     */
    [[nodiscard]] constexpr detail::program_view raw_program() const
    {
      return program_.view();
    }

    /*!
     * \brief Whether first-byte filtering is useful for this pattern.
     *
     * `true` iff every non-empty match provably begins with a byte from a known
     * set, so \ref may_start_with can reject positions. `false` when a zero-length
     * match is possible (or the set is empty) — then \ref may_start_with is `true`
     * for every byte and the filter buys nothing. This is the same set the engine's
     * own prefilter uses, exposed for embedders (e.g. a lexer's rule dispatch).
     *
     * \return `true` if the first-byte set is usable.
     */
    [[nodiscard]] constexpr bool has_first_byte_set() const noexcept
    {
      return raw_program().hints.first_bytes_valid;
    }

    /*!
     * \brief The single byte every non-empty match must begin with, if unique.
     *
     * \return The byte when the pattern has exactly one possible first byte (e.g.
     *         a plain literal like `if` / `def`); `std::nullopt` for zero or several.
     */
    [[nodiscard]] constexpr std::optional<unsigned char> unique_first_byte() const noexcept
    {
      const int first {raw_program().hints.single_first};
      return first < 0 ? std::nullopt : std::optional<unsigned char>(static_cast<unsigned char>(first));
    }

    /*!
     * \brief Whether a non-empty match can begin with \p byte (sound, conservative).
     *
     * A `false` result is a **guarantee**: no non-empty match of this pattern
     * begins with \p byte. A `true` result is a conservative superset — it does
     * not promise a match actually starts there. When first-byte filtering is not
     * usable (\ref has_first_byte_set is `false`, i.e. an empty match is possible),
     * this returns `true` for every byte, so it is safe to use on its own.
     *
     * \param[in] byte The candidate leading byte.
     * \return `false` only when \p byte can never start a non-empty match.
     */
    [[nodiscard]] constexpr bool may_start_with(unsigned char byte) const noexcept
    {
      const detail::program_view view {raw_program()};
      return !view.hints.first_bytes_valid || view.hints.first_bytes.test(byte);
    }

    /*!
     * \brief Resolves a group name to its number.
     * \param[in] name The group name.
     * \return The group number, or \ref real::npos if unknown.
     */
    [[nodiscard]] constexpr std::size_t group_index(std::string_view name) const
    {
      for (const detail::named_group& named_group : program_.view().names) {
        if (name_of(named_group) == name) {
          return static_cast<std::size_t>(named_group.group);
        }
      }
      return npos;
    }

    /*!
     * \brief All named groups as (name, number) pairs, in declaration order.
     * \return The list of named groups.
     */
    [[nodiscard]] constexpr std::vector<std::pair<std::string_view, std::size_t>>
    named_groups() const
    {
      std::vector<std::pair<std::string_view, std::size_t>> result;
      for (const detail::named_group& named_group : program_.view().names) {
        result.emplace_back(name_of(named_group), static_cast<std::size_t>(named_group.group));
      }
      return result;
    }

  private:

    Storage program_; //!< The storage policy holding the compiled program.

    /*!
     * \brief Returns its name, sliced from the pattern text.
     * \param[in] named_group A named group.
     * \return Its name, sliced from the pattern text.
     */
    [[nodiscard]] constexpr std::string_view name_of(const detail::named_group& named_group) const
    {
      return pattern().substr(static_cast<std::size_t>(named_group.begin),
                              static_cast<std::size_t>(named_group.end - named_group.begin));
    }

    /*!
     * \brief Appends \p replacement to \p out, substituting group references.
     *
     * Strict like Python: an invalid or out-of-range reference is an error.
     *
     * \param[in,out] out         The output string to append to.
     * \param[in]     match       The match supplying the captured groups.
     * \param[in]     replacement The replacement template (`$$`, `$&`, `$1`, `${name}`).
     * \throws real::regex_error on a malformed or out-of-range reference.
     */
    constexpr void expand_replacement(std::string&       out,
                                      const result_type& match,
                                      std::string_view   replacement) const
    {
      std::size_t i {};
      while (i < replacement.size()) {
        const char ch {replacement[i]};
        if (ch != '$') {
          out.push_back(ch);
          ++i;
          continue;
        }
        ++i;
        if (i >= replacement.size()) {
          throw regex_error("dangling $ in replacement", i - 1);
        }
        const char next_ch {replacement[i]};
        if (next_ch == '$') {
          out.push_back('$');
          ++i;
        }
        else if (next_ch == '&') {
          out.append(match[0]);
          ++i;
        }
        else if (next_ch >= '0' && next_ch <= '9') {
          std::size_t group {};
          while (i < replacement.size() && replacement[i] >= '0' &&
                 replacement[i] <= '9') {
            group = (group * 10) + static_cast<std::size_t>(replacement[i] - '0');
            ++i;
          }
          if (group >= match.size()) {
            throw regex_error("invalid group reference in replacement", i);
          }
          out.append(match[group]);
        }
        else if (next_ch == '{') {
          const std::size_t name_begin {i + 1};
          std::size_t       j          {name_begin};
          while (j < replacement.size() && replacement[j] != '}') {
            ++j;
          }
          if (j == replacement.size() || j == name_begin) {
            throw regex_error("malformed ${name} in replacement", i);
          }
          const std::size_t group =
            match.group_index(replacement.substr(name_begin, j - name_begin));
          if (group == npos) {
            throw regex_error("unknown group name in replacement", i);
          }
          out.append(match[group]);
          i = j + 1;
        }
        else {
          throw regex_error("invalid $ escape in replacement", i);
        }
      }
    }

    /*!
     * \brief Runs a single match attempt from offset 0 (backs match/search/fullmatch).
     * \param[in] text The subject text.
     * \param[in] mode The anchoring mode.
     * \return The match result.
     */
    [[nodiscard]] constexpr result_type run(std::string_view text,
                                            detail::run_mode mode) const
    {
      return run(text, 0, npos, mode);
    }

    /*!
     * \brief Region-aware single attempt: match over `text[0:endpos]` starting at \p pos.
     *
     * \p pos is the VM start offset, not a slice — zero-width assertions still see the
     * absolute position, so `\A` and `^` (non-multiline) fail at `pos > 0`, matching
     * Python `re`. \p endpos truncates the subject to a view (no copy), so `$` / `\Z`
     * treat it as the end. \p endpos is clamped to the text length; `pos > endpos` yields
     * no match. Capture offsets are absolute byte offsets in \p text.
     *
     * \param[in] text   The full subject (offsets are relative to it; must outlive the result).
     * \param[in] pos    Byte offset to start matching at.
     * \param[in] endpos Byte offset of the exclusive region end; \ref npos = end of text.
     * \param[in] mode   The anchoring mode.
     * \param[in] sem    Match semantics: leftmost-first (default) or the experimental leftmost-longest.
     * \return The match result, with offsets absolute in \p text.
     */
    [[nodiscard]] constexpr result_type run(std::string_view        text,
                                            std::size_t             pos,
                                            std::size_t             endpos,
                                            detail::run_mode        mode,
                                            match_semantics         sem = match_semantics::first) const
    {
      const std::size_t              end {endpos < text.size() ? endpos : text.size()};
      typename Storage::state_type   state;
      typename Storage::slot_storage slots;
      // Reference, not a copy: `program_view` is 408 bytes and this line runs once per search.
      // Binding to a const reference also covers the dynamic storage, whose view() still returns by
      // value -- the temporary's lifetime extends to this reference's scope.
      const detail::program_view&    prog    {program_.view()};
      // `state` above is freshly constructed for this one search against `prog` — same guarantee.
      detail::pike_vm<typename Storage::state_type, true> vm(prog, state);
      const auto                                          subject {text.substr(0, end)};
      // P3c cold: trailing-LA outside pike_vm::run (keeps pure class-loop run() pre-P3c-sized).
      // if constexpr: static_storage has no lookaround scratch / rejects LA at compile.
      bool matched {};
      if constexpr (requires(typename Storage::state_type & st) {
        st.lookaround;
      }) {
        if (sem == match_semantics::first && prog.hints.trailing_lookaround >= 0
            && (std::is_constant_evaluated() || !detail::trailing_la_route_disabled())) {
          detail::prof::tick_route(detail::prof::route::trailing_la);
          matched = prog.hints.stop_set_size >= 1
                      ? vm.template run_class_loop_trailing_la<true>(subject, pos, mode, slots)
                      : vm.template run_class_loop_trailing_la<false>(subject, pos, mode, slots);
        }
        else {
          matched = prog.hints.stop_set_size >= 1
                      ? vm.template run<true>(subject, pos, mode, slots, 0, sem)
                      : vm.template run<false>(subject, pos, mode, slots, 0, sem);
        }
      }
      else {
        // OPT-C Cascade is chosen once here (a single search), never in the per-byte scan.
        matched = prog.hints.stop_set_size >= 1
                    ? vm.template run<true>(subject, pos, mode, slots, 0, sem)
                    : vm.template run<false>(subject, pos, mode, slots, 0, sem);
      }
      return {text, std::move(slots), matched, pattern(), prog.names};
    }

  public:

    /*!
     * \brief EXPERIMENTAL, opt-in: a single leftmost-**longest** search (POSIX / RE2 `set_longest_match`), the
     *        default leftmost-first semantics left untouched. Among matches at the leftmost start it returns the
     *        longest; a lazy quantifier therefore behaves greedily, and captures are the leftmost-first thread's
     *        at that longest bound (not POSIX submatch). Runs on the general Pike loop (the first-match DFA /
     *        inner-literal fast paths are bypassed). A prototype for the `match_semantics` arc — not yet a stable
     *        API. Its iteration twin is \ref find_iter_longest.
     */
    [[nodiscard]] result_type search_longest(std::string_view text) const
    {
      return run(text, 0, npos, detail::run_mode::search, match_semantics::longest);
    }

    /*!
     * \brief Region-aware form of \ref search_longest — leftmost-longest search within `[pos, endpos)`. \p pos is the
     *        start (not a slice, per \ref run); \p endpos truncates the subject. Byte offsets.
     */
    [[nodiscard]] result_type search_longest(std::string_view text,
                                             std::size_t      pos,
                                             std::size_t      endpos = npos) const
    {
      return run(text, pos, endpos, detail::run_mode::search, match_semantics::longest);
    }
  };

  /*!
   * \brief The runtime-compiled regex type — the primary entry point.
   */
  using regex = basic_regex<detail::dynamic_storage>;

  /*!
   * \brief A fully compile-time regex.
   *
   * The pattern is parsed, compiled and exactly sized at compile time; matching
   * allocates nothing and also works in a constexpr context. An invalid pattern
   * is a compile error.
   *
   * \tparam Pattern The pattern, as a \ref fixed_string literal.
   * \tparam F       Compilation flags.
   */
  template <fixed_string Pattern, flags F = flags::none>
  using static_regex = basic_regex<detail::static_storage<Pattern, F>>;
} // namespace real

#endif // REAL_REAL_HPP
