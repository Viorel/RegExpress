/*!
 * \file pike.hpp
 * \brief The Pike VM — a Thompson NFA simulation — and its fast paths.
 *
 * Linear time in the input: every program counter is added to a list at most
 * once per position (generation-marked dedup), so no pattern can backtrack
 * catastrophically.
 *
 * The VM is generic over its container policy — `std::vector` for the
 * dynamic storage mode, fixed-capacity `static_vec` (storage.hpp) for
 * compile-time sized patterns, where a whole run performs zero heap
 * allocations.
 */
#ifndef REAL_PIKE_HPP
#define REAL_PIKE_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "real/version.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "real/core/charclass.hpp"
#include "real/engine/prefilter.hpp"
#include <mutex>
#include <optional>

#include "real/automata/lazy_dfa.hpp"
#include "real/automata/onepass.hpp"
#include "real/core/program.hpp"
#include "real/unicode/unicode_props.hpp"
#include "real/unicode/utf8.hpp"

namespace real::detail {

  /*!
   * \brief How a VM run is anchored.
   */
  enum class run_mode : std::uint8_t
  {
    prefix, //!< Anchored at the start position (Python `re.match`).
    full,   //!< Anchored at both ends (Python `re.fullmatch`).
    search, //!< First match anywhere (Python `re.search`).
  };

  /*!
   * \brief One frame on the epsilon-closure DFS stack (OPT D1): a program counter to explore, plus the
   *        capture block the branch carries. The block travels with the branch — a `split` shares it and a
   *        `save` copies it on write — so there is no slot-restore entry and no shared working array.
   */
  struct eps_entry
  {
    std::int32_t  pc;    //!< The program counter to explore.
    std::uint32_t block; //!< Index of the capture block this branch carries (into the capture pool).
  };

  /*!
   * \brief Copy-on-write pool of capture blocks (OPT D1) — the one capture-slot mechanism for both storages.
   *
   * A per-thread value model would snapshot all `slot_count` capture values every time a thread is stepped
   * or emitted — a fifth to a third of match time on capture-heavy loads. Instead, a thread references a
   * *block* by index; forks (`split`) share a block (refcount++), and a `save` — the ONE write — detaches
   * it first if shared (\ref cow_write). No value-journal, no per-thread copy.
   *
   * Block 0 is the canonical all-`npos` block: every seed shares it (a permanent sentinel ref keeps it
   * alive), so seeding a position costs one incref, not an allocation — the first `save` COWs off it. The
   * pool is trivially-copyable indices with a free list; no RAII handles (they would run destructors in
   * the SBO / static thread lists, which run none — the reserve the review named).
   */
  template <typename DataVec, typename RefVec, typename FreeVec>
  struct basic_capture_pool
  {
    DataVec       data;                            //!< Flat slot storage: block b's slots at [b*width, b*width+width).
    RefVec        refcount;                        //!< Live references per block (non-atomic — the VM is single-threaded).
    FreeVec       free_list;                       //!< Recycled block indices (refcount hit 0).
    std::uint16_t width                       {0}; //!< slot_count (values per block).

    static constexpr std::uint32_t npos_block {0}; //!< Canonical all-`npos` block, shared by every seed.

    //! \brief Reset for a new match run: block 0 = all-`npos`, held by a permanent sentinel ref. The
    //!        storage grows by \ref allocate (heap for dynamic; a compile-sized static_vec for static,
    //!        whose capacity bounds the live-block count and so is never exceeded).
    constexpr void reset(std::uint16_t slot_count)
    {
      width = slot_count;
      data.assign(slot_count, npos);
      refcount.assign(1, 1); // block 0, sentinel refcount 1 (never freed)
      free_list.clear();
    }

    //! \brief Pointer to block \p b's `width` slots. Invalidated by any \ref allocate that grows `data`.
    [[nodiscard]] constexpr std::size_t* slots(std::uint32_t b)
    {
      return &data[static_cast<std::size_t>(b) * width];
    }

    //! \brief A fresh block with refcount 1 (a recycled index if available, else a grown one).
    [[nodiscard]] constexpr std::uint32_t allocate()
    {
      if (!free_list.empty()) {
        const std::uint32_t b {free_list.back()};
        free_list.pop_back();
        refcount[b] = 1;
        return b;
      }
      const auto b {static_cast<std::uint32_t>(refcount.size())};
      refcount.push_back(1);
      for (std::uint16_t s = 0; s < width; ++s) {
        data.push_back(npos);
      }
      return b;
    }

    constexpr void incref(std::uint32_t b)
    {
      ++refcount[b];
    }

    constexpr void decref(std::uint32_t b)
    {
      if (--refcount[b] == 0) {
        free_list.push_back(b);
      }
    }

    //! \brief Write `slots(b)[slot] = value`, detaching first if \p b is shared. Returns the block that now
    //!        holds the write (a fresh private copy when shared, else \p b). The one place a block mutates.
    [[nodiscard]] constexpr std::uint32_t cow_write(std::uint32_t b,
                                                    std::uint16_t slot,
                                                    std::size_t   value)
    {
      if (refcount[b] > 1) {
        const std::uint32_t        b2  {allocate()}; // may grow data — recompute both pointers after
        std::size_t* const         dst {slots(b2)};
        const std::size_t* const   src {slots(b)};
        for (std::uint16_t s = 0; s < width; ++s) {
          dst[s] = src[s];
        }
        dst[slot] = value;
        decref(b);
        return b2;
      }
      slots(b)[slot] = value;
      return b;
    }

    //! \brief Sum of all live refcounts — the debug Σ-invariant checks this equals the references the VM
    //!        actually holds (list blocks + stack frames), catching a leaked or double-freed block.
    [[nodiscard]] constexpr long long total_refs() const
    {
      long long sum {0};
      for (std::size_t b = 0; b < refcount.size(); ++b) {
        // free blocks sit at refcount 0; the sentinel block 0 carries its permanent +1.
        sum += refcount[b] > 0 ? refcount[b] : 0;
      }
      return sum;
    }
  };

  //! \brief The dynamic-storage capture pool: heap vectors, grows on demand.
  using capture_pool = basic_capture_pool<std::vector<std::size_t>,
                                          std::vector<std::int32_t>,
                                          std::vector<std::uint32_t>>;

  /*!
   * \brief One priority-ordered list of NFA threads (leftmost-greedy semantics).
   *
   * `mark` is generation-stamped so clearing the list between positions is O(1).
   *
   * \tparam PcVec   Container of program counters.
   * \tparam SlotVec Flattened capture slots (pcs.size() * slot_count).
   * \tparam MarkVec Per-pc generation marks for O(1) dedup.
   */
  template <typename PcVec, typename SlotVec, typename MarkVec>
  struct basic_thread_list
  {
    PcVec         pcs;           //!< Live program counters, in priority order.
    SlotVec       slots;         //!< Flattened capture slots, parallel to \ref pcs.
    MarkVec       mark;          //!< Per-pc generation stamp (see \ref seen).
    std::uint64_t generation {}; //!< Current generation; bumped by \ref reset.

    /*!
     * \brief Clears the list in O(1) by bumping the generation.
     * \param[in] code_size Number of instructions (sizes the mark table once).
     */
    constexpr void reset(std::size_t code_size)
    {
      if (mark.size() != code_size) {
        mark.assign(code_size, 0);
        generation = 0;
      }
      ++generation;
      pcs.clear();
      slots.clear();
    }

    /*!
     * \brief Returns `true` if \p pc is already in this generation.
     * \param[in] pc A program counter.
     * \return `true` if \p pc is already in this generation.
     */
    [[nodiscard]] constexpr bool seen(std::int32_t pc) const
    {
      return mark[static_cast<std::size_t>(pc)] == generation;
    }

    /*!
     * \brief Marks \p pc as present in the current generation.
     * \param[in] pc The program counter.
     */
    constexpr void mark_seen(std::int32_t pc)
    {
      mark[static_cast<std::size_t>(pc)] = generation;
    }
  };

  /*!
   * \brief Reusable VM scratch state.
   *
   * One run allocates nothing once warm (and never allocates with static
   * containers); `find_all-style` loops reuse the same state across runs. The
   * two thread lists are flipped by index, never swapped.
   *
   * \tparam ThreadList The thread-list type (a \ref basic_thread_list).
   * \tparam EpsVec     Container for the epsilon-closure stack.
   */
  template <typename ThreadList, typename EpsVec>
  struct basic_pike_state
  {
    ThreadList lists[2]; //!< Current and next thread lists (flipped by index).
    EpsVec     stack;    //!< Epsilon-closure DFS stack.

    /*!
     * \brief Flat 256-byte membership table for the hot single-class scan, and
     *        the class index it was built for (-1 = none).
     *
     * The class-scanning fast paths (`[…]+`, `.`/negated-class) test one class
     * for every byte. A flat byte-indexed table answers membership with a single
     * load, versus the bitmap's shift-and-mask (measured ~2x faster in a tight
     * scan — the byte-classification technique used by DFA/JIT engines). It is
     * built once and reused across a `find_all`-style walk (the state is shared),
     * so it adds nothing to the program or to the static binary.
     */
    std::int32_t                   table_class {-1};
    std::array<std::uint8_t, 256>  table       {}; //!< 1 where the byte is in \ref table_class.

    /*!
     * \brief Membership bitmap for a `cp_class` over the 2-byte UTF-8 range `[U+0080, U+07FF]`, and
     *        the class it was built for. A `klass_cp` scan otherwise binary-searches ~771 ranges per
     *        non-ASCII code point; European text lives almost entirely in this range (Latin, IPA,
     *        Greek, Cyrillic, Hebrew, Arabic…), so a 240-byte bitmap answers it in one load. Built once
     *        per class and reused across a `find_all`-style walk; code points beyond U+07FF (CJK,
     *        astral) fall back to the range search.
     */
    std::int32_t                   cp_page_class {-1};
    std::array<std::uint64_t, 30>  cp_page       {}; //!< 1 where the code point (U+0080..U+07FF) is a member.
  };

  /*!
   * \brief Thread list specialized on `std::vector` (the dynamic storage mode).
   */
  using thread_list = basic_thread_list<std::vector<std::int32_t>, std::vector<std::size_t>, std::vector<std::uint64_t>>;
  /*!
   * \brief Reusable, isolated scratch for one level of lookaround evaluation (dynamic only).
   *
   * Vector-backed, independent of the main scratch's container policy; reset on each
   * evaluation, never sharing the main \ref basic_pike_state. One level suffices — nested
   * lookaround is rejected at compile time. Present only on the dynamic states; the static
   * state has no such member, so the lookaround code is `if constexpr`-elided there.
   */
  struct lookaround_scratch
  {
    thread_list            lists[2]; //!< Sub-VM thread lists (pcs only; the sub is capture-free).
    std::vector<eps_entry> stack;    //!< Sub-VM epsilon-closure stack.
  };

