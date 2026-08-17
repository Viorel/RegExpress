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

/*!
 * \brief REAL's public API: \ref real::regex, \ref real::static_regex, \ref real::flags and the
 *        match/iterator types built on them.
 */
namespace real {

  /*!
   * \brief The result of a match attempt: success, spans and captures.
   *
   * Group views point into the searched text, which must outlive the result —
   * the rvalue `std::string` overloads on the regex are deleted so the common
   * dangling mistake is a compile error.
   *
   * Named-group lookups need the regex's pattern text and name table. A result
   * from a live regex borrows both. A result from a temporary regex is
   * \ref real::basic_regex::owning_result_type — the same class, owning those
   * tables — so names still resolve after the regex dies. `find_iter` and
   * `find_all` have no such twin: they are deleted on an rvalue regex.
   *
   * \tparam SlotStorage The capture-slot container (vector- or static-backed),
   *         supplied by the storage policy.
   * \tparam NameOwner   How name tables are held: borrowed from a live regex,
   *         or owned after a temporary regex dies.
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
    // \p slots is an RVALUE REFERENCE, not a by-value parameter, and the difference is measured. By
    // value, the single caller's `std::move` constructs the parameter (one move) and the member is then
    // move-constructed from it (a second). Those two lower to a pair of block moves, each paying a fixed
    // startup whatever the volume -- and for a groupless pattern the volume is a couple of words, so the
    // startup IS the cost. The reference removes one of the two outright.
    // The only caller is \ref basic_regex::run, which always passes a local it is done with.
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
     * A template, and constrained, so that it exists only for the owning specialisation -- the
     * borrowing one is what every walk and every attempt on a live regex yields, and it must stay
     * exactly the type it was.
     *
     * Taken by value: the caller hands over a prvalue, so nothing is copied, and the slots move out
     * of the parameter rather than out of a subobject of a reference.
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
     * Paired with \ref engine_slots and \ref engine_set_matched, this is what lets
     * \ref basic_regex::run hand the engine the FINAL slot storage instead of filling a local and
     * moving it in. The move it removes was the last bulk copy per call: gcc lowered it to a
     * `rep movsq` worth **12.02 % of the inlined per-call path** (`small_vec::transfer_range<true>`
     * through `transfer_inline_from`), and `rep movs` pays a fixed startup whatever the volume — which
     * for a groupless pattern is sixteen bytes. `run()` has a single return statement, so the result is
     * NRVO-constructed in the caller's storage and the engine writes straight into it.
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
     * `search`, `match` and `fullmatch` are callable on a temporary regex, and must stay so -- the
     * one-expression form (`real::regex{"a+"}.search(text).matched()`) is safe, since the temporary
     * outlives the full-expression, and it is the shape most callers write. What is NOT safe is the
     * result outliving that temporary: \ref group_index reads the pattern text and the named-group
     * table, both of which the regex owns. This copies them into the result instead, so a result
     * from an rvalue regex resolves names correctly for as long as it exists.
     *
     * The borrowed views are cleared either way: with named groups \ref owner_ becomes the source of
     * truth (which is what keeps the implicit copy constructor correct -- a copy deep-copies the box
     * and reads its OWN, rather than inheriting views into the original's), and without them there
     * is nothing to copy, but a dangling view nobody dereferences would still be one.
     *
     * A no-op on the compile-time policy, whose tables have static storage duration.
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
     * \brief Engine-internal: re-run the search into this result's OWN slot buffer, reusing its
     *        capacity. Not part of the public API.
     *
     * The match iterator holds one result and refreshes it in place each step. `vm.run` fills the slots
     * via `assign`, which reuses the existing capacity, so a match-dense iteration allocates the slot
     * vector once instead of once per match — the measured per-match cost (a fresh allocation is ~5x a
     * reused one). A user-held copy of a previous match stays independent: copying a result deep-copies
     * its slots, so refilling this one never disturbs it.
     *
     * \tparam Cascade Select the memchr-cascade class-run variant (chosen once per walk).
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

    /*!
     * \internal
     * \brief Binds the invariant context (subject, pattern, named groups) once. For an iterator that refills
     *        the same result many times, these never change within a walk — set them here, not per match.
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
     * \brief Per-match refill for an iterator whose context is already bound via \ref bind_context runs the
     *        VM and records only the outcome (the invariant fields are already set), the find_iter hot path.
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
     * \brief Cold path for TrailingLA walks only (never referenced from pure walks).
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
     * For a caller that copies every group in one pass (the C ABI's `spans`
     * buffer is this layout). Only meaningful on a matched result — an unmatched
     * one carries whatever the last fill left. Prefer \ref start / \ref end when
     * reading a single group; those return \ref real::npos if it did not
     * participate. Copy pairwise (`spans[2g]`, `spans[2g+1]`).
     *
     * \return A view of `2 * size()` slot values; empty when there are no slots.
     */
    // The C shim fills its buffer by walking this view instead of calling
    // start/end per group (each re-tests matched() and the bound). memcpy of a
    // runtime length is a libc call that costs more than the stores for the
    // 1–2 slot shapes that dominate. Measured on the C shim: `\b\w+\b`
    // −20.7 % per match, `[a-z]+` −7.1 %, multi-group patterns neutral.
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
        // decided ONCE per walk, never per match. Longest forces the non-cascade variant: like every other
        // fast path, the memchr-cascade class-run is guarded to first mode (the PROTO-kLongest lesson), so a
        // longest walk must run the general loop.
        cascade_(sem == match_semantics::first && prog.hints.stop_set_size >= 1),
        sem_(sem)
    {
      // Batching eligibility, decided ONCE per walk like cascade_ above. Every exclusion here names a
      // shape whose per-match bookkeeping fill_cp_class_spans does not reproduce: leftmost-longest
      // (different answer), a `\b`/`\B` wrap or the maximal-run window guard (assertions checked per
      // candidate), a `{k,}` minimum (a too-short run is skipped, not matched), and the trailing-
      // lookaround walk (its own once-per-walk route). Constant evaluation stays on the general path,
      // where the route seams are honoured as written.
      if constexpr (TrailingLA) {
        // This specialization IS the trailing-lookaround walk, so it sets the same flag the pure walk
        // computes -- one mechanism, not two. Before, the choice was a template parameter here and a
        // runtime flag there, with byte-identical branch bodies (clang-tidy's bugprone-branch-clone saw
        // it before a reader would).
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
    // OUTLINED AND COLD, and both are load-bearing rather than tidy. This is the constructor's cold
    // half -- it runs once per walk, never per match -- but `count_matches` inlines the constructor, so
    // as constructor-body code it was absorbed into the hot measurement loop. The consequence was
    // measured: appending ONE route to the dispatch left every scan filler byte-identical (398 function
    // bodies compared, five changed, none of them a scan loop) yet moved `single [a-z]` +10.7 %,
    // `\b\w+\b` +4.0 %, `\w+` +3.7 %, `\w{2,}` +3.2 % and `fields [^,]+` +2.8 %, each above its own
    // calibrated floor at 24 of 24 paired draws. The two functions that changed were this constructor
    // and `count_matches` -- so the toll for touching the dispatch was paid by rows whose own code never
    // moved. Behind a call the compiler does not inline, the eligibility logic can grow without
    // recompiling what runs per match.
    // The lazy-DFA route declines under pike.hpp's `lazy_dfa_min_input` bytes of runway, so on a short
    // subject its filler can only fail -- once per match, for nothing. That cost is measured:
    // `short trim replace` +9.7 % [+7.4, +12.9] at 24 of 24 draws against a 4.5 % floor.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
    constexpr void decide_batching(detail::program_view prog,
                                   match_semantics      sem,
                                   std::size_t          text_bytes)
    {
      // Decided here rather than in the constructor body on purpose: this function is already outlined and
      // cold, so the flag costs the constructor nothing (verified — the constructor's body is unchanged).
      if (!std::is_constant_evaluated() && prog.hints.trailing_lookaround >= 0
          && !detail::trailing_la_route_disabled()) {
        trailing_la_walk_ = true;
      }
      // An ANCHORED shape is excluded, and the exclusion is load-bearing rather than tidy: the
      // fillers scan forward, `run()` is where `\A`/`^` is turned into prefix anchoring, and a
      // batched walk bypasses `run()` entirely. Without this, `^[a-z]+` over "  abc" reports a
      // match at offset 2. It costs nothing to give up: an anchored pattern yields at most one
      // match per walk, so there is no per-match return to amortise.
      // The prerequisites EVERY batched route shares, named once. They were written out four times,
      // identically, and the copies were not merely long: a shape excluded from one route and not
      // the next would have been a silent wrong answer, not a slow one. Nothing here is hot — this
      // is the constructor, once per walk, never per match.
      // DECLARED then ASSIGNED, which is required rather than stylistic: as a `const bool`
      // INITIALIZER this expression is manifestly constant-evaluated, and clang rejects
      // `std::is_constant_evaluated()` there under -Werror=constant-evaluated ("will always
      // evaluate to true"). The two-step form is not manifestly constant-evaluated and keeps the
      // runtime/constexpr split the original four copies had.
      bool batchable {};
      batchable = !std::is_constant_evaluated() && sem == match_semantics::first
                  && !detail::class_fastpath_disabled()
                  && !prog.hints.anchored_start && !prog.hints.line_anchored;
      // A KEPT `\b`/`\B` wrap is handled by the BYTE class filler and by nothing else, so the other
      // routes still require its absence. The assertion is one a word-SUBSET class genuinely needs: a
      // maximal `[a-z]+` run can start after `_` or a digit, so unlike `\b\w+\b`'s this one is not
      // redundant, cannot be dropped at recognition time, and has to be evaluated on every span.
      const bool no_wrap {prog.hints.wb_lead == 0 && prog.hints.wb_trail == 0};
      wb_kept_ = !no_wrap;
      // A DROPPED leading `\b` (\ref real::detail::pattern_hints::wb_lead_maximal_run) does not
      // disqualify the two class-run routes: their fillers carry the same one-position window-edge
      // guard the general route does. Without it, a pattern the recognizer had already proved the
      // assertion redundant for -- everywhere except at a caller-supplied `pos` -- lost its route
      // entirely. The other two routes have not been taught the guard and still decline.
      // Each route then adds only its OWN selector, which is what the four lines below now read as.
      wb_edge_         = prog.hints.wb_lead_maximal_run;
      // A `{k,}` minimum does not disqualify either class-run route: the fillers apply the same "a
      // too-short maximal run cannot satisfy X{k,}, skip it" rule the general route does. Declining
      // instead costs the pattern its route -- for a length comparison.
      batch_bytes_     = batchable && prog.hints.greedy_class_loop >= 0
                         && prog.hints.greedy_class_loop_end == 0;
      batch_cp_ascii_  = batchable && no_wrap && !prog.hints.wb_lead_maximal_run
                         && prog.hints.codepoint_class_ascii >= 0
                         && prog.hints.greedy_class_loop < 0 && prog.hints.greedy_cp_class < 0;
      // The bare single byte-class (`[a-z]`, no quantifier) needs no `{k,}` or capture exclusion:
      // the 4-opcode shape pattern_hints::single_class recognizes admits neither.
      batch_single_cl_ = batchable && no_wrap && !prog.hints.wb_lead_maximal_run
                         && prog.hints.single_class >= 0;
      // The code-point class loop is the fourth batched route and deliberately gets NO member of its
      // own: it is \ref refill_batch's `else`, so naming it would grow the iterator for nothing —
      // and this iterator's size is measured, not assumed (see \ref batch_cap).
      // BOTH class routes now accept a `{k,}` minimum. The code-point one had been declined twice on
      // a cost measured in the four-engine harness that does not exist in a consumer-shaped
      // translation unit -- see fill_cp_class_spans's note and docs/MEASUREMENT.md §5.5.
      const bool cp_class {batchable && no_wrap && prog.hints.greedy_cp_class >= 0
                           && prog.hints.greedy_cp_class_end == 0
      }; // no is_constant_evaluated: see above

      // The fixed ALTERNATION, small-set shape only (2..8 distinct branch first bytes -- what the
      // filler's mask scan needs; outside that range run_alternation takes a different scan and this
      // route declines rather than growing a second body there). Its cost is almost entirely per MATCH
      // rather than per byte, which is what makes batching it worth a route at all.
      // fixed_alternation already excludes captures and asserts by construction, so slot_count is 2.
      // Branch count BELOW the Aho-Corasick floor, and that bound is the load-bearing one. The AC
      // gate is consulted inside `run()`, which a batched walk bypasses entirely, so batching a
      // shape the gate could claim would silently overrule a routing decision that was measured --
      // and the gate takes the automaton below twelve branches too when the subject is dense enough
      // (tests/engine/test_ac_density_gate.cpp pins exactly that). Under four branches the automaton
      // is never considered at all, so nothing is overruled. That subset is also the common one and
      // contains §A's own `alt the|fox|dog` row. Batching the AC route is a separate piece of work,
      // not a widening of this condition.
      batch_alt_       = batchable && no_wrap && prog.hints.fixed_alternation
                         && prog.hints.alternation_branch_count > 0
                         && prog.hints.alternation_branch_count < 4
                         && prog.hints.small_set_size >= 2 && prog.hints.small_set_size <= 8
                         && prog.hints.greedy_class_loop < 0 && prog.hints.greedy_cp_class < 0
                         && prog.hints.codepoint_class_ascii < 0 && prog.hints.single_class < 0;
      // The LAZY-DFA route, last in the cascade because every shape above it is faster: this is what a
      // pattern falls to when no recognizer claims it, and it was the only route with no filler at all
      // -- 0.9949 engine entries per match against 0.2501 for every batched route (see
      // fill_lazy_dfa_spans for the two-density fit that says the return, not the scan, is the cost).
      // The conditions MIRROR the route's own gate in `run()` rather than inventing a set, because a
      // batched walk bypasses that gate entirely and any divergence is a wrong answer, not a slow one:
      //   * `first_bytes_valid` -- the filler carries only the anchored-from-candidate sub-scan.
      //   * `slot_count <= 2`   -- the route writes slots directly only there; with captures it calls
      //                            run_general per match, which IS the cost this would amortise.
      //   * not nullable        -- a zero-width match needs `forbid_empty_until_`, and the batched span
      //                            path does not apply it. The route's own gate refuses to run once
      //                            that is non-zero; this takes the same exclusion one step earlier.
      //   * the seam            -- `lazy_dfa_route_disabled()` must take this out with the route.
      // Nothing is required of the OTHER routes' hints beyond their flags being clear: this arms only
      // where none of them did, so any shape they claim keeps its faster filler.
      //   * NOT a shape the Aho-Corasick gate could claim, and this one is load-bearing exactly as it is
      //     for `batch_alt_`: that gate is consulted INSIDE `run()`, per search, on the subject's own
      //     density, so a batched walk silently overrules a routing decision that was measured -- and
      //     tests/engine/test_ac_density_gate.cpp reads the verdict, so it reports `not_consulted`
      //     rather than a wrong answer. Same bound as there: below the branch floor the automaton is
      //     never considered at all.
      //   * NOT a trailing-lookaround shape. That route is chosen by \ref trailing_la_walk_ and lives on
      //     `advance`'s per-match path, which the batched path preempts; the two cannot both own the
      //     walk. `[a-z]+(?=[a-z])` and `[0-9]+(?![0-9])` diverged across the enumerating surfaces
      //     until this line existed (make route-surface-parity).
      //   * the route must be the one `run()` would TAKE -- pike.hpp's `lazy_dfa_is_the_route`,
      //     which states one condition per route sitting above it in the cascade. Stated as a residue
      //     instead ("whatever the four recognizers left"), this charged `literal charlie` +81.1 %: a
      //     plain literal has neither a class loop nor a fixed alternation, so it fell through here and
      //     lost its memmem. The predicate lives beside the cascade it mirrors, not here.
      //   * enough RUNWAY. The route declines under `lazy_dfa_min_input` bytes, so on a shorter subject
      //     the filler can only fail -- once per match, for nothing: `short trim replace` +9.7 %.
      batch_lazy_dfa_  = batchable && no_wrap && !prog.hints.wb_lead_maximal_run
                         && !detail::lazy_dfa_route_disabled()
                         && prog.hints.first_bytes_valid && !prog.hints.empty_match_possible
                         && prog.slot_count <= 2
                         && prog.hints.alternation_branch_count < 4
                         && prog.hints.trailing_lookaround < 0
                         && detail::pike_vm<typename Storage::state_type, true>::lazy_dfa_is_the_route(prog.hints)
                         && text_bytes >= detail::pike_vm<typename Storage::state_type, true>::lazy_dfa_min_input
                         && !batch_bytes_ && !batch_cp_ascii_ && !batch_single_cl_ && !cp_class
                         && !batch_alt_;
      // The EXACT-LITERAL route, sixth, and a REOPENED REFUSAL rather than a new idea -- %pike.hpp's
      // `fill_exact_literal_spans` carries the whole record, including the five rows the first attempt
      // charged and the machine-code mechanism that was blamed. What reopens it is the fifth route above:
      // it enlarged `refill_batch` too and charged nothing measurable, so the law the refusal rested on
      // does not hold as stated. If the +10.7 % reproduces, this line goes and the second refutation is
      // recorded with it.
      batch_exact_lit_ = batchable && no_wrap && !prog.hints.wb_lead_maximal_run
                         && prog.slot_count == 2
                         && detail::pike_vm<typename Storage::state_type, true>::exact_literal_is_the_route(prog.hints);
      // The INNER-LITERAL route, seventh. Same signature as the two above it -- one engine entry per
      // match, a per-match constant flat across densities -- and the same arming
      // discipline: %pike.hpp's `inner_literal_is_the_route` states one clause per route above it in the
      // cascade. `slot_count == 2` is what lets the filler use a two-slot sink and reuse the route function
      // verbatim; a nullable pattern is excluded because the batched span path applies no empty-match rule,
      // and the route's own seam must take this out with it.
      batch_inner_lit_ = batchable && no_wrap && !prog.hints.wb_lead_maximal_run
                         && !detail::inner_literal_route_disabled()
                         && !prog.hints.empty_match_possible && prog.slot_count == 2
                         && detail::pike_vm<typename Storage::state_type, true>::inner_literal_is_the_route(prog);
      batch_eligible_  = batch_bytes_ || batch_cp_ascii_ || batch_single_cl_ || cp_class || batch_alt_
                         || batch_lazy_dfa_ || batch_exact_lit_ || batch_inner_lit_;
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
     * Prefer this in a hand-rolled loop: `it == basic_match_iterator{}` answers
     * the same question, but a default-constructed iterator is a full walker
     * and is expensive to materialise just to test.
     *
     * \return `true` once no further match will be produced.
     */
    // A default-constructed iterator carries a whole state_type (heap-backed
    // members). The compat regex_iterator compared against one per step for a
    // round and paid 2.4x.
    [[nodiscard]] constexpr bool exhausted() const noexcept
    {
      return done_;
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
    bool                         cascade_            {};                       //!< Chosen once — run the memchr-cascade class-run variant for this whole walk.
    match_semantics              sem_                {match_semantics::first}; //!< leftmost-first (default) or longest (find_iter_longest).
    value_type                   current_;                                     //!< The current match.
    typename Storage::state_type state_;                                       //!< VM scratch, reused across the walk.

    //! \brief Buffered spans for the code-point-class route — see \ref batch_eligible_.
    //!
    //! **Tuned, not picked.** A wider buffer captures the same Unicode gain but charges the rows that
    //! never touch the batch at all, and outlining the refill does not recover that -- which is what
    //! rules out \ref advance's own size as the cause and points at the ITERATOR's, this array being
    //! part of every walk's state whether or not the walk batches.
    static constexpr std::size_t                                          batch_cap         {4};
    typename detail::pike_vm<typename Storage::state_type, true>::cp_span batch_[batch_cap] {}; //!< The buffered spans; indices \ref batch_i_ .. \ref batch_n_ are the unread ones.
    std::size_t                                                           batch_n_          {}; //!< Spans currently buffered.
    std::size_t                                                           batch_i_          {}; //!< Next span to hand out.
    bool                                                                  batch_eligible_   {}; //!< Route/shape allows batching (decided once).

    /*!
     * \brief This walk takes the trailing-lookaround route, chosen once here rather than by
     *        specialization — which is what lets \ref basic_regex::find_iter reach it at all.
     *
     * The route exists for `[a-z]+(?=[a-z])` and its family, and three of the four entry points took it:
     * `count_matches` and `find_all` branch internally onto
     * `basic_match_range<Storage, TrailingLA = true>`. `find_iter` could NOT — its return type names the
     * specialization, so a runtime hint cannot pick one — and it fell to the general Pike VM instead.
     * The gap that opened was an order of magnitude, for the same pattern and the same match count, where
     * every pattern WITHOUT a trailing lookaround has the two surfaces within noise of each other -- so
     * result construction costs nothing and the whole gap was the missed route. With this flag the two
     * surfaces meet again.
     *
     * It also says why the defect survived: the benchmark measured `count_matches` for every row, so the
     * published figure described the fast path while the iterator API ran an order of magnitude slower and
     * no table showed it. That instrument now carries a `find_iter` row for exactly this reason.
     *
     * **WHAT IT COSTS, AND TWO ATTEMPTS TO REMOVE THAT COST THAT FAILED.** The test this flag adds sits in
     * `advance`'s general path, so the routes that are NOT batched pay it once per match. The trade is
     * lopsided but not free: the target row gains almost all of its time back, while the row most exposed
     * to a per-match test loses measurably -- `exact_literal`, unbatched, whose matches are short and
     * frequent, so a per-match constant lands on it hardest.
     *
     * Two ways out were tried and both made it WORSE, established by disassembly before any campaign:
     * folding this flag and `batch_eligible_` into one dense `enum` field -- they are mutually exclusive,
     * so one field should mean one load -- grew `count_matches` from 372 to 381 instructions, because a
     * compare-to-constant costs more than a test-nonzero; and moving the fold's assignment into the cold
     * `decide_batching` recovered only the constructor, leaving the same +9. So the trade STANDS and is
     * recorded rather than quietly carried: 14x on an API path a caller cannot avoid, against 17 % on a row
     * that leads PCRE2-JIT by 1.29x / 1.94x and can afford it. Anyone reopening this needs a way to select
     * the walk WITHOUT a per-match test, not a cheaper flag.
     */
    bool                                                                  trailing_la_walk_ {};
    bool                                                                  batch_bytes_      {}; //!< Batch the BYTE-class route rather than the code-point one.
    //! \brief Batch the `.`/negated-class route (\ref real::detail::pike_vm::fill_codepoint_class_spans).
    //!
    //! It was the one class scan with no filler, so it paid a full route entry per match where the other
    //! two pay one per \ref batch_cap -- several times the per-match cost of its own batched neighbours.
    bool                                                                  batch_cp_ascii_   {};
    //! \brief Batch the bare single byte-class route (\ref real::detail::pike_vm::fill_single_class_spans).
    //!
    //! An unquantified `[a-z]` crossed one full route entry per accepted BYTE — slower per byte than
    //! `.`, which matches at every position — because \ref real::detail::pattern_hints::greedy_class_loop
    //! describes `class+` only and carries no "single" flag to batch on.
    bool                                                                  batch_single_cl_  {};
    //! \brief The walk's pattern carries a DROPPED leading `\b` (\ref
    //!        real::detail::pattern_hints::wb_lead_maximal_run), so its filler needs the
    //!        one-position window-edge guard. Decided once, and passed as a template argument rather
    //!        than tested in the scan loop.
    bool                                                                  wb_edge_          {};
    //! \brief Batch the fixed-alternation route (\ref real::detail::pike_vm::fill_alternation_spans).
    //!        Its per-match return was 99 % of the row at density -- see that filler's own note.
    bool                                                                  batch_alt_        {};
    //! \brief Batch the lazy-DFA route (%pike.hpp's `fill_lazy_dfa_spans`) — the fifth, and the
    //!        one shape recognition never reaches.
    bool                                                                  batch_lazy_dfa_   {};
    //! \brief Batch the exact-literal route (%pike.hpp's `fill_exact_literal_spans`) — the sixth, and a
    //!        refusal reopened on a contrary measurement rather than on a new idea; see there.
    bool                                                                  batch_exact_lit_  {};
    //! \brief Batch the inner-literal route (%pike.hpp's `fill_inner_literal_spans`) — the seventh, and the
    //!        second to need \ref batch_partial_ (its guards abandon).
    bool                                                                  batch_inner_lit_  {};
    //! \brief That filler stopped WITHOUT proving the subject spent, so an empty buffer means "resume on
    //!        the per-match path", not "the walk is over". Never set by the other four fillers, whose
    //!        scans cover the whole subject and for which an empty buffer IS exhaustion.
    bool                                                                  batch_partial_    {};
    //! \brief The walk's pattern KEEPS a `\b`/`\B` wrap, so the byte-class filler evaluates it on every
    //!        span. Only that filler handles it; the other batched routes decline such patterns.
    bool                                                                  wb_kept_          {};

    /*!
     * \brief Cold half of the batched walk: refills \ref batch_ from the engine.
     *
     * Outlined, and the attribute is load-bearing for the same reason \ref advance is NOT
     * force-inlined: this iterator's translation unit sits on gcc's per-unit inline budget
     * (docs/design.dox §10.1). Inline, this refill grows `advance` enough to charge rows that never touch
     * the batch at all. Outlined, `advance`'s hot path is a compare, an index and a span copy, and it runs
     * once per
     * \ref batch_cap matches instead of once per match.
     *
     * Each branch bills its route to \ref real::detail::prof::tick_route, which the unbatched routes in
     * \ref real::detail::pike_vm::run also do — the SAME identifier on purpose, so `entries / matches`
     * stays one number across both. The reading changes meaning, though: a batched route bills once per
     * REFILL, so an effective batch reads `1 / batch_cap` (0.25 at four) where an unbatched route reads
     * 1.000. That ratio is therefore the batch's efficiency, and 1.000 on a route that should batch is
     * the signal that it stopped. Billing nothing here — which is what this walk did until now — makes a
     * batched route indistinguishable from one never entered, and it hid every route this file batches
     * from the one instrument that is indifferent to machine load.
     *
     * \warning **Do not add a route here without reading docs/MEASUREMENT.md §3.2 first.** This function is
     *          entered once per \ref batch_cap matches by every batched route and is reached from
     *          `count_matches`, which is what every throughput measurement runs. Adding ONE branch --
     *          calling an existing filler, flag computed in the cold outlined \ref decide_batching, no struct
     *          reflow -- leaves `advance` and all six fillers byte-identical and moves only this function
     *          (+10 instructions) and `count_matches` (−2), and that was enough to put **17 of 18** rows'
     *          medians positive (+0.2 % to +9.7 %, 21 of 24 draws on most) where the same base without the
     *          branch read 13 of 18 negative. No row is REAL by \ref real's decision rule, and the sign
     *          across rows is not noise either. Three routes still bill one entry per MATCH --
     *          `exact_literal`, `inner_literal` and `fixed_shape` (which serves `date`, the weakest published
     *          row) -- and batching any of them through here taxes the other seventeen by about what it might
     *          win on one. §3.2 also records why the obvious doors around it are not free.
     * \return `true` if at least one span was buffered.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    constexpr bool refill_batch()
    {
      detail::pike_vm<typename Storage::state_type, true> bvm {prog_, state_};
      if (batch_bytes_) {
        // Four instantiations, chosen once per walk. `wb_edge_` is nearly always false, and when it
        // is the guard is not merely untaken but ABSENT -- see fill_class_spans's own note for the
        // two runtime spellings that were measured and refused.
        // `wb_edge_` (a DROPPED wrap needing the one-position guard) and `wb_kept_` (a wrap the
        // recognizer could not drop, needing a check per span) are mutually exclusive by
        // construction, so three instantiation pairs cover every case and the fourth never exists.
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
        detail::prof::tick_route(detail::prof::route::alternation);
        batch_n_ = bvm.fill_alternation_spans(text_, pos_, batch_, batch_cap);
      }
      else if (batch_lazy_dfa_) {
        detail::prof::tick_route(detail::prof::route::lazy_dfa_anchored);
        batch_n_ = bvm.fill_lazy_dfa_spans(text_, pos_, batch_, batch_cap, batch_partial_);
      }
      else if (batch_exact_lit_) {
        detail::prof::tick_route(detail::prof::route::exact_literal);
        batch_n_ = bvm.fill_exact_literal_spans(text_, pos_, batch_, batch_cap);
      }
      else if (batch_inner_lit_) {
        detail::prof::tick_route(detail::prof::route::inner_literal);
        bool disarm {false};
        batch_n_ = bvm.fill_inner_literal_spans(text_, pos_, batch_, batch_cap, batch_partial_, disarm);
        if (disarm) {
          // The route gave up on this haystack, and its abandon is sticky there. Every further refill
          // would repeat the same wasted memmem before handing the match back to the per-match path, so
          // the walk stops batching for the rest of its life -- which is exactly the behaviour that
          // existed before this filler. Not doing this cost `date dense` +10 % on the veto matrix (3634
          // attempts against 7) while the row the filler targets kept its win.
          batch_inner_lit_ = false;
          batch_eligible_  = false;
        }
      }
      else {
        // The code-point class loop — the fourth eligible route, reached as the `else` rather than
        // through a flag of its own (see the constructor's `cp_class`). Nothing else can arrive here:
        // batch_eligible_ is the disjunction of exactly these four.
        detail::prof::tick_route(detail::prof::route::cp_class_loop);
        batch_n_ = wb_edge_ ? bvm.template fill_cp_class_spans<true>(text_, pos_, batch_, batch_cap)
                            : bvm.template fill_cp_class_spans<false>(text_, pos_, batch_, batch_cap);
      }
      batch_i_ = 0;
      return batch_n_ != 0;
    }

    /*!
     * \brief Finds the next match, applying the empty-match advance rules.
     *
     * \c TrailingLA is fixed for the whole walk (constructor / range). Pure walks
     * (`TrailingLA = false`) contain no trailing-lookahead symbols at all — what the class-loop codegen
     * needs (see pattern_hints::trailing_lookaround).
     *
     * \note **Not force-inlined, and that was measured rather than assumed.** This function is a large
     *       share of a steady-state class scan, and about half of its own cost is prologue and epilogue --
     *       a stack frame per match, against a body of a few dozen instructions. The obvious fix is
     *       `always_inline`.
     *
     *       It is a regression. One ISA reads it as a win with its gauges inside the layout floor; the
     *       other regresses the TARGET itself, and regresses unrelated class rows further still. Inlining
     *       this into its callers bloats the translation unit past `--param inline-unit-growth`, and the
     *       compiler then starts declining inlines that mattered more -- the cliff documented in
     *       docs/design.dox 10.1, reproduced here in one experiment.
     *
     *       So the per-match frame is real and stays. Removing it needs the frame to shrink, not the call
     *       to disappear.
     */
    constexpr void advance()
    {
      if (done_ || pos_ > text_.size()) {
        done_ = true;
        return;
      }
      // Batched code-point-class walk: the route's cost is its per-match RETURN, not its scan (see
      // pike.hpp's fill_cp_class_spans), so hand out a buffered span and only re-enter the engine
      // once the buffer drains. Every shape this cannot reproduce is excluded by batch_eligible_,
      // decided once per walk.
      if (batch_eligible_) {
        // The partial test sits INSIDE the exhausted branch, not beside the hot one, and that placement
        // was measured. Written as two sequential tests -- refill, then `batch_i_ < batch_n_` -- it put a
        // second comparison on the path every batched route walks per match, and the per-row rule saw
        // nothing while the cross-row sign test did: 17 of 21 medians positive on rows this change cannot
        // touch, p = 0.007, median +1.3 % (docs/MEASUREMENT.md §3.2 is the instrument for exactly that).
        // In this shape the four shape-recognized fillers execute what they always did, and the extra
        // test is reached once per walk, when a refill comes back empty.
        if (batch_i_ == batch_n_ && !refill_batch()) {
          // An empty buffer means the walk is over for those four, whose scan covers the whole subject.
          // The lazy-DFA filler can stop with matches still ahead -- its declined fallback sub-scan, a
          // tail under lazy_dfa_min_input, DFAs not built yet -- and says so with `batch_partial_`. There
          // the answer is to fall through to the per-match path below, which re-enters the full gate in
          // `run()`; concluding the subject was spent would silently drop the rest of the matches.
          if (!batch_partial_) {
            done_ = true;
            return;
          }
        }
        else {
          detail::pike_vm<typename Storage::state_type, true> wvm {prog_, state_};
          const auto&                                         sp  {batch_[batch_i_++]};
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
        // The trailing-lookaround route, selected by a FLAG rather than by specialization -- which is what
        // lets `find_iter` reach it (a return type cannot name a specialization a runtime hint picks). The
        // claim that once stood here, "this specialization is never mixed into pure walks", is RETIRED by
        // measurement: keeping the walk out of the pure specialization is exactly what left `find_iter` on
        // the general VM at 12x the cost of `count_matches` for the same pattern. See
        // \ref trailing_la_walk_.
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
    // matching_only is applied to the STORED view, after the copy this constructor
    // was going to make anyway. Mutating a caller-owned view and handing it over
    // would copy the view a second time -- a fixed per-call cost, flat in the
    // subject length (see the size ceiling on detail::program_view). Taking the
    // intent instead costs one copy, exactly what find_iter pays.
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
    // Distinct from result_type so no conversion can drop ownership. Folding
    // the owner into result_type was recorded as a regression and did not
    // reproduce (code placement; design.dox 10.1). The borrowing path stays
    // trivially destructible.
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
      return match(std::string_view(text));
    }

    /*!
     * \brief `fullmatch` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type fullmatch(const char* text) const&
    {
      return fullmatch(std::string_view(text));
    }

    /*!
     * \brief `search` overload for string literals.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]] constexpr result_type search(const char* text) const&
    {
      return search(std::string_view(text));
    }

    // Single attempts on a TEMPORARY regex. These stay callable, unlike find_iter and find_all: the
    // one-expression form is safe (the temporary outlives the full-expression) and it is what most
    // callers write -- this project's own suite alone has ~870 of them. The result may still be
    // stored, though, and then a named lookup reads a pattern and a name table the regex took with
    // it. So the result takes its own copy before this regex dies; see \ref
    // basic_match_result::detach_from_regex, which is a no-op wherever those tables are static.
    // Ref-qualification is all-or-nothing per parameter list, hence the `const&` on every twin
    // above; the deleted `const std::string&&` text overloads have their own parameter lists and
    // keep enforcing the separate rule that the SUBJECT must outlive the result.

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
      return std::move(*this).match(std::string_view(text));
    }

    /*!
     * \brief `fullmatch` on a temporary regex, string-literal overload.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type fullmatch(const char* text) const&&
    {
      return std::move(*this).fullmatch(std::string_view(text));
    }

    /*!
     * \brief `search` on a temporary regex, string-literal overload.
     * \param[in] text NUL-terminated text.
     * \return The result.
     */
    [[nodiscard]]
    constexpr owning_result_type search(const char* text) const&&
    {
      return std::move(*this).search(std::string_view(text));
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
    // The range's return type is fixed at compile time (`TrailingLA = false`) so
    // a bare `[a-z]+` walk carries no lookaround code. Eligible trailing-LA
    // patterns take the faster route on count_matches / find_all / search /
    // match / replace; correctness is identical.
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
     * \brief Experimental leftmost-**longest** `find_iter`: iterate matches with POSIX (leftmost-longest)
     *        bounds rather than the default leftmost-first — the iterator twin of \ref search_longest, sharing
     *        its prototype status. Region semantics match \ref
     *        find_iter — \p endpos truncates the subject to a view, \p pos is the start (not a slice). Byte
     *        offsets; captures are the winning thread's, not POSIX submatch. Every fast path is bypassed.
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
     * Prefer this over walking \ref find_iter when only the count is needed.
     * Region semantics match \ref find_iter -- \p pos is a start offset, not a
     * slice (`\\A` / `^` still see the absolute position).
     *
     * \param[in] text   The subject text.
     * \param[in] pos    Byte offset to begin counting from (0 = start of text).
     * \param[in] endpos Exclusive end of the region; \ref npos = end of text.
     * \return The number of non-overlapping matches in the region.
     */
    // Matching-only walk. Once-per-walk TrailingLA dispatch when the hint is
    // set; find_iter stays TrailingLA=false so a bare [a-z]+ does not carry
    // that code. find_all builds a vector of results and can dominate a
    // high-cardinality scan.
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
     * With \ref named_group_at, this is the allocation-free way to enumerate them. \ref named_groups
     * materialises a `vector` on every call, so a caller walking the names one at a time — which is what
     * a name-by-number ABI does — paid a fresh vector per name. Reading the program's own span instead
     * removes the allocation entirely, a constant factor that grows with the name count.
     *
     * \note It is a constant factor and **not** a complexity fix, which is worth being precise about:
     *       resolving N names through an interface keyed by group NUMBER is N scans of N either way.
     *       Making that linear needs an index-keyed entry point at the ABI, not a cheaper accessor here.
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
      // By VALUE, not by reference: the dynamic storage's view() returns a prvalue, and binding a
      // reference into its span trips gcc's -Wdangling-reference even though the span's target outlives
      // the temporary. `named_group` is three int32s, so the copy costs nothing to avoid the argument.
      const detail::named_group named_group {program_.view().names[index]};
      return {name_of(named_group), static_cast<std::size_t>(named_group.group)};
    }

  private:

    // Implementation calices, private: `count_matches` is the surface, these two are how its walk is
    // shaped. `count_trailing_la` had been public since it was outlined -- an oversight rather than a
    // contract, with no caller anywhere in the repository, the bindings included -- and `count_walk`
    // would have repeated it. Nothing outside should be able to pick a walk.
    /*!
     * \brief \ref count_matches's walk: the ordinary one, run MATCHING-ONLY.
     *
     * OUTLINED FIRST, AT ZERO BEHAVIOUR, AND JUDGED BEFORE ANYTHING WAS PUT IN IT. Adding a single branch to
     * `count_matches` once recompiled it from 610 to 606 instructions and charged `single [a-z]` +10.7 %,
     * `\b\w+\b` +4.0 %, `\w+` +3.7 % and `fields [^,]+` +2.8 %, all above their floors at 24 of 24 draws, on
     * rows whose own code was byte-identical: the toll was that function being re-decided, not the branch. So
     * the container went in alone and measured flat over 26 rows (medians -0.3 % to +0.8 %, 0 rows REAL)
     * before this policy was added, which is why the policy needs no branch in `count_matches` at all.
     *
     * THE POLICY. This function returns a NUMBER: no caller can observe a capture group, so writing them is
     * pure loss. The walk therefore runs on a private copy of the program whose
     * \ref detail::pattern_hints::capture_free_walk is set — the same walk `(?:...)`-only patterns already
     * get, where a thread's whole capture state is group 0's start in one scalar and the refcounted COW pool
     * is never touched. On a capture-heavy pattern that takes the general VM, the increfs and
     * copy-on-writes drop to ZERO while the VM steps exactly the same positions. That last part is the
     * point — the walk is identical, only its bookkeeping is gone.
     *
     * ONE FIELD, DELIBERATELY. `slot_count` is left alone even though the walk now fills only two slots: the
     * batched span routes arm on `slot_count == 2`, so lowering it would ROUTE the pattern somewhere else and
     * the measurement would be of a different engine. The same trap in reverse is what made the census's
     * headline finding an illusion: `(?:foo|bar)+baz` is far cheaper than `(foo|bar)+baz`, but the second is
     * a different PROGRAM taking a different route, and its VM steps an order of magnitude fewer positions.
     * No walk flag can produce that; rewriting a user's groups to non-capturing at compile time might, and
     * is a separate question with its own answers to give.
     *
     * The structural condition is still asked (\ref detail::capture_free_walk_structural) rather than assumed:
     * it is a property of the program, and a program whose `save 0` can be skipped would give a wrong answer,
     * not a slow one. What this drops is the other half of the compiler's guard — no `save` past slot 1, and
     * `slot_count == 2` — which exists to protect captures nobody here is going to read.
     *
     * THE FLAG IS SET BY THE RANGE, NOT HERE, and that is a measured requirement rather than a preference.
     * Mutating a local view and handing it over costs a SECOND copy of a `program_view`, a fixed per-call
     * cost with no proportional work behind it -- flat in the subject length. The canonical rows never saw
     * it: they measure this surface as THROUGHPUT on multi-kilobyte subjects, where one fixed copy amortises
     * under the noise floor. Passing the intent instead of a mutated view leaves
     * exactly \ref find_iter's one copy.
     *
     * `noinline` but NOT `cold`, unlike \ref count_trailing_la -- that branch is taken only when a hint is
     * armed, this one is the ordinary path.
     *
     * \param[in] text   The subject.
     * \param[in] pos    Where the walk starts.
     * \param[in] endpos Region end, as \ref find_iter takes it.
     * \return The match count.
     */
    [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    constexpr std::size_t count_walk(std::string_view text,
                                     std::size_t      pos,
                                     std::size_t      endpos) const
    {
      const std::size_t end {endpos < text.size() ? endpos : text.size()};
      // NOT a range-for, and the loop condition is the reason. `basic_match_range::end()` returns a
      // default-constructed iterator -- the same type, carrying the same `state_type` -- built once per
      // range-for purely to be compared against, and no comparison ever reads its state (\ref
      // basic_match_iterator::operator== looks at `done_` and `pos_` only). On a short subject that
      // construction is a large share of the whole call, since the state is sized for the worst case and
      // the work is not. `exhausted()` asks the same question and builds nothing.
      //
      // `find_iter` still pays it, and that is left standing rather than papered over: making the state
      // lazy so the sentinel becomes cheap was tried TWICE and refused by measurement -- `std::optional`
      // and a `construct_at` union both charged the WORKING iterator about 26 %, indistinguishably, on a
      // walk that builds no sentinel at all. The remaining vehicle is a distinct sentinel type, which
      // this API cannot take: the C binding stores an iterator and its end in one `real_iter`, and every
      // `std::` algorithm wanting a homogeneous pair would stop compiling. So the saving is taken where
      // it costs nothing to take -- here, on a private walk with no iterator type to preserve.
      basic_match_range<Storage> range {program_.view(), pattern(), text.substr(0, end),
                                        pos,            match_semantics::first, true};
      std::size_t                n     {};
      for (auto it = range.begin(); !it.exhausted(); ++it) {
        ++n;
      }
      return n;
    }

    /*!
     * \brief \ref count_matches over the trailing-lookaround walk, outlined.
     *
     * OUTLINED AND COLD for the same reason \ref basic_match_iterator::decide_batching is: this branch is
     * taken only when `pattern_hints::trailing_lookaround` is armed, yet inline it put a SECOND fully
     * inlined walk inside `count_matches` -- the function every throughput measurement runs. That made
     * `count_matches` large enough to sit on a codegen cliff: adding a single branch to the batched
     * dispatch (no new function body, eligibility already outlined) recompiled it from 610 to 606
     * instructions, and the campaign that measured that state charged `single [a-z]` +10.7 %,
     * `\b\w+\b` +4.0 %, `\w+` +3.7 %, `\w{2,}` +3.2 % and `fields [^,]+` +2.8 %, all above their own
     * floors at 24 of 24 draws, on rows whose own code was byte-identical. The toll was not the change --
     * it was this function being re-decided.
     *
     * \param[in] region The already-clamped subject.
     * \param[in] pos    Where the walk starts.
     * \return The match count.
     */
    [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
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
            // Unsigned overflow wraps, and a wrapped value can land back INSIDE the group range: the
            // 20-digit `$18446744073709551616` is 2^64, which became group 0 and substituted the
            // whole match, and the next integer up substituted group 1. Both silent, where Python
            // raises. Guarding the multiply leaves every reference that fits untouched.
            if (group > (npos - 9) / 10) {
              throw regex_error("invalid group reference in replacement", i);
            }
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
      // FRESH PER SEARCH, and reusing it across searches was measured and refused rather than
      // overlooked. Constructing plus destroying this state is a per-call constant: a fifth to a quarter
      // of a SHORT search, and negligible once captures dominate. No single member accounts for it --
      // every part is small -- so there is no lazy-init lever to pull, only reuse.
      //
      // Reuse is safe WITHIN one regex -- \ref basic_match_iterator already holds one state for a
      // whole walk, which is where the iterator surfaces' advantage over repeated searches comes from.
      // Reuse ACROSS
      // regexes is not: a `static thread_local` state shared by two patterns segfaults on the first
      // search with the second, and AddressSanitizer names it a heap-use-after-free through the
      // class-table pointer `tbl` in run_class_loop (pike.hpp). The state carries program-derived
      // pointers whose invalidation is not built for switching programs; which one is not
      // established here and the note does not guess.
      //
      // So the only safe granularity is per regex per thread, and a lookup keyed that way costs more than
      // the construction it would save. Callers who want the saving already have it: `find_iter`.
      typename Storage::state_type   state;
      // Reference, not a copy: `program_view` is 432 bytes and this line runs once per search.
      // Binding to a const reference also covers the dynamic storage, whose view() still returns by
      // value -- the temporary's lifetime extends to this reference's scope.
      const detail::program_view&    prog    {program_.view()};
      // The result is declared HERE, BEFORE the engine runs, so its slot storage is the one the engine
      // fills — see the note at the return statement for the copy this removes. It has to follow `prog`,
      // whose named-group table it borrows.
      result_type                    out {text, pattern(), prog.names};
      // `state` above is freshly constructed for this one search against `prog` — same guarantee.
      detail::pike_vm<typename Storage::state_type, true> vm(prog, state);
      const auto                                          subject {text.substr(0, end)};
      // Cold path: the trailing-lookahead walk stays outside pike_vm::run, so a pure class-loop run()
      // carries none of its code.
      // if constexpr: static_storage has no lookaround scratch / rejects LA at compile.
      bool matched {};
      if constexpr (requires(typename Storage::state_type & st) {
        st.lookaround;
      }) {
        if (sem == match_semantics::first && prog.hints.trailing_lookaround >= 0
            && (std::is_constant_evaluated() || !detail::trailing_la_route_disabled())) {
          detail::prof::tick_route(detail::prof::route::trailing_la);
          matched = prog.hints.stop_set_size >= 1
                      ? vm.template run_class_loop_trailing_la<true>(subject, pos, mode, out.engine_slots())
                      : vm.template run_class_loop_trailing_la<false>(subject, pos, mode, out.engine_slots());
        }
        else {
          matched = prog.hints.stop_set_size >= 1
                      ? vm.template run<true>(subject, pos, mode, out.engine_slots(), 0, sem)
                      : vm.template run<false>(subject, pos, mode, out.engine_slots(), 0, sem);
        }
      }
      else {
        // The memchr-cascade variant is chosen once here (a single search), never in the per-byte scan.
        matched = prog.hints.stop_set_size >= 1
                    ? vm.template run<true>(subject, pos, mode, out.engine_slots(), 0, sem)
                    : vm.template run<false>(subject, pos, mode, out.engine_slots(), 0, sem);
      }
      // THE RESULT IS BUILT FIRST AND THE ENGINE FILLS IT IN PLACE, which is what removed the last bulk
      // copy per call. Filling a local `slots` and moving it into the result costs a block move through
      // `small_vec::transfer_range`, and a block move pays a fixed startup whatever the volume -- for a
      // groupless pattern the volume is a couple of words, so the startup IS the cost. Narrowing the
      // by-value parameter to an rvalue reference had already removed the FIRST of two such copies; this
      // removes the second. There is exactly one return statement here, so the result is NRVO-constructed
      // in the caller's storage and the engine writes to its final address.
      //
      // ONE ATTEMPT IS REFUTED and stays refuted: making the copy cheaper for small counts -- a bounded
      // loop in `transfer_range` below eight elements -- regressed a dozen rows, because `transfer_range`
      // also serves the thread lists in their hot path, where the bound is never the common case.
      out.engine_set_matched(matched);
      return out;
    }

  public:

    /*!
     * \brief EXPERIMENTAL, opt-in: a single leftmost-**longest** search (POSIX / RE2 `set_longest_match`), the
     *        default leftmost-first semantics left untouched. Among matches at the leftmost start it returns the
     *        longest; a lazy quantifier therefore behaves greedily, and captures are the leftmost-first thread's
     *        at that longest bound (not POSIX submatch). Runs on the general Pike loop (the first-match DFA /
     *        inner-literal fast paths are bypassed). A prototype for the `match_semantics` arc — not yet a stable
     *        API. Its iteration twin is \ref find_iter_longest.
     *
     * \param[in] text Subject.
     * \return The leftmost-longest match; falsy when there is none.
     */
    [[nodiscard]] result_type search_longest(std::string_view text) const
    {
      return run(text, 0, npos, detail::run_mode::search, match_semantics::longest);
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
   * \brief The result type of the default, runtime-compiled \ref real::regex.
   *
   * DERIVED, not re-spelled, and that is the whole point. Until v2026.8.8 this alias named
   * `basic_match_result<std::vector<std::size_t>>` while the dynamic policy's slots are SBO-backed,
   * so the type documented as "what `real::regex` returns" was not that type and declaring a
   * variable with it did not compile. Nothing detected it because the alias RESTATED a type instead
   * of asking for it; the restatement and the thing it restated were free to drift apart, and did,
   * from the first commit. Deriving it makes that class of drift unrepresentable rather than merely
   * detectable — the same reason \ref real::regex and \ref real::static_regex never drifted.
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
