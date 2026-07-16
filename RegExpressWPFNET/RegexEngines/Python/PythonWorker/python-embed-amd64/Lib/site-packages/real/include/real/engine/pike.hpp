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
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>).

#include "real/version.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <bit>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include "real/core/charclass.hpp"
#include "real/engine/prefilter.hpp"
#include "real/engine/simd.hpp" // the ISA-exclusive intrinsics behind the mask-carried scans below
#include <mutex>
#include <optional>

#include "real/automata/lazy_dfa.hpp"
#include "real/automata/onepass.hpp"
#include "real/core/program.hpp"
#include "real/core/profile.hpp"
#include "real/engine/aho_corasick.hpp"
#include "real/unicode/unicode_props.hpp"
#include "real/unicode/utf8.hpp"

namespace real::detail {

#if defined(__ARM_NEON) || defined(__SSE2__)
  /*!
   * \brief L-SIMD v3.2: fused scan+verify for a HOMOGENEOUS fixed shape (every position accepts the
   *        identical <= 2-range set — \ref pattern_hints::fixed_shape_simd_len > 0).
   *
   * Mirrors the two-level structure of the ceil_simd.cpp hex prototype: an outer loop skips whole
   * 16-byte windows with no candidate at all (one compare per 16 bytes — the coarse scan), and an inner
   * loop that, once a candidate is found, verifies it and — on a mismatch — finds the NEXT candidate by
   * reusing the mask it just computed (via \ref next_set_lane) rather than a fresh scalar scan
   * (`next_candidate` is not called at all here: the same homogeneous set is every position's
   * first-byte set, so the good-mask already carries where the next candidate is). A fresh mask is
   * still loaded at each *candidate* (not each byte), which is what makes the reuse sound: the low
   * `simd_len` lanes of a mask loaded AT a candidate are exactly that candidate's verify.
   *
   * Written ONCE against simd.hpp's uniform mask_t interface (\ref load_range_mask, \ref empty, \ref
   * first_lane, \ref window_all_set, \ref first_clear_lane, \ref next_set_lane) — no `#if` ISA branch
   * of its own. The intrinsics behind those primitives are ISA-exclusive by construction and live in
   * simd.hpp (excluded from the coverage floor for exactly that reason); this function is the
   * decision/loop logic — eligibility already decided by the caller, the block-boundary guard, the
   * skip-after-failure math, the tail hand-off — which is the SAME C++ on every ISA and is what the
   * ordinary test suite exercises, regardless of which SIMD leg compiled. (The first cut of this
   * function still had a `#if NEON ... #elif SSE2 ...` pair of near-identical loop bodies — dead
   * weight structurally uncoverable on the other ISA's CI runner, the actual coverage-check deficit;
   * this rewrite is the real fix, not another test chasing the symptom.)
   *
   * \param[in]  text        The subject text.
   * \param[in]  start       Offset to begin scanning from.
   * \param[in]  hints       The pattern's hints (`fixed_shape_lo0/hi0/lo1/hi1/simd_len`).
   * \param[out] resume_from On no match, where the scalar tail (`fast_search`/`next_candidate`) should
   *                         resume from — unset on a match.
   * \return The match start offset, or \ref npos if the SIMD scan found no match (< 16 bytes remaining
   *         from some point on, or no candidate byte left at all).
   */
  inline std::size_t simd_fixed_shape_scan(std::string_view      text,
                                           std::size_t           start,
                                           const pattern_hints&  hints,
                                           std::size_t&          resume_from)
  {
    const std::size_t L   {hints.fixed_shape_simd_len};
    const std::size_t sz  {text.size()};
    std::size_t       pos {start};
    while (pos + 16 <= sz) {
      std::array<std::uint8_t, 16> blk {};
      std::memcpy(blk.data(), text.data() + pos, 16); // MISRA-clean byte load (no pointer type-pun)
      const mask_t m                   {load_range_mask(blk.data(), hints.fixed_shape_lo0, hints.fixed_shape_hi0,
                                                        hints.fixed_shape_lo1, hints.fixed_shape_hi1)};
      if (empty(m)) {
        pos += 16; // no candidate anywhere in this window -- skip the whole block, one compare paid
        continue;
      }
      std::size_t c {pos + first_lane(m)};
      // Chain through every candidate this SAME loaded window can still verify -- reusing m (no
      // reload) as long as the candidate's own L-lane window fits inside [pos, pos + 16). Once a
      // candidate's verify would read past what m covers, stop the chain and reload fresh AT it (the
      // block-boundary case): querying m past what it actually covers is never attempted (the loop
      // condition below guards it), so there is no risk of a false negative OR a false skip.
      while (c + L <= pos + 16) {
        const std::size_t start_lane {c - pos};
        if (window_all_set(m, start_lane, L)) {
          resume_from = c;
          return c; // all L lanes from start_lane are in range -- a match at c
        }
        const std::size_t z   {first_clear_lane(m, start_lane, L)}; // absolute lane of the first failing byte
        const std::size_t nxt {next_set_lane(m, z + 1U)};           // reuse m -- no rescan: the next candidate lane
        if (nxt >= 16U) {
          c = pos + z + 1U; // no further candidate visible in the still-loaded window -- reload from here
          break;
        }
        c = pos + nxt; // next candidate, same loaded mask
      }
      pos = c; // either the next window to coarse-scan, or the exact candidate that needs a fresh load
    }
    resume_from = pos;
    return npos;
  }

#endif

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
    lookaround_scratch          lookaround;                  //!< Isolated sub-scratch for bounded lookaround evaluation.
    capture_pool                pool;                        //!< OPT D1: copy-on-write capture blocks (heap-backed).
    std::optional<lazy_dfa>     fwd_dfa;                     //!< Fallback when immut is null; prefer shared_fwd_dfa (D1).
    std::optional<reverse_dfa>  rev_dfa;                     //!< Fallback reverse; prefer shared_rev_dfa (D1).
    const void *                dfa_program       {nullptr}; //!< Program the per-state DFAs were built for (fallback).
    std::optional<reverse_dfa>  il_prefix_rev;               //!< Fallback IL prefix reverse; prefer shared_il_prefix_rev (D1).
    const void *                il_prefix_for     {nullptr}; //!< Fallback: prefix program il_prefix_rev was built for.
    const void *                il_text           {nullptr}; //!< IL: the haystack \ref il_abandoned refers to (reset the flag when it changes).
    bool                        il_abandoned      {false};   //!< IL: a linearity/density guard tripped on this haystack — stay on the core.
    std::uint32_t               il_density_cands  {};        //!< O1: IL candidates seen on this haystack (density sample).
    std::size_t                 il_density_origin {npos};    //!< O1: byte offset of the first IL candidate this haystack.
    // D1-AC fields placed LAST (own reason as pattern_hints::alternation_branch_count): inserting
    // here right after il_prefix_for -- as an earlier draft of this struct did -- shifted il_text/
    // il_abandoned/il_density_cands/il_density_origin (the inner-literal density-gate fields, read
    // on every search that takes that route) by the full size of std::optional<ac_automaton> plus
    // a pointer. Caught by re-inspection against dynamic_storage::state_type (storage.hpp), which
    // already had this right -- this struct (pike_state) is test-harness-only (the meta-seam
    // differential), never real::regex's own runtime state, so the mid-struct version never
    // affected any measured path; fixed anyway for consistency with the stated placement rule.
    std::optional<ac_automaton> ac_state;               //!< D1-AC: the multi-literal automaton (built once per program).
    const void*                 ac_state_for {nullptr}; //!< D1-AC: the program \ref ac_state was built for.
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
     * \param[in]  sem   Match semantics: \ref match_semantics::first (default, leftmost-first) or the
     *             experimental \ref match_semantics::longest (which forces the general loop, off every fast path).
     * \return `true` if a match was found.
     *
     * D1-AC gcc/x86 note: an x86 A/B (relayed, not independently run here) found `[^,]+` +39%
     * slower with the AC engine present but never dispatched to (a class-loop pattern returns from
     * \ref run_class_loop before ever reaching the fixed_alternation check) — callgrind/cachegrind
     * showed byte-identical instructions/cache misses, isolating it to front-end loop-alignment
     * codegen-luck from the AC code's mere presence in this translation unit, not a logic or
     * memory-access change. Two targeted `optimize("align-loops=N")` attempts were tried and
     * reverted: one on \ref run_class_loop (provably inert once force-inlined — confirmed on this
     * machine's own gcc via `-S`, the attribute does not travel with an `always_inline` callee's
     * body into its caller) and one here on `run()` itself (did align the right loop — confirmed
     * via the same `-S` method, `.p2align 6` where baseline only had one incidental occurrence —
     * but `[^,]+`, `[a-z]+`, and `\w+` all share this same inlined class-loop body with different
     * alignment optima, so uniformly forcing one traded the `[^,]+` regression for new ones on the
     * other two). No uniform alignment satisfies all three; accepted as a documented, gcc+x86-only,
     * arm64/clang-unaffected regression rather than trading one hot path's regression for another's
     * — the Alternation route's own gain (this file's `aho_corasick_route_disabled()` — see
     * `run_aho_corasick`) is unconditional and far larger.
     */
    template <bool Cascade = false, typename OutSlots>
    constexpr bool run(std::string_view text,
                       std::size_t      start,
                       run_mode         mode,
                       OutSlots&        out_slots,
                       std::size_t      forbid_empty_until = 0,
                       match_semantics  sem                = match_semantics::first)
    {
      text_               = text;
      forbid_empty_until_ = forbid_empty_until;
      sem_                = sem;
      // Fast paths only fire for patterns that always consume (literal /
      // class+), which can never produce the empty match the flag guards.
      // P3c trailing-LA is NOT dispatched here — it lives outside pike_vm::run (real.hpp /
      // find_iter) so this function stays pre-P3c-sized and keeps inlining into find_iter
      // (x86: +16–20 % when cold code bloated run() past the inline threshold).
      if (sem_ == match_semantics::first && prog_.hints.greedy_class_loop >= 0
          && (std::is_constant_evaluated() || !class_fastpath_disabled())) {
        // OPT-C: the memchr-cascade instantiation (Cascade) is selected ONCE by the caller (a whole
        // find_iter/search) from stop_set_size, never per match — so when it is off this run is byte-for-
        // byte the pre-OPT-C per-byte loop and the hot path pays nothing. The caller only sets Cascade
        // when stop_set_size >= 1, so the cascade tail always has real stop bytes.
        prof::tick_route(prof::route::class_loop);
        if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
          prof::tick_event(prof::event::wb_b2_wrap);
        }
        return run_class_loop<Cascade>(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.greedy_cp_class >= 0
          && (std::is_constant_evaluated() || !class_fastpath_disabled())) {
        prof::tick_route(prof::route::cp_class_loop);
        if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
          prof::tick_event(prof::event::wb_b2_wrap);
        }
        return run_cp_class_loop(text, start, mode, out_slots);
      }
      // D1-perf (Étage A): possessive class+/++ loop -- bare/suffixed or delimited/"quoted". A
      // possessive pattern can never also be greedy_class_loop/greedy_cp_class (mutually exclusive
      // AST shapes), so ordering relative to those two is moot -- it matters only relative to
      // exact_literal/inner_literal/lazy-DFA below, which this shape reliably beats (see the
      // D1-perf fiche's route-profile: those routes decline every possessive opcode outright today).
      // The route-name split (bare vs delimited, profiling only) is pushed into the runners
      // themselves so this dispatch site stays exactly as small as the existing two above it.
      if (sem_ == match_semantics::first && prog_.hints.possessive_class.kind == class_kind::byte
          && (std::is_constant_evaluated() || !possessive_fastpath_disabled())) {
        return run_possessive_byte_loop(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.possessive_class.kind == class_kind::klass
          && (std::is_constant_evaluated() || !possessive_fastpath_disabled())) {
        return run_possessive_class_loop(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.possessive_class.kind == class_kind::klass_cp
          && (std::is_constant_evaluated() || !possessive_fastpath_disabled())) {
        return run_possessive_cp_class_loop(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.exact_literal_len > 0) {
        prof::tick_route(prof::route::exact_literal);
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
            && sem_ == match_semantics::first // longest semantics need the general loop (these routes are kFirstMatch)
            && prog_.hints.inner_literal_len > 0 && prog_.hints.inner_literal_prefix >= 1
            && !prog_.prefix_code.empty()) {
          // A required literal at offset 0 (a match that DOES begin with a literal) is a *prefix*, not an inner
          // literal — it keeps the faster find_prefix path. Only a genuine inner literal (offset >= 1, for which
          // the compiler built a `prefix_code` for the reverse-confirm) takes this route; the old
          // `inner_literal_prefix == 0` clause routed prefixes here too and cost ~3.3x on dense corpora.
          // No size guard: on a no-match haystack the route is memmem-only (the reverse setup is lazy, built on
          // the first candidate, never here), so it wins at every size; and the prefix byte-program is a
          // per-regex immutable (built once, amortized by any later use — the lazy-DFA warmup's own contract).
          if (state_.il_text != static_cast<const void*>(text.data())) {
            state_.il_abandoned      = false; // a fresh haystack: re-enable the route and re-evaluate its guards
            state_.il_density_cands  = 0;
            state_.il_density_origin = npos;
            state_.il_text           = static_cast<const void*>(text.data());
          }
          if (!state_.il_abandoned) {
            bool       abandon {false};
            const bool matched {run_inner_literal(text, start, out_slots, abandon)};
            if (!abandon) {
              prof::tick_route(prof::route::inner_literal);
              prof::tick_event(prof::event::memmem);
              return matched;
            }
            prof::tick_event(prof::event::il_abandoned);
            state_.il_abandoned = true; // linearity or density guard: stay on the core for the rest of this haystack
          }
        }
      }
      if (sem_ == match_semantics::first && prog_.hints.fixed_shape) {
        prof::tick_route(prof::route::fixed_shape);
        return run_fixed_shape(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.codepoint_class_ascii >= 0) {
        // OPT-C-1b: the SWAR variant (Cascade) is chosen once per walk, like the class-loop cascade.
        prof::tick_route(prof::route::codepoint_class);
        return run_codepoint_class<Cascade>(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.fixed_alternation) {
        // D1-AC: past the measured branch-count threshold, a single Aho-Corasick automaton walk
        // beats small_set's 2..8-member memchr-cascade scan (which has no fast path at all past 8
        // distinct first bytes — issue #3's "Alternation" gap). Search mode only (the automaton's
        // whole value is candidate-finding; full/prefix modes keep the existing run_alternation,
        // unchanged). Dynamic storage only (if constexpr elides this for static_regex, which does
        // not benefit from AC — see the D1-AC report) and only when the automaton actually built
        // (ensure_ac_automaton declines, leaving it unset, on a pathological icase-expansion
        // branch — falls through to run_alternation below, zero behavior change).
        if constexpr (requires(State & s) {
          s.ac_state;
        }) {
          if (!std::is_constant_evaluated() && !aho_corasick_route_disabled() && mode == run_mode::search
              && prog_.hints.alternation_branch_count >= ac_branch_threshold) {
            ensure_ac_automaton();
            if (state_.ac_state.has_value()) {
              prof::tick_route(prof::route::aho_corasick);
              return run_aho_corasick(text, start, out_slots);
            }
          }
        }
        prof::tick_route(prof::route::alternation);
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
            prof::tick_route(prof::route::onepass_full);
            return true;
          }
        }
        // forbid_empty_until_ != 0 means the iterator just yielded an empty match and the next may not be
        // empty at the same spot; forward_end does not model that rule, so those searches stay on the Pike
        // VM (which does). Empty-matching patterns thus alternate DFA/VM across a find_iter; all others route.
        if (!std::is_constant_evaluated() && !lazy_dfa_route_disabled() && mode == run_mode::search
            && sem_ == match_semantics::first // kFirstMatch forward pass; longest uses the general loop below
            && forbid_empty_until_ == 0 && text.size() - start >= lazy_dfa_min_input) {
          // D1: noinline out of run() so the shared-DFA body cannot bloat class-loop codegen (x86
          // witness/wplus regression pattern — same fix shape as ensure_ac_automaton).
          if (const std::optional<bool> dfa_result {
            try_shared_lazy_dfa_search<Cascade>(text, start, mode, out_slots)}) {
            return *dfa_result;
          }
        }
      }
      if (sem_ == match_semantics::longest) {
        // The longest path uses the plain general loop (the memchr-cascade OPT-C variant is a first-match
        // acceleration; correctness, not throughput, is what the experimental mode needs).
        prof::tick_route(prof::route::general_full);
        return run_general<false>(text, start, mode, out_slots);
      }
      prof::tick_route(prof::route::general_full);
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
          // D1: anchored_end on the shared confirm DFA (under slot.mu). begin_scan mirrors pre-D1
          // forward_end's per-confirm thrash reset; the transition cache itself stays warm across iters.
          std::size_t match_end {npos};
          const bool  dfa_ok    {
            with_search_dfas([&](lazy_dfa& fwd, reverse_dfa& /*rev*/) {
                               fwd.begin_scan();
                               const auto ar {fwd.anchored_end(text, s)};
                               match_end = ar.end;
                               stop      = (ar.end != npos) ? ar.end : ar.scanned_to;
                             })};
          if (dfa_ok) {
            if (match_end == npos) {
              // Pre-D1 forward_end miss set stop = text.size(); keep a floor of s for the IL backstop.
              if (stop < s) {
                stop = s;
              }
              out_slots.assign(prog_.slot_count, npos);
              return false;
            }
            const std::size_t e {match_end};
            stop = e;
            ensure_immutables();
            if (prog_.immut != nullptr && prog_.immut->op_table.has_value() && prog_.immut->op_table->eligible()
                && prog_.immut->op_table->extract(text, s, e, out_slots)) {
              return true;
            }
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
        // O1 density gate: sticky candidate sample across the haystack (find_iter). Capture-free +
        // DFA-eligible only — see \ref il_density_milli_threshold. Checked before reverse/confirm so a
        // dense stream of successful hits still switches after K candidates.
        if constexpr (requires(State & s) {
          s.il_density_cands;
        }) {
          if (state_.il_density_origin == npos) {
            state_.il_density_origin = h;
          }
          ++state_.il_density_cands;
          if (state_.il_density_cands == il_density_probe_candidates && prog_.slot_count <= 2) {
            const std::size_t origin {state_.il_density_origin};
            const std::size_t span   {(h >= origin) ? (h - origin + 1) : 1};
            if (static_cast<std::size_t>(state_.il_density_cands) * 1000U / span >=
                il_density_milli_threshold) {
              if (prog_.immut != nullptr && prog_.immut->byte_prog.eligible) {
                abandon = true;
                return false;
              }
            }
          }
        }
        if (h < min_pre_start) {
          abandon = true; // guard 2: the scan is regressing into confirmed territory -> retry on the core
          return false;
        }
        std::size_t s {h}; // boundary 0 = head literal: the reverse is the identity
        if (boundary >= 1 && prog_.hints.il_fused_eligible) {
          // IL-FUSION: the whole pattern (prefix + literal + suffix) is a plain fixed-width byte/klass
          // sequence (prog_.hints.fixed_shape, checked at compile time -- compiler.hpp's il_fused_eligible
          // wiring), so the match start is pure arithmetic: no reverse DFA. Bounds-guarded both ways -- a
          // hit closer to the text start than the prefix's width, or whose only possible start falls
          // below the reverse floor, has no valid candidate here (mirrors reverse_start returning npos).
          const std::size_t prefix_w {prog_.hints.il_fused_prefix_width};
          s = (h >= prefix_w && h - prefix_w >= min_match_start) ? h - prefix_w : npos;
        }
        else if (boundary >= 1) {
          // The prefix's byte program lives in the per-regex immutables — built once (call_once, already done
          // by the first-candidate guard above), not per find_iter; the expensive klass_cp expansion is what a
          // small-input regex must not pay repeatedly.
          if (prog_.immut == nullptr || !prog_.immut->il_prefix_prog.eligible) {
            abandon = true; // no per-regex cache, or the prefix is not byte-DFA-eligible — let the core VM handle it
            return false;
          }
          // D1: shared IL-prefix reverse under slot.mu (warmed once per regex via epoch).
          {
            shared_dfa_slot&                  slot {shared_dfa_for(prog_.immut)};
            const std::lock_guard<std::mutex> lock {slot.mu};
            ensure_slot_il_prefix_rev_unlocked(*prog_.immut, slot);
            if (slot.il_prefix_rev.has_value()) {
              s = slot.il_prefix_rev->reverse_start(text, h, min_match_start);
            }
            else {
              s = npos;
            }
          }
        }
        if (s == npos) {
          pos = h + 1; // the prefix reaches no start within [min_match_start, h] -> next candidate
        }
        else if (prog_.hints.il_fused_eligible) {
          // The fused verify: one match_byte_klass_run pass over the WHOLE span (prefix + literal +
          // suffix, all byte/klass ops by construction) -- no forward DFA, no one-pass extraction. A
          // fixed-width match has every save at a compile-time-constant offset from the start, exactly
          // like run_fixed_shape's own grouped path, so fill_fixed_saves (no re-match) fills captures.
          const std::size_t match_end {prog_.slot_count <= 2 ? match_byte_klass_run<false>(text, 1, s)
                                                              : match_byte_klass_run<true>(text, 1, s)};
          if (match_end != npos) {
            out_slots.assign(prog_.slot_count, npos);
            out_slots[0] = s;
            out_slots[1] = match_end;
            if (prog_.slot_count > 2) {
              fill_fixed_saves(s, out_slots);
            }
            return true;
          }
          // min_pre_start intentionally not advanced here: match_byte_klass_run reports pass/fail only,
          // not how far it got, so there is no sound tighter floor to claim (and none is needed for
          // linearity -- the fused verify is a hard-bounded O(il_fused_max_width) check per candidate,
          // not the reverse/forward-DFA cost the guard was built to bound).
          pos = h + 1;
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

    //! \brief Match semantics for the current run (\ref match_semantics::first by default; \ref
    //!        match_semantics::longest is the experimental opt-in). Read by \ref step and the fast-path routing.
    match_semantics sem_ {match_semantics::first};

    /*!
     * \brief The concrete thread-list type taken from the bound `State`.
     */
    using list_type = std::remove_reference_t<decltype(std::declval<State&>().lists[0])>;

    //! \brief Below this input length the lazy-DFA routing is skipped (the two-pass setup does not amortise
    //!        on a short subject — the Pike VM goes direct). A measured, documented threshold.
    static constexpr std::size_t lazy_dfa_min_input {512};

    /*!
     * \brief O1 density-gate sample size and threshold (inner-literal → core/DFA when candidate density is high).
     *
     * Measured 2026-07 on M1 Pro, pattern \c (?:\\w+)_(?:\\w+), 300&nbsp;KB corpora, best-of clean timing
     * vs \c il_off: dens = candidates/byte ≈ underscore/byte for this shape. Crossover IL≈DFA at dens ≈ 0.037;
     * dens 0.05 → IL 1.3× worse; dens 0.077 → 2.3×; dens 0.17 (P0 \c ident_dense) → ~8×. Threshold 60/1000
     * (dens 0.06) sits conservatively above crossover so sparse IL wins (dens ≪ 0.01) stay on IL. Capture-free
     * only (\c slot_count ≤ 2): with groups, IL still beat forced DFA on dense (P0). Probe after K candidates
     * across the haystack (sticky on \ref pike_state::il_density_cands).
     */
    static constexpr std::uint32_t il_density_probe_candidates {8};
    static constexpr std::size_t   il_density_milli_threshold  {60};

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
                       // D1: address-reuse of *immut must not keep a previous pattern's shared DFAs.
                       reset_shared_dfas(immut);
                     });
    }

    //! \brief D1: warm shared search DFAs for \p immut into \p slot (caller holds \p slot.mu).
    void ensure_slot_search_dfas_unlocked(detail::regex_immutables& immut,
                                          shared_dfa_slot&          slot)
    {
      if (!immut.byte_prog.eligible) {
        return;
      }
      if (!slot.fwd.has_value()) {
        slot.fwd.emplace(immut.byte_prog.code, immut.byte_prog.classes, lazy_dfa::state_budget, &immut.alphabet);
        slot.rev.emplace(immut.byte_prog.code, immut.byte_prog.classes, reverse_dfa::state_budget, &immut.alphabet);
      }
    }

    //! \brief D1: warm shared IL-prefix reverse DFA (caller holds \p slot.mu).
    void ensure_slot_il_prefix_rev_unlocked(detail::regex_immutables& immut,
                                            shared_dfa_slot&          slot)
    {
      if (!immut.il_prefix_prog.eligible) {
        return;
      }
      if (!slot.il_prefix_rev.has_value()) {
        slot.il_prefix_rev.emplace(immut.il_prefix_prog.code, immut.il_prefix_prog.classes);
      }
    }

    //! \brief D1: run \p fn with the shared search DFAs under the slot lock.
    //!        Returns false when the route must stay on the Pike VM (no immut / ineligible).
    template <typename Fn>
    bool with_search_dfas(Fn&& fn)
    {
      detail::regex_immutables* const immut {prog_.immut};
      if (immut == nullptr) {
        return false; // same contract as pre-D1 ensure_lazy_dfa: no per-regex cache → no DFA route
      }
      ensure_immutables();
      shared_dfa_slot&                  slot {shared_dfa_for(immut)};
      const std::lock_guard<std::mutex> lock {slot.mu};
      ensure_slot_search_dfas_unlocked(*immut, slot);
      if (!slot.fwd.has_value() || !slot.rev.has_value() || !slot.fwd->eligible()) {
        return false;
      }
      // Pre-D1 thrashing was per-iterator (a new iterator re-armed). On a shared slot a sticky thrash
      // flag would permanently decline the DFA route for every later search on this regex — re-arm
      // per logical entry. Callers that walk many candidates (A2) still call begin_scan once more
      // for a single thrash window across that loop; a double-reset here is harmless.
      slot.fwd->begin_scan();
      std::forward<Fn>(fn)(*slot.fwd, *slot.rev);
      return true;
    }

    //! \brief D1: lazy-DFA search route on the shared confirm DFAs. \c noinline so its body cannot
    //!        inflate \ref run (x86 class-loop codegen neighbor — same shape as \ref ensure_ac_automaton).
    //! \return matched / no-match when the route handled the search; empty when the caller must fall to Pike.
    template <bool Cascade, typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    std::optional<bool> try_shared_lazy_dfa_search(std::string_view text,
                                                   std::size_t      start,
                                                   run_mode         mode,
                                                   OutSlots&        out_slots)
    {
      std::optional<bool> dfa_result;
      std::size_t         scan_start {start};
      const bool          used       {
        with_search_dfas([&](lazy_dfa& fwd, reverse_dfa& rev) {
                           // A2: anchored-from-candidate when first_bytes is sound; else forward_end + reverse.
                           if (prog_.hints.first_bytes_valid) {
                             fwd.begin_scan();
                             std::size_t c {scan_start};
                             while (true) {
                               c = next_candidate(text, c, scan_start);
                               if (c > text.size()) {
                                 out_slots.assign(prog_.slot_count, npos);
                                 dfa_result = false;
                                 return;
                               }
                               const auto anchored {fwd.anchored_end(text, c)};
                               prefilter_note_scan(anchored.scanned_to - c);
                               if (anchored.end != npos) {
                                 const std::size_t match_end {anchored.end};
                                 prof::tick_route(prof::route::lazy_dfa_anchored);
                                 if (prog_.slot_count <= 2) {
                                   out_slots.assign(2, npos);
                                   out_slots[0] = c;
                                   out_slots[1] = match_end;
                                   dfa_result   = true;
                                   return;
                                 }
                                 if (prog_.immut != nullptr && prog_.immut->op_table.has_value()
                                     && prog_.immut->op_table->eligible()
                                     && prog_.immut->op_table->extract(text, c, match_end, out_slots)) {
                                   prof::tick_route(prof::route::onepass_window);
                                   dfa_result = true;
                                   return;
                                 }
                                 prof::tick_route(prof::route::general_window);
                                 dfa_result = run_general<Cascade>(text.substr(0, match_end), c, mode, out_slots);
                                 return;
                               }
                               if (anchored.scanned_to >= text.size()) {
                                 scan_start = c + 1;
                                 break;
                               }
                               ++c;
                             }
                           }
                           const std::size_t match_end {fwd.forward_end(text.substr(scan_start))};
                           prefilter_note_scan(text.size() - scan_start);
                           if (match_end == npos) {
                             prof::tick_route(prof::route::lazy_dfa_fwd_rev);
                             out_slots.assign(prog_.slot_count, npos);
                             dfa_result = false;
                             return;
                           }
                           const std::size_t abs_end   {scan_start + match_end};
                           const std::size_t abs_start {rev.reverse_start(text, abs_end, scan_start)};
                           prof::tick_route(prof::route::lazy_dfa_fwd_rev);
                           if (prog_.slot_count <= 2) {
                             out_slots.assign(2, npos);
                             out_slots[0] = abs_start;
                             out_slots[1] = abs_end;
                             dfa_result   = true;
                             return;
                           }
                           if (prog_.immut != nullptr && prog_.immut->op_table.has_value() && prog_.immut->op_table->eligible()
                               && prog_.immut->op_table->extract(text, abs_start, abs_end, out_slots)) {
                             prof::tick_route(prof::route::onepass_window);
                             dfa_result = true;
                             return;
                           }
                           prof::tick_route(prof::route::general_window);
                           dfa_result = run_general<Cascade>(text.substr(0, abs_end), abs_start, mode, out_slots);
                         })};
      if (used && dfa_result.has_value()) {
        return dfa_result;
      }
      return std::nullopt;
    }

    //! \brief Branch count of a \ref pattern_hints::fixed_alternation at or above which a single
    //!        Aho-Corasick automaton walk beats \ref pattern_hints::small_set's 2..8-member
    //!        memchr-cascade scan (which has no fast path at all past 8 distinct first bytes).
    //!        Measured 2026-07 on M1 Pro against the WIRED engine (real::regex, find_iter, min-of-5,
    //!        aho_corasick_route_disabled() toggle) on two corpus shapes — mostly-non-matching prose
    //!        and majority-matching text: at N=11 AC already wins on the match-heavy corpus (0.66x)
    //!        but is ~8% SLOWER on prose (1.08x); at N=12 it wins on both (0.72x prose, 0.61x match)
    //!        with no measured regression. 12, not 11, so the gate matches its own contract — AC
    //!        BEATS the VM-branch path at the threshold, not roughly ties it (see D1-AC report; the
    //!        issue #3 repro's own N=10 stays correctly below threshold either way, AC/VM=1.16x
    //!        there against the standalone POC — inside the closed-gap target of <=~1.5x).
    static constexpr std::uint16_t ac_branch_threshold {12};

    //! \brief Build (or rebuild, on a program change) this iterator's Aho-Corasick automaton for a
    //!        `fixed_alternation` program whose branch count has reached \ref ac_branch_threshold.
    //!        Declines (leaves \ref pike_state::ac_state unset) if any branch's icase-fold
    //!        expansion would exceed \ref ac_max_branch_expansion — the caller falls back to the
    //!        existing \ref run_alternation, zero behavior change. Runs once per iterator/program,
    //!        off the hot path, mirroring \ref with_search_dfas's cache-by-program-pointer contract.
    //!
    //!        `noinline`, deliberately NOT `cold` (round-2 x86 isolation A/B: neither the
    //!        pattern_hints field alone nor the pike_state size growth alone regressed the
    //!        non-alternation hot-corpus witnesses, isolating the cause to code called directly
    //!        from `run()`'s own dispatch chain — a codegen-neighbor/inlining-bloat effect on
    //!        `run_class_loop`, which shares the same translation unit and physically returns
    //!        before ever reaching this call at runtime, so it's presence, not execution, doing the
    //!        damage). `cold` would additionally deprioritize this function's OWN optimization —
    //!        wrong here, since it (unlike the actual construction work in aho_corasick.hpp,
    //!        already marked `cold`) is called on every AC-eligible search, not just once per
    //!        program. `noinline` alone keeps it fully optimized while stopping the compiler from
    //!        folding its body into `run()`'s.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void ensure_ac_automaton()
    {
      const auto* const program {static_cast<const void*>(prog_.code.data())};
      if (state_.ac_state_for == program) {
        return;
      }
      const std::size_t body_pc {prog_.hints.body_pc == 0 ? std::size_t {1}
                                                          : static_cast<std::size_t>(prog_.hints.body_pc)};
      state_.ac_state     = build_ac_automaton(prog_.code, prog_.classes, body_pc);
      state_.ac_state_for = program;
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
     * This function is the no-LA path only (pre-P3c shape). Trailing-lookaround
     * class+ is dispatched outside \ref run (see real.hpp / find_iter) into
     * \ref run_class_loop_trailing_la. always_inline: must stay in the find_iter
     * body on x86 (out-of-line call cost ~16 % over 42k matches).
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
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((always_inline))
#endif
    constexpr bool run_class_loop(std::string_view text,
                                  std::size_t      start,
                                  run_mode         mode,
                                  OutSlots&        out_slots)
    {
      // P1 (issue #3): minimum run length in BYTES for the `X{k,}` desugaring (k identical copies
      // of the atom + a loop of it, see prefilter.hpp's extended class+ recognizer); 1 for the
      // original bare `X+` shape, where every one of the checks below is a dead branch (byte
      // runs are never shorter than 1) -- byte-identical to the pre-P1 behavior.
      const std::size_t         min_len {prog_.hints.greedy_class_loop_min};
      const std::uint8_t* const tbl =
        class_table(static_cast<std::size_t>(prog_.hints.greedy_class_loop));
      const auto in_class = [&](std::size_t i) {
                              return tbl[static_cast<std::uint8_t>(text[i])] != 0U;
                            };
      const auto scan_end = [&](std::size_t match_start) -> std::size_t {
                              std::size_t match_end {match_start + 1};
                              // OPT-C: Cascade memchr-stop after a long run; see historical comment.
                              // Sound because run_class_loop never validates UTF-8 (test_utf8 perimeter).
                              if constexpr (Cascade) {
                                if (!std::is_constant_evaluated()) {
                                  while (match_end < text.size() && in_class(match_end)) {
                                    ++match_end;
                                    if (match_end - match_start == cascade_run_threshold) {
                                      match_end = run_cascade_stop(text, match_end);
                                      break;
                                    }
                                  }
                                  return match_end;
                                }
                              }
                              while (match_end < text.size() && in_class(match_end)) {
                                ++match_end;
                              }
                              return match_end;
                            };

      // Arc B-2: optional `\b`/`\B` — try successive maximal class runs until boundaries hold.
      if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
        if (mode == run_mode::full || mode == run_mode::prefix) {
          if (start >= text.size() || !in_class(start)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          const std::size_t match_end {scan_end(start)};
          if ((mode == run_mode::full && match_end != text.size()) ||
              !wb_boundaries_ok(start, match_end) || (match_end - start) < min_len) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          out_slots.assign(prog_.slot_count, npos);
          fill_span_slots(out_slots, start, match_end);
          return true;
        }
        std::size_t pos {start};
        while (pos < text.size()) {
          std::size_t match_start {pos};
          while (match_start < text.size() && !in_class(match_start)) {
            ++match_start;
          }
          if (match_start >= text.size()) {
            break;
          }
          const std::size_t match_end {scan_end(match_start)};
          if ((match_end - match_start) >= min_len && wb_boundaries_ok(match_start, match_end)) {
            out_slots.assign(prog_.slot_count, npos);
            fill_span_slots(out_slots, match_start, match_end);
            return true;
          }
          pos = match_end; // next run (e.g. "9abc def" → skip "abc", try "def")
        }
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }

      // B-1 window-edge guard, mode::full/prefix: anchored at `start` with no retry available --
      // see pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
      if ((mode == run_mode::full || mode == run_mode::prefix) && prog_.hints.wb_lead_maximal_run &&
          start > 0 && start < text.size() && in_class(start) &&
          !assertion_holds(assert_kind::word_boundary, start, false)) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      std::size_t match_start {start};
      std::size_t match_end   {};
      while (true) {
        if (mode == run_mode::search) {
          while (match_start < text.size() && !in_class(match_start)) {
            ++match_start;
          }
          if (match_start >= text.size()) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          // B-1 window-edge guard: a candidate found by scanning forward past a non-class byte
          // is provably preceded by one (the scan just confirmed it), so B-1's redundancy
          // argument holds unconditionally there. The ONE exception is the very first candidate
          // when it coincides with `start` itself (no forward scan occurred) AND `start > 0` --
          // see pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
          if (prog_.hints.wb_lead_maximal_run && match_start == start && match_start > 0 &&
              !assertion_holds(assert_kind::word_boundary, match_start, false)) {
            match_start = scan_end(match_start); // no genuine boundary here: skip this whole run
            continue;
          }
        }
        if (match_start >= text.size() || !in_class(match_start)) {
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }
        match_end = scan_end(match_start);
        if (mode == run_mode::full && match_end != text.size()) {
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }
        // P1: a maximal run shorter than the required minimum can never satisfy `X{k,}` starting
        // here -- in search mode, skip past the whole (too-short) run and try the next one,
        // exactly like the wb-boundary retry above; anchored modes have no retry, so fail outright.
        if ((match_end - match_start) < min_len) {
          if (mode != run_mode::search) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          match_start = match_end;
          continue;
        }
        break;
      }
      out_slots.assign(prog_.slot_count, npos);
      fill_span_slots(out_slots, match_start, match_end);
      return true;
    }

  public:

    /*!
     * \brief Trailing-lookaround class+ (P3c): body scan + longest end where lookaround holds.
     *
     * Cold, noinline: must not share a function body or inlining unit with
     * \ref run_class_loop (the daily [a-z]+ path). Invoked from real.hpp / find_iter
     * **outside** \ref run so pure class-loop run() stays pre-P3c-sized. Dynamic-only.
     */
    template <bool Cascade, typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
    bool run_class_loop_trailing_la(std::string_view text,
                                    std::size_t      start,
                                    run_mode         mode,
                                    OutSlots&        out_slots)
    {
      // Static storage has no lookaround scratch and never arms trailing_lookaround.
      if constexpr (!requires(State & st) {
        st.lookaround;
      }) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      else {
        text_ = text; // lookaround_holds reads text_ (callers are outside run())
        const std::uint8_t* const tbl =
          class_table(static_cast<std::size_t>(prog_.hints.trailing_la_class));
        const auto in_class = [&](std::size_t i) {
                                return tbl[static_cast<std::uint8_t>(text[i])] != 0U;
                              };
        const auto scan_end = [&](std::size_t match_start) -> std::size_t {
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
                                    return match_end;
                                  }
                                }
                                while (match_end < text.size() && in_class(match_end)) {
                                  ++match_end;
                                }
                                return match_end;
                              };

        const auto sub_id {static_cast<std::uint16_t>(prog_.hints.trailing_lookaround)};
        const auto try_ends = [&](std::size_t ms, std::size_t me) -> bool {
                                for (std::size_t e = me; e > ms; --e) {
                                  if (lookaround_holds(sub_id, e)) {
                                    out_slots.assign(prog_.slot_count, npos);
                                    fill_span_slots(out_slots, ms, e);
                                    return true;
                                  }
                                }
                                return false;
                              };

        if (mode == run_mode::full) {
          if (start >= text.size() || !in_class(start)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          const std::size_t match_end {scan_end(start)};
          if (match_end != text.size() || !lookaround_holds(sub_id, match_end)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          out_slots.assign(prog_.slot_count, npos);
          fill_span_slots(out_slots, start, match_end);
          return true;
        }

        if (mode == run_mode::prefix) {
          if (start >= text.size() || !in_class(start)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          if (try_ends(start, scan_end(start))) {
            return true;
          }
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }

        std::size_t pos {start};
        while (pos < text.size()) {
          std::size_t match_start {pos};
          while (match_start < text.size() && !in_class(match_start)) {
            ++match_start;
          }
          if (match_start >= text.size()) {
            break;
          }
          const std::size_t match_end {scan_end(match_start)};
          if (try_ends(match_start, match_end)) {
            return true;
          }
          pos = match_end;
        }
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
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
    // O2r-1b (gcc-only outline of the >= 0x80 path in run_cp_class_loop): split into
    // real/engine/cpclass_gcc.hpp (full rationale + measured numbers there), excluded from the
    // coverage floor like simd.hpp — a branch clang never compiles shouldn't inflate this file's line
    // count. #else (in run_cp_class_loop below) is the original nested-closure shape, untouched.
#if defined(__GNUC__) && !defined(__clang__)
#include "real/engine/cpclass_gcc.hpp"
#endif

    template <typename OutSlots>
    constexpr bool run_cp_class_loop(std::string_view text,
                                     std::size_t      start,
                                     run_mode         mode,
                                     OutSlots&        out_slots)
    {
      // P1 (issue #3): minimum run length in CODE POINTS (not bytes) for the `X{k,}` desugaring --
      // see run_class_loop's own doc comment; 1 for the original bare shape (dead branch below).
      const std::size_t min_len  {prog_.hints.greedy_cp_class_min};
      const std::size_t cp_index {static_cast<std::size_t>(prog_.hints.greedy_cp_class)};
#if defined(__GNUC__) && !defined(__clang__)
#include "real/engine/cpclass_gcc_loop.hpp"
#else
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
      const auto extend_run = [&](std::size_t match_start) -> std::size_t {
                                const std::size_t first {width(match_start)};
                                if (first == 0) {
                                  return npos;
                                }
                                std::size_t match_end {match_start + first};
                                if (prog_.hints.greedy_cp_class_plus) {
                                  while (match_end < text.size()) {
                                    const auto lead {static_cast<std::uint8_t>(text[match_end])};
                                    if (lead < 0x80U) {
                                      if (asc[lead] == 0U) {
                                        break;
                                      }
                                      ++match_end;
                                      continue;
                                    }
                                    const detail::decoded_codepoint dc {
                                      detail::decode_codepoint_strict(text, match_end)};
                                    if (!dc.valid || !member_hi(dc.cp)) {
                                      break;
                                    }
                                    match_end += dc.length;
                                  }
                                }
                                return match_end;
                              };
#endif

      // P1: counts code points in [s, e) -- only walked when min_len > 1 (the {k,} shape); the
      // range is already known to be a valid run of class-member code points (extend_run just
      // built it), so this simply re-walks UTF-8 lead bytes to count boundaries, never re-validates.
      const auto count_cps = [&](std::size_t s, std::size_t e) -> std::size_t {
                               std::size_t n {0};
                               std::size_t i {s};
                               while (i < e) {
                                 const auto lead {static_cast<std::uint8_t>(text[i])};
                                 i += lead < 0x80U ? std::size_t {1}
                                                   : detail::decode_codepoint_strict(text, i).length;
                                 ++n;
                               }
                               return n;
                             };

      // Arc B-2: `\b`/`\B` on subset cp-class (e.g. `\b\d+\b`) — try successive runs.
      if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
        if (mode == run_mode::full || mode == run_mode::prefix) {
          const std::size_t match_end {extend_run(start)};
          if (match_end == npos || (mode == run_mode::full && match_end != text.size()) ||
              !wb_boundaries_ok(start, match_end) ||
              (min_len > 1 && count_cps(start, match_end) < min_len)) {
            return false;
          }
          fill_span_slots(out_slots, start, match_end);
          return true;
        }
        std::size_t pos {start};
        while (pos < text.size()) {
          std::size_t match_start {pos};
          while (match_start < text.size() && width(match_start) == 0) {
            ++match_start;
          }
          if (match_start >= text.size()) {
            break;
          }
          const std::size_t match_end {extend_run(match_start)};
          if (match_end != npos && wb_boundaries_ok(match_start, match_end) &&
              (min_len <= 1 || count_cps(match_start, match_end) >= min_len)) {
            fill_span_slots(out_slots, match_start, match_end);
            return true;
          }
          pos = match_end == npos ? match_start + 1 : match_end;
        }
        return false;
      }

      // B-1 window-edge guard, mode::full/prefix: anchored at `start` with no retry available --
      // see pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
      if ((mode == run_mode::full || mode == run_mode::prefix) && prog_.hints.wb_lead_maximal_run &&
          start > 0 && start < text.size() && width(start) != 0 &&
          !assertion_holds(assert_kind::word_boundary, start, false)) {
        return false;
      }
      std::size_t match_start {start};
      std::size_t match_end   {};
      while (true) {
        if (mode == run_mode::search) {
          while (match_start < text.size() && width(match_start) == 0) {
            ++match_start;
          }
          if (match_start >= text.size()) {
            return false;
          }
          // B-1 window-edge guard: a candidate found by scanning forward past a non-class
          // code point is provably preceded by one, so B-1's redundancy argument holds
          // unconditionally there. The ONE exception is the very first candidate when it
          // coincides with `start` itself (no forward scan occurred) AND `start > 0` -- see
          // pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
          if (prog_.hints.wb_lead_maximal_run && match_start == start && match_start > 0 &&
              !assertion_holds(assert_kind::word_boundary, match_start, false)) {
            const std::size_t skip {extend_run(match_start)};
            if (skip == npos) {
              return false; // malformed sequence right at the window edge: nothing to skip to
            }
            match_start = skip; // no genuine boundary here: skip this whole run
            continue;
          }
        }
        if (match_start >= text.size()) {
          return false;
        }
        // The first code point must match: this path is only chosen for `\w`/`\w+` (never nullable).
        match_end = extend_run(match_start);
        if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
          return false;
        }
        // P1: a maximal run shorter than the required minimum can never satisfy `X{k,}` starting
        // here -- in search mode, skip past the whole (too-short) run and try the next one;
        // anchored modes have no retry, so fail outright (mirrors run_class_loop's own min-check).
        if (min_len > 1 && count_cps(match_start, match_end) < min_len) {
          if (mode != run_mode::search) {
            return false;
          }
          match_start = match_end;
          continue;
        }
        break;
      }
      fill_span_slots(out_slots, match_start, match_end);
      return true;
    }

    /*!
     * \brief D1-perf (Étage A) shared driver: a possessive class+/++ loop, bare/suffixed (\ref
     *        pattern_hints::possessive_prefix_size == 0) or delimited/"quoted" (non-zero) -- the
     *        BODY's own class/cp-class membership test is supplied by \p in_class / \p scan_end so this
     *        one driver serves both the byte-class and the code-point-class runners below.
     *
     * A possessive run never gives back: once matched, it always advances maximally, so -- unlike \ref
     * run_class_loop's whole-pattern shape, which has nothing AFTER the loop to fail against -- this
     * scan can hit a required literal SUFFIX (or, for the delimited shape, fail to find the closing
     * SUFFIX after a required PREFIX) that does not follow. There is nothing to retry within one
     * attempt (that is exactly what "possessive" means); in \c search mode the NEXT candidate is tried,
     * and the retry skips straight to the failed attempt's own body end -- provably safe and linear, not
     * merely fast, PROVIDED the eligibility \ref pattern_hints documents held at recognition time
     * (prefilter.hpp): every candidate strictly between the attempt's start and its body end is
     * guaranteed to fail identically (an unbounded possessive run has no shorter/longer variant to
     * offer), so skipping them loses no leftmost match.
     *
     * \tparam InClass    `bool(std::size_t) -> true` if the body's class/cp-class accepts the
     *                    byte/code point starting at that offset.
     * \tparam ScanEnd    `std::size_t(std::size_t from) -> end` of the maximal body run starting at
     *                    \p from (== \p from itself when \p from is not a valid start -- a
     *                    zero-length run).
     * \tparam LastWidth  `std::size_t(std::size_t end) -> width` (in bytes) of the LAST atom
     *                    consumed by a non-empty run ending at \p end -- fixed at 1 for a byte or
     *                    byte-class body, a backward UTF-8 decode for a code-point-class body (see
     *                    \ref codepoint_retreat). Only ever called with \p end strictly greater than
     *                    the run's own start, so there is always at least one atom to measure.
     */
    template <typename OutSlots, typename InClass, typename ScanEnd, typename LastWidth>
    constexpr bool run_possessive_loop_generic(std::string_view    text,
                                               std::size_t         start,
                                               run_mode            mode,
                                               OutSlots&           out_slots,
                                               const InClass&      in_class,
                                               const ScanEnd&      scan_end,
                                               const LastWidth&    last_width)
    {
      const auto&        h           {prog_.hints};
      const std::uint8_t prefix_size {h.possessive_prefix_size};
      const std::uint8_t suffix_size {h.possessive_suffix_size};
      const auto         suffix_ok = [&](std::size_t pos) {
                                       if (suffix_size == 0) {
                                         return true;
                                       }
                                       if (pos + suffix_size > text.size()) {
                                         return false;
                                       }
                                       for (std::uint8_t k {0}; k < suffix_size; ++k) {
                                         if (text[pos + k] != h.possessive_suffix[k]) {
                                           return false;
                                         }
                                       }
                                       return true;
                                     };
      // R2 capture fix: the captured group is the possessive loop's own LAST iteration, not the
      // whole match span -- re's own semantics, matching what the general VM already got right (it
      // was never routed through this driver for a byte-literal body before R2 armed one, which is
      // how this bug -- shipped since D1-perf's original klass/klass_cp fast path, 7.36 -- surfaced
      // live). \p body_end is the loop's own end (before any suffix); defaults to npos for the
      // delimited ("quoted") shape, which never captures at all (possessive_group_start stays -1
      // there by construction, so the branch below never runs regardless of what body_end is).
      // Zero iterations (body_end == s) leaves the group UNSET (npos, re's `None`), never [s, s).
      const auto write_success = [&](std::size_t s, std::size_t e, std::size_t body_end = npos) {
                                   out_slots.assign(prog_.slot_count, npos);
                                   out_slots[0] = s;
                                   out_slots[1] = e;
                                   if (h.possessive_group_start >= 0 && body_end != npos && body_end > s) {
                                     const std::size_t w {last_width(body_end)};
                                     out_slots[static_cast<std::size_t>(h.possessive_group_start)] =
                                       body_end - w;
                                     out_slots[static_cast<std::size_t>(h.possessive_group_end)] = body_end;
                                   }
                                 };
      const auto fail = [&]() {
                          out_slots.assign(prog_.slot_count, npos);
                          return false;
                        };
      if (prefix_size > 0) {
        // Delimited ("quoted") shape: no capture, no \b wrap by construction (prefilter.hpp never
        // arms both together this train) -- suffix_ok / write_success above already cover it exactly.
        const auto find_prefix = [&](std::size_t from) -> std::size_t {
                                   if (from > text.size() || prefix_size > text.size() - from) {
                                     return npos;
                                   }
                                   for (std::size_t i {from}; i <= text.size() - prefix_size; ++i) {
                                     bool ok {true};
                                     for (std::uint8_t k {0}; k < prefix_size; ++k) {
                                       if (text[i + k] != h.possessive_prefix[k]) {
                                         ok = false;
                                         break;
                                       }
                                     }
                                     if (ok) {
                                       return i;
                                     }
                                   }
                                   return npos;
                                 };
        if (mode == run_mode::full || mode == run_mode::prefix) {
          if (prefix_size > (text.size() >= start ? text.size() - start : 0)) {
            return fail();
          }
          for (std::uint8_t k {0}; k < prefix_size; ++k) {
            if (text[start + k] != h.possessive_prefix[k]) {
              return fail();
            }
          }
          const std::size_t body_end {scan_end(start + prefix_size)};
          if (!suffix_ok(body_end)) {
            return fail();
          }
          const std::size_t end {body_end + suffix_size};
          if (mode == run_mode::full && end != text.size()) {
            return fail();
          }
          write_success(start, end);
          return true;
        }
        std::size_t pos {start};
        while (true) {
          const std::size_t cand {find_prefix(pos)};
          if (cand == npos) {
            return fail();
          }
          const std::size_t body_end {scan_end(cand + prefix_size)};
          if (suffix_ok(body_end)) {
            write_success(cand, body_end + suffix_size);
            return true;
          }
          pos = body_end > cand ? body_end : cand + 1;
        }
      }
      // Bare / suffixed (no leading literal).
      const bool min_nonzero {h.possessive_min_nonzero};
      // B-1 window-edge guard: see pattern_hints::wb_lead_maximal_run's own doc comment. Applies
      // only when `start` itself is the candidate AND is actually in-class (a zero-length body at
      // a non-class `start` has no "run" for B-1's argument to be about in the first place).
      const auto b1_edge_blocks = [&](std::size_t pos) {
                                    return h.wb_lead_maximal_run && pos > 0 && pos < text.size() &&
                                           in_class(pos) &&
                                           !assertion_holds(assert_kind::word_boundary, pos, false);
                                  };
      if (mode == run_mode::full || mode == run_mode::prefix) {
        if (min_nonzero && (start >= text.size() || !in_class(start))) {
          return fail();
        }
        if (b1_edge_blocks(start)) {
          return fail();
        }
        const std::size_t body_end {start < text.size() && in_class(start) ? scan_end(start) : start};
        if (!wb_boundaries_ok(start, body_end) || !suffix_ok(body_end)) {
          return fail();
        }
        const std::size_t end {body_end + suffix_size};
        if (mode == run_mode::full && end != text.size()) {
          return fail();
        }
        write_success(start, end, body_end);
        return true;
      }
      std::size_t pos {start};
      while (pos <= text.size()) {
        if (min_nonzero) {
          while (pos < text.size() && !in_class(pos)) {
            ++pos;
          }
          if (pos >= text.size()) {
            break;
          }
        }
        if (pos == start && b1_edge_blocks(pos)) {
          // No genuine boundary at the window's own edge: skip past this whole run (a candidate
          // reached by scanning forward past a non-class byte is provably preceded by one, so
          // this guard can never re-trigger on a LATER iteration of this same loop).
          pos = scan_end(pos);
          continue;
        }
        const std::size_t body_end {pos < text.size() && in_class(pos) ? scan_end(pos) : pos};
        if (wb_boundaries_ok(pos, body_end) && suffix_ok(body_end)) {
          write_success(pos, body_end + suffix_size, body_end);
          return true;
        }
        pos = body_end > pos ? body_end : pos + 1;
      }
      return fail();
    }

    //! \brief R2 (phase Raffinement): possessive literal-byte +/++ loop (`byte_loop_possessive`,
    //!        e.g. `a++`) -- the asymmetry class_ref's typing made natural to close: this opcode was
    //!        already emitted and executed by the general VM, but had no dedicated recognizer or
    //!        runner, so `a++` fell back to the general VM despite the class/cp-class family
    //!        already having one. See \ref run_possessive_loop_generic for the shared algorithm.
    template <typename OutSlots>
    constexpr bool run_possessive_byte_loop(std::string_view  text,
                                            std::size_t       start,
                                            run_mode          mode,
                                            OutSlots&         out_slots)
    {
      prof::tick_route(prog_.hints.possessive_prefix_size > 0 ? prof::route::possessive_delimited
                                                               : prof::route::possessive_byte_loop);
      const std::uint8_t target {static_cast<std::uint8_t>(prog_.hints.possessive_class.index)};
      const auto         in_class = [&](std::size_t i) {
                                      return static_cast<std::uint8_t>(text[i]) == target;
                                    };
      const auto scan_end = [&](std::size_t from) -> std::size_t {
                              std::size_t e {from};
                              while (e < text.size() && in_class(e)) {
                                ++e;
                              }
                              return e;
                            };
      // A byte's own width is always 1 -- no decode needed.
      const auto last_width = [](std::size_t) { return std::size_t {1}; };
      return run_possessive_loop_generic(text, start, mode, out_slots, in_class, scan_end, last_width);
    }

    //! \brief D1-perf Étage A: possessive class+/++ loop over a BYTE class (`klass_loop_possessive`).
    //!        See \ref run_possessive_loop_generic for the shared algorithm.
    template <typename OutSlots>
    constexpr bool run_possessive_class_loop(std::string_view  text,
                                             std::size_t       start,
                                             run_mode          mode,
                                             OutSlots&         out_slots)
    {
      prof::tick_route(prog_.hints.possessive_prefix_size > 0 ? prof::route::possessive_delimited
                                                               : prof::route::possessive_class_loop);
      const std::uint8_t* const tbl {
        class_table(static_cast<std::size_t>(prog_.hints.possessive_class.index))};
      const auto in_class = [&](std::size_t i) {
                              return tbl[static_cast<std::uint8_t>(text[i])] != 0U;
                            };
      const auto scan_end = [&](std::size_t from) -> std::size_t {
                              std::size_t e {from};
                              while (e < text.size() && in_class(e)) {
                                ++e;
                              }
                              return e;
                            };
      // A byte-class member is always 1 byte wide -- no decode needed.
      const auto last_width = [](std::size_t) { return std::size_t {1}; };
      return run_possessive_loop_generic(text, start, mode, out_slots, in_class, scan_end, last_width);
    }

    //! \brief D1-perf Étage A: possessive class+/++ loop over a CODE-POINT class
    //!        (`klass_cp_loop_possessive`). Mirrors \ref run_cp_class_loop's decode/membership
    //!        primitives (no O2r-1b GCC split here yet -- measure-first; not in this train's scope).
    //!        See \ref run_possessive_loop_generic for the shared algorithm.
    template <typename OutSlots>
    constexpr bool run_possessive_cp_class_loop(std::string_view  text,
                                                std::size_t       start,
                                                run_mode          mode,
                                                OutSlots&         out_slots)
    {
      prof::tick_route(prog_.hints.possessive_prefix_size > 0 ? prof::route::possessive_delimited
                                                               : prof::route::possessive_cp_class_loop);
      const std::size_t         cp_index {static_cast<std::size_t>(prog_.hints.possessive_class.index)};
      const detail::cp_class&   cc       {prog_.cp_classes[cp_index]};
      const std::uint8_t* const asc      {cp_ascii_table(cp_index)};
      const auto                member_hi = [&](char32_t cp) -> bool {
                                              if (cp <= cp_page_max) {
                                                const std::uint64_t* const page {cp_page_table(cp_index)};
                                                const std::uint32_t        bit  {static_cast<std::uint32_t>(cp) - 0x80U};
                                                return ((page[bit >> 6U] >> (bit & 63U)) & std::uint64_t {1}) != 0U;
                                              }
                                              return cp_class_matches(cc, cp);
                                            };
      const auto cp_width = [&](std::size_t i) -> std::size_t {
                              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
                              if (!dc.valid) {
                                return 0;
                              }
                              const bool m {dc.cp < 0x80U ? asc[dc.cp] != 0U : member_hi(dc.cp)};
                              return m ? dc.length : 0;
                            };
      const auto in_class = [&](std::size_t i) { return i < text.size() && cp_width(i) != 0; };
      const auto scan_end = [&](std::size_t from) -> std::size_t {
                              std::size_t e {from};
                              while (e < text.size()) {
                                const std::size_t w {cp_width(e)};
                                if (w == 0) {
                                  break;
                                }
                                e += w;
                              }
                              return e;
                            };
      // The last code point's own width, decoded backward from its end -- see codepoint_retreat's
      // own doc comment. `start` is a sound floor: the loop can never have consumed anything before
      // its own start.
      const auto last_width = [&](std::size_t end) { return detail::codepoint_retreat(text, end, start); };
      return run_possessive_loop_generic(text, start, mode, out_slots, in_class, scan_end, last_width);
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
     * \brief O(1) lead/trail `\b`/`\B` check at match bounds [\p s, \p e).
     *
     * Single verification helper for every wb-wrapping fast path (class-loop, cp-class,
     * fixed-shape, literal, alternation). Hints 0/1/2 from \ref pattern_hints::wb_lead /
     * \ref pattern_hints::wb_trail.
     *
     * \param[in] s Match start (lead assert position).
     * \param[in] e Match end (trail assert position).
     * \return true if both configured boundaries hold (or are unset).
     */
    [[nodiscard]] constexpr bool wb_boundaries_ok(std::size_t s,
                                                  std::size_t e) const
    {
      if (prog_.hints.wb_lead != 0) {
        const assert_kind k {prog_.hints.wb_lead == 2 ? assert_kind::not_word_boundary
                                                      : assert_kind::word_boundary};
        if (!assertion_holds(k, s, false)) {
          return false;
        }
      }
      if (prog_.hints.wb_trail != 0) {
        const assert_kind k {prog_.hints.wb_trail == 2 ? assert_kind::not_word_boundary
                                                       : assert_kind::word_boundary};
        if (!assertion_holds(k, e, false)) {
          return false;
        }
      }
      return true;
    }

    //! \brief Fixed-shape body match from \ref pattern_hints::body_pc, then B1 `\b`/`\B` wrap.
    template <bool SkipSaves>
    [[nodiscard]] constexpr std::size_t match_fixed_body_wb(std::string_view text,
                                                            std::size_t      s) const
    {
      const std::size_t body_pc {prog_.hints.body_pc == 0 ? std::size_t {1}
                                                          : static_cast<std::size_t>(prog_.hints.body_pc)};
      const std::size_t e       {match_byte_klass_run<SkipSaves>(text, body_pc, s)};
      if (e == npos || !wb_boundaries_ok(s, e)) {
        return npos;
      }
      return e;
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
        const auto at {[&](std::size_t s) {
                         return match_fixed_body_wb</*SkipSaves=*/ false>(text, s);
                       }};
        if (mode != run_mode::search) {
          const std::size_t match_end {at(start)};
          if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
            return false;
          }
          out_slots[0] = start;
          out_slots[1] = match_end;
          return true;
        }
#if defined(__ARM_NEON) || defined(__SSE2__)
        // L-SIMD v3.1: hex scan+verify, fused. For a HOMOGENEOUS fixed shape (every position accepts
        // the identical <= 2-range set -- fixed_shape_simd_len > 0, see prefilter.hpp's
        // class_range_count), \ref simd_fixed_shape_scan does the whole candidate scan AND verify itself
        // (mirroring the ceil_simd.cpp hex prototype): it does not call next_candidate at all -- the
        // 53c2de4 cut did, and profiling showed the scalar bitmap scan (a 16-member class is outside the
        // small_set memchr-cascade) became the new bottleneck. A free function so run_fixed_shape's own
        // per-instantiation body stays this thin call-and-branch. Scalar tail (< 16 bytes remaining, or
        // no more candidates) falls through to the existing fast_search/next_candidate walk from
        // wherever the SIMD scan left off. Lead/trail `\b` rejections re-enter the scan past the miss.
        if (!std::is_constant_evaluated() && prog_.hints.fixed_shape_simd_len >= 1) {
          std::size_t pos {start};
          while (pos < text.size()) {
            std::size_t       resume {};
            const std::size_t found  {simd_fixed_shape_scan(text, pos, prog_.hints, resume)};
            if (found == npos) {
              return fast_search(text, resume, at, out_slots); // scalar tail
            }
            const std::size_t e {found + prog_.hints.fixed_shape_simd_len};
            if (wb_boundaries_ok(found, e)) {
              out_slots[0] = found;
              out_slots[1] = e;
              return true;
            }
            pos = found + 1; // body matched but `\b` failed — try next candidate
          }
          return false;
        }
#endif
        return fast_search(text, start, at, out_slots);
      }

      // Inner capturing groups: the run has interleaved saves, so the verify walk skips them
      // (SkipSaves) and the group slots are filled from their constant offsets on success only (not per
      // failed candidate). A separate body keeps the no-group loop above free of any grouping branch.
      out_slots.assign(prog_.slot_count, npos);
      const auto at {[&](std::size_t s) {
                       return match_fixed_body_wb</*SkipSaves=*/ true>(text, s);
                     }};
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
     *
     * Starts at \ref pattern_hints::body_pc, not a hardcoded `1`: an optional leading `\b`/`\B`
     * (`hints.wb_lead`) sits at pc 1, and starting the walk there instead of at the body's own
     * first byte/klass/save hits the assert_position immediately, which matches neither the
     * byte/klass nor the save arm below and so `break`s on the FIRST instruction — silently
     * filling zero capture slots. Found live: `\B(\w){2}` (plain greedy, no possessive quantifier
     * involved) loses group(1) entirely, `(\w){2}` without the `\B` does not — confirmed by
     * bisection, not assumed from reading the loop.
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
      const std::size_t body_pc {prog_.hints.body_pc == 0 ? std::size_t {1}
                                                          : static_cast<std::size_t>(prog_.hints.body_pc)};
      std::size_t offset        {};
      for (std::size_t pc {body_pc}; pc < prog_.code.size(); ++pc) {
        const instr& instruction {prog_.code[pc]};
        if (instruction.op == opcode::byte || instruction.op == opcode::klass) {
          ++offset;
        }
        else if (instruction.op == opcode::save) {
          out_slots[static_cast<std::size_t>(instruction.arg16)] = match_start + offset;
        }
        else {
          break; // reached a trailing \b/\B or match
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
      // Byte length of a matching codepoint at i, or 0 for no match. ASCII stays a direct table
      // hit; the 3-/4-byte cases bounds-check their FIRST continuation byte against
      // utf8_second_byte_bounds_table (charclass.hpp) instead of the generic [0x80,0xBF] `cont`
      // check -- that generic check accepted overlong (E0 80 80 / F0 80 80 80) and encoded-
      // surrogate (ED A0 80) sequences as one code point. A table lookup, not a full decode: an
      // earlier version reused decode_codepoint_strict (which accumulates the code point via
      // shifts and checks it against min_cp/the surrogate block after the fact) and cost +13%
      // ns/B on this exact path -- rejected. This keeps the original branch/comparison shape,
      // swapping only one hardcoded bound for a per-lead table entry.
      const auto width = [&](std::size_t i) -> std::size_t {
                           const auto byte_value {static_cast<std::uint8_t>(text[i])};
                           if (byte_value < 0x80) {
                             return ascii[byte_value] != 0U ? 1 : 0;
                           }
                           if (byte_value >= 0xC2 && byte_value <= 0xDF) {
                             return i + 1 < text.size() && cont(i + 1) ? 2 : 0;
                           }
                           if (byte_value >= 0xE0 && byte_value <= 0xEF) {
                             if (i + 2 >= text.size()) {
                               return 0;
                             }
                             const detail::utf8_second_byte_bounds& b {
                               detail::utf8_second_byte_bounds_table[byte_value]};
                             const auto b2                            {static_cast<std::uint8_t>(text[i + 1])};
                             return b2 >= b.lo && b2 <= b.hi && cont(i + 2) ? 3 : 0;
                           }
                           if (byte_value >= 0xF0 && byte_value <= 0xF4) {
                             if (i + 3 >= text.size()) {
                               return 0;
                             }
                             const detail::utf8_second_byte_bounds& b {
                               detail::utf8_second_byte_bounds_table[byte_value]};
                             const auto b2                            {static_cast<std::uint8_t>(text[i + 1])};
                             return b2 >= b.lo && b2 <= b.hi && cont(i + 2) && cont(i + 3) ? 4 : 0;
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
     * \brief D1-AC: multi-literal search via the automaton cached in \ref pike_state::ac_state.
     *
     * Search mode only — the automaton's own leftmost-first scan already IS the candidate search
     * (no separate memchr-cascade block scan). Non-`constexpr` by construction (needs a runtime
     * scratch cache), so this is never instantiated for the static storage's `State` — guarded at
     * the call site by `if constexpr (requires(State& s) { s.ac_state; })`.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if some branch matched.
     *
     * `noinline`, deliberately NOT `cold` — same reasoning as \ref ensure_ac_automaton (called on
     * every AC-eligible search, so it must stay fully optimized); only kept OUT of `run()`'s own
     * body, which is what a round-2 x86 isolation A/B (relayed) traced the "fields [^,]+ +39%"
     * finding to (neither the pattern_hints field nor the pike_state size growth alone regressed
     * it — only the full dispatch/search code sharing `run()`'s translation unit did).
     */
    template <typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool run_aho_corasick(std::string_view text,
                          std::size_t      start,
                          OutSlots&        out_slots)
    {
      out_slots.assign(2, npos);
      // Called only from the dispatch site's own state_.ac_state.has_value() guard, but that
      // invariant is invisible across the call boundary to static analysis -- an explicit local
      // check keeps this function's own contract self-contained (defensive, not defensive-in-name).
      if (!state_.ac_state.has_value()) {
        return false;
      }
      const auto wb_ok = [&](std::size_t s, std::size_t e) { return wb_boundaries_ok(s, e); };
      const auto m {state_.ac_state->search(text, start, wb_ok)};
      if (!m.matched) {
        return false;
      }
      out_slots[0] = m.start;
      out_slots[1] = m.end;
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
      // first), mirroring the VM's thread priority. body_pc skips a lead `\b`.
      const auto match_at = [&](std::size_t match_start, bool require_full) -> std::size_t {
                              std::size_t pc {prog_.hints.body_pc == 0
                                                ? std::size_t {1}
                                                : static_cast<std::size_t>(prog_.hints.body_pc)};
                              while (true) {
                                const bool        is_split  {code[pc].op == opcode::split};
                                const std::size_t branch    {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};
                                const std::size_t match_end {match_byte_klass_run(text, branch, match_start)};
                                if (match_end != npos && wb_boundaries_ok(match_start, match_end) &&
                                    (!require_full || match_end == text.size())) {
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
#if defined(__ARM_NEON) || defined(__SSE2__)
      // L-SIMD v2.1: mask-carried search. Scan a 16-byte block for any branch first-byte, then verify
      // every candidate the mask marks (in order — leftmost-first) with match_at before advancing to
      // the next block. The mask survives across candidates within the block (\ref clear_first, no
      // reload), which is where the win lives. Written ONCE against simd.hpp's uniform mask_t interface
      // — no `#if` ISA branch of its own (the first cut had a NEON/SSE2 pair of near-identical loop
      // bodies, dead weight on whichever ISA a given CI runner isn't; see simd_fixed_shape_scan's
      // comment for the same fix applied there). Scalar tail (< 16, the net-0-33 pins this boundary).
      if (!std::is_constant_evaluated() && prog_.hints.small_set_size >= 2 && prog_.hints.small_set_size <= 8) {
        const std::size_t           cnt {prog_.hints.small_set_size};
        std::array<std::uint8_t, 8> mem {};
        for (std::size_t i = 0; i < cnt; ++i) {
          mem[i] = static_cast<std::uint8_t>(prog_.hints.small_set[i]);
        }
        const std::size_t sz  {text.size()};
        std::size_t       pos {start};
        for (; pos + 16 <= sz; pos += 16) {
          std::array<std::uint8_t, 16> buf {};
          std::memcpy(buf.data(), text.data() + pos, 16); // MISRA-clean byte load (no pointer type-pun)
          mask_t mask                      {load_members_mask(buf.data(), mem.data(), cnt)};
          while (!empty(mask)) {
            const std::size_t lane {first_lane(mask)};
            const std::size_t me   {match_at(pos + lane, false)};
            if (me != npos) {
              out_slots[0] = pos + lane;
              out_slots[1] = me;
              return true;
            }
            mask = clear_first(mask);
          }
        }
        for (; pos < sz; ++pos) { // scalar tail: the last < 16 bytes (the net pins this boundary)
          const std::uint8_t b      {static_cast<std::uint8_t>(text[pos])};
          bool               member {false};
          for (std::size_t i = 0; i < cnt; ++i) {
            if (b == mem[i]) {
              member = true;
              break;
            }
          }
          if (member) {
            const std::size_t me {match_at(pos, false)};
            if (me != npos) {
              out_slots[0] = pos;
              out_slots[1] = me;
              return true;
            }
          }
        }
        return false;
      }
#endif
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
          case opcode::byte_loop_possessive:
          case opcode::klass_loop_possessive:
            // Tier 1 (D1, redesigned): by the time a leaf reaches step(), add_thread's closure
            // has ALREADY confirmed the atom matches at pos (that's precisely why it was parked
            // here instead of being routed to secondary_target immediately) — no re-test, no
            // fail branch, and no need to distinguish byte from klass here either (the arg8-vs-
            // arg16 test already happened in closure). See add_thread's own case for the full
            // rationale (a same-round-convergent alternation sibling could otherwise steal
            // priority from a step()-time exit decision, a real bug this redesign closes).
            tier1_capture_on_match(clist, i, instruction.primary_target, pos, pos + 1);
            advance_thread(clist, nlist, i, pc + 1, pos + 1);
            break;
          case opcode::klass_cp_loop_possessive:
            {
              // Closure already confirmed the codepoint matches; decode once more here purely
              // for dc.length (the chain-skip arithmetic) — cheap and deterministic, not a
              // second decision.
              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, pos)};
              tier1_capture_on_match(clist, i, instruction.primary_target, pos, pos + dc.length);
              advance_thread(clist, nlist, i,
                             pc + 1 + static_cast<std::int32_t>(4 - dc.length), pos + 1);
              break;
            }
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
              if (sem_ == match_semantics::longest) {
                // Leftmost-longest (POSIX / RE2 set_longest_match): keep the leftmost start, then the longest end
                // at that start. Record only a strictly-better match and do NOT cut — a lower-priority or later
                // thread may still extend it. Seeding has already stopped (matched), so no start past the
                // leftmost survives. A lazy quantifier therefore behaves greedily here (the longest end wins).
                const bool better {!matched
                                   || won[0] < out_slots[0]
                                   || (won[0] == out_slots[0] && won[1] > out_slots[1])};
                if (better) {
                  for (std::uint16_t s = 0; s < slot_count; ++s) {
                    out_slots[s] = won[s];
                  }
                }
                matched = true;
                break; // the match thread dies; the rest of the list and later positions may lengthen it
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
     * \brief Tier 1's on-match capture write (D1): if \p capture_start_slot is not -1, records
     *        [\p start, \p end) into thread \p i's capture block, in place.
     *
     * Called ONLY on a confirmed atom match — never speculatively before the test, which is
     * what makes this safe: a possessive loop always attempts one more repetition after every
     * success, so a `save` fired BEFORE knowing the next attempt succeeds would overwrite THIS
     * successful iteration's start the moment the next (possibly failing) attempt began,
     * corrupting the capture with a torn [next-attempt's-start, this-iteration's-end) pair. See
     * program.hpp's opcode-family note.
     *
     * \param[in,out] clist              The current thread list (whose slot this thread owns is updated).
     * \param[in]     i                  Index of the thread in \p clist.
     * \param[in]     capture_start_slot The start slot, or -1 for an uncaptured Tier 1 loop (a no-op).
     * \param[in]     start              Position before the atom was consumed.
     * \param[in]     end                Position after the atom was consumed.
     */
    constexpr void tier1_capture_on_match(list_type&    clist,
                                          std::size_t   i,
                                          std::int32_t  capture_start_slot,
                                          std::size_t   start,
                                          std::size_t   end)
    {
      if (capture_start_slot < 0) {
        return;
      }
      const auto    slot  {static_cast<std::uint16_t>(capture_start_slot)};
      std::uint32_t block {static_cast<std::uint32_t>(clist.slots[i])};
      block          = state_.pool.cow_write(block, slot, start);
      block          = state_.pool.cow_write(block, static_cast<std::uint16_t>(slot + 1), end);
      clist.slots[i] = block;
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
          case opcode::byte_loop_possessive:
            // Tier 1 (D1, redesigned): the match/no-match decision is made HERE, at insertion
            // time, in the SAME priority-ordered closure pass as everything else — not deferred
            // to step() one round later. That deferral was the root cause of a real bug this
            // opcode family shipped with first: inside an alternation, a same-round-convergent
            // LOWER-priority sibling (single leaf, resolves in one round) could claim the shared
            // convergence pc via its own advance_thread call BEFORE a step()-time exit from this
            // (HIGHER-priority, but multi-round) construct got a chance to compete — plain "first
            // felt this generation wins" dedup has no notion of true priority once insertion
            // order is violated. Precedented by assert_lookaround just above: a whole sub-VM
            // decision, evaluated at closure time; testing one byte here is far cheaper.
            //
            // A match: park as a leaf, exactly like byte/klass/klass_cp (step() consumes it,
            // capture-writes if captured, and re-inserts the SAME pc at pos+1 — where THIS SAME
            // closure logic re-decides, fresh). A non-match (or end of text): do NOT park —
            // continue the closure walk via secondary_target RIGHT NOW, in this pass, so the
            // exit's priority position is exactly this thread's own earned position, identical
            // in spirit to how `jump`'s target is pushed above.
            if (pos < text_.size() && static_cast<std::uint8_t>(text_[pos]) == instruction.arg8) {
              list.pcs.push_back(pc);
              list.slots.push_back(block);
            }
            else {
              stack.push_back({.pc = instruction.secondary_target, .block = block});
            }
            break;
          case opcode::klass_loop_possessive:
            if (pos < text_.size() &&
                prog_.classes[instruction.arg16].test(static_cast<std::uint8_t>(text_[pos]))) {
              list.pcs.push_back(pc);
              list.slots.push_back(block);
            }
            else {
              stack.push_back({.pc = instruction.secondary_target, .block = block});
            }
            break;
          case opcode::klass_cp_loop_possessive:
            {
              bool matched_here {false};
              if (pos < text_.size()) {
                const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, pos)};
                matched_here = dc.valid && cp_class_matches(prog_.cp_classes[instruction.arg16], dc.cp);
              }
              if (matched_here) {
                list.pcs.push_back(pc);
                list.slots.push_back(block);
              }
              else {
                stack.push_back({.pc = instruction.secondary_target, .block = block});
              }
              break;
            }
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
      const lookaround_sub& sub {prog_.lookarounds[sub_id]};
      // L1 peephole: a single-width body compiles to exactly [one consuming op; match] (code_length 2). Test it
      // directly, skipping the sub-VM scaffolding (~37 ns/eval — a ~4x win on the common single-class assertion,
      // P0-measured). Negation is over the RESULT (applied below), so an empty / boundary position flips right.
      if (sub.code_length == 2) {
        const instr& body   {prog_.code[static_cast<std::size_t>(sub.code_offset)]};
        const bool   direct {body.op == opcode::byte || body.op == opcode::klass
                             || (body.op == opcode::klass_cp && !prog_.byte_mode)};
        if (direct) {
          const bool matched {sub.direction == look_dir::behind ? single_class_behind(body, pos)
                                                                : single_class_ahead(body, pos)};
          return sub.negative ? !matched : matched;
        }
      }
      const bool matched {sub.direction == look_dir::behind ? lookbehind_matches(sub, pos)
                                                            : lookahead_matches(sub, pos)};
      return sub.negative ? !matched : matched;
    }

    //! \brief L1 peephole — does the single consuming op \p body match the code point / byte AT \p pos (ahead)?
    //!        Mirrors the per-op logic of \ref lookahead_matches for a one-instruction sub-program.
    [[nodiscard]] constexpr bool single_class_ahead(const instr& body,
                                                    std::size_t  pos)
    {
      if (pos >= text_.size()) {
        return false; // nothing ahead: the body cannot match — (?=…) false / (?!…) true (negated by the caller)
      }
      if (body.op == opcode::klass_cp) {
        const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, pos)};
        return dc.valid && cp_class_matches(prog_.cp_classes[body.arg16], dc.cp);
      }
      const auto b {static_cast<std::uint8_t>(text_[pos])};
      return body.op == opcode::byte ? b == body.arg8 : prog_.classes[body.arg16].test(b);
    }

    //! \brief L1 peephole — does \p body match the code point / byte ending EXACTLY at \p pos (behind)?
    //!        The defining lookbehind trap: the match must END at \p pos, so the code point is the one whose
    //!        aligned start s gives `s + length == pos` (byte mode: `pos - 1`).
    [[nodiscard]] constexpr bool single_class_behind(const instr& body,
                                                     std::size_t  pos)
    {
      if (pos == 0) {
        return false; // nothing behind: (?<=…) false / (?<!…) true (negated by the caller)
      }
      if (body.op == opcode::klass_cp) {
        std::size_t s {pos - 1};
        while (s > 0 && (static_cast<std::uint8_t>(text_[s]) & 0xC0U) == 0x80U) {
          --s; // recede over UTF-8 continuation bytes to the code point's aligned start
        }
        const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, s)};
        return dc.valid && s + static_cast<std::size_t>(dc.length) == pos
               && cp_class_matches(prog_.cp_classes[body.arg16], dc.cp);
      }
      const auto b {static_cast<std::uint8_t>(text_[pos - 1])};
      return body.op == opcode::byte ? b == body.arg8 : prog_.classes[body.arg16].test(b);
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
          // intentionally uncovered: -Wswitch exhaustiveness arm; nesting is rejected at parse
          // time, so a sub-program never contains an assert_lookaround.
          //
          // Tier 1's possessive-loop family is ALSO structurally absent here, for a related but
          // distinct reason: the compiler rejects a possessive/atomic quantifier inside a
          // lookaround (emit_possessive_repeat / emit_atomic_group throw on capture_free), so a
          // sub-program never contains one of these either — this dispatcher (and lookahead_
          // matches'/sub_fullmatch_window's own inline byte/klass/klass_cp-only dispatch) would
          // otherwise silently misread klass_cp_loop_possessive's arg16 against the wrong class
          // table. Folded into this same arm (not a separate one) — bugprone-branch-clone flags
          // adjacent case labels whose bodies are both just `break;`, comments notwithstanding.
          case opcode::byte_loop_possessive:
          case opcode::klass_loop_possessive:
          case opcode::klass_cp_loop_possessive:
            break;
        }
      }
    }
  };
} // namespace real::detail

#endif // REAL_PIKE_HPP