  /*!
   * \brief VM scratch state for the dynamic storage mode, plus the lookaround sub-scratch.
   */
  struct pike_state : basic_pike_state<thread_list, std::vector<eps_entry>>
  {
    lookaround_scratch         lookaround;              //!< Isolated sub-scratch for bounded lookaround evaluation.
    capture_pool               pool;                    //!< OPT D1: copy-on-write capture blocks (heap-backed).
    std::optional<lazy_dfa>    fwd_dfa;                 //!< OPT lazy-DFA: forward pass (built lazily, cache persists across a find_iter).
    std::optional<reverse_dfa> rev_dfa;                 //!< OPT lazy-DFA: the reverse start-finder.
    const void*                dfa_program   {nullptr}; //!< The program the DFAs were built for (rebuild if it changes).
    std::optional<reverse_dfa> il_prefix_rev;           //!< IL: the inner-literal prefix reverse DFA (built once per program).
    const void*                il_prefix_for {nullptr}; //!< IL: the prefix program \ref il_prefix_rev was built for.
    const void*                il_text       {nullptr}; //!< IL: the haystack \ref il_abandoned refers to (reset the flag when it changes).
    bool                       il_abandoned  {false};   //!< IL: a linearity guard tripped on this haystack — stay on the core.
  };

  /*!
   * \brief The Pike VM, generic over the scratch-state container policy.
   * \tparam State A \ref basic_pike_state instantiation (vector- or static-backed).
   */
  template <typename State>
  class pike_vm
  {
  public:

    /*!
     * \brief Binds the VM to a program and caller-owned scratch state.
     * \param[in]     prog  The compiled program to execute.
     * \param[in,out] state Reusable scratch (borrowed; must outlive the VM).
     */
    constexpr pike_vm(const program_view& prog,
                      State&              state)
      : prog_(prog),
        state_(state)
    {}

    /*!
     * \brief Runs the VM over \p text starting at \p start.
     *
     * On success fills \p out_slots with byte offsets (npos for unset capture
     * slots; slots 0/1 are the whole match).
     *
     * \tparam Cascade  Select the OPT-C memchr-cascade class-run variant (chosen once by the caller from
     *                  stop_set_size, never per match). Off = the pre-OPT-C hot path, byte for byte.
     * \tparam OutSlots Output slot container (resized to the program's slot count).
     * \param[in]  text               The subject text.
     * \param[in]  start              Index to begin matching/searching from.
     * \param[in]  mode               Anchoring mode (\ref run_mode).
     * \param[out] out_slots          Receives the capture slots on success.
     * \param[in]  forbid_empty_until Reject an empty match whose start is below
     *             this offset (the iterator sets it to the next codepoint
     *             boundary so a non-empty match may follow an empty one without
     *             re-yielding it — CPython 3.7+ rule). 0 means no restriction.
     * \return `true` if a match was found.
     */
    template <bool Cascade = false, typename OutSlots>
    constexpr bool run(std::string_view text,
                       std::size_t      start,
                       run_mode         mode,
                       OutSlots&        out_slots,
                       std::size_t      forbid_empty_until = 0)
    {
      text_               = text;
      forbid_empty_until_ = forbid_empty_until;
      // Fast paths only fire for patterns that always consume (literal /
      // class+), which can never produce the empty match the flag guards.
      if (prog_.hints.greedy_class_loop >= 0) {
        // OPT-C: the memchr-cascade instantiation (Cascade) is selected ONCE by the caller (a whole
        // find_iter/search) from stop_set_size, never per match — so when it is off this run is byte-for-
        // byte the pre-OPT-C per-byte loop and the hot path pays nothing. The caller only sets Cascade
        // when stop_set_size >= 1, so the cascade tail always has real stop bytes.
        return run_class_loop<Cascade>(text, start, mode, out_slots);
      }
      if (prog_.hints.greedy_cp_class >= 0) {
        return run_cp_class_loop(text, start, mode, out_slots);
      }
      if (prog_.hints.exact_literal_len > 0) {
        return run_exact_literal(text, start, mode, out_slots);
      }
      // OPT inner-literal: memmem a required inner literal and reverse/confirm the match around it — the
      // most selective prefilter for a pattern whose match does not begin with a literal (the date `-`, the
      // `@`). Placed AFTER the literal / class-loop fast paths (an exact-literal `dog` must keep its own path)
      // but BEFORE the fixed-shape / DFA scans it beats. Search mode, runtime, dynamic-only. On a linearity
      // guard it abandons and falls through to the scans below.
      if constexpr (requires(State & s) {
        s.il_prefix_rev;
      }) {
        if (!std::is_constant_evaluated() && !inner_literal_route_disabled() && mode == run_mode::search
            && prog_.hints.inner_literal_len > 0 && prog_.hints.inner_literal_prefix >= 0
            && (prog_.hints.inner_literal_prefix == 0 || !prog_.prefix_code.empty())) {
          // No size guard: on a no-match haystack the route is memmem-only (the reverse setup is lazy, built on
          // the first candidate, never here), so it wins at every size; and the prefix byte-program is a
          // per-regex immutable (built once, amortized by any later use — the lazy-DFA warmup's own contract).
          if (state_.il_text != static_cast<const void*>(text.data())) {
            state_.il_abandoned = false; // a fresh haystack: re-enable the route and re-evaluate its guards
            state_.il_text      = static_cast<const void*>(text.data());
          }
          if (!state_.il_abandoned) {
            bool       abandon {false};
            const bool matched {run_inner_literal(text, start, out_slots, abandon)};
            if (!abandon) {
              return matched;
            }
            state_.il_abandoned = true; // a linearity guard tripped: stay on the core for the rest of this haystack
          }
        }
      }
      if (prog_.hints.fixed_shape) {
        return run_fixed_shape(text, start, mode, out_slots);
      }
      if (prog_.hints.codepoint_class_ascii >= 0) {
        // OPT-C-1b: the SWAR variant (Cascade) is chosen once per walk, like the class-loop cascade.
        return run_codepoint_class<Cascade>(text, start, mode, out_slots);
      }
      if (prog_.hints.fixed_alternation) {
        return run_alternation(text, start, mode, out_slots);
      }
      // OPT inner-literal: memmem a required literal and reverse/confirm the match around it — for patterns
      // whose match need not begin with a literal (a leading class/quantifier), so no prefix skip applies but
      // a rarer INNER literal does (the date `-`, the `@`). Search mode, runtime, dynamic-only (it needs the
      // prefix sub-program). Placed before the DFA: a memchr skip to a rare byte beats a per-byte DFA scan.
      // On a linearity guard it abandons and falls through to the DFA / core VM below.
      // OPT lazy-DFA: for an eligible pattern on a large enough input, a forward DFA finds the match end
      // (capture-free, ~12x a Pike no-match scan) and a reverse DFA its start; the Pike VM then runs only on
      // the [s, e] window for the captures and the empty-match rule (the DFA supplies the span, nothing
      // else). Ineligible patterns, a tripped thrash flag, small inputs, and non-search modes stay on the VM.
      if constexpr (requires(State & s) {
        s.fwd_dfa;
      }) {
        // Direct anchored routing: a full-match's window is exactly [start, text.size()] — no search, no DFA.
        // A one-pass pattern (Tier A, or Tier B now that assertions are edge conditions) fills its captures in
        // a single pass over that window; extract returns false (and we fall to the VM) if it does not one-pass
        // or the span does not in fact match there. Only the immutables are needed, so no DFA build is paid.
        if (!std::is_constant_evaluated() && !lazy_dfa_route_disabled() && mode == run_mode::full) {
          ensure_immutables();
          if (prog_.immut != nullptr && prog_.immut->op_table.has_value() && prog_.immut->op_table->eligible()
              && prog_.immut->op_table->extract(text, start, text.size(), out_slots)) {
            return true;
          }
        }
        // forbid_empty_until_ != 0 means the iterator just yielded an empty match and the next may not be
        // empty at the same spot; forward_end does not model that rule, so those searches stay on the Pike
        // VM (which does). Empty-matching patterns thus alternate DFA/VM across a find_iter; all others route.
        if (!std::is_constant_evaluated() && !lazy_dfa_route_disabled() && mode == run_mode::search
            && forbid_empty_until_ == 0 && text.size() - start >= lazy_dfa_min_input) {
          if (state_.dfa_program != static_cast<const void*>(prog_.code.data())) {
            ensure_lazy_dfa(); // once per iterator, not per match — skips the call_once atomic load on the hot path
          }
          if (state_.fwd_dfa.has_value() && state_.rev_dfa.has_value() && state_.fwd_dfa->eligible()
              && !state_.fwd_dfa->thrashing()) {
            const std::size_t match_end {state_.fwd_dfa->forward_end(text.substr(start))};
            if (match_end == npos) {
              out_slots.assign(prog_.slot_count, npos);
              return false; // the forward DFA rejected the whole suffix at DFA speed
            }
            const std::size_t abs_end   {start + match_end};
            const std::size_t abs_start {state_.rev_dfa->reverse_start(text, abs_end, start)};
            // OPT onepass (Tier A): a one-pass pattern fills captures in a single pass over [s, e] with no
            // thread lists (the shared per-regex table). Otherwise the window-Pike runs the general loop
            // there. Both give the same slots.
            if (prog_.immut->op_table.has_value() && prog_.immut->op_table->eligible()
                && prog_.immut->op_table->extract(text, abs_start, abs_end, out_slots)) {
              return true; // extract filled out_slots directly — no intermediate buffer or copy
            }
            return run_general<Cascade>(text.substr(0, abs_end), abs_start, mode, out_slots);
          }
        }
      }
      return run_general<Cascade>(text, start, mode, out_slots);
    }

    /*!
     * \brief The general Pike VM search loop (the match semantics), factored so the lazy-DFA routing can run
     *        it on the `[s, e]` window a two-pass DFA has located, and so the direct path can call it too.
     */
    template <bool Cascade = false, typename OutSlots>
    constexpr bool run_general(std::string_view text,
                               std::size_t      start,
                               run_mode         mode,
                               OutSlots&        out_slots,
                               std::size_t*     forward_stop = nullptr) // IL: how far the forward scan reached
    {
      text_ = text;
      const std::size_t code_size {prog_.code.size()};
      auto*             clist     {&state_.lists[0]};
      auto*             nlist     {&state_.lists[1]};
      clist->reset(code_size);
      nlist->reset(code_size);
      out_slots.assign(prog_.slot_count, npos);
      state_.pool.reset(prog_.slot_count); // OPT D1: fresh COW pool (block 0 = all-npos, per this run)

      bool        matched {};
      std::size_t pos     {start};
      while (pos <= text.size()) {
        const bool seeding = (pos == start) || (mode == run_mode::search && !matched);
        if (seeding && mode == run_mode::search && !matched && clist->pcs.empty()) {
          // No thread is alive: jump straight to the next position
          // that could start a match (prefilter). Single pass, so the
          // linear-time guarantee is unaffected.
          pos = next_candidate(text, pos, start);
          if (pos > text.size()) {
            break; // no further start is possible (includes npos)
          }
          // Fresh generation before seeding at the jumped position: the list
          // may still carry `seen` marks from a previous position's epsilon
          // exploration whose threads all died, which would otherwise dedup
          // away (drop) the seed's own threads here.
          clist->reset(code_size);
        }
        if (seeding && seed_viable(text, pos, start)) {
          // OPT D1: a seed shares the canonical all-npos block (one incref, no allocation); the first save
          // in its closure copies-on-write off it, so block 0 is never mutated.
          state_.pool.incref(pool_type::npos_block);
          add_thread(*clist, 0, pos, pool_type::npos_block);
        }
        if (clist->pcs.empty()) {
          // The seed itself may die in the closure (failed assertion):
          // later positions must still be tried while searching. The
          // dead seed's seen-marks must not block the next one.
          if (matched || mode != run_mode::search || pos >= text.size()) {
            break;
          }
          clist->reset(code_size);
          ++pos;
          continue;
        }
        step(*clist, *nlist, pos, mode, matched, out_slots);
        auto* swap {clist};
        clist = nlist;
        nlist = swap;
        cow_release_blocks(*nlist); // OPT D1: the old clist was consumed by step — drop its block refs
        nlist->reset(code_size);
        ++pos;
      }
      cow_release_blocks(*clist); // OPT D1: drain both surviving lists on exit (the leak class the review named)
      cow_release_blocks(*nlist);
      // Σ-invariant: after a full drain only the canonical npos block's sentinel ref remains. A leaked
      // block (missing decref) or a double-free (underflow) breaks it. Debug/sanitize builds only.
      assert(state_.pool.total_refs() == 1 && "OPT-D1 capture-block refcount leak or imbalance");
      if (forward_stop != nullptr) {
        *forward_stop = pos; // the position the forward scan reached — IL's min_pre_start on a failed confirm
      }
      return matched;
    }

