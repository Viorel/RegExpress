/*!
 * \file real.hpp
 * \brief The public API: `real::regex`, `real::static_regex` and results.
 *
 * Header-only, C++20, constexpr from end to end. Include this one header.
 */
#ifndef REAL_REAL_HPP
#define REAL_REAL_HPP

#include "real/version.hpp"
#include "real/core/config.hpp"

#include <cassert>
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

/*!
 * \brief REAL's public API: \ref real::regex, \ref real::static_regex, \ref real::flags and the
 *        match/iterator types built on them.
 */
namespace real {

  /*!
   * \brief The result of a match attempt: success, spans and captures.
   *
   * Group views point into the searched text, which must outlive the result (the rvalue
   * `std::string` overloads are deleted). Named lookups borrow the regex's pattern and name table; a
   * result from a temporary regex is \ref real::basic_regex::owning_result_type, which owns them, so
   * names still resolve. `find_iter` and `find_all` are deleted on an rvalue regex.
   *
   * \tparam SlotStorage The capture-slot container (vector- or static-backed), from the storage policy.
   * \tparam NameOwner   How name tables are held: borrowed from a live regex, or owned.
   */
  template <typename SlotStorage, typename NameOwner = detail::borrowed_names>
  class basic_match_result
  {
  public:

    /*!
     * \brief Constructs an empty (non-matched) result.
     */
    constexpr basic_match_result() = default;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    /*!
     * \internal
     * \brief Constructs a result from raw slots (used internally by the engine).
     * \param[in] text    The searched text (borrowed; must outlive the result).
     * \param[in] slots   Flattened capture slots (byte offsets, npos for unset).
     * \param[in] matched Whether a match occurred.
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] names   The regex's named-group table (borrowed).
     */
    // Rvalue, not by value: a second block move's fixed startup is the whole cost on a groupless pattern.
    constexpr basic_match_result(std::string_view                     text,
                                 SlotStorage                       && slots,
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
     * \internal
     * \brief Engine-internal: adopts the fields of the borrowing twin, to be detached next.
     *
     * Constrained to the owning specialisation, so the borrowing one every walk yields keeps its exact
     * type. By value: the caller hands over a prvalue, and the slots move out of the parameter.
     *
     * \tparam OtherOwner The source's name owner, necessarily not this one's.
     * \param[in] other The freshly run result to take over.
     */
    template <typename OtherOwner>
    requires(!std::is_same_v<OtherOwner, NameOwner>)
    constexpr explicit basic_match_result(basic_match_result<SlotStorage, OtherOwner> other)
      : text_(other.text_),
        slots_(std::move(other.slots_)),
        matched_(other.matched_),
        pattern_(other.pattern_),
        names_(other.names_)
    {}

    /*!
     * \internal
     * \brief Engine-internal: an empty, unmatched result whose slot storage is built IN PLACE.
     *
     * With \ref engine_slots and \ref engine_set_matched, lets \ref basic_regex::run hand the engine the
     * final slot storage instead of moving a filled local in (a `rep movs` whose fixed startup dominates
     * a groupless call). Keep `run()` to a single return statement: the result is NRVO-constructed in the
     * caller's storage.
     *
     * \param[in] text    The searched text (borrowed; must outlive the result).
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] names   The regex's named-group table (borrowed).
     */
    constexpr basic_match_result(std::string_view                     text,
                                 std::string_view                     pattern,
                                 std::span<const detail::named_group> names)
      : text_(text),
        pattern_(pattern),
        names_(names)
    {}
#endif

    /*!
     * \internal
     * \brief Engine-internal: the slot storage, for the engine to fill in place.
     * \return A mutable reference to the flattened capture slots.
     */
    [[nodiscard]] constexpr SlotStorage& engine_slots() noexcept
    {
      return slots_;
    }

    /*!
     * \internal
     * \brief Engine-internal: records whether the fill that just ran produced a match.
     * \param[in] matched Whether a match occurred.
     */
    constexpr void engine_set_matched(bool matched) noexcept
    {
      matched_ = matched;
    }

    //! The twin specialisation reads these fields to adopt them; nothing else does.
    template <typename, typename>
    friend class basic_match_result;

    /*!
     * \internal
     * \brief Engine-internal: stop borrowing the regex's name tables, because it is about to die.
     *
     * Copies the pattern and name table \ref group_index reads into the result, which may outlive a
     * temporary regex. The views are cleared either way, so \ref owner_ is the source of truth and a copy
     * deep-copies rather than inheriting views into the original. A no-op when the names are borrowed.
     */
    constexpr void detach_from_regex()
    {
      if constexpr (!std::is_same_v<NameOwner, detail::borrowed_names>) {
        if (!names_.empty()) {
          owner_.emplace(std::string {pattern_},
                         std::vector<detail::named_group> {names_.begin(), names_.end()});
        }
        pattern_ = {};
        names_   = {};
      }
    }

    /*!
     * \internal
     * \brief Binds the subject, pattern and named groups once per walk, not per match.
     * \param[in] text    The subject the walk runs over.
     * \param[in] pattern The pattern text, for diagnostics and group naming.
     * \param[in] names   The pattern's named groups.
     */
    constexpr void bind_context(std::string_view                     text,
                                std::string_view                     pattern,
                                std::span<const detail::named_group> names)
    {
      text_    = text;
      pattern_ = pattern;
      names_   = names;
    }

    /*!
     * \internal
     * \brief Per-match refill after \ref bind_context — runs the VM and records only the outcome.
     * \tparam Cascade Whether the VM may take its memchr-cascade tail.
     * \tparam Vm      The engine type, deduced.
     * \param[in,out] vm     The engine to run.
     * \param[in]     text   The subject.
     * \param[in]     pos    Byte offset to attempt at.
     * \param[in]     mode   Anchoring: full, prefix or search.
     * \param[in]     forbid Offset at which a zero-length match is refused (the find_iter no-progress rule).
     * \param[in]     sem    Leftmost-first or leftmost-longest.
     * \return True on a match; the slots hold it.
     */
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

    /*!
     * \internal
     * \brief Refill from a span the engine already found in a batch, bypassing the VM entirely.
     * \tparam Vm The engine type, deduced.
     * \param[in,out] vm The engine, used only to reconstruct the slot layout for this span.
     * \param[in]     s  Match start.
     * \param[in]     e  Match end.
     */
    template <typename Vm>
    constexpr void engine_refill_span(Vm&         vm,
                                      std::size_t s,
                                      std::size_t e)
    {
      vm.write_cp_span_slots(slots_, s, e);
      matched_ = true;
    }

    /*!
     * \internal
     * \brief Cold path for a trailing-lookaround walk.
     * \tparam Cascade Whether the VM may take its memchr-cascade tail.
     * \tparam Vm      The engine type, deduced.
     * \param[in,out] vm   The engine to run.
     * \param[in]     text The subject.
     * \param[in]     pos  Byte offset to attempt at.
     * \return True on a match; the slots hold it.
     */
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
     * \return Whether the attempt matched.
     */
    [[nodiscard]] constexpr bool matched() const
    {
      return matched_;
    }

    /*!
     * \brief Returns `true` if the attempt matched (explicit bool conversion).
     * \return Whether the attempt matched.
     */
    constexpr explicit operator bool() const {
      return matched_;
    }

    /*!
     * \brief Returns the number of groups, including group 0 (the whole match).
     * \return The group count.
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
     * \brief View of a group's matched text — `std::smatch`'s spelling for \ref operator[].
     *
     * Unlike `std::smatch::str` it returns a view: it never copies, and the subject must outlive the
     * result. `std::string s = m.str(1);` does not compile (the conversion is explicit); write
     * `std::string s {m.str(1)};` for an owned copy.
     * \param[in] group Group number (0 = whole match).
     * \return A view into the searched text, empty if the group is unset.
     */
    [[nodiscard]] constexpr std::string_view str(std::size_t group = 0) const
    {
      return (*this)[group];
    }

    /*!
     * \brief Resolves a group name to its number.
     * \param[in] name The group name.
     * \return The group number, or \ref real::npos if unknown.
     */
    [[nodiscard]] constexpr std::size_t group_index(std::string_view name) const
    {
      // Owned context first, borrowed views otherwise. On the compile-time policy `get()` is a
      // constant `nullptr`, so this folds to the borrowed branch with nothing left to test.
      const detail::owned_name_context       *   owned   {owner_.get()};
      const std::string_view                     pattern {owned != nullptr ? std::string_view {owned->pattern} : pattern_};
      const std::span<const detail::named_group> names   {owned != nullptr
                                                          ? std::span<const detail::named_group> {owned->names}
                                                          : names_};
      for (const detail::named_group& named_group : names) {
        const auto begin  {static_cast<std::size_t>(named_group.begin)};
        const auto length {static_cast<std::size_t>(named_group.end - named_group.begin)};
        if (pattern.substr(begin, length) == name) {
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
     * \brief The capture slots as one flat `[start0, end0, start1, end1, …]` view.
     *
     * For copying every group in one pass, pairwise (`spans[2g]`, `spans[2g+1]`); the C ABI's `spans`
     * buffer has this layout. Only meaningful on a matched result: an unmatched one carries whatever the
     * last fill left. For a single group prefer \ref start / \ref end.
     *
     * \return A view of `2 * size()` slot values; empty when there are no slots.
     */
    // The C shim walks this view, not start/end per group (each re-tests matched() and the bound); a
    // runtime-length memcpy costs more than the stores for the dominant 1-2 slot shapes.
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
    std::span<const detail::named_group> names_;      //!< Named-group table: borrowed, or owned via \ref owner_.
    //! Owns what \ref pattern_ and \ref names_ point at, once \ref detach_from_regex has run; empty
    //! (and zero-sized) on the compile-time policy, null on every result that borrows.
    [[no_unique_address]] NameOwner      owner_ {};
  };

  /*!
   * \brief Forward iterator over the non-overlapping matches in a text.
   *
   * Follows Python's empty-match rules: an empty match is yielded (even right
   * after a non-empty one), then the scan advances by one codepoint. The regex
   * and the text must outlive the iterator. Obtained from \ref basic_match_range.
   *
   * \tparam Storage The regex's storage policy (selects the result/scratch types).
   */
  // TrailingLA is an engine-internal walk specialization. Callers never set it.
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

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    /*!
     * \internal
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
        // Fast paths are first-mode only: a leftmost-longest walk runs the general loop.
        cascade_(sem == match_semantics::first && prog.hints.stop_set_size >= 1),
        sem_(sem)
    {
      if constexpr (TrailingLA) {
        // This specialization is the trailing-lookaround walk: it sets the flag the pure walk computes.
        trailing_la_walk_ = true;
      }
      else {
        decide_batching(prog, sem, text_.size());
      }
      current_.bind_context(text_, pattern_, prog_.names); // invariant across the walk — set once, not per match
      advance();
    }

#endif

    /*!
     * \internal
     * \brief Once per walk: which batched filler, if any, serves this scan.
     * \param[in] prog       The compiled program, for its hints.
     * \param[in] sem        The walk's match semantics.
     * \param[in] text_bytes Subject length; the lazy-DFA filler needs a minimum runway.
     */
    // Keep noinline and cold: `count_matches` inlines the constructor, and inline this logic made every
    // dispatch change tax per-match rows whose code never moved (one added route: `single [a-z]` +10.7 %).
    REAL_COLD
    constexpr void decide_batching(detail::program_view prog,
                                   match_semantics      sem,
                                   std::size_t          text_bytes)
    {
      // Decided here, not in the inlined constructor, so the flag costs it nothing.
      if (!std::is_constant_evaluated() && prog.hints.trailing_lookaround >= 0
          && !detail::trailing_la_route_disabled()) {
        trailing_la_walk_ = true;
      }
      // The prerequisites every batched route shares, stated once: a shape excluded from one route and
      // not another is a wrong answer. Anchored shapes are out: the fillers scan forward and bypass
      // `run()`, which is where `\A`/`^` becomes prefix anchoring (`^[a-z]+` over "  abc" would match at
      // 2); an anchored pattern yields at most one match per walk anyway.
      // DECLARED then ASSIGNED: as a `const bool` initializer this is manifestly constant-evaluated, and
      // clang rejects `std::is_constant_evaluated()` there (-Werror=constant-evaluated).
      using vm = detail::pike_vm<typename Storage::state_type, true>;
      const detail::pattern_hints& h         {prog.hints};
      bool                         batchable {};
      batchable = !std::is_constant_evaluated() && sem == match_semantics::first
                  && !detail::class_fastpath_disabled()
                  && !h.anchored_start && h.line_anchored == 0U;
      // A kept `\b`/`\B` wrap (a word-subset class needs it: `[a-z]+` can start after `_` or a digit) is
      // checked per span by the byte and code-point class fillers only; the other routes require its absence.
      const bool                   no_wrap {h.wb_lead == 0 && h.wb_trail == 0};
      const bool                   plain   {batchable && no_wrap && !h.wb_lead_maximal_run};   // no boundary kept around a match
      const bool                   no_loop {h.greedy_class_loop < 0 && h.greedy_cp_class < 0}; // neither class loop claims it
      wb_kept_ = !no_wrap;
      // A dropped leading `\b` (\ref real::detail::pattern_hints::wb_lead_maximal_run) still admits the two
      // class-run routes, whose fillers carry the general route's window-edge guard; `plain` routes decline.
      wb_edge_        = h.wb_lead_maximal_run;
      // A `{k,}` minimum admits both class-run routes: their fillers skip a too-short maximal run.
      batch_bytes_    = batchable && h.greedy_class_loop >= 0
                        && h.greedy_class_loop_end == 0;
      batch_cp_ascii_  = plain && h.codepoint_class_ascii >= 0 && no_loop;
      // The bare single byte-class (`[a-z]`, no quantifier) needs no `{k,}` or capture exclusion:
      // the 4-opcode shape pattern_hints::single_class recognizes admits neither.
      batch_single_cl_ = plain && h.single_class >= 0;
      // The code-point class loop gets no member: it is \ref refill_batch's `else`, and the iterator's
      // size is measured (see \ref batch_cap). A kept wrap is checked per run (fill_cp_class_spans's WbKept).
      const bool cp_class {batchable && h.greedy_cp_class >= 0
                           && h.greedy_cp_class_end == 0
      }; // no is_constant_evaluated: see above

      // Fixed alternation, small-set shape only (2..8 distinct first bytes). From
      // \ref detail::pike_vm::ac_branch_floor branches up, `run()` may hand the subject to Aho-Corasick on
      // its density, a gate a batched walk bypasses: the first refill asks
      // pike_vm::alternation_automaton_claims (sticky per subject) and disarms the batch where it claims.
      const bool alternation {batchable && no_wrap && h.fixed_alternation && h.alternation_branch_count > 0
                              && no_loop && h.codepoint_class_ascii < 0 && h.single_class < 0};
      batch_alt_       = alternation && h.small_set_size >= 2 && h.small_set_size <= 8;
      // The lazy-DFA route, last: what a pattern falls to when no recognizer claims it. Its conditions mirror
      // the route's gate in `run()`, which a batched walk bypasses, so any divergence is a wrong answer. The
      // filler has only the anchored-from-candidate sub-scan (`first_bytes_valid`); the span path applies no
      // `forbid_empty_until_` (not nullable); `run()` consults the Aho-Corasick floor per search; a trailing
      // lookaround lives on `advance`'s per-match path (`make route-surface-parity`). `lazy_dfa_is_the_route`,
      // not a residue test: that sent plain literals here and lost memmem. A faster filler's shape stays its own.
      // A compile-time storage has no per-regex immutables, hence no shared DFAs: its filler could only ever
      // answer partial and leave every match to `run()` after a wasted refill.
      batch_lazy_dfa_  = plain && !Storage::is_compile_time && !detail::lazy_dfa_route_disabled() && h.first_bytes_valid
                         && !h.empty_match_possible
                         // A walk that reads no groups (count_matches) gets spans only, so groups need
                         // not cost it the batch.
                         && (prog.slot_count <= 2 || h.capture_free_walk)
                         && h.alternation_branch_count < 4
                         && h.trailing_lookaround < 0
                         && vm::lazy_dfa_is_the_route(h)
                         && text_bytes >= vm::lazy_dfa_min_input
                         && !batch_bytes_ && !batch_cp_ascii_ && !batch_single_cl_ && !cp_class
                         && !batch_alt_;
      // Exact-literal route; pike.hpp's fill_exact_literal_spans states what it may cost other rows.
      batch_exact_lit_ = plain && prog.slot_count == 2 && vm::exact_literal_is_the_route(h);
      // Inner-literal route, armed like the lazy-DFA one (pike.hpp's `inner_literal_is_the_route`).
      // `slot_count == 2` lets the filler reuse the route function with a two-slot sink; nullable is out
      // (no empty-match rule on the span path); the route's seam takes this out with it.
      batch_inner_lit_ = plain && !detail::inner_literal_route_disabled() && !h.empty_match_possible
                         && prog.slot_count == 2 && vm::inner_literal_is_the_route(prog);
      batch_fixed_     = batchable && prog.slot_count == 2 && !h.empty_match_possible && vm::fixed_shape_is_the_route(prog);
      batch_alt_asks_  = batch_alt_;
      // An alternation with more first bytes than the small set holds: run() tries run_alternation_wide
      // ahead of the automaton's gate; the filler asks run()'s questions once and disarms where it declines.
      batch_wide_      = alternation && h.small_set_size == 0 && h.first_bytes_valid && !h.empty_match_possible
                         && !batch_bytes_ && !batch_cp_ascii_ && !batch_single_cl_ && !cp_class && !batch_alt_
                         && !batch_lazy_dfa_;
      batch_eligible_  = batch_bytes_ || batch_cp_ascii_ || batch_single_cl_ || cp_class || batch_alt_ || batch_wide_
                         || batch_lazy_dfa_ || batch_exact_lit_ || batch_inner_lit_ || batch_fixed_;
    }

    /*!
     * \brief Returns the current match.
     * \return A reference to the result, valid until the next increment.
     */
    [[nodiscard]] constexpr const value_type& operator*() const
    {
      return current_;
    }

    /*!
     * \brief Returns pointer to the current match.
     * \return A pointer to the result, valid until the next increment.
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
     * \brief Whether the walk is over, without building an end sentinel to compare against.
     *
     * Prefer this in a hand-rolled loop: `it == basic_match_iterator{}` answers the same question but
     * materialises a full walker (a whole heap-backed scratch state) just to test.
     *
     * \return `true` once no further match will be produced.
     */
    [[nodiscard]] constexpr bool exhausted() const noexcept
    {
      return done_;
    }

    /*!
     * \brief Returns `true` if both denote the same position in the same walk, or both are the end.
     *
     * Live iterators compare offset, subject and program. Exhausted ones are equal whatever they walked:
     * \ref basic_match_range::end is a default-constructed sentinel carrying neither.
     *
     * \param[in] other Another iterator.
     * \return `true` if both are exhausted, or both are live at the same offset of the same walk.
     */
    [[nodiscard]] constexpr bool operator==(const basic_match_iterator& other) const
    {
      return done_ == other.done_ &&
             (done_ || (pos_ == other.pos_ && text_.data() == other.text_.data() &&
                        text_.size() == other.text_.size() &&
                        prog_.code.data() == other.prog_.code.data()));
    }

  private:

    template <typename>
    friend class basic_regex;

    /*!
     * \brief Counts the matches left, the current one included, and ends the walk.
     *
     * The spans a batched fill buffered past the current match are counted without being handed out:
     * handing one out writes its slots, which no count reads. The first span of each fill still goes
     * through \ref advance, so the walk keeps its one path for refills, partial fills and the per-match
     * fallback.
     *
     * \return The number of matches from the current one to the end.
     */
    constexpr std::size_t count_rest()
    {
      std::size_t n {0};
      // One loop: split by `batch_eligible_`, `advance` is inlined twice and costs every walk more than the
      // buffer test it spares a walk that never batches.
      while (!done_) {
        ++n;
        if (const std::size_t left {batch_n_ - batch_i_}; left != 0) {
          // The current match was handed out of this same fill, so the empty-match guard is already clear,
          // and the last span's end is where handing each out would have left the walk.
          n        += left;
          pos_      = batch_[batch_n_ - 1].end;
          batch_i_  = batch_n_;
        }
        advance();
      }
      return n;
    }

    detail::program_view         prog_;                                        //!< The program being run.
    std::string_view             pattern_;                                     //!< Pattern text (named lookups).
    std::string_view             text_;                                        //!< The text being scanned.
    std::size_t                  pos_                {};                       //!< Current scan offset.
    std::size_t                  forbid_empty_until_ {};                       //!< Empty-match guard (see pike.hpp).
    bool                         done_               {true};                   //!< True once exhausted.
    bool                         cascade_            {};                       //!< Chosen once — run the memchr-cascade class-run variant for this whole walk.
    match_semantics              sem_                {match_semantics::first}; //!< leftmost-first (default) or longest (find_iter_longest).
    value_type                   current_;                                     //!< The current match.
    typename Storage::state_type state_;                                       //!< VM scratch, reused across the walk.

    //! \brief Buffered spans for the batched routes — see \ref batch_eligible_.
    //!
    //! Tuned: a wider buffer gains nothing more and charges walks that never batch, since this array is part
    //! of every iterator (outlining the refill does not recover it).
    static constexpr std::size_t                                          batch_cap         {4};
    typename detail::pike_vm<typename Storage::state_type, true>::cp_span batch_[batch_cap] {}; //!< The buffered spans; indices \ref batch_i_ .. \ref batch_n_ are the unread ones.
    std::size_t                                                           batch_n_          {}; //!< Spans currently buffered.
    std::size_t                                                           batch_i_          {}; //!< Next span to hand out.
    bool                                                                  batch_eligible_   {}; //!< Route/shape allows batching (decided once).

    /*!
     * \brief This walk takes the trailing-lookaround route (`[a-z]+(?=[a-z])`), chosen once here rather
     *        than by specialization, so \ref basic_regex::find_iter, whose return type fixes the
     *        specialization, reaches it too.
     *
     * Costs one test per match on `advance`'s general path (14x won on `find_iter`, ~17 % lost on unbatched
     * `exact_literal`). An enum shared with `batch_eligible_` costs more: only a walk selected without a
     * per-match test removes it.
     */
    bool                                                                  trailing_la_walk_ {};
    bool                                                                  batch_bytes_      {}; //!< Batch the BYTE-class route rather than the code-point one.
    bool                                                                  batch_cp_ascii_   {}; //!< Batch the `.`/negated-class route (\ref real::detail::pike_vm::fill_codepoint_class_spans).
    bool                                                                  batch_single_cl_  {}; //!< Batch the bare single byte-class route (\ref real::detail::pike_vm::fill_single_class_spans).
    //! \brief The pattern carries a dropped leading `\b` (\ref real::detail::pattern_hints::wb_lead_maximal_run):
    //!        its filler takes the window-edge guard as a template argument, never as a loop test.
    bool                                                                  wb_edge_          {};
    bool                                                                  batch_alt_        {}; //!< Batch the fixed-alternation route (\ref real::detail::pike_vm::fill_alternation_spans).
    bool                                                                  batch_alt_asks_   {}; //!< The alternation batch has yet to ask whether the automaton takes the subject.
    bool                                                                  batch_wide_       {}; //!< Batch the wide alternation route (\ref detail::pike_vm::fill_alternation_wide_spans).
    bool                                                                  batch_spent_      {}; //!< The last fill stopped short of the buffer at the end of the subject.
    bool                                                                  batch_lazy_dfa_   {}; //!< Batch the lazy-DFA route (%pike.hpp's `fill_lazy_dfa_spans`), the one no recognizer claims.
    bool                                                                  batch_exact_lit_  {}; //!< Batch the exact-literal route (%pike.hpp's `fill_exact_literal_spans`).
    bool                                                                  batch_inner_lit_  {}; //!< Batch the inner-literal route (%pike.hpp's `fill_inner_literal_spans`); may stop short, see \ref batch_partial_.
    bool                                                                  batch_fixed_      {}; //!< Batch the fixed-shape route (\ref detail::pike_vm::fill_fixed_shape_spans).
    //! \brief The last fill stopped without proving the subject spent: an empty buffer means "resume on the
    //!        per-match path", not "the walk is over". Set by the lazy-DFA, inner-literal and wide-alternation
    //!        fillers and by the automaton's claim; every other filler scans the whole subject.
    bool                                                                  batch_partial_    {};
    //! \brief The pattern keeps a `\b`/`\B` wrap, evaluated on every span by the byte and code-point class
    //!        fillers; the other batched routes decline such patterns.
    bool                                                                  wb_kept_          {};

    /*!
     * \brief Cold half of the batched walk: refills \ref batch_ from the engine.
     *
     * Outlined: this unit sits on gcc's per-unit inline budget (docs/design.dox §10.1), and inlined it grows
     * `advance` enough to charge rows that never batch.
     *
     * Each branch bills \ref real::detail::prof::tick_route under the unbatched route's identifier, once per
     * refill: `entries / matches` reads `1 / batch_cap` while batching works.
     *
     * \warning Read docs/MEASUREMENT.md §3.2 before adding a branch here: one more branch, every filler
     *          byte-identical, moved 17 of 18 rows' medians positive.
     * \return `true` if at least one span was buffered.
     */
    REAL_NOINLINE
    constexpr bool refill_batch()
    {
      // A short fill without `batch_partial_` proved the rest spent: end the walk instead of rescanning.
      if (batch_spent_) [[unlikely]] {
        batch_n_ = 0;
        batch_i_ = 0;
        return false;
      }
      detail::note(detail::counter::batch_fills);
      detail::pike_vm<typename Storage::state_type, true> bvm {prog_, state_};
      if (batch_bytes_) {
        // Instantiations chosen once per walk, so an unneeded guard is absent, not untaken (fill_class_spans
        // states why a runtime test is refused). `wb_edge_` and `wb_kept_` are mutually exclusive by
        // construction, so three pairs cover every case.
        detail::prof::tick_route(detail::prof::route::class_loop);
        if (wb_kept_) {
          batch_n_ = cascade_ ? bvm.template fill_class_spans<true, false, true>(text_, pos_, batch_, batch_cap)
                              : bvm.template fill_class_spans<false, false, true>(text_, pos_, batch_, batch_cap);
        }
        else if (wb_edge_) {
          batch_n_ = cascade_ ? bvm.template fill_class_spans<true, true, false>(text_, pos_, batch_, batch_cap)
                              : bvm.template fill_class_spans<false, true, false>(text_, pos_, batch_, batch_cap);
        }
        else {
          batch_n_ = cascade_ ? bvm.template fill_class_spans<true, false, false>(text_, pos_, batch_, batch_cap)
                              : bvm.template fill_class_spans<false, false, false>(text_, pos_, batch_, batch_cap);
        }
      }
      else if (batch_cp_ascii_) {
        detail::prof::tick_route(detail::prof::route::codepoint_class);
        batch_n_ = cascade_
                     ? bvm.template fill_codepoint_class_spans<true>(text_, pos_, batch_, batch_cap)
                     : bvm.template fill_codepoint_class_spans<false>(text_, pos_, batch_, batch_cap);
      }
      else if (batch_single_cl_) {
        detail::prof::tick_route(detail::prof::route::class_loop);
        batch_n_ = bvm.fill_single_class_spans(text_, pos_, batch_, batch_cap);
      }
      else if (batch_alt_) {
        if (batch_alt_asks_) {
          batch_alt_asks_ = false;
          if (bvm.alternation_automaton_claims(text_, pos_)) {
            // The automaton takes this subject: every search goes through `run()`, which hands it there.
            batch_alt_      = false;
            batch_eligible_ = false;
            batch_partial_  = true;
            batch_n_        = 0;
            return false;
          }
        }
        detail::prof::tick_route(detail::prof::route::alternation);
        batch_n_ = bvm.fill_alternation_spans(text_, pos_, batch_, batch_cap);
      }
      else if (batch_wide_) {
        detail::prof::tick_route(detail::prof::route::alternation);
        bool disarm {false};
        batch_n_ = bvm.fill_alternation_wide_spans(text_, pos_, batch_, batch_cap, batch_partial_, disarm);
        if (disarm) {
          // The route declined the subject: every further search goes through `run()`, which takes it elsewhere.
          batch_wide_     = false;
          batch_eligible_ = false;
        }
      }
      else if (batch_lazy_dfa_) {
        detail::prof::tick_route(detail::prof::route::lazy_dfa_anchored);
        batch_n_ = bvm.fill_lazy_dfa_spans(text_, pos_, batch_, batch_cap, batch_partial_);
      }
      else if (batch_exact_lit_) {
        detail::prof::tick_route(detail::prof::route::exact_literal);
        batch_n_ = bvm.fill_exact_literal_spans(text_, pos_, batch_, batch_cap);
      }
      else if (batch_fixed_) {
        detail::prof::tick_route(detail::prof::route::fixed_shape);
        batch_n_ = bvm.fill_fixed_shape_spans(text_, pos_, batch_, batch_cap);
      }
      else if (batch_inner_lit_) {
        detail::prof::tick_route(detail::prof::route::inner_literal);
        bool disarm {false};
        batch_n_ = bvm.fill_inner_literal_spans(text_, pos_, batch_, batch_cap, batch_partial_, disarm);
        if (disarm) {
          // The route's abandon is sticky per haystack: further refills would repeat the wasted memmem, so
          // the walk stops batching (staying armed cost `date dense` +10 %).
          batch_inner_lit_ = false;
          batch_eligible_  = false;
        }
      }
      else {
        // The code-point class loop, reached as the `else` (decide_batching's `cp_class`): every other
        // disjunct of batch_eligible_ has its branch above.
        detail::prof::tick_route(detail::prof::route::cp_class_loop);
        if (wb_kept_) {
          batch_n_ = wb_edge_ ? bvm.template fill_cp_class_spans_wrapped<true>(text_, pos_, batch_, batch_cap)
                              : bvm.template fill_cp_class_spans_wrapped<false>(text_, pos_, batch_, batch_cap);
        }
        else {
          batch_n_ = wb_edge_ ? bvm.template fill_cp_class_spans<true>(text_, pos_, batch_, batch_cap)
                              : bvm.template fill_cp_class_spans<false>(text_, pos_, batch_, batch_cap);
        }
      }
      batch_spent_ = !batch_partial_ && batch_n_ < batch_cap;
      batch_i_     = 0;
      return batch_n_ != 0;
    }

    /*!
     * \brief Finds the next match, applying the empty-match advance rules.
     *
     * \note Not force-inlined, by measurement: about half its cost is the per-match frame, but
     *       `always_inline` pushes the unit past `--param inline-unit-growth` and regresses the target and
     *       unrelated class rows on one ISA (docs/design.dox 10.1). Shrink the frame instead.
     */
    constexpr void advance()
    {
      if (done_ || pos_ > text_.size()) {
        done_ = true;
        return;
      }
      // A batched route's cost is its per-match return, not its scan: hand out buffered spans and re-enter
      // the engine only once the buffer drains.
      if (batch_eligible_) {
        // The partial test sits inside the exhausted branch: as a second sequential test on the per-match
        // path it moved 17 of 21 unrelated medians positive (p = 0.007, docs/MEASUREMENT.md §3.2).
        if (batch_i_ == batch_n_ && !refill_batch()) {
          // With `batch_partial_` the filler stopped with matches possibly ahead: the per-match path below
          // re-enters `run()`'s full gate, where ending the walk would drop them.
          if (!batch_partial_) {
            done_ = true;
            return;
          }
        }
        else {
          detail::pike_vm<typename Storage::state_type, true> wvm {prog_, state_};
          const auto&                                         sp  {batch_[batch_i_++]};
          detail::note(detail::counter::batch_handouts);
          current_.engine_refill_span(wvm, sp.start, sp.end);
          pos_ = sp.end;
          // No batched filler emits an empty span (each one's own note says why), so the find_iter
          // no-progress rule cannot apply here.
          forbid_empty_until_ = 0;
          return;
        }
        // Batched route, refill came back empty, subject NOT spent: the per-match path below takes it.
      }
      // `state_` is this iterator's own member and `prog_` is fixed for the walk, so the state never
      // meets a second program: the VM can drop its per-`run()` program-identity compare.
      detail::pike_vm<typename Storage::state_type, true> vm(prog_, state_);
      bool                                                ok {};
      if (trailing_la_walk_) {
        // Selected by a flag, not by specialization, so `find_iter` reaches it (\ref trailing_la_walk_).
        ok = cascade_ ? current_.template engine_refill_trailing_la<true>(vm, text_, pos_)
                      : current_.template engine_refill_trailing_la<false>(vm, text_, pos_);
      }
      else {
        // Pure walk. Cascade chosen once; both arms are the same hot family.
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
   *
   * The regex and the text must outlive the range. Empty matches follow Python:
   * an empty match is yielded, then the scan advances one codepoint.
   *
   * \tparam Storage The regex's storage policy.
   */
  // TrailingLA is an engine-internal walk specialization. Callers never set it.
  template <typename Storage, bool TrailingLA = false>
  class basic_match_range
  {
  public:

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    /*!
     * \internal
     * \brief Binds the range to a compiled program and a subject.
     *
     * Called by \ref basic_regex::find_iter; not constructed by user code.
     *
     * \param[in] prog    The compiled program.
     * \param[in] pattern The pattern text (for named-group resolution).
     * \param[in] text    The text to iterate over (borrowed).
     * \param[in] start   Byte offset to begin iterating from (0 = the whole text).
     * \param[in] sem     Match semantics: leftmost-first (default) or leftmost-longest.
     * \param[in] matching_only Capture-free walk for a caller that reads no groups
     *        (\ref basic_regex::count_matches). Ignored unless the program is
     *        structurally eligible.
     */
    // matching_only is applied to the stored view: mutating a caller's view first would copy it twice, a
    // fixed per-call cost (see detail::program_view's size ceiling).
    constexpr basic_match_range(detail::program_view prog,
                                std::string_view     pattern,
                                std::string_view     text,
                                std::size_t          start          = 0,
                                match_semantics      sem            = match_semantics::first,
                                bool                 matching_only  = false)
      : prog_(prog),
        pattern_(pattern),
        text_(text),
        start_(start),
        sem_(sem)
    {
      if (matching_only) {
        prog_.hints.capture_free_walk = detail::capture_free_walk_structural(prog_.code);
      }
    }

#endif

    /*!
     * \brief Returns an iterator to the first match.
     * \return An iterator positioned on the first match, or equal to \ref end when there is none.
     */
    [[nodiscard]] constexpr basic_match_iterator<Storage, TrailingLA> begin() const
    {
      return {prog_, pattern_, text_, start_, sem_};
    }

    /*!
     * \brief Returns the end sentinel.
     * \return The past-the-end iterator.
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

  namespace detail {
    /*!
     * \brief A C string as a subject. A null pointer is a caller's bug: a debug build stops on it, and a release
     *        build reads it as the empty subject, as the C API does, where constructing a `std::string_view` from
     *        it is undefined.
     * \param[in] text A NUL-terminated string; null only by mistake.
     * \return The view.
     */
    [[nodiscard]] constexpr std::string_view c_string_subject(const char* text) noexcept
    {
      assert(text != nullptr && "a null C string: pass \"\" for an empty subject");
      return text == nullptr ? std::string_view {} : std::string_view {text};
    }

    struct non_empty_access;

    /*!
     * \brief One attempt over a region, as every single search makes it: the trailing-lookaround walk where the
     *        pattern has one, else \ref pike_vm::run with its memchr-cascade variant chosen once, here.
     *
     * Shared by \ref basic_regex and the bindings that keep their own scratch, so all take the same routes.
     *
     * \tparam State    The caller's scratch state.
     * \tparam Bound    Whether the engine may skip its program-identity check.
     * \tparam Slots    The capture-slot container the engine fills.
     * \param[in]  vm      The engine.
     * \param[in]  prog    The program it runs.
     * \param[in]  subject The subject, already truncated to the region's end.
     * \param[in]  pos     Where the attempt starts.
     * \param[in]  mode    Anchoring.
     * \param[out] slots   Capture slots, filled on a match.
     * \param[in]  sem     Match semantics.
     * \return True on a match.
     */
    template <typename State, bool Bound, typename Slots>
    [[nodiscard]] constexpr bool run_attempt(pike_vm<State, Bound>& vm,
                                             const program_view&    prog,
                                             std::string_view       subject,
                                             std::size_t            pos,
                                             run_mode               mode,
                                             Slots&                 slots,
                                             match_semantics        sem = match_semantics::first)
    {
      // `if constexpr` because the static storage has no lookaround scratch.
      if constexpr (requires(State & st) {
        st.lookaround;
      }) {
        if (sem == match_semantics::first && prog.hints.trailing_lookaround >= 0
            && (std::is_constant_evaluated() || !trailing_la_route_disabled())) {
          prof::tick_route(prof::route::trailing_la);
          return prog.hints.stop_set_size >= 1 ? vm.template run_class_loop_trailing_la<true>(subject, pos, mode, slots)
                                               : vm.template run_class_loop_trailing_la<false>(subject, pos, mode, slots);
        }
      }
      return prog.hints.stop_set_size >= 1 ? vm.template run<true>(subject, pos, mode, slots, 0, sem)
                                           : vm.template run<false>(subject, pos, mode, slots, 0, sem);
    }
  } // namespace detail

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
     * \brief What a single attempt on a temporary regex yields.
     *
     * Same spans and groups as \ref result_type, plus ownership of the name
     * tables the regex would otherwise lend. Bind it with `auto`. Spelling
     * \ref result_type (or `real::match_result`) does not compile, which is
     * the point: there is no conversion that could drop the ownership and
     * leave dangling views.
     *
     * On `static_regex` the two aliases are the same type: the tables have
     * static storage duration, so a result from a temporary is already safe.
     */
    // Apart from result_type, so the borrowing path stays trivially destructible.
    using owning_result_type = basic_match_result<typename Storage::slot_storage,
                                                  typename Storage::name_owner>;

    /*!
     * \brief Compiles \p pattern at run time (the `real::regex` constructor).
     * \param[in] pattern      The pattern text.
     * \param[in] compile_flags Optional flags (merged with a leading global-flags group, `(?imsxaU)` or
     *                          `(?flags-flags)`).
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
    [[nodiscard]] constexpr result_type match(std::string_view text) const&
    {
      return run(text, detail::run_mode::prefix);
    }

    /*!
     * \brief Match the entire \p text (Python `re.fullmatch`).
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]] constexpr result_type fullmatch(std::string_view text) const&
    {
      return run(text, detail::run_mode::full);
    }

    /*!
     * \brief Leftmost match anywhere in \p text (Python `re.search`).
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]] constexpr result_type search(std::string_view text) const&
    {
      return run(text, detail::run_mode::search);
    }

    /*!
     * \brief Region-aware `match`: anchored at \p pos within `text[0:endpos]` (Python
     *        `re.match` with `pos` / `endpos`). Byte offsets; \p pos is not a slice (see
     *        \ref run — `\A` fails at `pos > 0`); \p endpos defaults to the end of \p text.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the match must start at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The match result; falsy when the pattern does not match at \p pos.
     */
    [[nodiscard]] constexpr result_type match(std::string_view text,
                                              std::size_t      pos,
                                              std::size_t      endpos = npos) const&
    {
      return run(text, pos, endpos, detail::run_mode::prefix);
    }

    /*!
     * \brief Whether `match(text, pos)` could come out differently if \p text continued past its end.
     *
     * For text that arrives in pieces: a lexer may commit to the match at \p pos only once no further text
     * can change it. `[a-z]+` on `"ab"` could still grow; on `"ab "` it cannot. The end of \p text is
     * treated as a place more text may follow, not as the end of the subject, so `$`, `\b` or a lookahead
     * that read it make the answer true. Conservative: it may say true where more text would in fact
     * change nothing, never false where it would. Runs the general matcher, not the fast paths.
     *
     * \param[in] text The text available so far.
     * \param[in] pos  Byte offset the match is anchored at.
     * \return True when text past the end of \p text could change the match at \p pos.
     */
    [[nodiscard]] bool can_extend(std::string_view text,
                                  std::size_t      pos = 0) const
    {
      if (pos > text.size()) {
        return true; // the anchor lies in text still to come
      }
      typename Storage::state_type                        state;
      const detail::program_view&                         prog {program_.view()};
      detail::pike_vm<typename Storage::state_type, true> vm(prog, state);
      typename Storage::slot_storage                      slots;
      return vm.extends_past_end(text, pos, slots);
    }

    /*!
     * \brief Region-aware `fullmatch`: the whole region `[pos, endpos)` must match.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the region starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The match result; falsy unless the whole region matches.
     */
    [[nodiscard]] constexpr result_type fullmatch(std::string_view text,
                                                  std::size_t      pos,
                                                  std::size_t      endpos = npos) const&
    {
      return run(text, pos, endpos, detail::run_mode::full);
    }

    /*!
     * \brief Region-aware `search`: leftmost match within `[pos, endpos)`.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The leftmost match in the region; falsy when there is none.
     */
    [[nodiscard]] constexpr result_type search(std::string_view text,
                                               std::size_t      pos,
                                               std::size_t      endpos = npos) const&
    {
      return run(text, pos, endpos, detail::run_mode::search);
    }

    /*!
     * \brief `match` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type match(const char* text) const&
    {
      return match(detail::c_string_subject(text));
    }

    /*!
     * \brief `fullmatch` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type fullmatch(const char* text) const&
    {
      return fullmatch(detail::c_string_subject(text));
    }

    /*!
     * \brief `search` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type search(const char* text) const&
    {
      return search(detail::c_string_subject(text));
    }

    // Region forms for string literals: without them a bare literal is ambiguous between the
    // `std::string_view` overload and the deleted `const std::string&&` one (two user-defined
    // conversions of equal rank), and the diagnostic calls the literal a temporary.

    /*!
     * \brief Region-aware `match` overload for string literals.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the match is anchored at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type match(const char* text,
                                              std::size_t pos,
                                              std::size_t endpos = npos) const&
    {
      return match(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Region-aware `fullmatch` overload for string literals.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the region starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type fullmatch(const char* text,
                                                  std::size_t pos,
                                                  std::size_t endpos = npos) const&
    {
      return fullmatch(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Region-aware `search` overload for string literals.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type search(const char* text,
                                               std::size_t pos,
                                               std::size_t endpos = npos) const&
    {
      return search(detail::c_string_subject(text), pos, endpos);
    }

    // Single attempts on a temporary regex stay callable, unlike find_iter and find_all (the one-expression
    // form is safe); a stored result owns a copy of the name context (basic_match_result::detach_from_regex).
    // Ref-qualification is all-or-nothing per parameter list, hence `const&` on every twin above; the
    // deleted `const std::string&&` overloads still require the subject to outlive the result.

    /*!
     * \brief `match` on a temporary regex; the result owns its name context.
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]]
    constexpr owning_result_type match(std::string_view text) const&&
    {
      return detach(run(text, detail::run_mode::prefix));
    }

    /*!
     * \brief `fullmatch` on a temporary regex; the result owns its name context.
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]]
    constexpr owning_result_type fullmatch(std::string_view text) const&&
    {
      return detach(run(text, detail::run_mode::full));
    }

    /*!
     * \brief `search` on a temporary regex; the result owns its name context.
     * \param[in] text The subject text (must outlive the result).
     * \return The match result.
     */
    [[nodiscard]]
    constexpr owning_result_type search(std::string_view text) const&&
    {
      return detach(run(text, detail::run_mode::search));
    }

    /*!
     * \brief Region-aware `match` on a temporary regex; the result owns its name context.
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the match must start at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The match result.
     */
    [[nodiscard]]
    constexpr owning_result_type match(std::string_view text,
                                       std::size_t      pos,
                                       std::size_t      endpos = npos) const&&
    {
      return detach(run(text, pos, endpos, detail::run_mode::prefix));
    }

    /*!
     * \brief Region-aware `fullmatch` on a temporary regex; the result owns its name context.
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the region starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The match result.
     */
    [[nodiscard]]
    constexpr owning_result_type fullmatch(std::string_view text,
                                           std::size_t      pos,
                                           std::size_t      endpos = npos) const&&
    {
      return detach(run(text, pos, endpos, detail::run_mode::full));
    }

    /*!
     * \brief Region-aware `search` on a temporary regex; the result owns its name context.
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The match result.
     */
    [[nodiscard]]
    constexpr owning_result_type search(std::string_view text,
                                        std::size_t      pos,
                                        std::size_t      endpos = npos) const&&
    {
      return detach(run(text, pos, endpos, detail::run_mode::search));
    }

    /*!
     * \brief `match` on a temporary regex, string-literal overload.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type match(const char* text) const&&
    {
      return std::move(*this).match(detail::c_string_subject(text));
    }

    /*!
     * \brief `fullmatch` on a temporary regex, string-literal overload.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type fullmatch(const char* text) const&&
    {
      return std::move(*this).fullmatch(detail::c_string_subject(text));
    }

    /*!
     * \brief `search` on a temporary regex, string-literal overload.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type search(const char* text) const&&
    {
      return std::move(*this).search(detail::c_string_subject(text));
    }

    /*!
     * \brief Region-aware `match` on a temporary regex, string-literal overload.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the match is anchored at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type match(const char* text,
                                       std::size_t pos,
                                       std::size_t endpos = npos) const&&
    {
      return std::move(*this).match(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Region-aware `fullmatch` on a temporary regex, string-literal overload.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the region starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type fullmatch(const char* text,
                                           std::size_t pos,
                                           std::size_t endpos = npos) const&&
    {
      return std::move(*this).fullmatch(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Region-aware `search` on a temporary regex, string-literal overload.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type search(const char* text,
                                        std::size_t pos,
                                        std::size_t endpos = npos) const&&
    {
      return std::move(*this).search(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Lazy range over all non-overlapping matches (Python `re.finditer`).
     *
     * Only callable on an lvalue regex: a C++20 range-for would dangle if the
     * regex were a temporary (the range initializer dies before the loop body),
     * so the rvalue overloads are deleted.
     *
     * \param[in] text The subject text (must outlive the range).
     * \return A \ref basic_match_range usable directly in a range-for.
     */
    // The iterator picks the trailing-lookaround route at run time; the return type cannot.
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
      return find_iter(detail::c_string_subject(text));
    }

    /*!
     * \brief Region-aware `find_iter` overload for string literals.
     * \param[in] text   NUL-terminated text (must outlive the range).
     * \param[in] pos    Byte offset iteration starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The range.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter(const char* text,
                                                                 std::size_t pos,
                                                                 std::size_t endpos = npos) const&
    {
      return find_iter(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Region-aware `find_iter`: iterate matches within `[pos, endpos)` (Python
     *        `finditer` with `pos` / `endpos`). \p endpos truncates the subject to a view
     *        so iteration stops at it; \p pos is the start, not a slice (see \ref run).
     *        Byte offsets; \p endpos defaults to the end of \p text.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset iteration starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return A range over the matches in the region.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter(std::string_view text,
                                                                 std::size_t      pos,
                                                                 std::size_t      endpos = npos) const&
    {
      const std::size_t end {endpos < text.size() ? endpos : text.size()};
      return {program_.view(), pattern(), text.substr(0, end), pos};
    }

    /*!
     * \brief Experimental leftmost-**longest** `find_iter` (POSIX bounds), the iterator twin of
     *        \ref search_longest. Region semantics as \ref find_iter; captures are the winning thread's,
     *        not POSIX submatch. Every fast path is bypassed.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset iteration starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return A range over the leftmost-longest matches in the region.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter_longest(std::string_view text,
                                                                         std::size_t      pos    = 0,
                                                                         std::size_t      endpos = npos) const&
    {
      const std::size_t end {endpos < text.size() ? endpos : text.size()};
      return {program_.view(), pattern(), text.substr(0, end), pos, match_semantics::longest};
    }

    /*!
     * \brief Region-aware `find_iter_longest` overload for string literals.
     * \param[in] text   NUL-terminated text (must outlive the range).
     * \param[in] pos    Byte offset iteration starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The range.
     */
    [[nodiscard]] constexpr basic_match_range<Storage> find_iter_longest(const char* text,
                                                                         std::size_t pos    = 0,
                                                                         std::size_t endpos = npos) const&
    {
      return find_iter_longest(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Deleted: the range borrows the subject, so a temporary `std::string` would dangle. Needs
     *        the `const char*` forwarder above, without which a bare literal would be ambiguous.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter_longest(const std::string &&, std::size_t = 0,
                                                               std::size_t = npos) const& = delete;

    /*!
     * \brief Deleted: `find_iter_longest` on a temporary regex would dangle, at every arity: without the
     *        same defaults, a shorter call would bind the `const&` overload.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter_longest(std::string_view, std::size_t = 0,
                                                               std::size_t = npos) const&& = delete;
    /*!
     * \brief Deleted: same, spelled for `const char*` so a literal resolves HERE rather than
     *        becoming ambiguous — the reader is told the REGEX is the temporary, which is true.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter_longest(const char*, std::size_t = 0,
                                                               std::size_t = npos) const&& = delete;

    /*!
     * \brief Deleted: `find_iter` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(std::string_view text) const&& = delete;
    /*!
     * \brief Deleted: `find_iter` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(const char* text) const&& = delete;
    /*!
     * \brief Deleted: region `find_iter` on a temporary regex would dangle. Spelled for `const char*`
     *        too, so a literal resolves here and the diagnostic names the regex as the temporary.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(const char* text, std::size_t,
                                                       std::size_t = npos) const&& = delete;
    /*!
     * \brief Deleted: region `find_iter` on a temporary regex would dangle.
     */
    [[nodiscard]] basic_match_range<Storage> find_iter(std::string_view text, std::size_t,
                                                       std::size_t = npos) const&& = delete;

    /*!
     * \brief Count non-overlapping matches without allocating result objects.
     *
     * Prefer this over walking \ref find_iter when only the count is needed.
     * Region semantics match \ref find_iter -- \p pos is a start offset, not a
     * slice (`\\A` / `^` still see the absolute position).
     *
     * \param[in] text   The subject text.
     * \param[in] pos    Byte offset to begin counting from (0 = start of text).
     * \param[in] endpos Exclusive end of the region; \ref npos = end of text.
     * \return The number of non-overlapping matches in the region.
     */
    // Matching-only walk; the trailing-lookaround one is a separate cold instantiation.
    [[nodiscard]] constexpr std::size_t count_matches(std::string_view text,
                                                      std::size_t      pos    = 0,
                                                      std::size_t      endpos = npos) const
    {
      const std::size_t      end    {endpos < text.size() ? endpos : text.size()};
      const std::string_view region {text.substr(0, end)};
      if constexpr (requires(typename Storage::state_type & st) {
        st.lookaround;
      }) {
        const auto& prog {program_.view()};
        if (prog.hints.trailing_lookaround >= 0
            && (std::is_constant_evaluated() || !detail::trailing_la_route_disabled())) {
          return count_trailing_la(region, pos);
        }
      }
      return count_walk(text, pos, endpos);
    }

    /*!
     * \brief All matches, eagerly (like Python `re.findall`, but full results).
     *
     * Lvalue-only, same reason as \ref find_iter. High match counts allocate
     * one result per hit; prefer \ref count_matches when only the number
     * matters, and \ref find_iter when you can stream.
     *
     * \param[in] text The subject text (must outlive the results).
     * \return A vector of match results.
     */
    [[nodiscard]] constexpr std::vector<result_type> find_all(std::string_view text) const&
    {
      std::vector<result_type> result;
      // Same dispatch as count_matches.
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
      return find_all(detail::c_string_subject(text));
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
     * \brief Replaces matches in \p text (ECMAScript / `std::regex_replace` `$1`).
     *
     * The \p replacement may reference groups: `$$` → '$', `$&` or `$0` →
     * whole match, `$1` …, and `${name}`. This is not Python `re.sub` (`\1` /
     * `\g<name>`) — that spelling is the Python and Go bindings. Returns an
     * owning string, so a temporary \p text is fine here.
     *
     * \param[in] text        The subject text.
     * \param[in] replacement The replacement template.
     * \param[in] max_count   Maximum replacements (0 = all).
     * \return The resulting string.
     * \throws real::regex_error on a malformed or out-of-range group reference in \p replacement, whether or
     *         not anything matches (as Python's `re.sub`): the template is read once, before the walk.
     */
    [[nodiscard]] constexpr std::string replace(std::string_view text,
                                                std::string_view replacement,
                                                std::size_t      max_count = 0) const
    {
      const std::vector<replacement_piece> pieces {parse_replacement(replacement)};
      std::string                          result;
      std::size_t                          last   {};
      std::size_t                          done   {};
      // The cap is tested after a replacement, not before the next: tested first, the walk had already searched
      // for one match past the cap, maybe the whole rest of the subject.
      for (const result_type& match : find_iter(text)) {
        result.append(text.substr(last, match.start() - last));
        for (const replacement_piece& piece : pieces) {
          if (piece.group == npos) {
            // One character pushed, not a one-byte slice appended: GCC pays the slice's setup per match.
            if (piece.literal.size() == 1) {
              result.push_back(piece.literal.front());
            }
            else {
              result.append(piece.literal);
            }
          }
          else {
            result.append(match[piece.group]);
          }
        }
        last = match.end();
        if (++done == max_count) {
          break;
        }
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
      for (const result_type& match : find_iter(text)) { // the cap after a split: see replace
        result.push_back(text.substr(last, match.start() - last));
        for (std::size_t group = 1; group < match.size(); ++group) {
          result.push_back(match[group]);
        }
        last = match.end();
        if (++done == max_splits) {
          break;
        }
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
      return split(detail::c_string_subject(text), max_splits);
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
     * \return The pattern, valid as long as this regex is alive.
     */
    [[nodiscard]] constexpr std::string_view pattern() const
    {
      return program_.pattern();
    }

    /*!
     * \brief The flag set in force: constructor flags, plus a leading `(?imsxa)`
     *        group, minus its `-removal`.
     *
     * `regex("(?-i)a", flags::icase)` reports no \ref flags::icase and matches
     * case-sensitively — the accessor and the engine agree.
     *
     * \return The effective flag set.
     */
    [[nodiscard]] constexpr flags compile_flags() const
    {
      return program_.compiled_flags();
    }

    /*!
     * \brief Returns the number of capturing groups (excluding group 0).
     * \return The capturing-group count.
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
     * \warning An advanced, unstable extension point for bindings and embedders: \ref detail::program_view is
     *          an implementation type, and its members may change in any release. Code that only matches has no
     *          use for it.
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
      result.reserve(program_.view().names.size());
      for (const detail::named_group& named_group : program_.view().names) {
        result.emplace_back(name_of(named_group), static_cast<std::size_t>(named_group.group));
      }
      return result;
    }

    /*!
     * \brief How many named groups the pattern declares.
     *
     * With \ref named_group_at, the allocation-free way to enumerate them (\ref named_groups builds a
     * `vector` per call). A constant factor, not a complexity fix: resolving N names through a
     * number-keyed interface is N scans of N either way.
     *
     * \return The number of named groups.
     */
    [[nodiscard]] constexpr std::size_t named_group_count() const
    {
      return program_.view().names.size();
    }

    /*!
     * \brief The \p index-th named group, in declaration order.
     * \param[in] index Position in `[0, named_group_count())`; out of range is undefined, as for any
     *                  indexed accessor on this class.
     * \return Its name (a view into the pattern text) and its capture-group number.
     */
    [[nodiscard]] constexpr std::pair<std::string_view, std::size_t> named_group_at(std::size_t index) const
    {
      // By value: a reference into the prvalue view()'s span trips gcc's -Wdangling-reference (a false
      // positive); the copy is three int32s.
      const detail::named_group named_group {program_.view().names[index]};
      return {name_of(named_group), static_cast<std::size_t>(named_group.group)};
    }

  private:

    // Private: `count_matches` is the surface; nothing outside picks a walk.
    /*!
     * \brief \ref count_matches's walk: the ordinary one, run matching-only.
     *
     * No caller can observe a group, so the walk runs with \ref detail::pattern_hints::capture_free_walk set:
     * the VM steps the same positions with no capture bookkeeping. The structural half is still asked
     * (\ref detail::capture_free_walk_structural), since a skippable `save 0` gives a wrong answer.
     * `slot_count` stays as is: the batched routes arm on `slot_count == 2`, so lowering it would reroute.
     * The range sets the flag, keeping one `program_view` copy. Outlined, so `count_matches` carries no
     * branch for it: one there charged byte-identical rows up to 10.7 %. `noinline`, not `cold`: this is
     * the ordinary path.
     *
     * \param[in] text   The subject.
     * \param[in] pos    Where the walk starts.
     * \param[in] endpos Region end, as \ref find_iter takes it.
     * \return The match count.
     */
    [[nodiscard]]
    REAL_NOINLINE
    constexpr std::size_t count_walk(std::string_view text,
                                     std::size_t      pos,
                                     std::size_t      endpos) const
    {
      const std::size_t end {endpos < text.size() ? endpos : text.size()};
      // Not a range-for: its `end()` builds a sentinel with a full `state_type` only to compare against, a
      // large share of a short call. find_iter keeps paying it: a lazy state (`std::optional`, a
      // `construct_at` union) cost the working iterator ~26 %, and a distinct sentinel type would break the C
      // binding's `real_iter` and the homogeneous `std::` algorithms.
      basic_match_range<Storage> range {program_.view(), pattern(), text.substr(0, end),
                                        pos,            match_semantics::first, true};
      return range.begin().count_rest();
    }

    /*!
     * \brief \ref count_matches over the trailing-lookaround walk, outlined.
     *
     * Outlined and cold, like \ref basic_match_iterator::decide_batching. Inline, a second walk inside
     * `count_matches` left it on a codegen cliff where one unrelated branch charged byte-identical rows up
     * to 10.7 %.
     *
     * \param[in] region The already-clamped subject.
     * \param[in] pos    Where the walk starts.
     * \return The match count.
     */
    [[nodiscard]]
    REAL_COLD
    constexpr std::size_t count_trailing_la(std::string_view region,
                                            std::size_t      pos) const
    {
      std::size_t n {};
      for (const result_type& match :
           basic_match_range<Storage, /*TrailingLA=*/ true> {program_.view(), pattern(), region, pos}) {
        (void) match;
        ++n;
      }
      return n;
    }

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
     * \brief One piece of a parsed replacement template: a slice of the template, or a group to copy.
     */
    struct replacement_piece
    {
      std::string_view literal;        //!< The template's slice to copy, when \ref group is \ref npos.
      std::size_t      group   {npos}; //!< The group to copy, or \ref npos for the literal slice.
    };

    /*!
     * \brief Reads \p replacement once into literal slices and group references.
     *
     * An invalid or out-of-range reference is an error (Python's rule), not left in place, and is reported
     * whatever the subject. The template spelling is ECMAScript / `std::regex`: `$1`, not `\1`.
     *
     * \param[in] replacement The replacement template (`$$`, `$&`, `$1`, `${name}`).
     * \return The pieces, in order.
     * \throws real::regex_error on a malformed or out-of-range reference.
     */
    [[nodiscard]] constexpr std::vector<replacement_piece> parse_replacement(std::string_view replacement) const
    {
      std::vector<replacement_piece> pieces;
      const std::size_t              groups {group_count() + 1};
      std::size_t                    i      {};
      while (i < replacement.size()) {
        if (replacement[i] != '$') {
          const std::size_t begin {i};
          while (i < replacement.size() && replacement[i] != '$') {
            ++i;
          }
          pieces.push_back({.literal = replacement.substr(begin, i - begin)});
          continue;
        }
        ++i;
        if (i >= replacement.size()) {
          throw regex_error("dangling $ in replacement", i - 1);
        }
        const char next_ch {replacement[i]};
        if (next_ch == '$') {
          pieces.push_back({.literal = replacement.substr(i, 1)});
          ++i;
        }
        else if (next_ch == '&') {
          pieces.push_back({.literal = {}, .group = 0});
          ++i;
        }
        else if (next_ch >= '0' && next_ch <= '9') {
          std::size_t group {};
          while (i < replacement.size() && replacement[i] >= '0' && replacement[i] <= '9') {
            // Unguarded, a wrapped value lands back in range (`$18446744073709551616` reads as group 0).
            if (group > (npos - 9) / 10) {
              throw regex_error("invalid group reference in replacement", i);
            }
            group = (group * 10) + static_cast<std::size_t>(replacement[i] - '0');
            ++i;
          }
          if (group >= groups) {
            throw regex_error("invalid group reference in replacement", i);
          }
          pieces.push_back({.literal = {}, .group = group});
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
          const std::size_t group {group_index(replacement.substr(name_begin, j - name_begin))};
          if (group == npos) {
            throw regex_error("unknown group name in replacement", i);
          }
          pieces.push_back({.literal = {}, .group = group});
          i                          = j + 1;
        }
        else {
          throw regex_error("invalid $ escape in replacement", i);
        }
      }
      return pieces;
    }

    /*!
     * \brief Hands a result the name context it will need after this regex is gone.
     *
     * Taken and returned by value so the prvalue from \ref run is constructed straight into the
     * parameter and named-returned out: the rvalue overloads pay no move for going through here.
     *
     * \param[in] result The freshly run result.
     * \return The same result, no longer borrowing from this regex.
     */
    [[nodiscard]]
    static constexpr owning_result_type detach(result_type result)
    {
      // Same type wherever there is nothing to own (the compile-time policy): just hand it back.
      if constexpr (std::is_same_v<owning_result_type, result_type>) {
        return result;
      }
      else {
        owning_result_type owning {std::move(result)};
        owning.detach_from_regex();
        return owning;
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
      // An inverted region holds no match, not even an empty one (as in `re`). Required, not an
      // optimisation: past the truncated subject a `substr` deep in the VM throws std::out_of_range.
      if (pos > end) {
        return result_type {};
      }
      // Fresh per search: construction is a fifth to a quarter of a short search, with no single member to
      // make lazy. Reuse is safe only per regex per thread (a state shared by two patterns is a
      // heap-use-after-free through run_class_loop's class table), and a lookup keyed that way costs more
      // than it saves; find_iter already keeps one state per walk.
      typename Storage::state_type   state;
      // Reference, not a copy (`program_view` is 432 bytes); a prvalue view() is lifetime-extended.
      const detail::program_view&    prog    {program_.view()};
      // Built before the engine runs, which fills its slots in place; after `prog`, whose names it borrows.
      result_type                    out     {text, pattern(), prog.names};
      // `state` is fresh for `prog` alone, so the VM may skip its program-identity compare.
      detail::pike_vm<typename Storage::state_type, true> vm(prog, state);
      const bool                                          matched {detail::run_attempt(vm, prog, text.substr(0, end), pos, mode, out.engine_slots(), sem)};
      // One return statement: the result is NRVO-constructed in the caller and filled in place, sparing a
      // block move whose fixed startup is the whole cost on a groupless pattern. A small-count loop in
      // `transfer_range` instead regressed a dozen rows (it also serves the thread lists' hot path).
      out.engine_set_matched(matched);
      return out;
    }

    friend struct detail::non_empty_access;

    /*!
     * \brief \ref run that accepts no empty match: the leftmost position where a non-empty match starts, and there
     *        the match the leftmost-first priority prefers among the non-empty ones (`match_not_null`). The DFAs do
     *        not model the rule, so the VM decides the search; it stays linear. Kept apart from \ref run, whose
     *        every caller is a hot path.
     * \param[in] text The subject (must outlive the result).
     * \param[in] pos  Byte offset to start at.
     * \param[in] mode Search, or anchored at \p pos (prefix).
     * \return The match result, with offsets absolute in \p text.
     */
    [[nodiscard]] result_type run_non_empty(std::string_view text,
                                            std::size_t      pos,
                                            detail::run_mode mode) const
    {
      if (pos > text.size()) {
        return result_type {};
      }
      typename Storage::state_type                        state;
      const detail::program_view&                         prog    {program_.view()};
      result_type                                         out     {text, pattern(), prog.names};
      detail::pike_vm<typename Storage::state_type, true> vm(prog, state);
      const std::size_t                                   nowhere {text.size() + 1}; // empty refused everywhere
      const bool                                          matched {
        prog.hints.stop_set_size >= 1
          ? vm.template run<true>(text, pos, mode, out.engine_slots(), nowhere, match_semantics::first)
          : vm.template run<false>(text, pos, mode, out.engine_slots(), nowhere, match_semantics::first)};
      out.engine_set_matched(matched);
      return out;
    }

  public:

    /*!
     * \brief EXPERIMENTAL, opt-in: a single leftmost-**longest** search (POSIX / RE2 `set_longest_match`).
     *        Among matches at the leftmost start it returns the longest, so a lazy quantifier behaves greedily;
     *        captures are the leftmost-first thread's at that bound (not POSIX submatch). Runs on the general
     *        Pike loop. Not yet a stable API; its iteration twin is \ref find_iter_longest.
     *
     * \param[in] text Subject.
     * \return The leftmost-longest match; falsy when there is none.
     */
    [[nodiscard]] result_type search_longest(std::string_view text) const&
    {
      return run(text, 0, npos, detail::run_mode::search, match_semantics::longest);
    }

    /*!
     * \brief `search_longest` on a temporary regex; the result owns its name context.
     *
     * Without it, a lookup by name would read the temporary's freed pattern and name table.
     *
     * \param[in] text The subject text (must outlive the result).
     * \return The leftmost-longest match, owning its name context.
     */
    [[nodiscard]] owning_result_type search_longest(std::string_view text) const&&
    {
      return detach(run(text, 0, npos, detail::run_mode::search, match_semantics::longest));
    }

    /*!
     * \brief Region-aware form of \ref search_longest — leftmost-longest search within `[pos, endpos)`. \p pos is the
     *        start (not a slice, per \ref run); \p endpos truncates the subject. Byte offsets.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The leftmost-longest match in the region; falsy when there is none.
     */
    [[nodiscard]] result_type search_longest(std::string_view text,
                                             std::size_t      pos,
                                             std::size_t      endpos = npos) const&
    {
      return run(text, pos, endpos, detail::run_mode::search, match_semantics::longest);
    }

    /*!
     * \brief Region-aware `search_longest` on a temporary regex; the result owns its name context.
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The leftmost-longest match in the region, owning its name context.
     */
    [[nodiscard]] owning_result_type search_longest(std::string_view text,
                                                    std::size_t      pos,
                                                    std::size_t      endpos = npos) const&&
    {
      return detach(run(text, pos, endpos, detail::run_mode::search, match_semantics::longest));
    }

    /*!
     * \brief `search_longest` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The leftmost-longest match.
     */
    [[nodiscard]] result_type search_longest(const char* text) const&
    {
      return search_longest(detail::c_string_subject(text));
    }

    /*!
     * \brief `search_longest` on a temporary regex, string-literal overload.
     *
     * Needs its own `const&&`: otherwise a literal on a temporary regex binds the borrowing `const&`
     * overload and detaches nothing.
     *
     * \param[in] text NUL-terminated text.
     * \return The leftmost-longest match, owning its name context.
     */
    [[nodiscard]] owning_result_type search_longest(const char* text) const&&
    {
      return std::move(*this).search_longest(detail::c_string_subject(text));
    }

    /*!
     * \brief Region-aware `search_longest` overload for string literals.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The leftmost-longest match in the region.
     */
    [[nodiscard]] result_type search_longest(const char* text,
                                             std::size_t pos,
                                             std::size_t endpos = npos) const&
    {
      return search_longest(detail::c_string_subject(text), pos, endpos);
    }

    /*!
     * \brief Region-aware `search_longest` on a temporary regex, string-literal overload.
     * \param[in] text   NUL-terminated text.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The leftmost-longest match in the region, owning its name context.
     */
    [[nodiscard]] owning_result_type search_longest(const char* text,
                                                    std::size_t pos,
                                                    std::size_t endpos = npos) const&&
    {
      return std::move(*this).search_longest(detail::c_string_subject(text), pos, endpos);
    }

    // The searched text must outlive the result; the `const char*` forwarders keep a literal unambiguous.
    [[nodiscard]] result_type search_longest(const std::string&&) const = delete; //!< Deleted: temporary text would dangle.
    [[nodiscard]] result_type search_longest(const std::string &&, std::size_t,
                                             std::size_t = npos) const = delete;  //!< Deleted: temporary text would dangle.
  };

  /*!
   * \brief The runtime-compiled regex type — the primary entry point.
   */
  using regex = basic_regex<detail::dynamic_storage>;

  namespace detail {
    /*!
     * \brief The searches that accept no empty match (`std::regex_constants::match_not_null`), for the std drop-in;
     *        not part of REAL's own interface.
     */
    struct non_empty_access
    {
      /*!
       * \brief Leftmost search from \p pos that accepts no empty match.
       * \tparam Storage The regex's storage policy.
       * \param[in] re   The pattern.
       * \param[in] text The subject (must outlive the result).
       * \param[in] pos  Byte offset to start at; the text before it is context.
       * \return The match, offsets absolute in \p text.
       */
      template <typename Storage>
      [[nodiscard]] static auto search(const basic_regex<Storage>& re,
                                       std::string_view            text,
                                       std::size_t                 pos)
      {
        return re.run_non_empty(text, pos, run_mode::search);
      }

      /*!
       * \brief Match anchored at \p pos that accepts no empty match.
       * \tparam Storage The regex's storage policy.
       * \param[in] re   The pattern.
       * \param[in] text The subject (must outlive the result).
       * \param[in] pos  Byte offset the match starts at; the text before it is context.
       * \return The match, offsets absolute in \p text.
       */
      template <typename Storage>
      [[nodiscard]] static auto match(const basic_regex<Storage>& re,
                                      std::string_view            text,
                                      std::size_t                 pos)
      {
        return re.run_non_empty(text, pos, run_mode::prefix);
      }
    };
  } // namespace detail

  /*!
   * \brief The result type of the default, runtime-compiled \ref real::regex.
   *
   * Derived, not re-spelled, so it cannot drift from what `real::regex` returns.
   */
  using match_result = regex::result_type;

  /*!
   * \brief What a single attempt on a TEMPORARY \ref real::regex returns, owning its name context
   *        rather than borrowing it (see \ref real::basic_regex::owning_result_type).
   */
  using owning_match_result = regex::owning_result_type;

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