    /*!
     * \brief Confirm a match anchored at \p s: find its end with the forward DFA and fill captures with the
     *        one-pass table — the same fast laddering the lazy-DFA route uses (§7.6/7.7), so the inner-literal
     *        confirm is not a raw Pike pass. Falls back to the anchored Pike when the pattern is not
     *        DFA/one-pass eligible, or when the forward DFA's leftmost match does not in fact begin at \p s
     *        (then the anchored Pike returns false and the caller advances). \p stop reports how far the confirm
     *        reached, for the linearity backstop. Returns true and fills \p out_slots on a match at \p s.
     */
    template <typename OutSlots>
    bool confirm_at(std::string_view text,
                    std::size_t      s,
                    OutSlots&        out_slots,
                    std::size_t&     stop)
    {
      stop = s;
      if constexpr (requires(State & st) {
        st.fwd_dfa;
      }) {
        if (!lazy_dfa_route_disabled()) {
          if (state_.dfa_program != static_cast<const void*>(prog_.code.data())) {
            ensure_lazy_dfa();
          }
          if (state_.fwd_dfa.has_value() && state_.fwd_dfa->eligible() && !state_.fwd_dfa->thrashing()) {
            const std::size_t match_end {state_.fwd_dfa->forward_end(text.substr(s))};
            if (match_end == npos) {
              stop = text.size();
              out_slots.assign(prog_.slot_count, npos);
              return false; // the forward DFA rejects the whole suffix
            }
            const std::size_t e {s + match_end};
            stop = e;
            ensure_immutables();
            if (prog_.immut != nullptr && prog_.immut->op_table.has_value() && prog_.immut->op_table->eligible()
                && prog_.immut->op_table->extract(text, s, e, out_slots)) {
              return true; // one-pass filled the captures for [s, e] anchored at s
            }
            // Not one-pass, or the DFA's leftmost match did not begin at s: the anchored window Pike decides.
            return run_general<false>(text.substr(0, e), s, run_mode::prefix, out_slots, &stop);
          }
        }
      }
      return run_general<false>(text, s, run_mode::prefix, out_slots, &stop);
    }

    /*!
     * \brief The inner-literal search: memmem a required literal, reverse-match the prefix to the match start,
     *        forward-confirm — the reverse-inner protocol (regex-automata's `ReverseInner`). On a match fills
     *        `out_slots` and returns true; on none returns false. Sets \p abandon (and returns false) when a
     *        linearity guard trips, so the caller retries the whole search on the core VM. Search mode only,
     *        runtime only (the reverse DFA is not constexpr). Two guards keep it linear: the reverse is bounded
     *        below by `min_match_start` (the previous literal's end), and a literal starting before
     *        `min_pre_start` (the last confirm's forward reach) abandons the scan.
     */
    template <typename OutSlots>
    bool run_inner_literal(std::string_view text,
                           std::size_t      start,
                           OutSlots&        out_slots,
                           bool&            abandon)
    {
      abandon = false;
      std::array<char, 16> lit_buf {}; // copy the literal into char storage (no pointer cast to appease both lints)
      for (std::size_t i = 0; i < prog_.hints.inner_literal_len; ++i) {
        lit_buf[i] = static_cast<char>(prog_.hints.inner_literal[i]);
      }
      const std::string_view lit        {lit_buf.data(), prog_.hints.inner_literal_len};
      const std::int32_t     boundary   {prog_.hints.inner_literal_prefix};

      std::size_t       pos             {start};
      const std::size_t min_match_start {start}; // reverse floor = this search's start (the finditer resume); never advances mid-call
      std::size_t       min_pre_start   {start}; // literal-scan floor (last confirm's reach) — the linearity backstop
      bool              first_candidate {true};
      while (true) {
        const std::size_t h {find_literal(text, pos, lit)};
        if (h == npos) {
          out_slots.assign(prog_.slot_count, npos);
          return false;   // no more candidates (no-match): memmem-only — the guard below was never reached
        }
        if (first_candidate) {
          first_candidate = false;
          // Small-haystack guard, decided ONCE at the first candidate (per-scan, made sticky by the caller's
          // il_abandoned). It therefore applies only to a haystack that HAS a match — a no-match scan returns
          // above, memmem-only, and is never gated (its huge win is safe by construction). Below the
          // prefix-scaled threshold the reverse DFA's per-iterator cache does not amortize, so the core (the
          // pre-IL baseline, with its own one-pass/lazy-DFA) is faster: hand it the whole scan.
          ensure_immutables();
          if (!inner_literal_guard_disabled() && prog_.immut != nullptr && text.size() < prog_.immut->il_min_haystack) {
            abandon = true;
            return false;
          }
        }
        if (h < min_pre_start) {
          abandon = true; // guard 2: the scan is regressing into confirmed territory -> retry on the core
          return false;
        }
        std::size_t s {h}; // boundary 0 = head literal: the reverse is the identity
        if (boundary >= 1) {
          // The prefix's byte program lives in the per-regex immutables — built once (call_once, already done
          // by the first-candidate guard above), not per find_iter; the expensive klass_cp expansion is what a
          // small-input regex must not pay repeatedly. The reverse DFA that spans it is a cheap per-iterator
          // wrapper, (re)created when this iterator binds a new program.
          if (prog_.immut == nullptr || !prog_.immut->il_prefix_prog.eligible) {
            abandon = true; // no per-regex cache, or the prefix is not byte-DFA-eligible — let the core VM handle it
            return false;
          }
          if (state_.il_prefix_for != static_cast<const void*>(prog_.immut->il_prefix_prog.code.data())) {
            state_.il_prefix_rev.emplace(prog_.immut->il_prefix_prog.code, prog_.immut->il_prefix_prog.classes);
            state_.il_prefix_for = static_cast<const void*>(prog_.immut->il_prefix_prog.code.data());
          }
          if (state_.il_prefix_rev.has_value()) {
            s = state_.il_prefix_rev->reverse_start(text, h, min_match_start);
          }
        }
        if (s == npos) {
          pos = h + 1; // the prefix reaches no start within [min_match_start, h] -> next candidate
        }
        else {
          std::size_t stop {s};
          if (confirm_at(text, s, out_slots, stop)) {
            return true;          // confirmed: out_slots holds [s, e]
          }
          if (stop > min_pre_start) {
            min_pre_start = stop; // the failed forward's reach bounds future candidates
          }
          pos = h + 1;
        }
        // min_match_start does NOT advance within a call: it advances only on a YIELD (the finditer's next
        // start). Advancing it per candidate (to the previous literal's end) would bound the next reverse too
        // tightly and miss a leftmost match whose start precedes a failed candidate — e.g. `((.))a` on "aaaab",
        // where the "a" at 0 fails but the match [0,2) is found from the "a" at 1 only if the reverse may still
        // reach 0. The min_pre_start backstop (a candidate before the last confirm's forward reach) keeps it
        // linear instead.
      }
    }

  private:

    const program_view& prog_;  //!< The program being executed (borrowed; a stable lvalue that outlives the VM).
    State&              state_; //!< Borrowed reusable scratch state.
    std::string_view    text_;  //!< The subject text for the current run.

    /*!
     * \brief Reject empty matches whose start is below this offset.
     *
     * The CPython 3.7+ rule: after an empty match, the next match may not be
     * empty at the same spot, letting a non-empty match start there. The
     * iterator sets this to the next codepoint boundary so the skip stays
     * UTF-8 aligned. 0 means no restriction (single match/search/fullmatch
     * never restrict).
     */
    std::size_t forbid_empty_until_ {};

    /*!
     * \brief The concrete thread-list type taken from the bound `State`.
     */
    using list_type = std::remove_reference_t<decltype(std::declval<State&>().lists[0])>;

    //! \brief Below this input length the lazy-DFA routing is skipped (the two-pass setup does not amortise
    //!        on a short subject — the Pike VM goes direct). A measured, documented threshold.
    static constexpr std::size_t lazy_dfa_min_input {512};

    //! \brief Build the forward/reverse DFAs into the reusable state on first eligible use, or rebuild them
    //!        if the state is now bound to a different program (its `code` pointer changed). The cache then
    //!        persists across a whole find_iter (where it pays), and forward_end resets its per-search thrash
    //!        flag itself. Instantiated only for the dynamic state (the one carrying the optionals).
    //! \brief Build the per-regex immutables once, race-free: the Tier-A byte-program the DFAs run over (and
    //!        its shared alphabet), plus the one-pass extractor. The extractor is built Tier-B (assertions
    //!        kept as edge conditions), so one table serves both the search window (Tier-A patterns have
    //!        empty assertion masks) and direct anchored match/fullmatch (Tier-B patterns). Needs no DFA, so
    //!        the anchored path can call this without paying the DFA build.
    void ensure_immutables()
    {
      detail::regex_immutables* const immut {prog_.immut};
      if (immut == nullptr) {
        return;                                                      // no per-regex cache (not the dynamic storage) — the caller keeps the VM
      }
      std::call_once(immut->once, [&] {
                       immut->byte_prog = build_byte_program(prog_); // Tier-A: ineligible if assert/lookaround
                       if (immut->byte_prog.eligible) {
                         immut->alphabet =
                           compute_lazy_alphabet(immut->byte_prog.code, immut->byte_prog.classes); // shared by both DFAs
                       }
                       const byte_program tier_b {build_byte_program(prog_, /*keep_assertions=*/ true)};
                       if (tier_b.eligible) {
                         immut->op_table.emplace(tier_b); // one-pass extractor: Tier-A window + Tier-B anchored
                       }
                       if (!prog_.prefix_code.empty()) {  // IL: expand the inner-literal prefix once per regex (not per find_iter)
                         program_view pv {};
                         pv.code                = prog_.prefix_code;
                         pv.classes             = prog_.prefix_classes;
                         pv.cp_classes          = prog_.prefix_cp_classes;
                         pv.cp_ranges           = prog_.prefix_cp_ranges;
                         pv.unicode_word        = prog_.unicode_word;
                         immut->il_prefix_prog  = build_byte_program(pv);
                         // The reverse DFA's per-iterator cache re-warms per find_iter; below a size scaled by
                         // the prefix byte-program (its cache size) that cost does not amortize on a haystack
                         // that HAS candidates, and the core is faster. Measured crossover (route on vs core):
                         // ~158 KB for the email \w+ (3436 instr, dense — the harder density), <64 KB for the
                         // date \d{4} (1031 instr). N = size * 64, clamped [64 KB, 512 KB] — ~40% above the
                         // email crossover (220 KB) so the mid-size win at 256 KB+ is kept, while every size
                         // below stays on the core. Checked ONLY after the first memmem hit (see
                         // run_inner_literal), so no-match — memmem-only, a win at every size — is never gated.
                         const std::size_t sz {immut->il_prefix_prog.code.size()};
                         immut->il_min_haystack = std::min<std::size_t>(512UL * 1024, std::max<std::size_t>(64UL * 1024, sz * 64));
                       }
                     });
    }

    //! \brief Ensure this iterator's lazy DFA caches are warm for the current program. Builds the shared
    //!        immutables first (\ref ensure_immutables), then — only when the state is now bound to a
    //!        different program — (re)creates the per-iterator forward and reverse lazy-DFA transition caches
    //!        over the shared byte-program. The caches are mutable (they fill during a scan) and per-iterator,
    //!        so this runs once per iterator, not per match, off the hot path.
    void ensure_lazy_dfa()
    {
      detail::regex_immutables* const immut {prog_.immut};
      if (immut == nullptr) {
        return; // no per-regex cache (not the dynamic storage) — the caller keeps the VM
      }
      ensure_immutables();
      // The DFA transition caches are mutable (warm per scan): they stay per-iterator, spanning the shared
      // byte-program, rebuilt only if this state is now bound to a different program.
      const auto* const program {static_cast<const void*>(prog_.code.data())};
      if (state_.dfa_program != program) {
        if (immut->byte_prog.eligible) {
          state_.fwd_dfa.emplace(immut->byte_prog.code, immut->byte_prog.classes, lazy_dfa::state_budget,
                                 &immut->alphabet);
          state_.rev_dfa.emplace(immut->byte_prog.code, immut->byte_prog.classes, reverse_dfa::state_budget,
                                 &immut->alphabet);
        }
        else {
          state_.fwd_dfa.reset();
          state_.rev_dfa.reset();
        }
        state_.dfa_program = program;
      }
    }

    //! \brief The capture-block pool type of the bound `State` (OPT D1) — heap-backed for dynamic,
    //!        compile-sized static_vec for static. The one capture-slot mechanism, both storages.
    using pool_type = std::remove_reference_t<decltype(std::declval<State&>().pool)>;

    /*!
     * \brief Returns a flat 256-byte membership table for class \p class_index.
     *
     * Materializes the class bitmap into a byte-indexed table the first time it
     * is requested, caching it in the shared scratch so a `find_all`-style walk
     * builds it once. In a tight per-byte scan, `table[b]` (one load) replaces
     * the bitmap's shift-and-mask — the byte-classification trick of DFA/JIT
     * engines, measured ~2x faster on the class-scanning fast paths.
     *
     * \param[in] class_index Index into the program's interned classes.
     * \return Pointer to a 256-entry table: 1 where the byte is in the class.
     */
    constexpr const std::uint8_t* class_table(std::size_t class_index)
    {
      if (state_.table_class != static_cast<std::int32_t>(class_index)) {
        const char_class& klass {prog_.classes[class_index]};
        for (std::size_t b {0}; b < 256; ++b) {
          state_.table[b] = klass.test(static_cast<std::uint8_t>(b)) ? 1U : 0U;
        }
        state_.table_class = static_cast<std::int32_t>(class_index);
      }
      return state_.table.data();
    }

    /*!
     * \brief Byte-indexed membership table for a `cp_class`'s ASCII bitmap — the same one-load trick
     *        as \ref class_table, for the `klass_cp` scan-loop fast path. Keyed negatively so it never
     *        collides with a `class_table` key (a whole-pattern shorthand has no byte-NFA classes, so
     *        the two never interleave for one pattern anyway).
     * \param[in] cp_index Index into the program's `cp_classes`.
     * \return Pointer to a 256-entry table: 1 where the byte (< 0x80) is a member.
     */
    constexpr const std::uint8_t* cp_ascii_table(std::size_t cp_index)
    {
      const std::int32_t key {-2 - static_cast<std::int32_t>(cp_index)};
      if (state_.table_class != key) {
        const char_class& klass {prog_.cp_classes[cp_index].ascii};
        for (std::size_t b {0}; b < 256; ++b) {
          state_.table[b] = klass.test(static_cast<std::uint8_t>(b)) ? 1U : 0U;
        }
        state_.table_class = key;
      }
      return state_.table.data();
    }

    //! \brief Highest code point covered by the `cp_page` bitmap (the 2-byte UTF-8 range).
    static constexpr std::uint32_t cp_page_max {0x7FFU};

    //! \brief Cap on how far a jump chain is followed to a loop head (empty-iteration exit routing);
    //!        a loop join reaches its split in one hop, so this is a generous bound, never a hot cost.
    static constexpr int max_loop_hops {8};

    //! \brief Accepted-byte count after which a `class+` run switches from the per-byte advance to a
    //!        memchr-cascade to the next stop byte (OPT-C). Below it a run pays nothing extra, so a
    //!        stop-dense stream of short runs stays at baseline cost; the crossover is measured.
    static constexpr std::size_t cascade_run_threshold {32};

    /*!
     * \brief Builds (once, cached) and returns the `cp_class`'s membership bitmap over
     *        `[U+0080, U+07FF]` — a one-load replacement for the range search on the common
     *        two-byte code points (see \ref basic_pike_state::cp_page).
     * \param[in] cp_index Index into the program's `cp_classes`.
     * \return Pointer to the 30-word bitmap (bit `cp - 0x80`).
     */
    constexpr const std::uint64_t* cp_page_table(std::size_t cp_index)
    {
      const std::int32_t key {-2 - static_cast<std::int32_t>(cp_index)};
      if (state_.cp_page_class != key) {
        state_.cp_page.fill(0);
        const detail::cp_class& cc {prog_.cp_classes[cp_index]};
        for (std::uint32_t k {0}; k < cc.range_count; ++k) {
          const detail::code_range& r {prog_.cp_ranges[cc.range_begin + k]};
          if (r.lo > cp_page_max) {
            break; // ranges are sorted: nothing more falls in the page
          }
          const std::uint32_t lo {r.lo < 0x80U ? 0x80U : r.lo};
          const std::uint32_t hi {r.hi > cp_page_max ? cp_page_max : r.hi};
          for (std::uint32_t c {lo}; c <= hi; ++c) {
            const std::uint32_t bit {c - 0x80U};
            state_.cp_page[bit >> 6U] |= std::uint64_t {1} << (bit & 63U);
          }
        }
        state_.cp_page_class = key;
      }
      return state_.cp_page.data();
    }

    /*!
     * \brief Writes a class-loop fast-path result into \p out_slots: the whole-match span in slots
     *        0/1, and — for a pattern wrapped in one capturing group (`(\w+)`, `([a-z]+)`) —
     *        the same span mirrored into the group's slots (its span equals the whole match by
     *        construction, so no re-match is needed). Sizes the slots to the program's slot count.
     */
    template <typename OutSlots>
    constexpr void fill_span_slots(OutSlots&   out_slots,
                                   std::size_t match_start,
                                   std::size_t match_end) const
    {
      out_slots[0] = match_start;
      out_slots[1] = match_end;
      if (prog_.hints.greedy_group_start >= 0) {
        out_slots[static_cast<std::size_t>(prog_.hints.greedy_group_start)] = match_start;
        out_slots[static_cast<std::size_t>(prog_.hints.greedy_group_end)]   = match_end;
      }
    }

    //! \brief OPT-C run tail: the next stop byte at or after \p from, or the text end. Kept in its own
    //!        function so the memchr-cascade never inlines into \ref run_class_loop's hot per-byte loop
    //!        (that bloat measurably slowed stop-dense short runs). Reached only once a run has already
    //!        passed \ref cascade_run_threshold accepted bytes, so the out-of-line call is free.
    [[nodiscard]] constexpr std::size_t run_cascade_stop(std::string_view text,
                                                         std::size_t      from) const
    {
      const std::size_t stop {
        find_bytes_cascade(text, from, prog_.hints.stop_set.data(), prog_.hints.stop_set_size)};
      return stop == npos ? text.size() : stop;
    }

    /*!
     * \brief Fast path for a whole-pattern "class+".
     *
     * Matches a maximal run of class bytes with one scan loop — exactly the
     * VM's greedy result, with no thread lists.
     *
     * \tparam Cascade  Take the OPT-C memchr-cascade run tail (chosen once per walk from stop_set_size).
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the (start, end) span on success.
     * \return `true` if a non-empty run was found.
     */
    template <bool Cascade, typename OutSlots>
    constexpr bool run_class_loop(std::string_view text,
                                  std::size_t      start,
                                  run_mode         mode,
                                  OutSlots&        out_slots)
    {
      const std::uint8_t* const tbl =
        class_table(static_cast<std::size_t>(prog_.hints.greedy_class_loop));
      const auto in_class = [&](std::size_t i) {
                              return tbl[static_cast<std::uint8_t>(text[i])] != 0U;
                            };
      std::size_t match_start {start};
      if (mode == run_mode::search) {
        while (match_start < text.size() && !in_class(match_start)) {
          ++match_start;
        }
      }
      if (match_start >= text.size() || !in_class(match_start)) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      // OPT-C: this is a byte-wise run whose accepted set may have a small complement (the STOP bytes).
      // Only the Cascade instantiation carries the memchr-cascade; it advances per byte until a run
      // passes a 32-byte threshold — a genuinely long run — then jumps to the next stop by a cascade, so
      // a stop-dense stream of short runs stays on the per-byte path. The constant-evaluation guard is a
      // per-call property, hoisted out of the loop; at compile time the plain loop runs. The common
      // classes (`[a-z]+`, `\d+`) compile to the pristine per-byte loop with none of the cascade code.
      // Sound because run_class_loop never validates UTF-8 (the OPT-C perimeter pin, test_utf8.cpp).
      std::size_t match_end {match_start + 1};
      if constexpr (Cascade) {
        if (!std::is_constant_evaluated()) {
          while (match_end < text.size() && in_class(match_end)) {
            ++match_end;
            if (match_end - match_start == cascade_run_threshold) {
              match_end = run_cascade_stop(text, match_end);
              break;
            }
          }
        }
        else {
          while (match_end < text.size() && in_class(match_end)) {
            ++match_end;
          }
        }
      }
      else {
        while (match_end < text.size() && in_class(match_end)) {
          ++match_end;
        }
      }
      if (mode == run_mode::full && match_end != text.size()) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      out_slots.assign(prog_.slot_count, npos);
      fill_span_slots(out_slots, match_start, match_end);
      return true;
    }

    /*!
     * \brief Fast path for a whole-pattern code-point class `klass_cp`, optionally a greedy `+`.
     *
     * Scans code points directly against the class predicate (ASCII bitmap below 0x80, range binary
     * search above), advancing by the code point's byte width, with no thread lists — the analog of
     * \ref run_class_loop for a Unicode shorthand (`\w+`, `\d+`, `\s+`). A malformed sequence stops
     * the run, exactly as the VM's `klass_cp` fails on it.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if a non-empty run was found.
     */
    template <typename OutSlots>
    constexpr bool run_cp_class_loop(std::string_view text,
                                     std::size_t      start,
                                     run_mode         mode,
                                     OutSlots&        out_slots)
    {
      const std::size_t          cp_index {static_cast<std::size_t>(prog_.hints.greedy_cp_class)};
      const detail::cp_class&    cc       {prog_.cp_classes[cp_index]};
      const std::uint8_t* const  asc      {cp_ascii_table(cp_index)};
      // Membership of a non-ASCII code point (>= 0x80): a one-load page-bitmap test over the two-byte
      // range, the range search only for CJK / astral code points beyond it. The page is built
      // lazily on the first non-ASCII code point, so a pure-ASCII scan never pays for it.
      const auto member_hi = [&](char32_t cp) -> bool {
                               if (cp <= cp_page_max) {
                                 const std::uint64_t* const page {cp_page_table(cp_index)};
                                 const std::uint32_t        bit  {static_cast<std::uint32_t>(cp) - 0x80U};
                                 return ((page[bit >> 6U] >> (bit & 63U)) & std::uint64_t {1}) != 0U;
                               }
                               return cp_class_matches(cc, cp);
                             };
      // Byte width of a matching code point at i, or 0. Used for the leftmost-scan step and the first
      // code point; the hot greedy run is scanned inline below.
      const auto width = [&](std::size_t i) -> std::size_t {
                           const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
                           if (!dc.valid) {
                             return 0;
                           }
                           const bool m {dc.cp < 0x80U ? asc[dc.cp] != 0U : member_hi(dc.cp)};
                           return m ? dc.length : 0;
                         };
      out_slots.assign(prog_.slot_count, npos);
      std::size_t match_start {start};
      if (mode == run_mode::search) {
        while (match_start < text.size() && width(match_start) == 0) {
          ++match_start;
        }
      }
      if (match_start >= text.size()) {
        return false;
      }
      // The first code point must match: this path is only chosen for `\w`/`\w+` (never nullable), so
      // it reports a non-empty match or none — it can never produce the empty match `forbid_empty_until_`
      // guards, which is why that state is not consulted here (see the fast-path dispatch in run()).
      const std::size_t first {width(match_start)};
      if (first == 0) {
        return false;
      }
      std::size_t match_end {match_start + first};
      if (prog_.hints.greedy_cp_class_plus) {
        while (match_end < text.size()) {
          // Tight ASCII inner loop: a byte-indexed table lookup with no decode and no call,
          // the same one-load trick the byte-NFA scan loop uses; only a non-ASCII lead decodes.
          const auto lead {static_cast<std::uint8_t>(text[match_end])};
          if (lead < 0x80U) {
            if (asc[lead] == 0U) {
              break;
            }
            ++match_end;
            continue;
          }
          const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, match_end)};
          if (!dc.valid || !member_hi(dc.cp)) {
            break;
          }
          match_end += dc.length;
        }
      }
      if (mode == run_mode::full && match_end != text.size()) {
        return false;
      }
      fill_span_slots(out_slots, match_start, match_end);
      return true;
    }

    /*!
     * \brief Matches the run of byte/klass instructions starting at \p pc.
     *
     * Shared by the fixed-shape and alternation fast paths. Consumes one text
     * byte per instruction and stops at the first non-consuming op (a save,
     * jump or match).
     *
     * \param[in] text The subject text.
     * \param[in] pc   Index of the first instruction of the run.
     * \param[in] s    Text offset to match from.
     * \return The end offset on a full match, or \ref npos on a mismatch.
     */
    template <bool SkipSaves = false>
    [[nodiscard]] constexpr std::size_t match_byte_klass_run(std::string_view text,
                                                             std::size_t      pc,
                                                             std::size_t      s) const
    {
      std::size_t consumed {};
      while (pc < prog_.code.size()) {
        const instr& instruction {prog_.code[pc]};
        if constexpr (SkipSaves) {
          // Grouped fixed shape: interleaved capturing saves are epsilon here (slots filled
          // separately). if constexpr keeps this branch out of the no-group tight loop entirely.
          if (instruction.op == opcode::save) {
            ++pc;
            continue;
          }
        }
        if (instruction.op != opcode::byte && instruction.op != opcode::klass) {
          break;
        }
        if (s + consumed >= text.size()) {
          return npos;
        }
        const auto byte_value {static_cast<std::uint8_t>(text[s + consumed])};
        const bool ok         {instruction.op == opcode::byte ? byte_value == instruction.arg8
                                                   : prog_.classes[instruction.arg16].test(byte_value)};
        if (!ok) {
          return npos;
        }
        ++consumed;
        ++pc;
      }
      return s + consumed;
    }

    /*!
     * \brief Leftmost search by scanning candidate positions (first-byte hints).
     *
     * Shared by the fast paths that verify a fixed shape at a position: it walks
     * candidate starts via \ref next_candidate and reports the first that
     * \p match_at accepts.
     *
     * \tparam MatchAt  Callable `std::size_t(std::size_t pos)` returning the
     *                  match end at \p pos, or \ref npos.
     * \tparam OutSlots Output slot container (already sized to two).
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  match_at  The per-position matcher.
     * \param[out] out_slots Receives the (start, end) span on success.
     * \return `true` if a match was found.
     */
    template <typename MatchAt, typename OutSlots>
    constexpr bool fast_search(std::string_view text,
                               std::size_t      start,
                               MatchAt          match_at,
                               OutSlots&        out_slots)
    {
      std::size_t match_start {start};
      while (match_start <= text.size()) {
        match_start = next_candidate(text, match_start, start);
        if (match_start > text.size()) {
          break;
        }
        const std::size_t match_end {match_at(match_start)};
        if (match_end != npos) {
          out_slots[0] = match_start;
          out_slots[1] = match_end;
          return true;
        }
        ++match_start;
      }
      return false;
    }

    /*!
     * \brief Fast path for a whole-pattern fixed-width byte/klass sequence.
     *
     * A straight-line program (no branches/assertions) has exactly one thread,
     * so a match is a fixed-width sequence verified by a single walk: each
     * `byte`/`klass` instruction consumes one text byte. There is no greedy/lazy
     * ambiguity. Covers `class{n}` and mixed shapes like `\d{4}-\d{2}-\d{2}`.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if the sequence matched.
     */
    template <typename OutSlots>
    constexpr bool run_fixed_shape(std::string_view text,
                                   std::size_t      start,
                                   run_mode         mode,
                                   OutSlots&        out_slots)
    {
      // No inner groups (slot_count 2): a contiguous byte/klass run, the original tight path unchanged.
      if (prog_.slot_count <= 2) {
        out_slots.assign(2, npos);
        const auto at {[&](std::size_t s) { return match_byte_klass_run<false>(text, 1, s); }};
        if (mode != run_mode::search) {
          const std::size_t match_end {at(start)};
          if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
            return false;
          }
          out_slots[0] = start;
          out_slots[1] = match_end;
          return true;
        }
        return fast_search(text, start, at, out_slots);
      }

      // Inner capturing groups: the run has interleaved saves, so the verify walk skips them
      // (SkipSaves) and the group slots are filled from their constant offsets on success only (not per
      // failed candidate). A separate body keeps the no-group loop above free of any grouping branch.
      out_slots.assign(prog_.slot_count, npos);
      const auto at {[&](std::size_t s) { return match_byte_klass_run<true>(text, 1, s); }};
      if (mode != run_mode::search) {
        const std::size_t match_end {at(start)};
        if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
          return false;
        }
        out_slots[0] = start;
        out_slots[1] = match_end;
        fill_fixed_saves(start, out_slots);
        return true;
      }
      if (!fast_search(text, start, at, out_slots)) {
        return false;
      }
      fill_fixed_saves(out_slots[0], out_slots); // out_slots[0] is the winning match start
      return true;
    }

    /*!
     * \brief Fills the capturing-group slots of a fixed-shape match. Every consuming op is one
     *        byte wide, so each save sits at a constant offset from the match start; a single linear
     *        pass writes `slot = match_start + offset`. No-op when the pattern has no inner groups
     *        (slot_count 2). Not a re-match: the bytes were already verified.
     * \param[in]  match_start Byte offset where the match begins.
     * \param[out] out_slots   Receives the group slots.
     */
    template <typename OutSlots>
    constexpr void fill_fixed_saves(std::size_t match_start,
                                    OutSlots&   out_slots) const
    {
      if (prog_.slot_count <= 2) {
        return;
      }
      std::size_t offset {};
      for (std::size_t pc {1}; pc < prog_.code.size(); ++pc) {
        const instr& instruction {prog_.code[pc]};
        if (instruction.op == opcode::byte || instruction.op == opcode::klass) {
          ++offset;
        }
        else if (instruction.op == opcode::save) {
          out_slots[static_cast<std::size_t>(instruction.arg16)] = match_start + offset;
        }
        else {
          break; // reached match
        }
      }
    }

    /*!
     * \brief Fast path for `.` / a negated class, optionally a greedy `+`.
     *
     * Scans codepoints directly, mirroring the byte-level expansion the VM would
     * run: an ASCII byte matches the ASCII set; a valid 2–4 byte UTF-8 sequence
     * always matches (a negated ASCII class excludes only ASCII); anything else
     * (lone continuation, bad lead, truncation) stops, exactly as the VM's
     * lead/continuation branches would fail. Covers `.+`, `[^,]+`, `.`, `[^,]`.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if at least one codepoint matched.
     */
    template <bool Cascade, typename OutSlots>
    constexpr bool run_codepoint_class(std::string_view text,
                                       std::size_t      start,
                                       run_mode         mode,
                                       OutSlots&        out_slots)
    {
      const std::uint8_t* const ascii {
        class_table(static_cast<std::size_t>(prog_.hints.codepoint_class_ascii))};
      out_slots.assign(2, npos);

      const auto cont = [&](std::size_t i) {
                          const auto cont_byte {static_cast<std::uint8_t>(text[i])};
                          return cont_byte >= 0x80 && cont_byte <= 0xBF;
                        };
      // Byte length of a matching codepoint at i, or 0 for no match.
      const auto width = [&](std::size_t i) -> std::size_t {
                           const auto byte_value {static_cast<std::uint8_t>(text[i])};
                           if (byte_value < 0x80) {
                             return ascii[byte_value] != 0U ? 1 : 0;
                           }
                           if (byte_value >= 0xC2 && byte_value <= 0xDF) {
                             return i + 1 < text.size() && cont(i + 1) ? 2 : 0;
                           }
                           if (byte_value >= 0xE0 && byte_value <= 0xEF) {
                             return i + 2 < text.size() && cont(i + 1) && cont(i + 2) ? 3 : 0;
                           }
                           if (byte_value >= 0xF0 && byte_value <= 0xF4) {
                             return i + 3 < text.size() && cont(i + 1) && cont(i + 2) && cont(i + 3) ? 4 : 0;
                           }
                           return 0;
                         };

      std::size_t match_start {start};
      if (mode == run_mode::search) {
        while (match_start < text.size() && width(match_start) == 0) {
          ++match_start;
        }
      }
      if (match_start >= text.size()) {
        return false;
      }
      const std::size_t first_width {width(match_start)};
      if (first_width == 0) {
        return false;
      }
      std::size_t match_end {match_start + first_width};
      if (prog_.hints.codepoint_class_plus) {
        const auto scalar_scan = [&]() {
                                   while (match_end < text.size()) {
                                     const std::size_t codepoint_width {width(match_end)};
                                     if (codepoint_width == 0) {
                                       break;
                                     }
                                     match_end += codepoint_width;
                                   }
                                 };
        if constexpr (Cascade) {
          if (!std::is_constant_evaluated()) {
            // OPT-C-1b SWAR: the next ASCII stop bounds the whole run (an ASCII byte can never lie inside
            // a multi-byte cluster), so memchr it ONCE. Then walk [match_end, p1): the high-bit scan
            // skips ASCII stretches eight bytes at a time, and only a non-ASCII cluster drops to code-
            // point validation. A pure-ASCII stretch to the stop is exact (ASCII text == bytes); a
            // malformed sequence still stops the run via width() == 0 — the C-0 property, preserved.
            const std::size_t stop      {find_bytes_cascade(text, match_end, prog_.hints.stop_set.data(),
                                                            prog_.hints.stop_set_size)};
            const std::size_t p1        {stop == npos ? text.size() : stop};
            bool              malformed {false};
            while (match_end < p1) {
              const std::size_t high {first_high_byte(text, match_end, p1)};
              if (high == p1) {
                break; // the rest of [match_end, p1) is pure ASCII
              }
              match_end = high; // validate the non-ASCII cluster at `high`
              const std::size_t w {width(match_end)};
              if (w == 0) {
                malformed = true;
                break;
              }
              match_end += w;
            }
            if (!malformed) {
              match_end = p1; // the whole run up to the stop / text end is accepted
            }
          }
          else {
            scalar_scan();
          }
        }
        else {
          scalar_scan();
        }
      }
      if (mode == run_mode::full && match_end != text.size()) {
        return false;
      }
      out_slots[0] = match_start;
      out_slots[1] = match_end;
      return true;
    }

    /*!
     * \brief Fast path for an alternation of straight-line branches.
     *
     * Each branch is a fixed-width byte/klass sequence, so at a candidate the
     * branches are tried in source order (leftmost-first priority) and the first
     * that matches wins — exactly the Pike VM's thread priority. The branch
     * structure is read directly from the split chain in the program.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if some branch matched.
     */
    template <typename OutSlots>
    constexpr bool run_alternation(std::string_view text,
                                   std::size_t      start,
                                   run_mode         mode,
                                   OutSlots&        out_slots)
    {
      out_slots.assign(2, npos);
      const auto& code {prog_.code};

      // First branch that matches at \p s (and, for full, spans to the end). The
      // branches are read from the split chain in source order (highest priority
      // first), mirroring the VM's thread priority.
      const auto match_at = [&](std::size_t match_start, bool require_full) -> std::size_t {
                              std::size_t pc {1};
                              while (true) {
                                const bool        is_split  {code[pc].op == opcode::split};
                                const std::size_t branch    {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};
                                const std::size_t match_end {match_byte_klass_run(text, branch, match_start)};
                                if (match_end != npos && (!require_full || match_end == text.size())) {
                                  return match_end;
                                }
                                if (!is_split) {
                                  return npos;
                                }
                                pc = static_cast<std::size_t>(code[pc].secondary_target);
                              }
                            };

      if (mode != run_mode::search) {
        const std::size_t match_end {match_at(start, mode == run_mode::full)};
        if (match_end == npos) {
          return false;
        }
        out_slots[0] = start;
        out_slots[1] = match_end;
        return true;
      }
      return fast_search(text, start, [&](std::size_t match_start) { return match_at(match_start, false); }, out_slots);
    }

    /*!
     * \brief Tests whether the fixed literal prefix occurs at \p cand.
     * \param[in] text The subject text.
     * \param[in] cand Candidate start offset.
     * \param[in] len  Length of the literal (`hints.exact_literal_len`).
     * \return `true` if `text[cand : cand+len]` equals the literal.
     */
    [[nodiscard]] constexpr bool literal_at(std::string_view text,
                                            std::size_t      cand,
                                            std::size_t      len) const
    {
      if (cand + len > text.size()) {
        return false;
      }
      const auto pfx {std::string_view(prog_.hints.prefix.data(), len)};
      if (std::is_constant_evaluated()) {
        return text.substr(cand, len) == pfx;
      }
      return std::memcmp(text.data() + cand, pfx.data(), len) == 0;
    }

    /*!
     * \brief Fills capture slots for a literal match at \p cand.
     *
     * Replays `save` instructions at their consumed offsets and checks any
     * zero-width assertions in the chain at \p cand.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  cand      Start offset of the literal match.
     * \param[in]  len       Length of the literal.
     * \param[out] out_slots Receives the capture slots.
     * \return `false` (and clears \p out_slots) if an assertion fails here, so
     *         the caller tries the next occurrence; `true` otherwise.
     */
    template <typename OutSlots>
    constexpr bool replay_literal(std::size_t cand,
                                  std::size_t len,
                                  OutSlots&   out_slots) const
    {
      out_slots.assign(prog_.slot_count, npos);
      std::size_t consumed {};
      for (std::size_t pc = 0; pc < prog_.code.size(); ++pc) {
        const instr& instruction {prog_.code[pc]};
        if (instruction.op == opcode::save) {
          out_slots[instruction.arg16] = cand + consumed;
        }
        else if (instruction.op == opcode::assert_position) {
          if (!assertion_holds(static_cast<assert_kind>(instruction.arg8), cand + consumed, instruction.arg16 != 0U)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
        }
        else if ((instruction.op == opcode::byte || instruction.op == opcode::klass) && consumed < len) {
          ++consumed;
        }
        else if (instruction.op == opcode::match) {
          break;
        }
      }
      if (prog_.slot_count >= 2 && out_slots[1] == npos) {
        out_slots[1] = cand + len; // group 0 end, even if replay ended early
      }
      return true;
    }

    /*!
     * \brief Fast path for a pure-literal pattern.
     *
     * The prefilter locates the fixed bytes; this replays saves directly, with
     * no thread lists, epsilon stack or per-position stepping. A leading or
     * trailing zero-width assertion (`\b`, `^`, `$` …) may make a given
     * occurrence fail, so in search mode it scans successive occurrences until
     * the assertions hold — the case a differential-fuzz finding (`\B2` on
     * `"220"`) exposed.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the capture slots on success.
     * \return `true` if a match was found.
     */
    template <typename OutSlots>
    constexpr bool run_exact_literal(std::string_view text,
                                     std::size_t      start,
                                     run_mode         mode,
                                     OutSlots&        out_slots)
    {
      const std::size_t len {static_cast<std::size_t>(prog_.hints.exact_literal_len)};
      if (len == 0) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      if (mode != run_mode::search) {
        const bool full_ok = mode != run_mode::full || start + len == text.size();
        const bool ok      = literal_at(text, start, len) && full_ok &&
                             replay_literal(start, len, out_slots);
        if (!ok) {
          out_slots.assign(prog_.slot_count, npos);
        }
        return ok;
      }
      std::size_t from {start};
      while (true) {
        const std::size_t cand {next_candidate(text, from, start)};
        if (cand > text.size() || cand + len > text.size()) {
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }
        if (literal_at(text, cand, len) && replay_literal(cand, len, out_slots)) {
          return true;
        }
        from = cand + 1; // assertion failed here; try the next occurrence
      }
    }

    /*!
     * \brief First position >= \p pos that could start a match, per the hints.
     *
     * The prefilter step: jumps over positions that provably cannot start a
     * match (literal prefix search, unique first byte, line start, first-byte
     * set). Returns \p pos itself when no skipping applies.
     *
     * \param[in] text  The subject text.
     * \param[in] pos   Current position.
     * \param[in] start The run's start offset (for one-shot anchored patterns).
     * \return The next candidate offset, or \ref real::npos if none exists.
     */
    [[nodiscard]] constexpr std::size_t next_candidate(std::string_view text,
                                                       std::size_t      pos,
                                                       std::size_t      start) const
    {
      const pattern_hints& hints {prog_.hints};
      if (hints.anchored_start) {
        return pos == start ? pos : npos; // one shot at the start
      }
      if (hints.prefix_size >= 2) {
        return find_prefix(text, pos, std::string_view(hints.prefix.data(), hints.prefix_size));
      }
      if (hints.rare_byte >= 0) {
        // A required rare byte sits `rare_offset` into every match: memchr it (SIMD), then back up to the
        // candidate start. Far more selective than scanning a common first-byte class per byte. The VM
        // still verifies the candidate, so a false back-up is simply rejected there.
        const std::size_t from {pos + hints.rare_offset};
        if (from > text.size()) {
          return npos;
        }
        const std::size_t hit {find_byte(text, from, static_cast<char>(hints.rare_byte))};
        return hit == npos ? npos : hit - hints.rare_offset;
      }
      if (hints.single_first >= 0) {
        return find_byte(text, pos, static_cast<char>(hints.single_first));
      }
      if (hints.line_anchored && pos != start) {
        const std::size_t nl {find_byte(text, pos - 1, '\n')};
        return nl == npos ? npos : nl + 1;
      }
      if (hints.small_set_size >= 2) {
        // Adaptive: probe a short window with the bitmap loop first (one test per byte — the baseline
        // cost), so a near hit on dense text is found without paying the cascade's per-member memchr
        // overhead. Only when the window is clean (the set bytes are sparse) does the vectorised cascade
        // take over the long scan. The threshold is where measurement put the crossover.
        constexpr std::size_t probe      {32};
        const std::size_t     window_end {pos + probe < text.size() ? pos + probe : text.size()};
        std::size_t           p          {pos};
        while (p < window_end && !hints.first_bytes.test(static_cast<std::uint8_t>(text[p]))) {
          ++p;
        }
        if (p < window_end) {
          return p; // a near (dense) hit — the bitmap probe found it at baseline cost
        }
        if (window_end == text.size()) {
          return npos;
        }
        return find_bytes_cascade(text, window_end, hints.small_set.data(), hints.small_set_size);
      }
      if (hints.first_bytes_valid) {
        while (pos < text.size() &&
               !hints.first_bytes.test(static_cast<std::uint8_t>(text[pos]))) {
          ++pos;
        }
        return pos < text.size() ? pos : npos;
      }
      return pos;
    }

    /*!
     * \brief Cheap pre-check before seeding a new thread at \p pos.
     *
     * Live threads may force the loop through positions the prefilter would
     * have skipped; this avoids seeding where a match cannot start. It also
     * enforces codepoint alignment: in non-byte mode a UTF-8 continuation byte
     * is never a valid match start.
     *
     * \param[in] text  The subject text.
     * \param[in] pos   The candidate seed position.
     * \param[in] start The run's start offset.
     * \return `true` if a fresh thread should be seeded at \p pos.
     */
    [[nodiscard]] constexpr bool seed_viable(std::string_view text,
                                             std::size_t      pos,
                                             std::size_t      start) const
    {
      const pattern_hints& hints {prog_.hints};
      if (hints.anchored_start && pos != start) {
        return false;
      }
      // A match can never start inside a multi-byte codepoint: in non-byte mode
      // a UTF-8 continuation byte (10xxxxxx) is not a valid start position. This
      // keeps zero-width matches (\b, \B, ^, $, empty) codepoint-aligned, like a
      // codepoint-based engine — bytes mode seeds every byte.
      if (!prog_.byte_mode && pos < text.size() &&
          (static_cast<std::uint8_t>(text[pos]) & 0xC0U) == 0x80U) {
        return false;
      }
      if (!hints.first_bytes_valid) {
        return true;
      }
      return pos < text.size() && hints.first_bytes.test(static_cast<std::uint8_t>(text[pos]));
    }

    /*!
     * \brief Word-ness of the code point **ending exactly at** \p pos — the left side of a `\b`/`\B`/
     *        `\<`/`\>` boundary. False at the text start. In text mode it back-decodes the code point
     *        (up to three continuation bytes to the lead) and requires the sequence to end exactly at
     *        \p pos, so a malformed or misaligned run reads as non-word; bytes / `re.A` stay byte-level.
     *        This is the shared frontier notion (the same decode that codepoint alignment uses).
     */
    [[nodiscard]] constexpr bool word_before(std::size_t pos,
                                             bool        ascii_word) const
    {
      return real::detail::word_before(text_, pos, ascii_word); // shared free function (assert_eval.hpp)
    }

    //! \brief Word-ness of the code point **starting at** \p pos — the right side of a boundary. False
    //!        at the text end or on a malformed sequence; bytes / `re.A` stay byte-level.
    [[nodiscard]] constexpr bool word_after(std::size_t pos,
                                            bool        ascii_word) const
    {
      return real::detail::word_after(text_, pos, ascii_word); // shared free function (assert_eval.hpp)
    }

    /*!
     * \brief Evaluates a zero-width assertion at \p pos in the current text.
     * \param[in] kind The assertion to evaluate.
     * \param[in] pos  The position at which to evaluate it.
     * \param[in] word_ness_flipped For a word assert (`\b \B \< \>`), whether this instruction flips
     *            the program's default word-ness — set for a scoped `(?a:...)` / `(?-a:...)` island.
     * \return `true` if the assertion holds there.
     */
    [[nodiscard]] constexpr bool assertion_holds(assert_kind kind,
                                                 std::size_t pos,
                                                 bool        word_ness_flipped) const
    {
      // A word assert's word-ness is the program default (\ref program_view::unicode_word), flipped by
      // the instruction's flip bit for a scoped (?a:...) / (?-a:...) island — so non-scoped programs
      // keep flip == 0 and are byte-identical. ascii_word == unicode default matches iff not flipped.
      const bool ascii_word {prog_.unicode_word == word_ness_flipped};
      return real::detail::assertion_holds(kind, text_, pos, ascii_word); // shared free function
    }

    /*!
     * \brief Advances every thread of \p clist by the byte at \p pos.
     *
     * Survivors that consumed a byte land in \p nlist. A thread reaching
     * `match` records its slots and cuts all lower-priority threads, so
     * priority (leftmost-greedy) order is preserved.
     *
     * \tparam OutSlots Output slot container.
     * \param[in,out] clist     The current thread list (consumed).
     * \param[in,out] nlist     The next thread list (receives survivors).
     * \param[in]     pos        The current input position.
     * \param[in]     mode       Anchoring mode (affects `match` acceptance).
     * \param[in,out] matched    Set to `true` when a match is recorded.
     * \param[out]    out_slots  Receives the slots of an accepted match.
     */
    template <typename OutSlots>
    constexpr void step(list_type&  clist,
                        list_type&  nlist,
                        std::size_t pos,
                        run_mode    mode,
                        bool&       matched,
                        OutSlots&   out_slots)
    {
      const std::uint16_t slot_count {prog_.slot_count};
      for (std::size_t i = 0; i < clist.pcs.size(); ++i) {
        const std::int32_t pc          {clist.pcs[i]};
        const instr&       instruction {prog_.code[static_cast<std::size_t>(pc)]};
        switch (instruction.op) {
          case opcode::byte:
            if (pos < text_.size() &&
                static_cast<std::uint8_t>(text_[pos]) == instruction.arg8) {
              advance_thread(clist, nlist, i, pc + 1, pos + 1);
            }
            break;
          case opcode::klass:
            if (pos < text_.size() &&
                prog_.classes[instruction.arg16].test(static_cast<std::uint8_t>(text_[pos]))) {
              advance_thread(clist, nlist, i, pc + 1, pos + 1);
            }
            break;
          case opcode::klass_cp:
            if (pos < text_.size()) {
              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, pos)};
              if (dc.valid &&
                  cp_class_matches(prog_.cp_classes[instruction.arg16], dc.cp)) {
                advance_thread(clist, nlist, i,
                               pc + 1 + static_cast<std::int32_t>(4 - dc.length), pos + 1);
              }
            }
            break;
          case opcode::match:
            {
              if (mode == run_mode::full && pos != text_.size()) {
                break; // must consume the whole text: thread dies
              }
              // The winning thread's capture slots — its COW block.
              const std::size_t* const won {thread_slots(clist, i)};
              // Reject an empty match forbidden at this position; a lower-priority
              // thread may still consume a byte and win a non-empty match here.
              if (pos == won[0] && won[0] < forbid_empty_until_) {
                break;
              }
              for (std::uint16_t s = 0; s < slot_count; ++s) {
                out_slots[s] = won[s];
              }
              matched = true;
              return; // drop lower-priority threads
            }
          case opcode::split:
          case opcode::jump:
          case opcode::save:
          case opcode::assert_position:
          case opcode::assert_lookaround:
            break; // epsilon ops never appear in a stepped list
        }
      }
    }

    /*!
     * \brief Advances thread \p i of \p clist by one consumed byte, seeding its continuation's closure into
     *        \p nlist (OPT D1). The closure takes its own reference on the thread's capture block — no slot
     *        copy; the block is shared until a `save` copies it on write.
     */
    constexpr void advance_thread(list_type&   clist,
                                  list_type&   nlist,
                                  std::size_t  i,
                                  std::int32_t next_pc,
                                  std::size_t  next_pos)
    {
      const std::uint32_t block {static_cast<std::uint32_t>(clist.slots[i])};
      state_.pool.incref(block); // the new closure holds its own ref (paired with cow_release_blocks)
      add_thread(nlist, next_pc, next_pos, block);
    }

    /*!
     * \brief Pointer to thread \p i's `slot_count` capture values — its COW block's slots (OPT D1). Used by
     *        the `match` case to read out the winner.
     */
    [[nodiscard]] constexpr const std::size_t* thread_slots(list_type&  clist,
                                                            std::size_t i)
    {
      return state_.pool.slots(static_cast<std::uint32_t>(clist.slots[i]));
    }

    /*!
     * \brief Tests a decoded code point against a `klass_cp` class: ASCII bitmap below 0x80, a binary
     *        search of the class's range slice above. The class is already the effective set, so this
     *        is a plain positive membership test.
     * \param[in] cc The code-point class (from `prog_.cp_classes`).
     * \param[in] cp The decoded code point.
     * \return Whether \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_class_matches(const detail::cp_class& cc,
                                                  char32_t                cp) const
    {
      bool member {};
      if (cp < 0x80U) {
        member = cc.ascii.test(static_cast<std::uint8_t>(cp));
      }
      else {
        std::size_t lo {cc.range_begin};
        std::size_t hi {static_cast<std::size_t>(cc.range_begin) + cc.range_count};
        while (lo < hi) {
          const std::size_t mid {lo + ((hi - lo) / 2)};
          if (prog_.cp_ranges[mid].hi < cp) {
            lo = mid + 1;
          }
          else {
            hi = mid;
          }
        }
        member = lo < static_cast<std::size_t>(cc.range_begin) + cc.range_count &&
                 cp >= prog_.cp_ranges[lo].lo && cp <= prog_.cp_ranges[lo].hi;
      }
      return member;
    }

    /*!
     * \brief Adds \p pc0 and its whole epsilon closure to \p list — the one closure walk (OPT D1). Each
     *        DFS frame carries a capture-block index (in `eps_entry::block`) rather than mutating a
     *        shared working array, so capture state is copy-on-write and there are no slot-restore entries:
     *
     * - a frame popped from the stack **owns one reference** to its block;
     * - `split` shares (incref: one ref → the two pushed frames), `jump` transfers it;
     * - `save` — the ONLY write — copies-on-write first if the block is shared (\ref capture_pool::cow_write);
     * - a failed assertion or an already-seen pc **releases** the ref (decref);
     * - a consuming/accept leaf **transfers** it into the thread list (one block handle per thread).
     *
     * \param[in,out] list          The thread list to populate (its `slots` hold one block index per pc).
     * \param[in]     pc0           The program counter to seed from.
     * \param[in]     pos           The current input position.
     * \param[in]     initial_block The block the walk starts on — the caller passes an already-owned ref.
     */
    constexpr void add_thread(list_type&    list,
                              std::int32_t  pc0,
                              std::size_t   pos,
                              std::uint32_t initial_block)
    {
      auto& pool  {state_.pool};
      auto& stack {state_.stack};
      stack.clear();
      stack.push_back({.pc = pc0, .block = initial_block});
      while (!stack.empty()) {
        const auto          entry {stack.back()};
        stack.pop_back();
        const std::int32_t  pc    {entry.pc};
        const std::uint32_t block {entry.block}; // this frame owns 1 ref
        if (list.seen(pc)) {
          pool.decref(block);
          continue;
        }
        list.mark_seen(pc);
        const instr& instruction {prog_.code[static_cast<std::size_t>(pc)]};
        switch (instruction.op) {
          case opcode::jump:
            {
              // Identical FIX-1/2 loop-exit routing as add_thread; only the ref travels along.
              std::int32_t head {instruction.primary_target};
              for (int hops = 0; hops < max_loop_hops && list.seen(head)
                   && prog_.code[static_cast<std::size_t>(head)].op == opcode::jump; ++hops) {
                head = prog_.code[static_cast<std::size_t>(head)].primary_target;
              }
              const instr&       head_instruction {prog_.code[static_cast<std::size_t>(head)]};
              const std::int32_t target           {list.seen(head) && head_instruction.op == opcode::split
                                         ? head_instruction.secondary_target
                                         : instruction.primary_target};
              stack.push_back({.pc = target, .block = block}); // transfer the ref
            }
            break;
          case opcode::split:
            pool.incref(block); // one held ref -> two pushed frames
            stack.push_back({.pc = instruction.secondary_target, .block = block});
            stack.push_back({.pc = instruction.primary_target, .block = block});
            break;
          case opcode::save:
            {
              // The one write: copy-on-write off the block if shared, then record pos in the slot.
              const std::uint32_t written {pool.cow_write(block, instruction.arg16, pos)};
              stack.push_back({.pc = pc + 1, .block = written});
            }
            break;
          case opcode::assert_position:
            if (assertion_holds(static_cast<assert_kind>(instruction.arg8), pos, instruction.arg16 != 0U)) {
              stack.push_back({.pc = pc + 1, .block = block});
            }
            else {
              pool.decref(block); // thread dies here
            }
            break;
          case opcode::assert_lookaround:
            if constexpr (requires(State & s) {
            s.lookaround;
          }) {
              if (lookaround_holds(instruction.arg16, pos)) {
                stack.push_back({.pc = pc + 1, .block = block});
              }
              else {
                pool.decref(block);
              }
            }
            else {
              pool.decref(block); // unreachable (this walk is instantiated only for the pool-bearing state)
            }
            break;
          case opcode::byte:
          case opcode::klass:
          case opcode::klass_cp:
          case opcode::match:
            list.pcs.push_back(pc);
            list.slots.push_back(block); // transfer the ref to the thread: one block handle per pc
            break;
        }
      }
    }

    /*!
     * \brief Releases the block references a list's threads hold (OPT D1), before the list is reset or the
     *        run returns. This is the one decref site paired with the incref at each step→closure boundary
     *        — the classic double-free locus, kept single.
     */
    constexpr void cow_release_blocks(list_type& list)
    {
      for (std::size_t i = 0; i < list.pcs.size(); ++i) {
        state_.pool.decref(static_cast<std::uint32_t>(list.slots[i]));
      }
    }

    /*!
     * \brief Evaluates a bounded lookaround at \p pos (true if the thread should proceed).
     *
     * Dispatches on direction and applies the negation. Both directions run a self-contained
     * Pike simulation of the sub-program region on a DEDICATED, isolated sub-scratch
     * (`state_.lookaround`) — the main `state_` (lists/working/stack) is never touched, so an
     * in-flight match is unaffected (the isolation invariant) — and are bounded to `l_max`
     * bytes (the source of strict linearity per position). The sub is capture-free; `(?!` /
     * `(?<!` negate the result.
     *
     * \param[in] sub_id Index into `prog_.lookarounds`.
     * \param[in] pos    The text position the assertion is evaluated at.
     * \return `true` if the (possibly negated) assertion holds, so the thread proceeds.
     */
    [[nodiscard]] constexpr bool lookaround_holds(std::uint16_t sub_id,
                                                  std::size_t   pos)
    {
      const lookaround_sub& sub     {prog_.lookarounds[sub_id]};
      const bool            matched {sub.direction == look_dir::behind
                                       ? lookbehind_matches(sub, pos)
                                       : lookahead_matches(sub, pos)};
      return sub.negative ? !matched : matched;
    }

    /*!
     * \brief Lookahead: does the sub-pattern match a prefix starting at \p pos?
     *
     * Forward Pike simulation from \p pos, bounded to `l_max` bytes, stopping at the first
     * `match` (the sub is capture-free, so any reached `match` is a witness).
     */
    [[nodiscard]] constexpr bool lookahead_matches(const lookaround_sub& sub,
                                                   std::size_t           pos)
    {
      const std::size_t code_size {prog_.code.size()};
      thread_list*      clist     {&state_.lookaround.lists[0]};
      thread_list*      nlist     {&state_.lookaround.lists[1]};
      clist->reset(code_size);
      nlist->reset(code_size);
      bool matched            {};
      sub_add_thread(*clist, sub.code_offset, pos, matched);
      const std::size_t limit {pos + static_cast<std::size_t>(sub.l_max)};
      for (std::size_t p {pos}; !matched && !clist->pcs.empty() && p < text_.size() && p < limit; ++p) {
        const auto byte_value {static_cast<std::uint8_t>(text_[p])};
        for (const std::int32_t pc : clist->pcs) {
          const instr& in      {prog_.code[static_cast<std::size_t>(pc)]};
          if (in.op == opcode::klass_cp) {
            // A code-point predicate inside the lookahead: decode once, then enter the continuation
            // chain via the computed skip (same mechanism as the main VM's step).
            const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, p)};
            if (dc.valid && cp_class_matches(prog_.cp_classes[in.arg16], dc.cp)) {
              sub_add_thread(*nlist, pc + 1 + static_cast<std::int32_t>(4 - dc.length), p + 1, matched);
            }
            continue;
          }
          // Otherwise the parked pc is a byte/klass; the ternary's else assumes klass.
          assert((in.op == opcode::byte || in.op == opcode::klass) && "lookaround parked a non-consuming op");
          const bool   consume {in.op == opcode::byte ? byte_value == in.arg8
                                                      : prog_.classes[in.arg16].test(byte_value)};
          if (consume) {
            sub_add_thread(*nlist, pc + 1, p + 1, matched);
          }
        }
        thread_list* const done {clist};
        clist = nlist;
        nlist = done;
        nlist->reset(code_size);
      }
      return matched;
    }

    /*!
     * \brief Lookbehind: does the sub-pattern match a window ENDING EXACTLY at \p pos?
     *
     * The match must finish precisely at \p pos, not merely somewhere inside the window —
     * the defining correctness trap of lookbehind. Candidate starts run from \p pos backward
     * to `pos - l_max` (bytes, A1); in non-bytes mode a start may not fall on a UTF-8
     * continuation byte, which would split a codepoint (A9). The first start whose sub-pattern
     * fullmatches `[s, pos)` is a witness.
     */
    [[nodiscard]] constexpr bool lookbehind_matches(const lookaround_sub& sub,
                                                    std::size_t           pos)
    {
      const std::size_t lmax         {static_cast<std::size_t>(sub.l_max)};
      const std::size_t window_start {pos > lmax ? pos - lmax : 0};
      for (std::size_t s {pos};; --s) {
        // s == pos reads nothing (always a valid boundary); for s < pos the start must begin
        // a codepoint — not a 0x80–0xBF continuation byte — unless we are in raw-bytes mode.
        const bool aligned {prog_.byte_mode || s >= pos
                            || (static_cast<std::uint8_t>(text_[s]) & 0xC0U) != 0x80U};
        if (aligned && sub_fullmatch_window(sub.code_offset, s, pos)) {
          return true;
        }
        if (s == window_start) {
          break; // reached the far edge; stop before s underflows past 0
        }
      }
      return false;
    }

    /*!
     * \brief Reports whether the sub-program, run from \p start, reaches `match` EXACTLY at
     *        \p pos (a fullmatch of `[start, pos)`), on the isolated sub-scratch.
     *
     * A `match` reached before \p pos (a shorter window) is deliberately discarded — lookbehind
     * requires the sub to end at \p pos. Touches only `state_.lookaround`.
     */
    [[nodiscard]] constexpr bool sub_fullmatch_window(std::int32_t code_offset,
                                                      std::size_t  start,
                                                      std::size_t  pos)
    {
      const std::size_t code_size {prog_.code.size()};
      thread_list*      clist     {&state_.lookaround.lists[0]};
      thread_list*      nlist     {&state_.lookaround.lists[1]};
      clist->reset(code_size);
      nlist->reset(code_size);
      bool here {false};
      sub_add_thread(*clist, code_offset, start, here);
      if (start == pos) {
        return here; // empty window: the sub must match the empty string exactly at pos
      }
      bool sink {false}; // matches reached before pos: collected then ignored
      for (std::size_t p {start}; p < pos; ++p) {
        if (clist->pcs.empty()) {
          return false;
        }
        const bool last       {p + 1 == pos};
        bool       at_pos     {false};
        const auto byte_value {static_cast<std::uint8_t>(text_[p])};
        for (const std::int32_t pc : clist->pcs) {
          const instr& in      {prog_.code[static_cast<std::size_t>(pc)]};
          if (in.op == opcode::klass_cp) {
            const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, p)};
            if (dc.valid && cp_class_matches(prog_.cp_classes[in.arg16], dc.cp)) {
              sub_add_thread(*nlist, pc + 1 + static_cast<std::int32_t>(4 - dc.length), p + 1,
                             last ? at_pos : sink);
            }
            continue;
          }
          // Otherwise the parked pc is a byte/klass; the ternary's else assumes klass.
          assert((in.op == opcode::byte || in.op == opcode::klass) && "lookaround parked a non-consuming op");
          const bool   consume {in.op == opcode::byte ? byte_value == in.arg8
                                                      : prog_.classes[in.arg16].test(byte_value)};
          if (consume) {
            sub_add_thread(*nlist, pc + 1, p + 1, last ? at_pos : sink);
          }
        }
        if (last) {
          return at_pos; // a match counts only when it ends exactly at pos
        }
        thread_list* const done {clist};
        clist = nlist;
        nlist = done;
        nlist->reset(code_size);
      }
      return false; // intentionally uncovered: the p+1==pos iteration always returns above
    }

    /*!
     * \brief Epsilon-closure for the lookaround sub-VM, on the isolated sub-scratch.
     *
     * Parks consuming (`byte`/`klass`) program counters in \p list and sets \p matched on
     * reaching the sub's `match`. A capture-free sub emits no `save` (handled defensively as
     * epsilon) and no `assert_lookaround` (nesting is rejected at compile time). Touches only
     * `state_.lookaround.stack`, never the main `state_`. Linearity: `mark_seen` dedups
     * epsilon threads within a generation; once `p` advances, the same (pc,p) cannot recur,
     * so each `assert_lookaround` is evaluated at most once per position → O(n·k·L). No memo
     * table is needed (it would be redundant and break constexpr).
     *
     * \param[in,out] list    The sub thread list to populate.
     * \param[in]     pc0     The sub-program counter to seed from.
     * \param[in]     pos     The current input position (for assertions).
     * \param[in,out] matched Set to `true` if the sub's `match` is reachable here.
     */
    constexpr void sub_add_thread(thread_list& list,
                                  std::int32_t pc0,
                                  std::size_t  pos,
                                  bool&        matched)
    {
      auto& stack {state_.lookaround.stack};
      stack.clear();
      stack.push_back({.pc = pc0, .block = 0});
      while (!stack.empty()) {
        const std::int32_t pc {stack.back().pc};
        stack.pop_back();
        if (list.seen(pc)) {
          continue;
        }
        list.mark_seen(pc);
        const instr& in {prog_.code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::jump:
            stack.push_back({.pc = in.primary_target, .block = 0});
            break;
          case opcode::split:
            stack.push_back({.pc = in.secondary_target, .block = 0});
            stack.push_back({.pc = in.primary_target, .block = 0});
            break;
          case opcode::save:
            // intentionally uncovered: a capture-free sub emits no `save`; kept as an
            // epsilon arm for completeness should the emission ever change.
            stack.push_back({.pc = pc + 1, .block = 0});
            break;
          case opcode::assert_position:
            if (assertion_holds(static_cast<assert_kind>(in.arg8), pos, in.arg16 != 0U)) {
              stack.push_back({.pc = pc + 1, .block = 0});
            }
            break;
          case opcode::match:
            matched = true;
            break;
          case opcode::byte:
          case opcode::klass:
          case opcode::klass_cp:
            list.pcs.push_back(pc);
            break;
          case opcode::assert_lookaround:
            // intentionally uncovered: -Wswitch exhaustiveness arm; nesting is rejected at
            // parse time, so a sub-program never contains an assert_lookaround.
            break;
        }
      }
    }
  };
} // namespace real::detail

#endif // REAL_PIKE_HPP
