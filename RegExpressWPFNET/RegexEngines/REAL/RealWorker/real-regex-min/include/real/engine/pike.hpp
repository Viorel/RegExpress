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
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
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

  /*!
   * \brief Candidate scan for a HETEROGENEOUS fixed shape: two positions filtered per vector compare,
   *        each survivor handed to \p verify.
   *
   * \ref simd_fixed_shape_scan serves only a shape whose every position accepts the identical set,
   * because there "all L lanes in range" *is* the verify. A shape whose positions differ — `(?i)cafe`
   * (position 0 accepts `{c,C}`, position 3 `{e,E}`), `\w\d\w\d` — had no vector path at all and
   * scanned byte by byte. This one filters on the two most selective positions the compiler picked
   * (\ref pattern_hints::fs_pair_width and friends), so it is a *prefilter*, not a fused verify.
   *
   * The mask is `load_range_mask(A) & load_range_mask(B)` — no new ISA primitive: both legs return a
   * per-lane all-ones/all-zero mask, so the bitwise AND is the conjunction on either packing.
   *
   * Linear by \ref simd_literal_scan's argument: the block loop advances 16 per round and verifies at
   * most 16 candidates of `fs_pair_width` bytes each, so the work stays `O(n · width)`, `width <= 16`.
   *
   * \tparam Verify Callable `(std::size_t) -> std::size_t`: the match end, or \ref real::npos.
   *                (by value, like \ref fast_search takes its MatchAt -- a forwarding reference here
   *                is never forwarded, which clang-tidy rightly flags).
   * \param[in]  text        The subject text.
   * \param[in]  start       Offset to begin scanning from.
   * \param[in]  hints       The pattern's hints (`fs_pair_*`).
   * \param[in]  verify      The caller's per-position walk (it also applies the `\b` conditions).
   * \param[out] match_end   On a match, the verified end offset.
   * \param[out] resume_from On no match, where the scalar walk should resume.
   * \return The verified match start, or \ref real::npos when this scan found none.
   */
  template <typename Verify>
  inline std::size_t simd_fixed_shape_pair_scan(std::string_view     text,
                                                std::size_t          start,
                                                const pattern_hints& hints,
                                                Verify               verify,
                                                std::size_t&         match_end,
                                                std::size_t&         resume_from)
  {
    const std::size_t width {hints.fs_pair_width};
    const std::size_t sz    {text.size()};
    resume_from = start;
    if (sz < width) {
      return npos;
    }
    const std::size_t off_a {hints.fs_pair_off_a};
    const std::size_t off_b {hints.fs_pair_off_b};
    const std::size_t last  {sz - width};
    std::size_t       pos   {start};
    while (pos + 16 <= last + 1) {
      std::array<std::uint8_t, 16> blk_a {};
      std::array<std::uint8_t, 16> blk_b {};
      std::memcpy(blk_a.data(), text.data() + pos + off_a, 16); // MISRA-clean byte loads (no type-pun)
      std::memcpy(blk_b.data(), text.data() + pos + off_b, 16);
      mask_t m {load_range_mask(blk_a.data(), hints.fs_pair_a_lo0, hints.fs_pair_a_hi0,
                                hints.fs_pair_a_lo1, hints.fs_pair_a_hi1)
                & load_range_mask(blk_b.data(), hints.fs_pair_b_lo0, hints.fs_pair_b_hi0,
                                  hints.fs_pair_b_lo1, hints.fs_pair_b_hi1)};
#if defined(REAL_TEST_INSTRUMENT)
      // Bill this round's 16 candidate starts, as find_bytes_cascade bills its rounds. NOT optional:
      // the deterministic work-counter gate is what caught the historical O(n^2) icase cascade, and it
      // caught it only once that function billed. An icase literal reaches THIS path now, so a scan
      // that bills nothing would silently un-cover the very shape the gate exists for (observed:
      // work_units == 0 at both 256 KiB and 1 MiB, turning the assertion into 0 < 0).
      prefilter_note_scan(16);
#endif
      while (!empty(m)) {
        const std::size_t cand {pos + first_lane(m)};
        const std::size_t e    {verify(cand)};
        if (e != npos) {
          match_end = e;
          return cand;
        }
        m = clear_first(m); // this window can still hold a later candidate — no reload
      }
      pos += 16;
    }
    resume_from = pos; // fewer than 16 candidate starts left: the scalar walk takes it from here
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
   * \brief One frame on the epsilon-closure DFS stack (COW): a program counter to explore, plus the
   *        capture block the branch carries. The block travels with the branch — a `split` shares it and a
   *        `save` copies it on write — so there is no slot-restore entry and no shared working array.
   */
  struct eps_entry
  {
    std::int32_t  pc;    //!< The program counter to explore.
    std::uint32_t block; //!< Index of the capture block this branch carries (into the capture pool).
  };

  /*!
   * \brief Copy-on-write pool of capture blocks (COW) — the one capture-slot mechanism for both storages.
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

    /*!
     * \brief Reset for a new match run: block 0 = all-`npos`, held by a permanent sentinel ref.
     *
     * The storage grows by \ref allocate (heap for dynamic; a compile-sized static_vec for static,
     * whose capacity bounds the live-block count and so is never exceeded).
     *
     * \param[in] slot_count Slots per block, i.e. the program's capture-slot count.
     */
    constexpr void reset(std::uint16_t slot_count)
    {
      width = slot_count;
      // Reserve a modest block budget before the first allocate(). Without it these three vectors
      // grow by doubling DURING the search -- a general-VM search over `\w+@\w+` made 13 heap
      // allocations, and the size sequence (4, 8, 32, 16, 64, 128 ...) is a doubling ladder, not
      // work. Reserving collapses most of them, and fast-route patterns are untouched either way --
      // the control that makes the gain readable as this pool's own.
      //
      // A FLOOR, not a bound: allocate() still grows past it, so a capture-heavy walk is bounded by the
      // same free-list recycling as before, not by this constant.
      //
      // The compile-time storage's pool is `static_vec` and has no reserve(): it is sized exactly at
      // compile time and never allocates at all, which is why this is gated on the expression rather
      // than on a storage trait.
      constexpr std::size_t initial_blocks {8};
      if constexpr (requires { data.reserve(std::size_t {}); }) {
        data.reserve(static_cast<std::size_t>(slot_count) * initial_blocks);
        refcount.reserve(initial_blocks);
        free_list.reserve(initial_blocks);
      }
      data.assign(slot_count, npos);
      refcount.assign(1, 1); // block 0, sentinel refcount 1 (never freed)
      free_list.clear();
    }

    /*!
     * \brief Pointer to block \p b's `width` slots. Invalidated by any \ref allocate that grows `data`.
     * \param[in] b Block index.
     * \return Pointer to the block's first slot.
     */
    [[nodiscard]] constexpr std::size_t* slots(std::uint32_t b)
    {
      return &data[static_cast<std::size_t>(b) * width];
    }

    /*!
     * \brief A fresh block with refcount 1 (a recycled index if available, else a grown one).
     * \return Index of the new block; its slots hold whatever the recycled block last held.
     */
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

    /*!
     * \brief Take a reference on block \p b (each closure seeded into the next list holds its own).
     * \param[in] b Block index.
     */
    constexpr void incref(std::uint32_t b)
    {
      ++refcount[b];
    }

    /*!
     * \brief Drop a reference on block \p b, recycling it into the free list at zero.
     * \param[in] b Block index.
     */
    constexpr void decref(std::uint32_t b)
    {
      if (--refcount[b] == 0) {
        free_list.push_back(b);
      }
    }

    /*!
     * \brief Write `slots(b)[slot] = value`, detaching first if \p b is shared. The one place a block mutates.
     *
     * \param[in] b     Block to write through.
     * \param[in] slot  Slot index within the block.
     * \param[in] value Value to store.
     * \return The block that now holds the write: a fresh private copy when \p b was shared, else \p b.
     */
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

    /*!
     * \brief Sum of all live refcounts — the debug Σ-invariant checks this equals the references the VM
     *        actually holds (list blocks + stack frames), catching a leaked or double-freed block.
     * \return The sum, including the sentinel block's permanent reference.
     */
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
     *
     * `noinline`, and it buys codegen rather than cycles: this runs twice per search, so outlining
     * it is free, while `mark.assign` inlined here is a per-element `construct_at` loop whose mere
     * presence charges the class-scan routes on gcc/x86 -- routes that never build a thread list at all.
     * Measured alone the outlining is a REGRESSION; it pays only together with the SBO mark table, whose
     * codegen it is repairing. The two belong together or not at all.
     *
     * \param[in] code_size Number of instructions (sizes the mark table once).
     */
#if defined(__GNUC__)
    __attribute__((noinline))
#endif
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
   * \brief Unicode-property sparse 2-stage membership for code points > U+07FF (page = cp>>8 → 256-bit
   *        block). Thread-local heap cache only — \ref basic_pike_state's size is unchanged, which is what
   *        keeps the ASCII class loop clear of it.
   */
  struct cp_hi_table
  {
    static constexpr std::uint16_t             empty {0xFFFFU}; //!< \ref page_of sentinel: no members in that page.
    std::vector<std::uint16_t>                 page_of;         //!< page_of[cp>>8] → index into \ref blocks, or \ref empty.
    std::vector<std::array<std::uint8_t, 32>>  blocks;          //!< Non-empty 256-bit pages only.

    /*!
     * \brief Two-stage membership probe: page index, then bit test inside that page's 256-bit block.
     *
     * \param[in] cp Code point to test.
     * \return True when \p cp is a member; false for any code point above the built range.
     */
    [[nodiscard]] bool contains(char32_t cp) const noexcept
    {
      const std::uint32_t u    {static_cast<std::uint32_t>(cp)};
      const std::uint32_t page {u >> 8U};
      if (page >= page_of.size()) {
        return false;
      }
      const std::uint16_t idx {page_of[page]};
      if (idx == empty) {
        return false;
      }
      const auto&         blk {blocks[idx]};
      const std::uint32_t lo  {u & 0xFFU};
      return ((static_cast<unsigned>(blk[lo >> 3U]) >> (lo & 7U)) & 1U) != 0U;
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
  // MISRA deviation, documented in docs/MISRA.md: \ref table and \ref cp_page are deliberately left
  // uninitialized -- each is a lookup table filled in full on a miss against its own sentinel
  // (\ref table_class / \ref cp_page_class, both -1 here), so zeroing them was never observed and cost a
  // 496-byte clear on every state construction. `search()` builds a fresh state per call, so that landed on
  // every single search. Residual cost of suppressing at the record: a member added later WITHOUT an
  // initializer would not be flagged for this struct -- every other member here carries one.
  template <typename ThreadList, typename EpsVec>
  struct basic_pike_state
  {
    /*!
     * \brief Value-initializes \ref table and \ref cp_page during constant evaluation only.
     *
     * Same reason as \ref real::detail::static_vec's own constructor: MSVC's constant evaluator rejects an
     * object carrying an indeterminate subobject even where nothing reads it (C2131 on `static_regex`'s
     * compile-time assertions), while clang and gcc accept it. The run-time path, which is the one that
     * was paying a 496-byte clear per `search()`, keeps the trivial initialization everywhere.
     */
    // MISRA deviation, documented in docs/MISRA.md: \ref table and \ref cp_page are left trivially
    // initialized at run time on purpose — each is filled in FULL on a miss against its own sentinel
    // (\ref table_class / \ref cp_page_class, both -1 here), so the zeros were never read and cost a
    // 496-byte clear on every state construction, which `search()` pays per call.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    constexpr basic_pike_state() noexcept
    {
      if (std::is_constant_evaluated()) {
        table   = {};
        cp_page = {};
      }
    }

    // TWO NAMED MEMBERS, NOT `ThreadList lists[2]`, and it must stay that way. An array of two is
    // constructed by a loop, and gcc widens that loop body's sparse zero-stores (each list's handful of
    // bookkeeping words) into a `memset` spanning the whole element -- 2472 bytes per list, 4944 per
    // state, which is exactly the inline buffers this file documents as deliberately left
    // uninitialized. The layout is unchanged (two consecutive ThreadLists either way), so this is a
    // codegen constraint and nothing else; a single list, or two named ones, gets scalar stores at the
    // real offsets. `std::array<ThreadList, 2>` does NOT avoid it -- it is the loop that does it, not
    // the C array. Every access site takes `&list_a`/`&list_b` once and then rotates POINTERS, so
    // nothing indexes these with a runtime value and naming them costs no access.
    ThreadList list_a; //!< One of the two thread lists; the run rotates pointers, not indices.
    ThreadList list_b; //!< The other one — see \ref list_a.
    EpsVec     stack;  //!< Epsilon-closure DFS stack.

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
    std::array<std::uint8_t, 256>  table;         //!< 1 where the byte is in \ref table_class (filled on a class_table miss).

    /*!
     * \brief Membership bitmap for a `cp_class` over the 2-byte UTF-8 range `[U+0080, U+07FF]`, and
     *        the class it was built for. European text lives almost entirely in this range (Latin, IPA,
     *        Greek, Cyrillic, Hebrew, Arabic…), so a 240-byte bitmap answers it in one load. Built once
     *        per class and reused across a `find_all`-style walk.
     *
     *        Code points beyond U+07FF (CJK, astral) use \ref cp_hi_table (2-stage sparse pages), not an
     *        inline BMP array — expanding this field would inflate every pattern's hot state (ASCII
     *        class-loop sensitivity, §A 7.46). The table is heap-only (thread-local cache) and built
     *        lazily on the first high-cp membership probe.
     */
    std::int32_t                   cp_page_class {-1};
    std::array<std::uint64_t, 30>  cp_page;         //!< 1 where the code point (U+0080..U+07FF) is a member (filled on a cp_page_table miss).
    // Appended LAST, and it must stay that way -- same rule, and same reason, as pattern_hints states for
    // its own trailing fields. Placed next to `table` where they belong by meaning, these three shift every
    // field after them and charge a word-boundary walk heavily through the crate, on a pattern that reads
    // none of them. Only the C ABI path sees it: that path crosses per match, where the C++ harnesses call
    // the engine directly and measure the same change as neutral.
    //! \brief Program whose membership rows this state has already verified as filled. Lets a walk skip
    //!        the two acquire loads `class_table` would otherwise do once per `run()` — see there.
    const void*                    rows_verified_for {nullptr};
    const std::uint8_t*            row_ptr           {nullptr}; //!< The verified byte row, cached so the hot path returns it without re-deriving the address.
    const std::uint64_t*           page_ptr          {nullptr}; //!< As \ref row_ptr, for the two-byte page bitmap.
    // Appended after those, for the same reason they were: anything inserted earlier shifts every
    // field behind it. The sparse hi table is resolved once per (state, class) instead of once per
    // CODE POINT: `cp_hi_cached`'s hot path is two thread_local reads plus a fingerprint compare, and
    // callgrind put `cp_member_high` at a quarter of a `\p{L}+` walk's instructions, dozens per code point
    // for what is a two-load bit test once the table is in hand. Hoisting the same resolution into the span
    // filler instead was measured and refused -- a TLS access in that function's body charges the
    // property-class rows heavily -- so it lives here, where the page bitmap's own cache already lives.
    std::int32_t                   hi_class          {-1};      //!< Class \ref hi_ptr / \ref hi_never were resolved for; -1 while unresolved.
    const cp_hi_table*             hi_ptr            {nullptr}; //!< The resolved sparse hi table for \ref hi_class, or null when it has none usable.
    bool                           hi_never          {false};   //!< Set when \ref hi_class has no high ranges at all, so every cp past the page bitmap misses.
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
    //! \brief Isolated sub-scratch for bounded lookaround evaluation, built on first use — see
    //!        \ref real::detail::dynamic_storage::state_type for the measurement that made it lazy.
    std::optional<lookaround_scratch> lookaround;
    capture_pool                      pool;                          //!< copy-on-write capture blocks (heap-backed).
    std::optional<lazy_dfa>           fwd_dfa;                       //!< Fallback when immut is null; prefer shared_fwd_dfa.
    std::optional<reverse_dfa>        rev_dfa;                       //!< Fallback reverse; prefer shared_rev_dfa.
    const void       *                dfa_program         {nullptr}; //!< Program the per-state DFAs were built for (fallback).
    std::optional<reverse_dfa>        il_prefix_rev;                 //!< Fallback IL prefix reverse; prefer shared_il_prefix_rev.
    const void       *                il_prefix_for       {nullptr}; //!< Fallback: prefix program il_prefix_rev was built for.
    const void       *                il_text             {nullptr}; //!< IL: the haystack \ref il_abandoned refers to (reset the flag when it changes).
    bool                              il_abandoned        {false};   //!< IL: a linearity/density guard tripped on this haystack — stay on the core.
    std::uint32_t                     il_density_cands    {};        //!< IL candidates seen on this haystack (density sample).
    std::size_t                       il_density_origin   {npos};    //!< Byte offset of the first IL candidate this haystack.
    const void       *                rare_disc_text      {nullptr}; //!< Rare-disc: haystack \ref rare_disc_abandoned refers to.
    bool                              rare_disc_abandoned {false};   //!< Rare-disc density guard: stay on prefix for this haystack.
    const void       *                ac_text             {nullptr}; //!< AC: the haystack \ref ac_dense was decided on.
    bool                              ac_decided          {false};   //!< AC: the density sample has run on this haystack.
    bool                              ac_dense            {false};   //!< AC: candidates are dense enough that the automaton wins.
    // AC fields placed LAST (own reason as pattern_hints::alternation_branch_count): inserting
    // here right after il_prefix_for would shift il_text/
    // il_abandoned/il_density_cands/il_density_origin (the inner-literal density-gate fields, read
    // on every search that takes that route) by the full size of std::optional<ac_automaton> plus
    // a pointer. Caught by re-inspection against dynamic_storage::state_type (storage.hpp), which
    // already had this right -- this struct (pike_state) is test-harness-only (the meta-seam
    // differential), never real::regex's own runtime state, so the mid-struct version never
    // affected any measured path; fixed anyway for consistency with the stated placement rule.
    //! \brief Marks a state whose storage benefits from the multi-literal route. A compile-time-sized
    //!        marker, not a field: the automaton itself lives per REGEX in \ref detail::regex_immutables,
    //!        so nothing about it belongs in a state that is rebuilt on every `search()`.
    static constexpr bool supports_aho_corasick {true};
  };

  /*!
   * \brief The Pike VM, generic over the scratch-state container policy.
   * \tparam State A \ref basic_pike_state instantiation (vector- or static-backed).
   * \tparam StateBoundToProgram The caller guarantees this state is never used with a second program —
   *         it is either freshly constructed for this search or owned by a walk over one regex. The
   *         membership-row accessors then need no program-identity compare: a fresh state's
   *         `table_class` is already -1, so a row key matching is proof on its own. That compare is per
   *         `run()`, and `run()` is per MATCH on a walk (11 327 times over 64 KiB on `[a-z]+`), which
   *         measured 2.9 points of that walk. Defaults to \c false: an embedder holding a state across
   *         regexes (the Python binding, the meta-seam harness) must keep the compare.
   */

  template <typename State, bool StateBoundToProgram = false>
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
     * \tparam Cascade  Select the memchr-cascade class-run variant (chosen once by the caller from
     *                  stop_set_size, never per match). Off = the plain hot path, byte for byte.
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
     * \note On gcc/x86 the mere PRESENCE of the Aho-Corasick code in this translation unit slows a
     *       class scan that never dispatches to it -- instructions and cache misses byte-identical, so
     *       it is front-end loop alignment and nothing the code does. Forcing `align-loops` does not
     *       fix it: the class-scan routes share one inlined loop body with different alignment optima,
     *       so any single value trades one route's regression for another's. Accepted as it stands.
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
      // The trailing-lookahead walk is NOT dispatched here — it lives outside pike_vm::run (real.hpp /
      // find_iter) so this function keeps its size and keeps inlining into find_iter
      // (on x86 it was a double-digit regression once cold code pushed run() past the inline threshold).
      if (sem_ == match_semantics::first && prog_.hints.greedy_class_loop >= 0
          && (std::is_constant_evaluated() || !class_fastpath_disabled())) {
        // the memchr-cascade instantiation (Cascade) is selected ONCE by the caller (a whole
        // find_iter/search) from stop_set_size, never per match — so when it is off this run is byte-for-
        // byte the plain per-byte loop and the hot path pays nothing. The caller only sets Cascade
        // when stop_set_size >= 1, so the cascade tail always has real stop bytes.
        prof::tick_route(prof::route::class_loop);
        if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
          prof::tick_event(prof::event::wb_b2_wrap);
        }
        // One branch, and everything an anchor implies is behind it. `run()` has to stay small enough
        // to inline into find_iter (see basic_match_iterator::advance's own note), and an earlier
        // shape of this -- two lambdas inline here -- charged the Unicode rows on one toolchain while
        // gaining on the other, which is this translation unit's usual answer to being grown.
        if (prog_.hints.anchored_start || prog_.hints.greedy_class_loop_end != 0) {
          return run_class_loop_anchored<Cascade>(text, start, mode, out_slots);
        }
        return run_class_loop<Cascade>(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.greedy_cp_class >= 0
          && (std::is_constant_evaluated() || !class_fastpath_disabled())) {
        prof::tick_route(prof::route::cp_class_loop);
        if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
          prof::tick_event(prof::event::wb_b2_wrap);
        }
        if (prog_.hints.anchored_start) {
          if (start != 0) { // a region past 0 cannot hold a `\A`-anchored match, in any mode
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
          return run_cp_class_loop(text, start,
                                   mode == run_mode::search ? run_mode::prefix : mode, out_slots);
        }
        return run_cp_class_loop(text, start, mode, out_slots);
      }
      // possessive class+/++ loop -- bare/suffixed or delimited/"quoted". A
      // possessive pattern can never also be greedy_class_loop/greedy_cp_class (mutually exclusive
      // AST shapes), so ordering relative to those two is moot -- it matters only relative to
      // exact_literal/inner_literal/lazy-DFA below, which this shape reliably beats (see the
      // route-profile contract: those routes decline every possessive opcode outright today).
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
      // `il_text` as the proxy for "this state can track IL abandonment": every storage reaching this route
      // needs the per-haystack guard fields, and only the reverse-confirm sub-case below needs a reverse
      // DFA. Gating on the DFA instead would exclude a storage that profits from the literal sweep without
      // ever confirming through it (see `wants_inner_literal` in storage.hpp).
      if constexpr (requires(State & s) {
        s.il_text;
      }) {
        // The prefix sub-program feeds the reverse confirm and nothing else, so it is required only of a
        // storage that can run one. Without it the route is the memmem sweep plus a hand-back to the core
        // on the first candidate, which needs no prefix program at all -- and not compiling one is what
        // keeps a second compile out of the constant-evaluation budget.
        constexpr bool confirms_by_reverse {requires(State & s) {
                                              s.il_prefix_rev;
                                            }};
        if (!std::is_constant_evaluated() && !inner_literal_route_disabled() && mode == run_mode::search
            && sem_ == match_semantics::first // longest semantics need the general loop (these routes are kFirstMatch)
            && prog_.hints.inner_literal_len > 0 && prog_.hints.inner_literal_prefix >= 1
            && !prog_.hints.fixed_shape       // see the note below: IL never beats the fixed-shape route
            && (!confirms_by_reverse || !prog_.prefix_code.empty())) {
          // NOT WHEN A FIXED-SHAPE ROUTE EXISTS. That route scans for the shape's own first-byte class
          // and confirms a known width, and it is never slower than memmem-plus-reverse-confirm on the
          // patterns that have both: measured against the same patterns with this route disabled, the
          // inner literal is at best a tie and otherwise a small loss on every one of them.
          //
          // Across a density sweep the gap is widest where the OLD gate was most confident -- around a
          // few dozen candidates per thousand bytes, well under the
          // 60 that would have made il_density_milli_threshold abandon. The gate's threshold was
          // calibrated against the DFA as the fallback; against fixed_shape the crossover is not
          // merely elsewhere, it does not exist -- so the fix is a route condition, not a retune.
          //
          // Patterns the inner-literal route was built for are unaffected by construction: `\w+@\w+`
          // and friends are variable-width and have no fixed_shape hint to trip this.
          // A required literal at offset 0 (a match that DOES begin with a literal) is a *prefix*, not an inner
          // literal — it keeps the faster find_prefix path. Only a genuine inner literal (offset >= 1, for which
          // the compiler built a `prefix_code` for the reverse-confirm) takes this route; the old
          // `inner_literal_prefix == 0` clause routed prefixes here too and cost ~3.3x on dense corpora.
          // No size guard: on a no-match haystack the route is memmem-only (the reverse setup is lazy, built on
          // the first candidate, never here), so it wins at every size; and the prefix byte-program is a
          // per-regex immutable (built once, amortized by any later use — the lazy-DFA warmup's own contract).
          il_reset_on_new_haystack(text);
          if (!state_.il_abandoned) {
            bool       abandon {false};
            const bool matched {run_inner_literal(text, start, out_slots, abandon)};
            if (!abandon) {
              prof::tick_route(prof::route::inner_literal);
              prof::tick_event(prof::event::memmem);
              return matched;
            }
            // Fall through to core. Density/linearity set il_abandoned inside run_inner_literal
            // (sticky for this haystack). Size-floor abandon is ephemeral so the next advance can
            // re-enter IL after il_warmed flips (warm floor) — sticky size abandon would lock a
            // reused <cold-floor buffer on core forever.
            prof::tick_event(prof::event::il_abandoned);
          }
        }
      }
#if defined(__ARM_NEON) || defined(__SSE2__)
      // Heterogeneous fixed shape with a usable two-position filter (fs_pair_width; the compiler sets it
      // only when the homogeneous fused scan does NOT apply and no single byte is memchr-able). Its own
      // route so run_fixed_shape stays byte-identical -- see run_pair_filtered_shape.
      // slot_count <= 2 (groupless) is load-bearing, not a convenience: a CAPTURING fixed shape needs its
      // group slots filled, which run_fixed_shape does through its own grouped path (fill_fixed_saves at
      // compile-time-constant offsets). This route writes only [0,1], so on `([ab])(a)` it reported a
      // width-2 match with slots sized to 2 and the walk never terminated -- caught by exhaustive-compat
      // (3.2M cases) as a runaway, and by a routed-vs-unrouted differential over the same corpus as a
      // wrong span. Groupless is the whole target anyway ((?i)cafe, [ab][cd]); grouped shapes keep the
      // ordinary walk.
      if (!std::is_constant_evaluated() && sem_ == match_semantics::first
          && mode == run_mode::search && prog_.hints.fs_pair_width >= 2
          && prog_.slot_count <= 2 && !fixed_shape_pair_route_disabled()) {
        prof::tick_route(prof::route::fixed_shape_pair);
        return run_pair_filtered_shape(text, start, out_slots);
      }
#endif
      if (sem_ == match_semantics::first && prog_.hints.fixed_shape) {
        if (prog_.hints.anchored_start && start != 0) {
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }
        prof::tick_route(prof::route::fixed_shape);
        const bool matched {run_fixed_shape(text, start,
                                            prog_.hints.anchored_start && mode == run_mode::search
                                              ? run_mode::prefix : mode, out_slots)};
        if (matched && prog_.hints.fs_end_anchor != 0) {
          const std::size_t e      {out_slots[1]};
          const bool        at_end {e == text.size()
                                    || (prog_.hints.fs_end_anchor == 2 && e + 1 == text.size() && text[e] == 0x0A)};
          if (!at_end) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
        }
        return matched;
      }
      if (sem_ == match_semantics::first && prog_.hints.codepoint_class_ascii >= 0
          && (std::is_constant_evaluated() || !class_fastpath_disabled())) {
        // The SWAR variant (Cascade) is chosen once per walk, like the class-loop cascade.
        // class_fastpath_disabled: same test seam as class_loop / cp_class_loop (matrix codepoint_class rows).
        prof::tick_route(prof::route::codepoint_class);
        return run_codepoint_class<Cascade>(text, start, mode, out_slots);
      }
      if (sem_ == match_semantics::first && prog_.hints.fixed_alternation) {
        // Past the measured branch-count threshold, a single Aho-Corasick automaton walk
        // beats small_set's 2..8-member memchr-cascade scan (which has no fast path at all past 8
        // distinct first bytes — the alternation gap). Search mode only (the automaton's
        // whole value is candidate-finding; full/prefix modes keep the existing run_alternation,
        // unchanged). Dynamic storage only (if constexpr elides this for static_regex, which does
        // not benefit from AC — measured) and only when the automaton actually built
        // (ensure_ac_automaton declines, leaving it unset, on a pathological icase-expansion
        // branch — falls through to run_alternation below, zero behavior change).
        if constexpr (requires { State::supports_aho_corasick; }) {
          if (!std::is_constant_evaluated() && !aho_corasick_route_disabled() && mode == run_mode::search
              && prog_.hints.alternation_branch_count >= ac_branch_floor
              && ac_density_favours_automaton(text, start)) {
            if (ac_ready() != nullptr) {
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
          ensure_op_table();
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
          // noinline out of run() so the shared-DFA body cannot bloat class-loop codegen (x86
          // witness/wplus regression pattern — same fix shape as ensure_ac_automaton).
          if (const std::optional<bool> dfa_result {
            try_shared_lazy_dfa_search<Cascade>(text, start, mode, out_slots)}) {
            return *dfa_result;
          }
        }
      }
      if (sem_ == match_semantics::longest) {
        // The longest path uses the plain general loop (the memchr-cascade variant is a first-match
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
     *
     * \param[in]  text         Subject.
     * \param[in]  start        Byte offset to begin at.
     * \param[in]  mode         Anchoring: full, prefix or search.
     * \param[out] out_slots    Capture slots, filled on a match.
     * \param[out] forward_stop When non-null, receives how far the forward scan reached — the
     *                          inner-literal route's linearity backstop.
     * \return True on a match.
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
      auto*             clist     {&state_.list_a};
      auto*             nlist     {&state_.list_b};
      clist->reset(code_size);
      nlist->reset(code_size);
      out_slots.assign(prog_.slot_count, npos);
      state_.pool.reset(prog_.slot_count); // fresh COW pool (block 0 = all-npos, per this run)

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
          // a seed shares the canonical all-npos block (one incref, no allocation); the first save
          // in its closure copies-on-write off it, so block 0 is never mutated.
          if (!prog_.hints.capture_free_walk) {
            state_.pool.incref(pool_type::npos_block);
          }
          // Capture-free: `pos` is what `save 0` at pc 0 will set anyway; passing it keeps the parameter
          // meaningful rather than a sentinel the walk happens to ignore.
          add_thread(*clist, 0, pos,
                     prog_.hints.capture_free_walk ? pos : std::size_t {pool_type::npos_block});
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
        detail::prof::tick_thread_count(clist->pcs.size());
        step(*clist, *nlist, pos, mode, matched, out_slots);
        auto* swap {clist};
        clist = nlist;
        nlist = swap;
        cow_release_blocks(*nlist); // the old clist was consumed by step — drop its block refs
        nlist->reset(code_size);
        ++pos;
      }
      cow_release_blocks(*clist); // drain both surviving lists on exit (the leak class the review named)
      cow_release_blocks(*nlist);
      // Σ-invariant: after a full drain only the canonical npos block's sentinel ref remains. A leaked
      // block (missing decref) or a double-free (underflow) breaks it. Debug/sanitize builds only.
      assert(state_.pool.total_refs() == 1 && "COW capture-block refcount leak or imbalance");
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
     *        (then the anchored Pike returns false and the caller advances).
     *
     * \param[in]  text      Subject.
     * \param[in]  s         Candidate match start to confirm.
     * \param[out] out_slots Capture slots, filled on a match.
     * \param[out] stop      How far the confirm reached, for the linearity backstop.
     * \return True when a match begins exactly at \p s.
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
          // anchored_end on the shared confirm DFA (under slot.mu). begin_scan mirrors the per-regex design
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
              // A bare forward_end miss would set stop = text.size(); keep a floor of s for the IL backstop.
              if (stop < s) {
                stop = s;
              }
              out_slots.assign(prog_.slot_count, npos);
              return false;
            }
            const std::size_t e {match_end};
            stop = e;
            ensure_op_table();
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
     * \brief Re-enables the inner-literal route and clears its density counters on a new haystack.
     *
     * ONE MECHANISM, not two: the route's guards are sticky per haystack (`il_abandoned`, and the density
     * pair behind it), and both `run()`'s gate and \ref fill_inner_literal_spans have to observe the same
     * reset at the same moment. Written out in each place, a guard re-enabled in one and not the other is a
     * silent behaviour difference between a batched walk and a per-match walk -- the exact shape of defect
     * the batching work has produced twice already.
     *
     * \param[in] text The subject being scanned.
     */
    constexpr void il_reset_on_new_haystack(std::string_view text)
    {
      // GUARDED, and the guard is the whole reason this compiles for both storages: the IL fields exist on
      // the dynamic state and on a static one only when its tier wants IL (`static_il_guard_fields`), so an
      // unguarded body here fails to compile for `static_pike_scratch<…, WantsIL = false>`. The route's own
      // gate had the guard by living inside a discarded `if constexpr` branch; factoring the body out moved
      // the obligation here.
      if constexpr (requires(State & st) {
        st.il_abandoned;
      }) {
        if (state_.il_text != static_cast<const void*>(text.data())) {
          state_.il_abandoned      = false; // a fresh haystack: re-enable and re-evaluate its guards
          state_.il_density_cands  = 0;
          state_.il_density_origin = npos;
          state_.il_text           = static_cast<const void*>(text.data());
        }
      }
      else {
        static_cast<void>(text);
      }
    }

    /*!
     * \brief The inner-literal search: memmem a required literal, reverse-match the prefix to the match start,
     *        forward-confirm — the reverse-inner protocol (regex-automata's `ReverseInner`).
     *
     * Search mode only, runtime only (the reverse DFA is not constexpr). Two guards keep it linear: the
     * reverse is bounded below by `min_match_start` (the previous literal's end), and a literal starting
     * before `min_pre_start` (the last confirm's forward reach) abandons the scan.
     *
     * \param[in]  text      Subject.
     * \param[in]  start     Byte offset to begin the scan at.
     * \param[out] out_slots Capture slots, filled on a match.
     * \param[out] abandon   Set when a linearity guard trips, so the caller retries the whole search
     *                       on the core VM.
     * \param[in]  density_gate Whether to consult the candidate-density gate. False only for the batched
     *                       filler: that counter is read before reverse/confirm, so it cannot tell a
     *                       candidate that fails from one that completes, and the filler's stream is the
     *                       latter.
     * \return True on a match; false on none, and false with \p abandon set when the route gave up.
     */
    template <typename OutSlots>
    bool run_inner_literal(std::string_view text,
                           std::size_t      start,
                           OutSlots&        out_slots,
                           bool&            abandon,
                           bool             density_gate = true)
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
        if (h != npos) {
          detail::prof::tick_prefilter_candidate();
        }
        if (h == npos) {
          out_slots.assign(prog_.slot_count, npos);
          return false;   // no more candidates (no-match): memmem-only — the guard below was never reached
        }
        if (first_candidate) {
          first_candidate = false;
          // No immutables, no reverse-by-class AND no fixed code-point shape: IL is a NO-MATCH accelerator
          // here and nothing more.
          // Reaching this point means memmem found a candidate, and without a way to place its match start
          // the route's own confirm has to beat the core scans -- which for this storage are the
          // exactly-sized compile-time ones, and they win. A prefix that IS one class loop does have a way
          // (`il_rev_class`, the backward walk below); so does a fixed code-point shape, which steps back a
          // known count. Both stay. Measured over a 64 KiB corpus, for the shapes that have neither, static
          // vs the same pattern on the dynamic regex:
          //   a date shape with dates PRESENT      core slightly ahead of the inner literal
          //   the same shape with NO date present   core two orders of magnitude behind it
          // So: keep the memmem-only sweep, hand back the moment a candidate needs confirming. Sticky,
          // so the cost on a hit haystack is one candidate, once. A sparse-hit haystack would still
          // rather stay on IL; that needs a candidate-cost model this has no measurement for yet.
          if (prog_.immut == nullptr && prog_.hints.il_rev_class < 0 && !prog_.hints.il_cp_shape_eligible) {
            abandon             = true;
            state_.il_abandoned = true;
            return false;
          }
          // Small-haystack guard, once per scan (sticky via il_abandoned). Applies only when a match
          // candidate exists — no-match is memmem-only and never gated. Floor is cold vs warm: cold
          // first scan uses il_min_haystack (~94 KB email, amortizes reverse-DFA *build*); warm scans
          // (shared il_prefix_rev already in shared_dfa_slot) use il_warm_floor (~4 KB). WHICH
          // floor applies keys on slot.il_warmed ("this regex was candidate-scanned"), not
          // "il_prefix_rev is built" — a corpus always below the cold floor would never build the
          // reverse and would never warm. Whether EITHER applies is a different question, and keys on
          // `built_for` instead: see the note at the check itself.
          // Below the WARM floor the answer is the same whichever floor applies, so it is decided
          // BEFORE ensure_immutables rather than after. il_min_haystack clamps at 64 KB, so it is
          // never below il_warm_floor's 4 KB: while the build is unpaid, a haystack under 4 KB abandons
          // whichever floor applies. Deciding
          // after the build meant paying for it -- and for a text-mode class that build is not small.
          // On a first search over a short subject, a text-mode class spends orders of magnitude longer
          // constructing its UTF-8 machinery than the identical shape over an ASCII class -- and then
          // abandons this route on the very next line. The warm flag is still
          // set, since shared_dfa_for keys on the immutables ADDRESS and needs nothing built.
          // Both floors lift once the build is PAID, and the predicate is `built_for`, never `il_warmed`:
          // the branch below sets `il_warmed` on its way out, so keying the lift on it would let the
          // second short call through and charge it the build this placement exists to avoid.
          const bool built {prog_.immut != nullptr
                            && prog_.immut->built_for.load(std::memory_order_acquire) == prog_.code.data()};
          if (!inner_literal_guard_disabled() && prog_.immut != nullptr && !built
              && text.size() < il_warm_floor) {
            shared_dfa_for(prog_.immut).il_warmed.store(true, std::memory_order_relaxed);
            abandon = true;
            return false;
          }
          ensure_immutables();
          if (!inner_literal_guard_disabled() && prog_.immut != nullptr) {
            shared_dfa_slot&  slot  {shared_dfa_for(prog_.immut)};
            const bool        warm  {slot.il_warmed.load(std::memory_order_relaxed)};
            const std::size_t floor {warm ? il_warm_floor : prog_.immut->il_min_haystack};
            slot.il_warmed.store(true, std::memory_order_relaxed); // next scan is warm even if we abandon
            // THE floor decision; the check before `ensure_immutables` only avoids paying for a build
            // this one would then discard. `built` lifts both, and is read before that call so a scan
            // that builds here still meets the floor it met before.
            if (!built && text.size() < floor) {
              abandon = true;
              return false;
            }
          }
        }
        // Density gate: sticky candidate sample across the haystack (find_iter). Capture-free +
        // DFA-eligible only — see \ref il_density_milli_threshold. Checked before reverse/confirm so a
        // dense stream of successful hits still switches after K candidates.
        if constexpr (requires(State & s) {
          s.il_density_cands;
        }) {
          if (state_.il_density_origin == npos) {
            state_.il_density_origin = h;
          }
          ++state_.il_density_cands;
          // Counted BEFORE reverse/confirm, so a candidate that completes weighs the same as one that
          // fails. `run()` sees candidates that mostly fail and is right to yield; the batched filler
          // emits matches that mostly succeed, so the same count would make it yield on its own success --
          // hence `density_gate` false there, and only there. Recalibrating `run()`'s own threshold needs
          // a SECOND quantity, not a second number: the Aho-Corasick gate carries the same defect and
          // `ac_candidate_completes` is what fixed it.
          if (density_gate && state_.il_density_cands == il_density_probe_candidates
              && prog_.slot_count <= 2) {
            const std::size_t origin {state_.il_density_origin};
            const std::size_t span   {(h >= origin) ? (h - origin + 1) : 1};
            if (static_cast<std::size_t>(state_.il_density_cands) * 1000U / span >=
                il_density_milli_threshold) {
              if (prog_.immut != nullptr && prog_.immut->byte_prog.eligible) {
                abandon                     = true;
                state_.il_abandoned         = true; // sticky: dense memmem stream loses to core for this haystack
                il_density_last_abandoned() = true;
                return false;
              }
            }
          }
        }
        if (h < min_pre_start) {
          abandon             = true; // guard 2: regressing into confirmed territory -> core
          state_.il_abandoned = true; // sticky for this haystack
          return false;
        }
        std::size_t s {h}; // boundary 0 = head literal: the reverse is the identity
        if (boundary >= 1 && prog_.hints.il_rev_class >= 0) {
          // IL REVERSE-BY-CLASS: the prefix is one greedy class loop, so the match start for this candidate
          // is where the class run ending at `h` begins — a backward walk, no automaton and no per-regex
          // cache, which is what lets a storage without immutables take this route at all. `+` needs at
          // least one member, so a run of length zero yields no candidate here. Membership is tested
          // against the class directly rather than through `class_table`/`cp_page`: those cache ONE class
          // in the VM state, and the confirm that follows every candidate uses the pattern's other classes,
          // so routing through them would refill a 256-byte table per candidate.
          s = h;
          if (!prog_.hints.il_rev_is_cp) {
            const char_class& cc {prog_.classes[static_cast<std::size_t>(prog_.hints.il_rev_class)]};
            while (s > min_match_start && cc.test(static_cast<std::uint8_t>(text[s - 1]))) {
              --s;
            }
          }
          else {
            const cp_class& cc {prog_.cp_classes[static_cast<std::size_t>(prog_.hints.il_rev_class)]};
            while (s > min_match_start) {
              const std::size_t               w  {detail::codepoint_retreat(text, s, min_match_start)};
              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, s - w)};
              if (!dc.valid || dc.length != w || !cp_class_holds(cc, dc.cp)) {
                break;
              }
              s -= w;
            }
          }
          if (s == h) {
            s = npos; // no member immediately before the literal: this candidate has no start
          }
        }
        // Ordered after the class-loop reverse deliberately: putting this test first charges the shapes
        // that reach NEITHER branch, since they then pay the extra test ahead of their own. This order
        // leaves them where they were.
        else if (boundary >= 1 && prog_.hints.il_cp_shape_eligible) {
          // FIXED CODE-POINT SHAPE: no loop anywhere, so the start is exactly `il_cp_prefix_cps` code
          // points before the literal — arithmetic on UTF-8 boundaries, not a reverse pass. The forward
          // walk below is what decides; this only proposes a boundary.
          for (std::uint8_t k {0}; k < prog_.hints.il_cp_prefix_cps && s != npos; ++k) {
            if (s <= min_match_start) {
              s = npos; // the shape does not fit between the floor and this candidate
              break;
            }
            s -= detail::codepoint_retreat(text, s, min_match_start);
          }
        }
        else if (boundary >= 1) {
          // The prefix's byte program lives in the per-regex immutables — built once (call_once, already done
          // by the first-candidate guard above), not per find_iter; the expensive klass_cp expansion is what a
          // small-input regex must not pay repeatedly.
          if (prog_.immut == nullptr || !prog_.immut->il_prefix_prog.eligible) {
            abandon = true; // no per-regex cache, or the prefix is not byte-DFA-eligible — let the core VM handle it
            return false;
          }
          // Shared IL-prefix reverse under slot.mu (warmed once per regex via epoch).
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
          detail::prof::tick_prefilter_rejected();
          pos = h + 1; // the prefix reaches no start within [min_match_start, h] -> next candidate
        }
        else if (prog_.hints.il_fwd_class >= 0) {
          // TWO-RUN CONFIRM: the whole pattern is `class+ <literal> class+` (hints.il_fwd_class), and the
          // backward walk above already proved the prefix run reaches `s`. What remains is the suffix run,
          // so the match end is where that run stops — no match engine, no DFA, no one-pass table, which is
          // what a storage with no per-regex cache could not otherwise reach. `+` needs one member, so a
          // suffix run of length zero is no match at this candidate.
          const std::size_t lit_end {h + prog_.hints.inner_literal_len};
          std::size_t       e       {lit_end};
          if (!prog_.hints.il_fwd_is_cp) {
            const char_class& cc {prog_.classes[static_cast<std::size_t>(prog_.hints.il_fwd_class)]};
            while (e < text.size() && cc.test(static_cast<std::uint8_t>(text[e]))) {
              ++e;
            }
          }
          else {
            const cp_class& cc {prog_.cp_classes[static_cast<std::size_t>(prog_.hints.il_fwd_class)]};
            while (e < text.size()) {
              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, e)};
              if (!dc.valid || !cp_class_holds(cc, dc.cp)) {
                break;
              }
              e += dc.length;
            }
          }
          if (e > lit_end) {
            out_slots.assign(prog_.slot_count, npos);
            out_slots[0] = s;
            out_slots[1] = e;
            fill_two_run_saves(s, h, lit_end, e, out_slots);
            return true;
          }
          // No suffix member: this candidate cannot match. min_pre_start is not advanced -- the walk reports
          // where it stopped, but that is one class run, a hard-bounded check per candidate rather than the
          // reverse/forward-DFA cost the linearity guard exists to bound (same argument as the fused verify).
          pos = h + 1;
        }
        else if (prog_.hints.il_cp_shape_eligible) {
          // One linear walk verifies every atom of the fixed shape from the start the step-back proposed,
          // and fills every capture on the way — no engine, and no separate capture pass.
          out_slots.assign(prog_.slot_count, npos);
          const std::size_t match_end {match_cp_shape(text, s, out_slots)};
          if (match_end != npos) {
            out_slots[0] = s;
            out_slots[1] = match_end;
            return true;
          }
          // Same argument as the fused verify: the walk is a hard-bounded per-candidate check, not the
          // reverse/forward-DFA cost the linearity guard exists to bound, so min_pre_start is not advanced.
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
    using list_type = std::remove_reference_t<decltype(std::declval<State&>().list_a)>;

  public:

    //! \brief Below this input length the lazy-DFA routing is skipped (the two-pass setup does not amortise
    //!        on a short subject — the Pike VM goes direct). A measured, documented threshold.
    //!
    //!        PUBLIC because \ref real::basic_match_iterator reads it when deciding whether to batch this
    //!        route: below this length the route is not taken, so its filler could only fail once per
    //!        match. Paired with \ref lazy_dfa_is_the_route, which is public for the same reason.
    static constexpr std::size_t lazy_dfa_min_input {512};

  private:

    /*!
     * \brief Density-gate sample size and threshold (inner-literal → core/DFA when candidate density is high).
     *
     * Candidate density is what decides: below the crossover the inner literal skips most of the subject,
     * above it every candidate is a failed confirm and the core scan wins by a margin that grows with the
     * density. The threshold sits just past the crossover. Threshold 60/1000
     * (dens 0.06) sits conservatively above crossover so sparse IL wins (dens ≪ 0.01) stay on IL. Capture-free
     * only (\c slot_count ≤ 2): with groups, IL still beat forced DFA on dense (measured). Probe after K candidates
     * across the haystack (sticky on \ref pike_state::il_density_cands).
     *
     * \note **The threshold is calibrated against ONE alternative, and the crossover moves with which
     *       route the gate is arbitrating against.** It was measured on `(?:\w+)_(?:\w+)`, whose
     *       fallback is the DFA. `[0-9]{4}-[0-9]{2}-[0-9]{2}` falls back to \ref
     *       pattern_hints::fixed_shape instead, which is far cheaper -- and on a date-dense corpus
     *       that route is modestly faster than the inner-literal one
     *       while the gate never fires, because `-` at ~32 candidates per 1000 bytes sits under the
     *       60 calibrated for the other shape. On a sparse corpus the two are equal, so the gate is
     *       not wrong in general -- its single threshold is.
     *
     *       This is the same defect the Aho-Corasick gate had before 2026.8.0: one number where the
     *       crossover depends on what is being compared against. The AC fix keyed on a PRODUCT once
     *       the second variable was identified; the analogous variable here is the fallback route's
     *       cost, not the branch count.
     *
     *       Worth chasing because it sits on the engine's worst published row against the backtracking
     *       references. This does not close that gap, but it is the part of it that is understood.
     */
    static constexpr std::uint32_t il_density_probe_candidates {8};
    static constexpr std::size_t   il_density_milli_threshold  {60}; //!< Candidate density, in candidates per 1000 bytes, at or above which the IL route yields to the DFA.

    /*!
     * \brief AC routing: sample window, and the candidate-work product at or above which the
     *        automaton beats the memchr cascade.
     *
     * The branch COUNT cannot decide this and \ref ac_branch_threshold never could: the automaton scans at
     * a flat rate whatever the subject, while the cascade it replaces spans two orders of magnitude on the
     * SAME pattern and the same subject length. Only the haystack decides. What the haystack has to supply
     * is candidate DENSITY, and `benchmarks/ac_regime.cpp` measures where that crosses over — including
     * the part the reconnaissance did not predict, that the crossover MOVES with branch count, because the
     * cascade tries branches in order while the automaton does not: more branches, and the cascade starts
     * losing at a lower density.
     *
     * So the rule is a PRODUCT, not a density: `(candidates per 1000 bytes) * branch_count`. That product
     * is what stays roughly invariant across branch counts, and it is what this threshold is expressed in.
     *
     * **THIS QUANTITY CANNOT DECIDE ALONE, and the gate no longer asks it to.** Candidate density
     * counts positions where a branch HEAD occurs and cannot tell a false start from a completed
     * match, and those two pull in OPPOSITE directions: a false start punishes the cascade (verify,
     * reject, resume) and leaves the automaton indifferent, while a match rewards the cascade (it
     * stops there) and costs the automaton a per-match return. One number was arbitrating two forces
     * that oppose each other -- the same argument the branch COUNT lost, now applying to what replaced
     * it. A counter-example in the wild: a nine-branch alternation over ordinary prose runs about twice
     * as slow on the automaton as on the cascade -- on the side of the threshold that is supposed to be
     * a win.
     *
     * The gate therefore samples a SECOND quantity beside this one and takes the automaton only when
     * BOTH agree -- see \ref ac_completion_pct , which carries the sweep that measures it, the
     * derivation of its constant and the cost of asking. The two constants here were NOT retuned when
     * that landed: retuning them against that sweep's tables would have moved the error rather than
     * removed it.
     *
     * **The constant is the measured MINIMUM (588), not a mid-point, and that choice is a
     * consequence rather than a taste.** Today every alternation past \ref ac_branch_threshold takes
     * AC unconditionally, so switching too EARLY can never be worse than the behaviour being
     * replaced, while switching too LATE forfeits a win that exists today. Rounding below the
     * earliest crossover on either platform therefore cannot regress any subject, and the platforms'
     * 1.7x disagreement about the constant stops being a tuning argument. On arm the product is not
     * one level but two, with a step between the lower and upper branch counts -- a real discontinuity,
     * reproducible across rounds and unexplained. It does not affect the choice, since the minimum is on
     * the other platform either way.
     */
    static constexpr std::size_t   ac_density_sample_bytes       {256};
    static constexpr std::size_t   ac_density_min_span           {64};   //!< Shortest span an early verdict may rest on.
    static constexpr std::size_t   ac_density_work_threshold     {550};  //!< Product `(candidates per 1000 bytes) * branch_count` at or above which the automaton wins, at or above \ref ac_branch_threshold branches.
    static constexpr std::uint16_t ac_branch_floor               {4};    //!< Fewest branches the automaton is ever considered for; below this nothing is measured.
    /*!
     * \brief Percentage of sampled candidates that may COMPLETE a branch and still leave the automaton
     *        ahead. Above it the cascade wins whatever the candidate density says.
     *
     * The second quantity the gate needed, and the reason it needed one is measured rather than argued
     * (`benchmarks/ac_regime.cpp`'s third sweep): candidate density counts positions where a branch HEAD
     * occurs and cannot tell a false start from a match, yet those pull in OPPOSITE directions. A false
     * start punishes the cascade -- verify, reject, resume -- and leaves the automaton indifferent; a
     * match REWARDS the cascade, which stops there, and charges the automaton a per-match return. Holding
     * candidate density fixed and varying ONLY the completed fraction, the verdict flips from one end of
     * that sweep to the other -- which is the proof that a single number could not have been arbitrating
     * both.
     *
     * The two ISAs place the balance point differently, and the constant takes the CONSERVATIVE one: below
     * the true crossover on either, so it can decline where the automaton
     * would still have won but never take it where the cascade wins. That is the same safety direction
     * \ref ac_density_work_threshold_low argues for and for the same reason -- below
     * \ref ac_branch_threshold the automaton was historically never taken, so switching early regresses
     * what ships while switching late only forfeits.
     */
    static constexpr std::size_t   ac_completion_pct             {15};

    static constexpr std::size_t   ac_density_work_threshold_low {1400}; //!< The same product for \ref ac_branch_floor .. \ref ac_branch_threshold branches, where the safe direction is reversed.

    // A RELATION, not a value. No test reacts to a 4x change in
    // a constant; for a measured threshold the answer to that is a test, but for a relation between
    // constants a test merely samples where an assertion covers every build. The window clamp is
    // nonsense if its floor exceeds its cap, and nothing said so until now.
    static_assert(ac_density_min_span <= ac_density_sample_bytes,
                  "the sample window's floor must not exceed its cap");

    /*!
     * \brief Does a branch of the alternation COMPLETE at \p at?
     *
     * The gate's second quantity needs to tell a false start from a match, and a candidate is only a head
     * byte until something is tried at it. This walks the split chain in source order and asks
     * \ref match_byte_klass_run for each branch, exactly as `run_alternation` and
     * \ref fill_alternation_spans do -- one attempt per branch, no thread lists, no capture work.
     *
     * A COPY of that walk rather than a call into one, for the reason \ref fill_alternation_spans states
     * about its own: both routes are measured and working, and relocating a hot body to share it risks a
     * regression there that would cost more than this gate can win. Twelve lines, and the three copies
     * agree by construction because they ask the same primitive in the same order.
     *
     * \param[in] text The subject.
     * \param[in] at   A candidate position (a branch head byte occurs there).
     * \return True when some branch matches at \p at.
     */
    [[nodiscard]] bool ac_candidate_completes(std::string_view text,
                                              std::size_t      at)
    {
      const auto& code {prog_.code};
      std::size_t pc   {prog_.hints.body_pc == 0 ? std::size_t {1}
                                               : static_cast<std::size_t>(prog_.hints.body_pc)};
      while (true) {
        const bool        is_split  {code[pc].op == opcode::split};
        const std::size_t branch    {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};
        const std::size_t match_end {match_byte_klass_run(text, branch, at)};
        if (match_end != npos && wb_boundaries_ok(at, match_end)) {
          return true;
        }
        if (!is_split) {
          return false;
        }
        pc = static_cast<std::size_t>(code[pc].secondary_target);
      }
    }

    /*!
     * \brief Decides ONCE PER HAYSTACK whether the Aho-Corasick automaton should take this
     *        alternation's searches, by sampling candidate density at the search start.
     *
     * Sticky, for the same reason \ref pike_state::il_abandoned is: `find_iter` re-enters `search()`
     * once per match, and on a match-dense subject each of those searches ends almost immediately,
     * so a decision re-derived per search would never see the density that makes the automaton win
     * -- and would pay for the sample every time. Keyed on the subject's data pointer, like every
     * other per-haystack guard in this file.
     *
     * Sampling rather than an abandon predicate threaded through \ref fast_search -- the two designs
     * measure the same quantity, but the abandon predicate has to cross \ref fast_search, which has
     * four call sites across the fixed-shape, class-loop and alternation routes, and the SIMD
     * `small_set` block loop besides. This one touches nothing any other route executes. It reuses
     * \ref next_candidate, so "candidate" means exactly what it means to the cascade being measured
     * -- a second definition would be a second thing to keep true.
     *
     * \param[in] text  Subject.
     * \param[in] start Where this search begins; the window is measured from here.
     * \return `true` when the automaton should take over. Storages without the guard fields (the
     *         compile-time scratch) answer `true` unconditionally, preserving their behaviour.
     */
    template <typename Dummy = void>
    [[nodiscard]] bool ac_density_favours_automaton(std::string_view text,
                                                    std::size_t      start)
    {
      if constexpr (requires(State & s) {
        s.ac_text;
      }) {
        if (ac_density_gate_disabled()) {
          return true; // seam: route on branch count alone, the pre-gate behaviour
        }
        if (state_.ac_text != static_cast<const void*>(text.data())) {
          state_.ac_text    = static_cast<const void*>(text.data());
          state_.ac_decided = false;
        }
        if (state_.ac_decided) {
          ac_density_last_verdict() = state_.ac_dense ? ac_verdict::automaton : ac_verdict::cascade;
          return state_.ac_dense;
        }
        const std::size_t branches {static_cast<std::size_t>(prog_.hints.alternation_branch_count)};
        // WHICH threshold: the safe direction depends on what this route did before. At or above
        // ac_branch_threshold the automaton was taken UNCONDITIONALLY, so switching early cannot be
        // worse than the behaviour being replaced and the constant is the measured MINIMUM. Below
        // it the automaton was taken NEVER, so switching early is a regression against what ships
        // today, and the constant there is the measured MAXIMUM. Same rule, opposite tail, because
        // the risk is not symmetric between the two regions.
        const std::size_t want {branches >= ac_branch_threshold ? ac_density_work_threshold
                                                                : ac_density_work_threshold_low};
        // The window is sized by what the verdict needs, not by one constant for every shape.
        // Deciding on ~8 candidates takes `8000 * branches / want` bytes at threshold density: ~350
        // for a 24-branch alternation, ~23 for a 4-branch one, because the low-branch region demands
        // a far higher density and so reaches certainty in a fraction of the bytes. A fixed 256 made
        // every sparse SHORT alternation pay a full-window scan it could not need -- a charge on subjects
        // that stay on the cascade, which is the common case and therefore the one to protect.
        const std::size_t needed             {8000U * branches / (want == 0 ? std::size_t {1} : want)};
        const std::size_t span_cap           {std::clamp(needed, ac_density_min_span, ac_density_sample_bytes)};
        const std::size_t limit              {text.size() < start + span_cap ? text.size() : start + span_cap};
        std::size_t       cands              {0};
        std::size_t       completed          {0};
        bool              completion_decided {false};
        std::size_t       pos                {start};
        std::size_t       scanned            {limit > start ? limit - start : std::size_t {1}};
        // A TRUNCATED view, not the whole subject: next_candidate scans until it finds a candidate
        // or runs out of text, so bounding only what gets counted bounds nothing at all. On a
        // candidate-free haystack the "256-byte sample" read the WHOLE subject at a real cost -- two
        // thirds of the win this gate exists to deliver, spent finding out there was nothing to find.
        const std::string_view window {text.substr(0, limit)};
        while (pos < limit) {
          pos = next_candidate(window, pos, start);
          if (pos >= limit) {
            break;
          }
          ++cands;
          // THE SECOND QUANTITY, and its cost is asymmetric BY DESIGN. Verifying a candidate costs one
          // walk of the split chain -- what the cascade pays there anyway -- and the expensive direction
          // is verifying many of them, which only happens when few complete: exactly the regime where the
          // automaton then wins and amortises it. The direction that must stay cheap is the one that ENDS
          // on the cascade, and it does: two completions are enough to exceed the threshold on any sample
          // this window can hold, so a matching subject bails out after two walks.
          if (ac_candidate_completes(window, pos)) {
            ++completed;
            if (completed * 100U > cands * ac_completion_pct + 100U) {
              // The completed fraction is already past the threshold and more candidates can only be
              // read as more evidence for the cascade. Decline now, before paying for the rest.
              scanned            = pos > start ? pos - start : std::size_t {1};
              completion_decided = true;
              break;
            }
          }
          ++pos;
          // Stop as soon as the DENSITY verdict cannot change. The sample is not free -- next_candidate on
          // the first-bytes bitmap tests a byte at a time -- and a dense haystack reaches certainty
          // in a fraction of the window, which is exactly the case that must not pay for it.
          const std::size_t seen {pos > start ? pos - start : std::size_t {1}};
          if (seen >= ac_density_min_span && cands * 1000U * branches / seen >= want) {
            scanned = seen;
            break;
          }
        }
        const std::size_t span {scanned};
        // (candidates per 1000 bytes) * branch_count, in one expression so the division rounds once.
        const std::size_t work {cands * 1000U * branches / span};
        // BOTH quantities must favour the automaton. Density alone provably cannot decide (see
        // ac_completion_pct), so a dense sample whose candidates keep completing stays on the cascade.
        const bool        completion_ok {!completion_decided
                                         && completed * 100U <= cands * ac_completion_pct};
        state_.ac_dense           = work >= want && completion_ok;
        state_.ac_decided         = true;
        ac_density_last_verdict() = state_.ac_dense ? ac_verdict::automaton : ac_verdict::cascade;
        return state_.ac_dense;
      }
      else {
        return true;
      }
    }

    /*!
     * \brief Build (or rebuild) the per-regex immutables, race-free: the Tier-A byte-program the DFAs
     *        run over (and its shared alphabet), plus the one-pass extractor. Invalidation is by
     *        program identity (\ref regex_immutables::built_for == \c prog_.code.data()) — same pattern
     *        as \c state_type::dfa_program / \c il_prefix_for. Hot path is one atomic load; a spent
     *        \c once_flag would never rebuild after assign-onto-warmed (silent 0 matches). The
     *        extractor is Tier-B (assertions kept), so one table serves the search window and anchored
     *        match/fullmatch. Needs no DFA, so the anchored path can call this without the DFA build.
     */
    void ensure_immutables()
    {
      detail::regex_immutables* const immut {prog_.immut};
      if (immut == nullptr) {
        return; // no per-regex cache (not the dynamic storage) — the caller keeps the VM
      }
      const void* const want {prog_.code.data()};
      // Hot path: cache already matches this program (one acquire load; no mutex).
      if (immut->built_for.load(std::memory_order_acquire) == want) {
        return;
      }
      // Rebuild under a striped lock (not slot.mu / map_mu — reset_shared_dfas re-locks those).
      const std::lock_guard<std::mutex> lock {detail::immut_build_mu(immut)};
      if (immut->built_for.load(std::memory_order_relaxed) == want) {
        return; // double-check
      }
      // Destroy the old extractor BEFORE the program it spans is replaced: onepass keeps `code_` /
      // `classes_` as spans over its byte_program, so reassigning byte_prog first would leave the still-live
      // op_table pointing at a freed buffer. Nothing dereferences it in that window today; closing the
      // window costs one statement and removes the need to know that.
      immut->op_table.reset();
      immut->op_table_for.store(nullptr, std::memory_order_relaxed); // the extractor is gone with it
      immut->byte_prog = build_byte_program(prog_);                  // Tier-A: ineligible if assert/lookaround
      if (immut->byte_prog.eligible) {
        immut->alphabet =
          compute_lazy_alphabet(immut->byte_prog.code, immut->byte_prog.classes); // shared by both DFAs
      }
      else {
        immut->alphabet = {};
      }
      immut->il_prefix_prog  = {};
      immut->il_min_haystack = 0;
      if (!prog_.prefix_code.empty()) { // IL: expand the inner-literal prefix once per program
        program_view pv {};
        pv.code               = prog_.prefix_code;
        pv.classes            = prog_.prefix_classes;
        pv.cp_classes         = prog_.prefix_cp_classes;
        pv.cp_ranges          = prog_.prefix_cp_ranges;
        pv.unicode_word       = prog_.unicode_word;
        immut->il_prefix_prog = build_byte_program(pv);
        // Cold first-scan floor only (see run_inner_literal + shared_dfa_slot::il_warmed).
        // Reverse DFA lives in the shared slot (not per-iterator). Build cost still needs a
        // high cold floor; warm scans use il_warm_floor (~4 KB). N = size * 28, clamped
        // [64 KB, 512 KB]: email ~3436 instr → ~94 KB cold; date ~1031 → 64 KB clamp.
        // HONESTY: a cold-dense subject just above the floor may pay slightly against core. Checked ONLY
        // after the first memmem hit, so no-match — memmem-only — is never gated.
        const std::size_t sz {immut->il_prefix_prog.code.size()};
        immut->il_min_haystack =
          std::min<std::size_t>(512UL * 1024, std::max<std::size_t>(64UL * 1024, sz * 28));
      }
      // Same immut address, new program: drop previous pattern's shared DFAs (not just address-reuse).
      reset_shared_dfas(immut);
      immut->built_for.store(want, std::memory_order_release);
    }

    /*!
     * \brief Build (or rebuild) the one-pass capture extractor, on top of \ref ensure_immutables.
     *
     * Split out of \ref ensure_immutables because it is the expensive half and only some callers need it.
     * On a first search over a capture pattern this half dominates the cache -- more than the byte program
     * and the lazy DFA together. Bundled, every route that wanted only the byte program paid all of it,
     * including a capture-free pattern: a 2-slot twin with nothing to extract measured the same first
     * search as its 6-slot original. So the split is not a micro-optimisation: it stops a search from
     * building a capture extractor it cannot consult.
     *
     * Guarded by its own \ref regex_immutables::op_table_for, exactly as the membership rows are guarded by
     * \c rows_for and for the same reason -- an identity independent of \c built_for, because this is needed
     * by a different subset of routes. \ref ensure_immutables clears both the extractor and this flag when
     * it rebuilds, so a reassigned regex cannot read one built for the previous program.
     */
    void ensure_op_table()
    {
      ensure_immutables();
      detail::regex_immutables* const immut {prog_.immut};
      if (immut == nullptr) {
        return;
      }
      const void* const want {prog_.code.data()};
      if (immut->op_table_for.load(std::memory_order_acquire) == want) {
        return; // hot path: one acquire load, no mutex
      }
      const std::lock_guard<std::mutex> lock {detail::immut_build_mu(immut)};
      if (immut->op_table_for.load(std::memory_order_relaxed) == want) {
        return; // double-check
      }
      // Tier-B differs from Tier-A ONLY at `assert_position`: build_byte_program reads keep_assertions
      // nowhere else, and every other ineligibility (assert_lookaround, a Tier 1 possessive loop) is
      // decided identically either way. So a program with no `assert_position` yields a byte-for-byte
      // identical expansion, and rebuilding it means expanding every Unicode class's UTF-8 trie a second
      // time -- measured, that was 2 of the 5 trie builds per regex, plus a second full
      // compute_lazy_alphabet over the same input.
      //
      // Reusing immut->byte_prog is also the sounder lifetime: onepass keeps `code_` / `classes_` as spans
      // over the program it was built from, and the Tier-B local dies at the end of this block. Those spans
      // are read only by the constructor (build / minimize / build_edges / follow_jumps, all private), and
      // `extract` touches the node table alone -- so the Tier-B spans dangle without ever being
      // dereferenced. Latent, not live, and stated here because the next reader of `code_` would make it
      // live.
      if (std::any_of(prog_.code.begin(), prog_.code.end(),
                      [](const instr& in) { return in.op == opcode::assert_position; })) {
        const byte_program tier_b {build_byte_program(prog_, /*keep_assertions=*/ true)};
        if (tier_b.eligible) {
          immut->op_table.emplace(tier_b); // one-pass extractor: Tier-A window + Tier-B anchored
        }
      }
      else if (immut->byte_prog.eligible) {
        immut->op_table.emplace(immut->byte_prog);
      }
      immut->op_table_for.store(want, std::memory_order_release);
    }

    /*!
     * \brief Warm shared search DFAs for \p immut into \p slot (caller holds \p slot.mu).
     * \param[in]     immut Per-regex immutables naming the program to build for.
     * \param[in,out] slot  Process-wide DFA slot to populate.
     */
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

    /*!
     * \brief Warm shared IL-prefix reverse DFA (caller holds \p slot.mu).
     * \param[in]     immut Per-regex immutables naming the program to build for.
     * \param[in,out] slot  Process-wide DFA slot to populate.
     */
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

    /*!
     * \brief Run \p fn with the shared search DFAs under the slot lock.
     * \param[in] fn Callable taking `(lazy_dfa& fwd, reverse_dfa& rev)`.
     * \return True when \p fn ran; false when the route must stay on the Pike VM (no immut / ineligible).
     */
    template <typename Fn>
    bool with_search_dfas(Fn&& fn)
    {
      detail::regex_immutables* const immut {prog_.immut};
      if (immut == nullptr) {
        return false; // no cache → no DFA route, same contract as the per-regex design
      }
      // ensure_op_table, not ensure_immutables: \p fn is try_shared_lazy_dfa_search, whose confirm steps
      // DO extract through op_table. It must be built BEFORE slot.mu is taken -- ensure_op_table locks
      // immut_build_mu, and reset_shared_dfas walks immut_build_mu -> map_mu/slot.mu, so building it inside
      // the lambda would invert that order.
      // The extractor is built ONLY when there is something to extract. `fn`'s confirm step fills
      // out_slots through op_table, but a 2-slot program has nothing but the span the DFA already
      // found -- and when the table is absent the confirm falls to run_general, which is the same
      // path it takes whenever the extractor declines. Building it anyway cost 3330 allocations and
      // 4.57 MB on a first `\w+@\w+` search over 8 KB, for a table that could not be consulted.
      if (prog_.slot_count > 2) {
        ensure_op_table();
      }
      else {
        ensure_immutables(); // the DFAs still need the byte program and the shared alphabet
      }
      shared_dfa_slot&                  slot {shared_dfa_for(immut)};
      const std::lock_guard<std::mutex> lock {slot.mu};
      ensure_slot_search_dfas_unlocked(*immut, slot);
      if (!slot.fwd.has_value() || !slot.rev.has_value() || !slot.fwd->eligible()) {
        return false;
      }
      // With per-iterator caches, thrashing re-armed on each new iterator. On a shared slot a sticky thrash
      // flag would permanently decline the DFA route for every later search on this regex — re-arm
      // per logical entry. Callers that walk many candidates (A2) still call begin_scan once more
      // for a single thrash window across that loop; a double-reset here is harmless.
      slot.fwd->begin_scan();
      std::forward<Fn>(fn)(*slot.fwd, *slot.rev);
      return true;
    }

    /*!
     * \brief Lazy-DFA search route on the shared confirm DFAs. \c noinline so its body cannot
     *        inflate \ref run (x86 class-loop codegen neighbor — same shape as \ref ac_ready).
     * \param[in]  text      Subject.
     * \param[in]  start     Byte offset to begin at.
     * \param[in]  mode      Anchoring: full, prefix or search.
     * \param[out] out_slots Capture slots, filled on a match.
     * \return matched / no-match when the route handled the search; empty when the caller must fall to Pike.
     */
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
    //!        Measured against the wired engine (through the route's own disable toggle) on two corpus
    //!        shapes — mostly-non-matching prose and majority-matching text. Just below this count the
    //!        automaton already wins on the match-heavy corpus while still LOSING on prose; at this count
    //!        it wins on both
    //!        with no measured regression. 12, not 11, so the gate matches its own contract — AC
    //!        BEATS the VM-branch path at the threshold, not roughly ties it (measured; the
    //!        the N=10 repro stays correctly below threshold either way, AC/VM=1.16x
    //!        there against the standalone POC — inside the closed-gap target of <=~1.5x).
    static constexpr std::uint16_t ac_branch_threshold {12};

    // These two relations live here rather than beside their siblings above: a static_assert at
    // class scope is evaluated in declaration order, and ac_branch_threshold is declared far below
    // them, so placing them earlier fails to compile and says so obscurely.
    static_assert(ac_branch_floor <= ac_branch_threshold,
                  "the low-branch region is empty unless the floor sits under the threshold");
    // This one IS the safety argument in one line. At or above ac_branch_threshold the automaton was
    // taken UNCONDITIONALLY, so switching early cannot regress what it replaced and the constant is
    // the measured MINIMUM; below it the automaton was taken NEVER, so switching early DOES regress
    // and the constant is the measured MAXIMUM. Swap the two and both halves become unsound while
    // every test still passes -- each region would simply be routing on the other's number.
    static_assert(ac_density_work_threshold_low >= ac_density_work_threshold,
                  "below ac_branch_threshold the automaton was never taken, so that region must be "
                  "the MORE conservative of the two");

    /*!
     * \brief Build (or rebuild, on a program change) this iterator's Aho-Corasick automaton for a
     *        `fixed_alternation` program whose branch count has reached \ref ac_branch_threshold.
     *
     * \note **Cached per REGEX, in \ref detail::regex_immutables, not per state.** It lived on the
     *       state until that was measured: a state is fresh per `search()`, so crossing
     *       \ref ac_branch_threshold rebuilt the automaton on every call and made repeated search ~200x
     *       SLOWER rather than faster — orders of magnitude, with hundreds of heap allocations, against a
     *       3-branch alternation below the gate on the same subject, and `find_iter` offered no rescue.
     *       Moving it here removed the rebuild and the allocations entirely. Its identity atomic is
     *       its own, never folded into `built_for`: only this route consults it, and that cache's history
     *       records what bundling a route-specific product into the shared flag cost every other route.
     *
     *       The per-state build is KEPT as the fallback for a null `prog_.immut` — the compile-time
     *       storage and the meta-seam harness — and that is load-bearing rather than tidy. Declining the
     *       route instead would leave `tests/engine/test_fastpath_seam_matrix.cpp`'s
     *       `seam_run_aho_corasick` agreeing with itself on the general-VM leg and exercising nothing:
     *       a green differential testing neither side, which is the failure this engine spent
     *       v2026.7.62 removing.
     *
     * \warning **Removing the rebuild exposed what the automaton actually costs, and the gate above
     *          selects on the wrong property.** The automaton scans at a flat rate whatever the subject: it is
     *          worst-case insurance, not a fast path. Measured against the same pattern with the route
     *          disabled, on four subjects of one size — with NO match it is two orders of magnitude slower,
     *          one late match 12.35 against 0.25 (49x slower), match-dense 0.05 against 0.05 (a tie),
     *          and a subject full of false starts **12.66 against 132.86 (10.5x FASTER)**. AC wins only
     *          where the memchr cascade degrades, and \ref ac_branch_threshold gates on branch COUNT,
     *          which does not predict that regime. Selecting on candidate density is the shape that
     *          would, and it is a routing-policy change with its own measurement campaign — not a
     *          tuning, and deliberately not attempted here.
     *
     *          **Reconnaissance for whoever builds it, so the shape is not re-derived.** No *static*
     *          property can select correctly: AC's cost is flat while the cascade's swings by three
     *          orders of magnitude on the same pattern, so only the SUBJECT decides, and only at run
     *          time. The engine already has the right shape for that and it is not a threshold — the
     *          inner-literal route starts on `memmem` and ABANDONS mid-scan when candidate density
     *          betrays a bad haystack (\ref pike_state::il_density_cands, \ref pike_state::il_abandoned,
     *          sticky per subject, pinned by `tests/engine/test_il_density_gate.cpp`). Adapting that
     *          would delete \ref ac_branch_threshold rather than retune it, which is the point: a
     *          threshold that gets adjusted is a threshold that will be adjusted again.
     *
     *          The obstacle is where the counter has to live. Alternation search has three scan paths,
     *          and the false-start regime measured above takes **none** of the obvious one: with a
     *          shared prefix the branches collapse to `single_first`, with 24 distinct heads to the
     *          `first_bytes` bitmap — both inside \ref fast_search — and only 2..8 distinct heads reach
     *          the L-SIMD `small_set` block loop. \ref fast_search verifies candidates through a
     *          callback that cannot return from its caller, and it has four call sites across the
     *          fixed-shape, class-loop and alternation routes. So the first step is to give it an
     *          OPTIONAL abandon predicate, unwired by default, leaving the other three routes unchanged
     *          by construction; then wire the counter to alternation alone, with a budget scaled to the
     *          subject (the automaton's flat per-byte rate against the cost of a missed candidate in the
     *          bad regime, which sets the
     *          order of magnitude); then re-run the full matrix on both platforms, since it is the
     *          matrix that has to validate the result.
     *
     * \tparam Dummy Never named by a caller. A member TEMPLATE is instantiated only where it is actually
     *               called, and this one has a single `if constexpr`-gated call site — so the copies for
     *               `pike_vm` instantiations that never take the route are not emitted at all, instead of
     *               being emitted and counted as wholly uncovered.
     * \return The automaton to scan with, or `nullptr` when this program has none — either it is not a
     *         fixed alternation past the threshold, or the build declined a pathological icase-fold
     *         expansion. The caller falls back to \ref run_alternation, zero behaviour change.
     *        Declines (returns `nullptr`) if any branch's icase-fold
     *        expansion would exceed \ref ac_max_branch_expansion — the caller falls back to the
     *        existing \ref run_alternation, zero behavior change. Runs once per iterator/program,
     *        off the hot path, mirroring \ref with_search_dfas's cache-by-program-pointer contract.
     *
     *        `noinline`, deliberately NOT `cold` (round-2 x86 isolation A/B: neither the
     *        pattern_hints field alone nor the pike_state size growth alone regressed the
     *        non-alternation hot-corpus witnesses, isolating the cause to code called directly
     *        from `run()`'s own dispatch chain — a codegen-neighbor/inlining-bloat effect on
     *        `run_class_loop`, which shares the same translation unit and physically returns
     *        before ever reaching this call at runtime, so it's presence, not execution, doing the
     *        damage). `cold` would additionally deprioritize this function's OWN optimization —
     *        wrong here, since it (unlike the actual construction work in aho_corasick.hpp,
     *        already marked `cold`) is called on every AC-eligible search, not just once per
     *        program. `noinline` alone keeps it fully optimized while stopping the compiler from
     *        folding its body into `run()`'s.
     */
    template <typename Dummy = void>
    [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    const ac_automaton* ac_ready()
    {
      const auto* const                 program {static_cast<const void*>(prog_.code.data())};
      detail::regex_immutables* const   immut   {prog_.immut};
      const auto                        build = [this] {
                                                  const std::size_t body_pc {
                                                    prog_.hints.body_pc == 0
                                                      ? std::size_t {1}
                                                      : static_cast<std::size_t>(prog_.hints.body_pc)};
                                                  return build_ac_automaton(prog_.code, prog_.classes, body_pc);
                                                };
      if (immut != nullptr) {
        if (immut->ac_for.load(std::memory_order_acquire) == program) {
          return immut->ac.has_value() ? &*immut->ac : nullptr;         // hot path: one acquire load, no mutex
        }
        const std::lock_guard<std::mutex> lock {detail::immut_build_mu(immut)};
        if (immut->ac_for.load(std::memory_order_relaxed) != program) { // double-check
          immut->ac = build();
          immut->ac_for.store(program, std::memory_order_release);
        }
        return immut->ac.has_value() ? &*immut->ac : nullptr;
      }
      // No per-regex cache to hold it: decline, and the caller falls back to run_alternation. Line
      // coverage is what settled that this is safe rather than the trap it looked like -- the meta-seam
      // harness reaches this function 410 times and ALWAYS with immutables, so seam_run_aho_corasick
      // keeps exercising the route; a per-state fallback here measured zero executions.
      return nullptr;
    }

    //! \brief The capture-block pool type of the bound `State` (COW) — heap-backed for dynamic,
    //!        compile-sized static_vec for static. The one capture-slot mechanism, both storages.
    using pool_type = std::remove_reference_t<decltype(std::declval<State&>().pool)>;

    /*!
     * \brief Is the state's cached row key stale for \p want?
     *
     * \c StateBoundToProgram is the whole point: when the caller guarantees this state never meets a
     * second program, a matching key is proof on its own and the program-identity compare — a pointer
     * chase through the view, per `run()`, so per MATCH on a walk — disappears entirely at compile time.
     * Without the guarantee it is still required: a state carried across regexes would otherwise answer
     * from the previous program's rows.
     * \param[in] have The key the state last verified (\c table_class or \c cp_page_class).
     * \param[in] want The key wanted now.
     * \return \c true if the row must be re-verified.
     */
    [[nodiscard]] constexpr bool row_key_stale(std::int32_t have,
                                               std::int32_t want) const
    {
      if constexpr (StateBoundToProgram) {
        return have != want;
      }
      else {
        return have != want || state_.rows_verified_for != static_cast<const void*>(prog_.code.data());
      }
    }

    /*!
     * \brief Verifies (and if needed fills) the byte row for \p class_index, then caches it in the state.
     *
     * Must stay outlined: `class_table` has to remain small enough to inline into
     * `basic_match_iterator::advance`, and this body inline is what pushes it over. Emitted out of line
     * there instead, it costs a tenth of the instructions of a class-loop walk.
     * \param[in,out] cache       The per-regex immutables.
     * \param[in]     class_index Index into the program's interned byte classes.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void verify_class_row(detail::regex_immutables& cache,
                          std::size_t               class_index)
    {
      if (cache.rows_for.load(std::memory_order_acquire) != static_cast<const void*>(prog_.code.data())) {
        ensure_membership_rows(cache);
      }
      if (!cache.row_ready(class_index)) {
        fill_class_row(cache, class_index);
      }
      state_.table_class       = static_cast<std::int32_t>(class_index);
      state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
      state_.row_ptr           = cache.class_rows.data() + (class_index * 256);
    }

    /*!
     * \brief Derives the byte row into the VM state — the constant-evaluation path, where no per-regex
     *        cache exists.
     *
     * The attribute is load-bearing, for the same reason as \ref verify_class_row and more so: this is the
     * body that holds the 256-iteration loop, so inlined back into \ref class_table it is what makes that
     * accessor too large to enter `basic_match_iterator::advance`. Splitting the function out without the
     * attribute buys nothing — the compiler simply undoes it, and `class_table` is emitted out of line at
     * 6.2 M instructions against 0.85 M inlined on a 64 KiB `[a-z]+` walk.
     * \param[in] class_index Index into the program's interned byte classes.
     * \return The state's table.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    constexpr const std::uint8_t* derive_class_table(std::size_t class_index)
    {
      if (state_.table_class != static_cast<std::int32_t>(class_index)) {
        const char_class& klass {prog_.classes[class_index]};
        for (std::size_t b {0}; b < 256; ++b) {
          state_.table[b] = klass.test(static_cast<std::uint8_t>(b)) ? 1U : 0U;
        }
        state_.table_class = static_cast<std::int32_t>(class_index);
      }
      // Runtime only, and it is what keeps \ref class_table's leading fast path sound: that path
      // answers from `row_ptr` whenever the key matches, so every path that claims the key must leave
      // `row_ptr` on the row it claimed. Reached at runtime only when neither storage kind is present;
      // under constant evaluation nothing reads `row_ptr`, and the guard keeps the pointer out of the
      // constexpr state entirely.
      if (!std::is_constant_evaluated()) {
        state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
        state_.row_ptr           = state_.table.data();
      }
      return state_.table.data();
    }

    /*!
     * \brief Sizes the per-regex membership rows for this program, if they are not already.
     *
     * Outlined and cold: it runs once per regex, behind an acquire load on the hot path.
     * \param[in,out] cache The per-regex immutables.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
    void ensure_membership_rows(detail::regex_immutables& cache) const
    {
      const std::lock_guard<std::mutex> lock {detail::immut_build_mu(&cache)};
      const void* const                 want {static_cast<const void*>(prog_.code.data())};
      if (cache.rows_for.load(std::memory_order_relaxed) == want) {
        return; // another thread sized them while we waited
      }
      cache.class_rows.assign(prog_.classes.size() * 256, 0);
      cache.cp_ascii_rows.assign(prog_.cp_classes.size() * 256, 0);
      cache.cp_page_rows.assign(prog_.cp_classes.size() * 30, 0);
      // Value-initialized, so every flag starts clear; assigning a fresh vector moves the buffer and
      // never moves an atomic.
      cache.cp_ascii_ready_at = prog_.classes.size();
      cache.cp_page_ready_at  = cache.cp_ascii_ready_at + prog_.cp_classes.size();
      const std::size_t flags {cache.cp_page_ready_at + prog_.cp_classes.size()};
      cache.row_ready_bits.store(0, std::memory_order_relaxed);
      cache.row_ready_overflow = flags > detail::regex_immutables::row_ready_bit_capacity
                                   ? std::vector<std::atomic<char>>(flags - detail::regex_immutables::row_ready_bit_capacity)
                                   : std::vector<std::atomic<char>>();
      cache.rows_for.store(want, std::memory_order_release);
    }

    /*!
     * \brief Expands \p klass into one flat 256-byte membership row of the per-regex cache, once.
     *
     * The shared body of \ref fill_class_row and \ref fill_cp_ascii_row, which differed only in which
     * `char_class` they read, which array they wrote, and the offset their ready-bit sits at. Both are
     * `noinline, cold` and reached only through the once-per-class miss path, so collapsing them costs
     * nothing at run time and stops a fix from landing on one of two copies. \ref fill_cp_page_row is
     * deliberately NOT folded in: it builds a 30-word bitmap over a code-point page from range pairs,
     * sharing only the lock-and-ready-bit frame.
     *
     * \param[in,out] cache       The per-regex immutables.
     * \param[in]     ready_index Index of this row's ready bit.
     * \param[in]     klass       The membership set to expand.
     * \param[out]    row         Destination, 256 bytes.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
    static void fill_byte_row(detail::regex_immutables& cache,
                              std::size_t               ready_index,
                              const char_class&         klass,
                              std::uint8_t*             row)
    {
      const std::lock_guard<std::mutex> lock {detail::immut_build_mu(&cache)};
      if (cache.row_ready(ready_index)) {
        return; // filled while we waited
      }
      for (std::size_t b {0}; b < 256; ++b) {
        row[b] = klass.test(static_cast<std::uint8_t>(b)) ? 1U : 0U;
      }
      cache.set_row_ready(ready_index);
    }

    /*!
     * \brief Fills one byte-class row of the per-regex cache, once.
     * \param[in,out] cache       The per-regex immutables.
     * \param[in]     class_index Index into the program's interned byte classes.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold)) // as before the factoring: see verify_class_row's own note
#endif
    void fill_class_row(detail::regex_immutables& cache,
                        std::size_t               class_index) const
    {
      fill_byte_row(cache, class_index, prog_.classes[class_index],
                    cache.class_rows.data() + (class_index * 256));
    }

    /*!
     * \brief Fills one code-point-class ASCII row of the per-regex cache, once.
     * \param[in,out] cache    The per-regex immutables.
     * \param[in]     cp_index Index into the program's code-point classes.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold)) // as before the factoring: see verify_class_row's own note
#endif
    void fill_cp_ascii_row(detail::regex_immutables& cache,
                           std::size_t               cp_index) const
    {
      fill_byte_row(cache, cache.cp_ascii_ready_at + cp_index, prog_.cp_classes[cp_index].ascii,
                    cache.cp_ascii_rows.data() + (cp_index * 256));
    }

    /*!
     * \brief Fills one U+0080..U+07FF membership bitmap of the per-regex cache, once.
     * \param[in,out] cache    The per-regex immutables.
     * \param[in]     cp_index Index into the program's code-point classes.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
    void fill_cp_page_row(detail::regex_immutables& cache,
                          std::size_t               cp_index) const
    {
      const std::lock_guard<std::mutex> lock {detail::immut_build_mu(&cache)};
      if (cache.row_ready(cache.cp_page_ready_at + cp_index)) {
        return;
      }
      std::uint64_t* const    row {cache.cp_page_rows.data() + (cp_index * 30)};
      const detail::cp_class& cc  {prog_.cp_classes[cp_index]};
      for (std::size_t w {0}; w < 30; ++w) {
        row[w] = 0;
      }
      for (std::uint32_t k {0}; k < cc.range_count; ++k) {
        const detail::code_range& r {prog_.cp_ranges[cc.range_begin + k]};
        if (r.lo > cp_page_max) {
          break; // ranges are sorted: nothing more falls in the page
        }
        const std::uint32_t lo {r.lo < 0x80U ? 0x80U : r.lo};
        const std::uint32_t hi {r.hi > cp_page_max ? cp_page_max : r.hi};
        for (std::uint32_t c {lo}; c <= hi; ++c) {
          const std::uint32_t bit {c - 0x80U};
          row[bit >> 6U] |= std::uint64_t {1} << (bit & 63U);
        }
      }
      cache.set_row_ready(cache.cp_page_ready_at + cp_index);
    }

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
     *
     * Forced inline, and the attribute is load-bearing: left to its own judgement the compiler emits this
     * out of line, where the call frame alone costs more than the whole accessor does inlined — 6.2 M
     * instructions against 0.85 M on a 64 KiB `[a-z]+` walk. It only fits once \ref derive_class_table is
     * kept out of it, which is what that function's own attribute is for.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((always_inline))
#endif
    constexpr const std::uint8_t* class_table(std::size_t class_index)
    {
      const std::int32_t key {static_cast<std::int32_t>(class_index)};
      // The row-key compare comes FIRST, ahead of both storage-mode tests. Two acquire loads here would
      // be per-`run()`, and `run()` is per MATCH on a walk — 11 327 times over 64 KiB on `[a-z]+`; the
      // state is single-threaded by construction, so it remembers which row it last verified and a walk
      // that stays on one class pays one compare. The storage-mode tests are invariant for the whole
      // walk while the key is not, so asking them first charged every match two branches that no
      // compiler can hoist out of a per-match call — and being invariant is exactly why they must be
      // asked last, not first. Every branch here answers the ONE question the caller asked; ordering is
      // the whole optimisation.
      if (!std::is_constant_evaluated() && !row_key_stale(state_.table_class, key)) {
        return state_.row_ptr;
      }
      return resolve_class_table(class_index);
    }

    /*!
     * \brief Cold half of the class-loop route: everything a `\A`/`^` or `\Z`/`$` implies.
     *
     * Outlined so the unanchored path pays exactly one branch. `\A`/`^` is a MODE (a search becomes
     * prefix anchoring, and a region beginning past 0 cannot hold the match at all); `\Z`/`$` is a
     * LIMIT, handled by \ref run_class_loop_end_anchored. Both were peeled out of the program by the
     * shape recognizers, so this is the only thing left enforcing them.
     * \tparam Cascade   Whether the memchr stop-tail applies.
     * \tparam OutSlots  Output slot container.
     * \param[in]  text      The subject.
     * \param[in]  start     Region start.
     * \param[in]  mode      Anchoring mode as the caller asked for it.
     * \param[out] out_slots Receives the span on success.
     * \return `true` on a match.
     */
    template <bool Cascade, typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    constexpr bool run_class_loop_anchored(std::string_view text,
                                           std::size_t      start,
                                           run_mode         mode,
                                           OutSlots&        out_slots)
    {
      if (prog_.hints.anchored_start && start != 0) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      if (prog_.hints.greedy_class_loop_end != 0) {
        return run_class_loop_end_anchored(text, start, mode, out_slots);
      }
      return run_class_loop<Cascade>(text, start,
                                     prog_.hints.anchored_start && mode == run_mode::search
                                       ? run_mode::prefix
                                       : mode,
                                     out_slots);
    }

    /*!
     * \brief `X+$` / `^X+$` in search mode: the run that ENDS at the anchor, found by walking back.
     *
     * A trailing `\Z`/`$` pins the end, so the leftmost match is the maximal class run that finishes
     * exactly there -- one backward walk from the limit, not a forward scan that finds runs and
     * discards each one whose end is wrong -- milliseconds on the general VM for a subject a scan crosses
     * once.
     *
     * The limit is where `$` differs from `\Z` and from `fullmatch`, and getting it wrong is silent:
     * `$` (kind 2) also matches just before ONE final newline, which is why `^a+$` matches `"aaa\n"`
     * while `fullmatch(a+)` does not. `\Z` (kind 1) is the strict end.
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject.
     * \param[in]  start     Region start; the match may not begin before it.
     * \param[in]  mode      Anchoring mode: search, prefix or full.
     * \param[out] out_slots Receives the span on success.
     * \return `true` when a run ends at the anchor.
     */
    template <typename OutSlots>
    constexpr bool run_class_loop_end_anchored(std::string_view text,
                                               std::size_t      start,
                                               run_mode         mode,
                                               OutSlots&        out_slots)
    {
      const std::uint8_t* const tbl       {
        class_table(static_cast<std::size_t>(prog_.hints.greedy_class_loop))};
      std::size_t       limit             {text.size()};
      if (prog_.hints.greedy_class_loop_end == 2 && limit > 0 && text[limit - 1] == '\n') {
        --limit; // `$`: the position before one final newline is also an end
      }
      const auto fail = [&]() {
                          out_slots.assign(prog_.slot_count, npos);
                          return false;
                        };
      if (limit <= start) {
        return fail();
      }
      std::size_t match_start {limit};
      while (match_start > start && tbl[static_cast<std::uint8_t>(text[match_start - 1])] != 0U) {
        --match_start;
      }
      if (match_start == limit) {
        return fail(); // nothing of the class immediately before the anchor
      }
      // `^X+$`: the run must also BEGIN at 0. anchored_impossible() has already refused start != 0.
      if (prog_.hints.anchored_start && match_start != 0) {
        return fail();
      }
      // ALL modes come here, not just search: the assertion has been peeled OUT of the program, so
      // whoever handles the shape is the only thing left enforcing it. A prefix (`match`) call that
      // fell through to the ordinary loop would answer `[a-z]+$` over "abc def" with "abc", which the
      // pattern forbids.
      if (mode == run_mode::prefix || mode == run_mode::full) {
        if (match_start > start) {
          return fail(); // the run ending at the anchor does not reach back to the required start
        }
        match_start = start;
        if (mode == run_mode::full && limit != text.size()) {
          return fail(); // a full match must span the WHOLE subject, final newline included
        }
      }
      if (limit - match_start < prog_.hints.greedy_class_loop_min) {
        return fail();
      }
      if (!wb_boundaries_ok(match_start, limit)) {
        return fail();
      }
      fill_span_slots(out_slots, match_start, limit);
      return true;
    }

    /*!
     * \brief Cold half of \ref class_table — the storage-mode resolution, and the only path that writes
     *        the state's row cache for a byte class.
     *
     * Outlined for the reason \ref class_table has an attribute of its own — what has to stay inlined is the
     * row-key compare and the return, and every byte of resolution beside it competes for the budget
     * that lets the accessor enter `basic_match_iterator::advance`. Inlined back in, it charged the
     * class-scan rows on one toolchain while helping the same rows on the other: the hot path was already
     * right, and the cold path's SIZE was what decided the outcome.
     * \param[in] class_index Index into the program's interned byte classes.
     * \return Pointer to the 256-entry membership row, also cached in the state.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    constexpr const std::uint8_t* resolve_class_table(std::size_t class_index)
    {
      if (!std::is_constant_evaluated() && prog_.class_tables != nullptr) {
        // Compile-time storage. Recorded in the state like every other path so the fast path above
        // answers for this storage too: the invariant this accessor rests on is that `table_class`
        // names the row `row_ptr` points at, whichever path filled it.
        state_.table_class       = static_cast<std::int32_t>(class_index);
        state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
        state_.row_ptr           = prog_.class_tables + (class_index * 256);
        return state_.row_ptr;
      }
      if (!std::is_constant_evaluated() && prog_.immut != nullptr) {
        verify_class_row(*prog_.immut, class_index);
        return state_.row_ptr;
      }
      return derive_class_table(class_index);
    }

    /*!
     * \brief Byte-indexed membership table for a `cp_class`'s ASCII bitmap — the same one-load trick
     *        as \ref class_table, for the `klass_cp` scan-loop fast path. Keyed negatively so it never
     *        collides with a `class_table` key (a whole-pattern shorthand has no byte-NFA classes, so
     *        the two never interleave for one pattern anyway).
     * \param[in] cp_index Index into the program's `cp_classes`.
     * \return Pointer to a 256-entry table: 1 where the byte (< 0x80) is a member.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((always_inline))
#endif
    constexpr const std::uint8_t* cp_ascii_table(std::size_t cp_index)
    {
      // Key first, resolution outlined — see \ref class_table, whose shape this is exactly.
      const std::int32_t key {-2 - static_cast<std::int32_t>(cp_index)};
      if (!std::is_constant_evaluated() && !row_key_stale(state_.table_class, key)) {
        return state_.row_ptr;
      }
      if (!std::is_constant_evaluated() && prog_.cp_ascii_tables != nullptr) {
        // Recorded in the state so the fast path above answers for compile-time storage too — the
        // invariant is \ref class_table's: `table_class` names the row `row_ptr` points at.
        state_.table_class       = key;
        state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
        state_.row_ptr           = prog_.cp_ascii_tables + (cp_index * 256);
        return state_.row_ptr;
      }
      if (!std::is_constant_evaluated() && prog_.immut != nullptr) {
        detail::regex_immutables& cache {*prog_.immut};
        if (cache.rows_for.load(std::memory_order_acquire) != static_cast<const void*>(prog_.code.data())) {
          ensure_membership_rows(cache);
        }
        if (!cache.row_ready(cache.cp_ascii_ready_at + cp_index)) {
          fill_cp_ascii_row(cache, cp_index);
        }
        state_.table_class       = key;
        state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
        state_.row_ptr           = cache.cp_ascii_rows.data() + (cp_index * 256);
        return state_.row_ptr;
      }
      { // constant evaluation -- see class_table
        if (state_.table_class != key) {
          const char_class& klass {prog_.cp_classes[cp_index].ascii};
          for (std::size_t b {0}; b < 256; ++b) {
            state_.table[b] = klass.test(static_cast<std::uint8_t>(b)) ? 1U : 0U;
          }
          state_.table_class = key;
        }
        // Runtime only, and only when neither storage kind is present: the fast path answers from
        // `row_ptr`, so this path must leave it on the row it just claimed.
        if (!std::is_constant_evaluated()) {
          state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
          state_.row_ptr           = state_.table.data();
        }
        return state_.table.data();
      }
    }

    /*!
     * \brief Stateless membership of \p cp in \p cc — no VM-state cache touched.
     *
     * The cached paths (\ref cp_member_page, \ref cp_member_high) hold ONE class each in the state, which
     * is right for a scan that stays on one class. The inner-literal reverse alternates with the confirm's
     * classes on every candidate, so it reads the class directly: the ASCII bitmap for `cp < 0x80` (the
     * overwhelming case), and a binary search of the class's own range span above it.
     * \param[in] cc The code-point class.
     * \param[in] cp The code point.
     * \return `true` if \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_class_holds(const cp_class& cc,
                                                char32_t        cp) const
    {
      if (cp < 0x80U) {
        return cc.ascii.test(static_cast<std::uint8_t>(cp));
      }
      std::size_t lo {cc.range_begin};
      std::size_t hi {static_cast<std::size_t>(cc.range_begin) + cc.range_count};
      while (lo < hi) {
        const std::size_t mid {lo + ((hi - lo) / 2)};
        const code_range& r   {prog_.cp_ranges[mid]};
        if (cp < r.lo) {
          hi = mid;
        }
        else if (cp > r.hi) {
          lo = mid + 1;
        }
        else {
          return true;
        }
      }
      return false;
    }

    static constexpr std::uint32_t cp_page_max {0x7FFU}; //!< Highest code point covered by the `cp_page` bitmap (the 2-byte UTF-8 range).

    //! \brief Cap on how far a jump chain is followed to a loop head (empty-iteration exit routing);
    //!        a loop join reaches its split in one hop, so this is a generous bound, never a hot cost.
    // A GENEROUS BOUND on an unreachable path, not a tuning knob: a loop join reaches its split in one
    // hop, so eight is headroom against a shape the compiler does not emit. Nothing guards its value,
    // because no pattern gets near it -- which is the intent, and a test manufacturing a nine-hop chain
    // would pin the bound rather than any behaviour the engine has.
    static constexpr int max_loop_hops {8};

    //! \brief Accepted-byte count after which a `class+` run switches from the per-byte advance to a
    //!        memchr-cascade to the next stop byte. Below it a run pays nothing extra, so a
    //!        stop-dense stream of short runs stays at baseline cost; the crossover is measured.
    static constexpr std::size_t cascade_run_threshold {32};

    /*!
     * \brief Builds (once, cached) and returns the `cp_class`'s membership bitmap over
     *        `[U+0080, U+07FF]` — a one-load replacement for the range search on the common
     *        two-byte code points (see \ref basic_pike_state::cp_page).
     * \param[in] cp_index Index into the program's `cp_classes`.
     * \return Pointer to the 30-word bitmap (bit `cp - 0x80`).
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((always_inline))
#endif
    constexpr const std::uint64_t* cp_page_table(std::size_t cp_index)
    {
      // Key first, resolution outlined — see \ref class_table, whose shape this is exactly.
      const std::int32_t key {-2 - static_cast<std::int32_t>(cp_index)};
      if (!std::is_constant_evaluated() && !row_key_stale(state_.cp_page_class, key)) {
        return state_.page_ptr;
      }
      if (!std::is_constant_evaluated() && prog_.cp_page_tables != nullptr) {
        // Recorded in the state so the fast path above answers for compile-time storage too — the
        // invariant is \ref class_table's, on this accessor's own pair of fields.
        state_.cp_page_class     = key;
        state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
        state_.page_ptr          = prog_.cp_page_tables + (cp_index * 30);
        return state_.page_ptr;
      }
      if (!std::is_constant_evaluated() && prog_.immut != nullptr) {
        detail::regex_immutables& cache {*prog_.immut};
        if (cache.rows_for.load(std::memory_order_acquire) != static_cast<const void*>(prog_.code.data())) {
          ensure_membership_rows(cache);
        }
        if (!cache.row_ready(cache.cp_page_ready_at + cp_index)) {
          fill_cp_page_row(cache, cp_index);
        }
        state_.cp_page_class     = key;
        state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
        state_.page_ptr          = cache.cp_page_rows.data() + (cp_index * 30);
        return state_.page_ptr;
      }
      { // constant evaluation -- see class_table
        if (state_.cp_page_class != key) {
          state_.cp_page.fill(0);
          const detail::cp_class& cc {prog_.cp_classes[cp_index]};
          for (std::uint32_t k {0}; k < cc.range_count; ++k) {
            const detail::code_range& r {prog_.cp_ranges[cc.range_begin + k]};
            if (r.lo > cp_page_max) {
              break;
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
        // Runtime only, and only when neither storage kind is present: the fast path answers from
        // `page_ptr`, so this path must leave it on the page it just claimed.
        if (!std::is_constant_evaluated()) {
          state_.rows_verified_for = static_cast<const void*>(prog_.code.data());
          state_.page_ptr          = state_.cp_page.data();
        }
        return state_.cp_page.data();
      }
    }

    /*!
     * \brief Cache entry for \ref cp_hi_cached (thread-local, not on \ref basic_pike_state).
     *        Keyed by a content fingerprint of the class (never a pointer into a program):
     *        programs die while this cache lives for the thread, and the allocator can recycle
     *        the same `cp_ranges` address for a *different* class — a pointer key then returns
     *        the wrong sparse table (false membership, e.g. emoji matching `[\w€]` after a prior
     *        high-range class was destroyed). Seen as a deterministic wrong-match on macos-clang
     *        CI after a long test binary has churned many classes (find_iter euro empty-alt pin).
     */
    struct cp_hi_cache_entry
    {
      std::uint64_t                key_fp {0}; //!< The class's content fingerprint; 0 marks the entry unused.
      std::unique_ptr<cp_hi_table> table;      //!< The sparse table built for that class, owned by the cache.
    };

    /*!
     * \brief Cold path: build a sparse hi table and install it in the thread-local cache.
     *        Outlined so the hot membership check never inlines the range-walk builder.
     * \param[in]     prog     Program owning the class.
     * \param[in]     cp_index Index of the code-point class in \c prog.cp_classes.
     * \param[in]     key_fp   The class's content fingerprint, the cache key.
     * \param[in,out] cache    Thread-local entries, one of which is overwritten.
     * \param[out]    last_tab Sticky last-hit table pointer, set to the built table.
     * \param[out]    last_fp  Sticky last-hit fingerprint, set to \p key_fp.
     * \return The installed table, owned by \p cache.
     */
    [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold))
#endif
    static const cp_hi_table* cp_hi_build(const program_view&               prog,
                                          std::size_t                       cp_index,
                                          std::uint64_t                     key_fp,
                                          std::array<cp_hi_cache_entry, 8>& cache,
                                          const cp_hi_table*&               last_tab,
                                          std::uint64_t&                    last_fp)
    {
      const detail::cp_class& cc       {prog.cp_classes[cp_index]};
      auto                    table    {std::make_unique<cp_hi_table>()};
      bool                    needs_hi {false};
      if (cc.range_count > 0) {
        const detail::code_range& last {
          prog.cp_ranges[static_cast<std::size_t>(cc.range_begin) + cc.range_count - 1U]};
        needs_hi = last.hi > cp_page_max;
      }
      if (needs_hi) {
        table->page_of.assign(0x1100U, cp_hi_table::empty);
        for (std::uint32_t k {0}; k < cc.range_count; ++k) {
          const detail::code_range& r {prog.cp_ranges[cc.range_begin + k]};
          if (r.hi <= cp_page_max) {
            continue;
          }
          std::uint32_t       cp  {r.lo > cp_page_max ? r.lo : (cp_page_max + 1U)};
          const std::uint32_t end {r.hi < 0x110000U ? r.hi : 0x10FFFFU};
          while (cp <= end) {
            const std::uint32_t page     {cp >> 8U};
            const std::uint32_t page_end {((page + 1U) << 8U) - 1U};
            const std::uint32_t hi       {end < page_end ? end : page_end};
            std::uint16_t&      slot     {table->page_of[page]};
            if (slot == cp_hi_table::empty) {
              slot = static_cast<std::uint16_t>(table->blocks.size());
              table->blocks.push_back({});
            }
            auto& blk {table->blocks[slot]};
            for (std::uint32_t c {cp}; c <= hi; ++c) {
              const std::uint32_t lo {c & 0xFFU};
              blk[lo >> 3U] =
                static_cast<std::uint8_t>(blk[lo >> 3U] | static_cast<std::uint8_t>(1U << (lo & 7U)));
            }
            if (hi == 0x10FFFFU) {
              break;
            }
            cp = hi + 1U;
          }
        }
      }
      // Prefer an empty slot; if the 8-entry cache is full, evict slot 0 and drop last-hit if it
      // pointed at the table we are about to destroy (otherwise last_tab would dangle).
      cp_hi_cache_entry* slot {&cache[0]};
      for (cp_hi_cache_entry& e : cache) {
        if (!e.table) {
          slot = &e;
          break;
        }
      }
      if (slot->table && last_tab == slot->table.get()) {
        last_tab = nullptr;
        last_fp  = 0;
      }
      slot->key_fp = key_fp;
      slot->table  = std::move(table);
      return slot->table.get();
    }

    /*!
     * \brief Thread-local sparse hi tables, keyed by \ref cp_class::fingerprint (set once at intern).
     *        Hot path: load `uint64` + sticky compare (cheap, like the pre-poisoning pointer key) —
     *        never re-hash ranges per codepoint. Keeps \ref basic_pike_state sizeof unchanged.
     * \param[in] prog     Program owning the class.
     * \param[in] cp_index Index of the code-point class in \c prog.cp_classes.
     * \return The class's sparse table, built on first use for this thread.
     */
    [[nodiscard]] static const cp_hi_table* cp_hi_cached(const program_view& prog,
                                                         std::size_t         cp_index)
    {
      const detail::cp_class& cc                  {prog.cp_classes[cp_index]};
      const std::uint64_t     key_fp              {cc.fingerprint}; // interned once — O(1) load, not FNV of 675 ranges
      // One-thread sticky last hit: a tight `\p{L}+` run probes the same class millions of times.
      thread_local std::uint64_t         last_fp  {0};
      thread_local const cp_hi_table*    last_tab {nullptr};
      if (last_fp == key_fp && last_tab != nullptr) {
        return last_tab;
      }
      thread_local std::array<cp_hi_cache_entry, 8> cache {};
      for (const cp_hi_cache_entry& e : cache) {
        if (e.key_fp == key_fp && e.table) {
          last_fp  = key_fp;
          last_tab = e.table.get();
          return last_tab;
        }
      }
      const cp_hi_table* const built {cp_hi_build(prog, cp_index, key_fp, cache, last_tab, last_fp)};
      last_fp  = key_fp;
      last_tab = built;
      return built;
    }

    //! \brief Below this many total ranges, high-cp membership stays on bsearch (small scripts).
    //!        Re-measured after this value stood at 32 on the strength of a quasi-tie: at 32 the two
    //!        classes that straddle it are NOT a tie, they pull opposite ways: the one just above wants
    //!        the sparse table on both ISAs, the one just below wants bsearch. So the crossover lies
    //!        between them, and this constant is that GAP rather than either measurement -- raising it
    //!        past the upper class costs that class, lowering it past the lower one costs the other.
    //!        Dense classes sit far above and are unreachable by any value here; they are the control,
    //!        flat across the change, which is what makes the gain readable as this decision's own.
    static constexpr std::uint32_t cp_hi_range_threshold {20U};

    /*!
     * \brief Page-bitmap membership for U+0080..U+07FF. Kept separate so class-loop lambdas can
     *        inline it without pulling the sparse-hi path into the European hot stream (`\p{N}`, accented).
     * \param[in] cp_index Index of the code-point class.
     * \param[in] cp       Code point in U+0080..U+07FF; outside that range the bit index is meaningless.
     * \return True when \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_member_page(std::size_t cp_index,
                                                char32_t    cp)
    {
      const std::uint64_t* const page {cp_page_table(cp_index)};
      const std::uint32_t        bit  {static_cast<std::uint32_t>(cp) - 0x80U};
      return ((page[bit >> 6U] >> (bit & 63U)) & std::uint64_t {1}) != 0U;
    }

    /*!
     * \brief Membership for cp > U+07FF: sparse 2-stage hi table, else bsearch (small classes / constexpr).
     *        The table *build* is cold-outlined; the per-cp probe is last-hit + bit test.
     * \param[in] cp_index Index of the code-point class.
     * \param[in] cp       Code point above \ref cp_page_max.
     * \return True when \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_member_high(std::size_t cp_index,
                                                char32_t    cp)
    {
      if (std::is_constant_evaluated()) {
        return cp_class_matches(prog_.cp_classes[cp_index], cp);
      }
      // Resolved once per (state, class) rather than once per code point -- everything below the
      // memo is invariant for a whole scan, since `cp_index` does not change within one. Same shape
      // as \ref cp_page_table's own cache, and the same reason.
      if (state_.hi_class != static_cast<std::int32_t>(cp_index)) [[unlikely]] {
        resolve_hi(cp_index);
      }
      if (state_.hi_ptr != nullptr) {
        return state_.hi_ptr->contains(cp);
      }
      if (state_.hi_never) {
        return false; // no high ranges at all: every cp past the page bitmap is a non-member
      }
      return cp_class_matches(prog_.cp_classes[cp_index], cp);
    }

    /*!
     * \brief Fills the state's sparse-hi memo for \p cp_index — the cold half of \ref cp_member_high.
     *
     * Outlined so the per-code-point path is a class-key compare and a bit test, with the threshold
     * question, the fingerprint compare and `cp_hi_cached`'s two thread_local reads behind the miss.
     *
     * \param[in] cp_index Index of the code-point class to resolve.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void resolve_hi(std::size_t cp_index)
    {
      state_.hi_ptr   = nullptr;
      state_.hi_never = false;
      // Small classes: bsearch is already O(log ~32) and avoids the sparse-table build/cache.
      if (prog_.cp_classes[cp_index].range_count >= cp_hi_range_threshold) {
        const cp_hi_table* const hi {cp_hi_cached(prog_, cp_index)};
        if (hi != nullptr) {
          if (hi->page_of.empty()) {
            state_.hi_never = true;
          }
          else {
            state_.hi_ptr = hi;
          }
        }
      }
      state_.hi_class = static_cast<std::int32_t>(cp_index);
    }

    /*!
     * \brief Non-ASCII membership: European page bitmap, then sparse 2-stage hi / bsearch.
     * \param[in] cp_index Index of the code-point class.
     * \param[in] cp       Code point at or above U+0080.
     * \return True when \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_member_hi(std::size_t cp_index,
                                              char32_t    cp)
    {
      if (cp <= cp_page_max) {
        return cp_member_page(cp_index, cp);
      }
      return cp_member_high(cp_index, cp);
    }

    /*!
     * \brief Size \p out without a full npos fill when already sized.
     *
     * Production storage has \c ensure_size; seam tests pass \c std::vector (resize is enough —
     * it does not re-fill existing elements).
     *
     * \param[in,out] out Slot storage to grow.
     * \param[in]     n   Minimum size required; \p out is never shrunk.
     */
    template <typename OutSlots>
    static constexpr void ensure_slot_size(OutSlots&   out,
                                           std::size_t n)
    {
      if constexpr (requires { out.ensure_size(n); }) {
        out.ensure_size(n); // at-least-n; never shrinks
      }
      else if (out.size() < n) {
        out.resize(n);      // std::vector (seam tests): grow only
      }
    }

    /*!
     * \brief Writes a class-loop fast-path result into \p out_slots: the whole-match span in slots
     *        0/1, and — for a pattern wrapped in one capturing group (`(\w+)`, `([a-z]+)`) —
     *        the same span mirrored into the group's slots (its span equals the whole match by
     *        construction, so no re-match is needed).
     *
     * \c ensure_slot_size only (no \c npos fill). For no-capture and single greedy-group shapes
     * this writer covers every slot the program has; a prior \c assign(slot_count, npos) was dead
     * work on every find_iter match after the first (slots already sized, values overwritten).
     *
     * \param[out] out_slots   Capture slots to write.
     * \param[in]  match_start Whole-match start offset.
     * \param[in]  match_end   Whole-match end offset.
     */
    // always_inline, guarded as profile.hpp guards its own tick helpers. This writer is four stores and a
    // branch, yet it was emitted OUT OF LINE and cost 31 instructions a match -- most of it the call frame.
    // It runs once per match, not per byte, so inlining grows the caller without touching the per-byte
    // loop -- instruction counts and wall clock both fall on every row tried, none regressing.
    template <typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((always_inline))
#endif
    constexpr void fill_span_slots(OutSlots&   out_slots,
                                   std::size_t match_start,
                                   std::size_t match_end) const
    {
      ensure_slot_size(out_slots, prog_.slot_count);
      out_slots[0] = match_start;
      out_slots[1] = match_end;
      if (prog_.hints.greedy_group_start >= 0) {
        out_slots[static_cast<std::size_t>(prog_.hints.greedy_group_start)] = match_start;
        out_slots[static_cast<std::size_t>(prog_.hints.greedy_group_end)]   = match_end;
      }
    }

    /*!
     * \brief The memchr-cascade run tail: the next stop byte at or after \p from, or the text end. Kept in its own
     *        function so the memchr-cascade never inlines into \ref run_class_loop's hot per-byte loop
     *        (that bloat measurably slowed stop-dense short runs). Reached only once a run has already
     *        passed \ref cascade_run_threshold accepted bytes, so the out-of-line call is free.
     * \param[in] text Subject.
     * \param[in] from Offset to search from.
     * \return Offset of the next stop byte, or `text.size()` when none remains.
     */
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
     * This function is the no-lookaround path only. Trailing-lookaround
     * class+ is dispatched outside \ref run (see real.hpp / find_iter) into
     * \ref run_class_loop_trailing_la. always_inline: must stay in the find_iter
     * body on x86, where an out-of-line call costs a double-digit share of a match-dense walk).
     *
     * \tparam Cascade  Take the memchr-cascade run tail (chosen once per walk from stop_set_size).
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
      // Minimum run length in BYTES for the `X{k,}` desugaring (k identical copies
      // of the atom + a loop of it, see prefilter.hpp's extended class+ recognizer); 1 for the
      // original bare `X+` shape, where every one of the checks below is a dead branch (byte
      // runs are never shorter than 1) -- byte-identical to a bare `+`.
      const std::size_t         min_len {prog_.hints.greedy_class_loop_min};
      const std::uint8_t* const tbl =
        class_table(static_cast<std::size_t>(prog_.hints.greedy_class_loop));
      const auto in_class = [&](std::size_t i) {
                              return tbl[static_cast<std::uint8_t>(text[i])] != 0U;
                            };
      const auto scan_end = [&](std::size_t match_start) -> std::size_t {
                              std::size_t match_end {match_start + 1};
                              // Cascade memchr-stop after a long run; see historical comment.
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

      // the WRAP rule: optional `\b`/`\B` — try successive maximal class runs until boundaries hold.
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
          fill_span_slots(out_slots, start, match_end); // ensure_size + write, no npos assign
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
            fill_span_slots(out_slots, match_start, match_end);
            return true;
          }
          pos = match_end; // next run (e.g. "9abc def" → skip "abc", try "def")
        }
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }

      // the DROP rule window-edge guard, mode::full/prefix: anchored at `start` with no retry available --
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
          // the DROP rule window-edge guard: a candidate found by scanning forward past a non-class byte
          // is provably preceded by one (the scan just confirmed it), so the DROP rule’s redundancy
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
        // A maximal run shorter than the required minimum can never satisfy `X{k,}` starting
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
      fill_span_slots(out_slots, match_start, match_end);
      return true;
    }

  public:

    /*!
     * \brief The lookaround sub-scratch, built on first use.
     *
     * Two thread lists and an epsilon stack with their own containers. A `search()` builds a fresh state,
     * so constructing and destroying all of that landed on every search — for every pattern, including the
     * overwhelming majority with no lookaround at all. Making it lazy pays on every single search, and a
     * pattern that DOES use lookarounds is unchanged -- the emplace happens once per state, not once per
     * evaluation.
     * \return The scratch, engaged.
     */
    lookaround_scratch& lookaround_state()
    {
      if (!state_.lookaround.has_value()) {
        state_.lookaround.emplace();
      }
      return *state_.lookaround;
    }

    /*!
     * \brief Trailing-lookaround class+: body scan + longest end where lookaround holds.
     *
     * Cold, noinline: must not share a function body or inlining unit with
     * \ref run_class_loop (the daily [a-z]+ path). Invoked from real.hpp / find_iter
     * **outside** \ref run so a pure class-loop run() carries none of its code. Dynamic-only.
     *
     * \param[in]  text      Subject.
     * \param[in]  start     Byte offset to begin at.
     * \param[in]  mode      Anchoring: full, prefix or search.
     * \param[out] out_slots Capture slots, filled on a match.
     * \return True on a match.
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
        // Resolve the lookaround ONCE for the whole walk. `lookaround_holds` re-derives, per call,
        // things that cannot change between calls with the same sub_id: the sub lookup, its code
        // length, and its body's opcode. Callgrind on `[a-z]+(?=[a-z])` over 64 KB of prose puts it at
        // a quarter of the workload across a million-plus calls -- a few dozen instructions each, most of them
        // prologue and epilogue and ~10 are that invariant re-checking. Only the class test varies
        // with the position. Hoisting leaves the loop calling a small inlinable predicate instead of
        // an out-of-line function; it removes the work rather than moving it, which is what
        // force-inlining would have done (and that was measured as a regression -- see
        // basic_match_iterator::advance).
        const lookaround_sub& la_sub    {prog_.lookarounds[sub_id]};
        const instr*          la_simple {nullptr};
        if (la_sub.code_length == 2) {
          const instr& body {prog_.code[static_cast<std::size_t>(la_sub.code_offset)]};
          if (body.op == opcode::byte || body.op == opcode::klass
              || (body.op == opcode::klass_cp && !prog_.byte_mode)) {
            la_simple = &body;
          }
        }
        const auto la_at = [&](std::size_t e) {
                             if (la_simple == nullptr) {
                               return lookaround_holds(sub_id, e);
                             }
                             const bool matched {la_sub.direction == look_dir::behind
                                                   ? single_class_behind(*la_simple, e)
                                                   : single_class_ahead(*la_simple, e)};
                             return matched != la_sub.negative;
                           };
        const auto try_ends = [&](std::size_t ms, std::size_t me) -> bool {
                                for (std::size_t e = me; e > ms; --e) {
                                  if (la_at(e)) {
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
          if (match_end != text.size() || !la_at(match_end)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
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
     * \brief One buffered `cp_class_loop` match: the whole-match span, which for this route is the whole
     *        answer (a capturing wrap mirrors it, and \ref fill_span_slots reconstructs that).
     */
    struct cp_span
    {
      std::size_t start {}; //!< Match start, byte offset.
      std::size_t end   {}; //!< Match end, byte offset.
    };

    /*!
     * \brief Fills up to \p cap `class_loop` matches from \p start without leaving the route.
     *
     * The byte-class twin of \ref fill_cp_class_spans, and it exists for the same measurement: this
     * route emits a match every few bytes on word text (`[a-z]+` over prose is 42 858 matches in
     * a large subject) and the scan is a table lookup per byte. What is left is the per-match
     * return, and it is the same return.
     *
     * \tparam Cascade Whether the memchr stop-tail applies, chosen once per walk by the caller.
     * \param[in]  text  The subject.
     * \param[in]  start Where to begin.
     * \param[out] out   Buffer for the spans found.
     * \param[in]  cap   Capacity of \p out.
     * \return How many spans were written.
     *
     * \note **A filler for the `.`/negated-class route was refused here once, then landed.** The first
     *       attempt gained heavily on one toolchain and almost nothing on the other, where it took back
     *       most of what this filler had won (`words` 1.708 -> 3.155, `digits` 1.089 -> 1.933) — a
     *       translation-unit inline-budget effect, not a property of the scan (docs/design.dox §10.1).
     *       \ref fill_codepoint_class_spans is the version that did land, and it disclosed its own
     *       residual cost on unrelated rows rather than hiding it. The lesson that survives is the
     *       measurement discipline, not the conclusion "two fillers is what fits": each added branch
     *       in `refill_batch` must be re-measured on BOTH ISAs against rows that never touch it.
     */
    template <bool Cascade, bool WbEdge, bool WbKept>
    constexpr std::size_t fill_class_spans(std::string_view text,
                                           std::size_t      start,
                                           cp_span*         out,
                                           std::size_t      cap)
    {
      const std::uint8_t* const tbl {
        class_table(static_cast<std::size_t>(prog_.hints.greedy_class_loop))};
      // A TEMPLATE parameter, so the guard below compiles away entirely for every pattern that does
      // not carry a dropped leading `\b` -- which is nearly all of them. Two weaker versions were
      // measured and REFUSED first, both against calibrated layout floors: written inline per iteration
      // it charged every class-scan row, because it re-read the hint struct on every span emitted;
      // hoisting it to a runtime local still charged five of them. Only
      // `if constexpr` leaves those rows compiling to what they compiled to before.
      // The `{k,}` minimum run length, in BYTES, read ONCE outside the loop. A bare `+` leaves
      // it 1, where the compare can never fire (a run is non-empty), so the shapes that do not use
      // it pay a register compare and no hint-struct access -- the distinction that was costly when
      // the wb guard was first written the other way (see this file's WbEdge note).
      //
      // THE CODE-POINT TWIN EXISTS NOW, and how it got here is the part worth keeping. It was refused
      // twice -- a counter as a closure outside the loop, then the same templated away -- because each
      // charged `\p{L}+`, a pattern whose min is 1 and which never runs the check, twice over and above
      // its floor. Both readings were correct FOR THE INSTRUMENT that produced them:
      // benchmarks/bench_engines.cpp links <regex>, PCRE2 and RE2 beside real.hpp and sits on the
      // per-unit inlining budget. A consumer compiles only real.hpp. Re-judged in
      // benchmarks/bench_minimal.cpp -- which IS that unit -- the same change is a large gain on the
      // `{k,}` rows it targets, with `\p{L}+` indistinguishable from zero.
      // The cost belonged to the harness, not to the library. docs/MEASUREMENT.md §5.5.
      //
      // The refusals are kept rather than deleted because the reasoning was sound and only the
      // instrument was wrong; deleting them would erase the one case that shows how that happens.
      // A KEPT `\b`/`\B` wrap, checked per span. `wb_boundaries_ok` is the member the general route
      // calls; it reads `text_`, which `run()` binds and a filler never does, so this mirrors it
      // against this filler's own `text` -- the same null-view trap the WbEdge guard hit.
      // A maximal run of a word SUBSET can legitimately start after `_` or a digit, so unlike
      // `\b\w+\b`'s this assertion is NOT redundant and cannot be dropped at recognition time -- it has
      // to be evaluated here, which is what earns this filler its route.
      const bool         wb_ascii          {!prog_.unicode_word};
      const assert_kind  wb_lead_k         {prog_.hints.wb_lead == 2 ? assert_kind::not_word_boundary
                                                              : assert_kind::word_boundary};
      const assert_kind  wb_trail_k        {prog_.hints.wb_trail == 2 ? assert_kind::not_word_boundary
                                                               : assert_kind::word_boundary};
      const std::uint8_t wb_lead_h         {prog_.hints.wb_lead};
      const std::uint8_t wb_trail_h        {prog_.hints.wb_trail};
      const std::size_t  min_len           {prog_.hints.greedy_class_loop_min};
      bool               wb_edge           {WbEdge && start > 0};
      std::size_t        n                 {0};
      std::size_t        i                 {start};
      while (i < text.size()) {
        if (tbl[static_cast<std::uint8_t>(text[i])] == 0U) {
          ++i;
          continue;
        }
        std::size_t end {i + 1};
        if constexpr (Cascade) {
          while (end < text.size() && tbl[static_cast<std::uint8_t>(text[end])] != 0U) {
            ++end;
            if (end - i == cascade_run_threshold) {
              end = run_cascade_stop(text, end);
              break;
            }
          }
        }
        else {
          while (end < text.size() && tbl[static_cast<std::uint8_t>(text[end])] != 0U) {
            ++end;
          }
        }
        // the DROP rule window-edge guard, and it fires at exactly ONE position per walk. A candidate reached
        // by scanning forward past a non-member byte is provably preceded by one, so the DROP rule’s
        // redundancy argument (a maximal run can only start where the preceding character is
        // non-word) holds for it unconditionally. The single exception is the first candidate when
        // it coincides with `start` and `start > 0`, because a caller-supplied `pos` does NOT assert
        // that `text[pos - 1]` is absent or non-word -- see pattern_hints::wb_lead_maximal_run. On a
        // refill, `start` is the previous span's end, whose byte the scan just rejected, so the
        // condition cannot hold there; this is the same test, at the same position, that
        // run_class_loop applies.
        // The guard, and it is a REGISTER test that goes false after the first span. Written the
        // obvious way -- the whole condition inline, per iteration -- it read `prog_.hints` out of the
        // hint struct on every span emitted and charged every class-scan row above its own calibrated
        // floor. Hoisted, those rows are back inside noise. The assertion evaluator is the FREE
        // function given this filler's own `text`, not the member wrapper: the wrapper reads `text_`,
        // which `run()` binds and a filler never does, so calling it here segfaults in word_before on
        // a null view (ASan caught it the first time). `ascii_word` mirrors the wrapper exactly for a
        // non-flipped site.
        if constexpr (WbEdge) {
          if (wb_edge) {
            wb_edge = false; // can only ever be the FIRST candidate -- see the pre-loop initialiser
            if (i == start
                && !detail::assertion_holds(assert_kind::word_boundary, text, i, !prog_.unicode_word)) {
              i = end; // no genuine boundary here: skip this whole run, as the general route does
              continue;
            }
          }
        }
        // A maximal run shorter than the required minimum can never satisfy `X{k,}` starting
        // here, so skip past the whole run and try the next one -- exactly what run_class_loop's
        // search mode does.
        if (end - i < min_len) {
          i = end;
          continue;
        }
        // The same retry the general route uses: a run whose wrap does not hold cannot match starting
        // here, so skip the WHOLE run and try the next. Absent entirely when WbKept is false.
        if constexpr (WbKept) {
          if ((wb_lead_h != 0 && !detail::assertion_holds(wb_lead_k, text, i, wb_ascii))
              || (wb_trail_h != 0 && !detail::assertion_holds(wb_trail_k, text, end, wb_ascii))) {
            i = end;
            continue;
          }
        }
        out[n] = cp_span {.start = i, .end = end};
        ++n;
        i = end;
        if (n == cap) {
          break;
        }
      }
      return n;
    }

    /*!
     * \brief Fills up to \p cap bare single byte-class matches from \p start without leaving the route.
     *
     * The unquantified sibling of \ref fill_class_spans, and the reason it exists is the same one, in
     * its sharpest form: `[a-z]` has no `+` to amortise anything over, so every single accepted byte is a
     * full route entry -- one per match -- which makes it slower per byte than `.`, a pattern that matches
     * at EVERY position, and several times slower than its own `+` form.
     *
     * There is no run to coalesce and so no `Cascade` variant: one accepted byte is one match, spans
     * are exactly one byte wide, and consecutive matches are consecutive positions. The accept test is
     * \ref class_table on \ref pattern_hints::single_class — the SAME table the general route consults,
     * not a second copy of the membership rule.
     *
     * Narrow by construction, and the guard is the caller's (\ref real::basic_match_iterator): search
     * semantics, no anchor, no `\b`/`\B` wrap. The shape itself (a 4-opcode program) rules out capture
     * groups and a `{k,}` minimum, so unlike its siblings this filler has no bookkeeping it could fail
     * to reproduce.
     *
     * \param[in]  text  The subject.
     * \param[in]  start Where to begin.
     * \param[out] out   Buffer for the spans found.
     * \param[in]  cap   Capacity of \p out; the walk stops there and resumes from the last end.
     * \return How many spans were written.
     */
    constexpr std::size_t fill_single_class_spans(std::string_view text,
                                                  std::size_t      start,
                                                  cp_span*         out,
                                                  std::size_t      cap)
    {
      const std::uint8_t* const tbl               {class_table(static_cast<std::size_t>(prog_.hints.single_class))};
      std::size_t               n                 {0};
      for (std::size_t i {start}; i < text.size(); ++i) {
        if (tbl[static_cast<std::uint8_t>(text[i])] == 0U) {
          continue;
        }
        out[n] = cp_span {.start = i, .end = i + 1};
        ++n;
        if (n == cap) {
          break;
        }
      }
      return n;
    }

    /*!
     * \brief Fills up to \p cap `cp_class_loop` matches from \p start without leaving the route.
     *
     * The route's per-match cost is not its scan. Holding the class and the bytes fixed and varying
     * only how often a match must be emitted puts the inner scan several times below the same bytes
     * emitted one code point at a time: MOST of such a row is the per-match return through `run()`'s
     * dispatch, `fill_span_slots` and the iterator's re-entry, paid once every few bytes for a
     * single-code-point pattern. Filling a buffer amortises all of it over \p cap matches and
     * hoists `asc` once for the batch instead of once per match.
     *
     * Narrow by construction, and the guard is the caller's (\ref basic_match_iterator): search
     * semantics, no `\b`/`\B` wrap, no `{k,}` minimum. Those shapes have bookkeeping this loop does
     * not reproduce, and batching them would answer a different question than the one asked.
     * \param[in]  text  The subject.
     * \param[in]  start Where to begin.
     * \param[out] out   Buffer for the spans found.
     * \param[in]  cap   Capacity of \p out; the walk stops there and resumes from the last end.
     * \return How many spans were written.
     */
    template <bool WbEdge>
    constexpr std::size_t fill_cp_class_spans(std::string_view text,
                                              std::size_t      start,
                                              cp_span*         out,
                                              std::size_t      cap)
    {
      const std::size_t         cp_index {static_cast<std::size_t>(prog_.hints.greedy_cp_class)};
      const std::uint8_t* const asc {cp_ascii_table(cp_index)};
      const auto                member_hi = [&](char32_t cp) -> bool {
                                              if (cp <= cp_page_max) {
                                                return cp_member_page(cp_index, cp);
                                              }
                                              return cp_member_high(cp_index, cp);
                                            };
      const auto width = [&](std::size_t i) -> std::size_t {
                           const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
                           if (!dc.valid) {
                             return 0;
                           }
                           const bool m {dc.cp < 0x80U ? asc[dc.cp] != 0U : member_hi(dc.cp)};
                           return m ? dc.length : 0;
                         };
      const std::size_t min_len {prog_.hints.greedy_cp_class_min};
      const auto        count_cps = [&](std::size_t s, std::size_t e) -> std::size_t {
                                      std::size_t k {0};
                                      for (std::size_t j {s}; j < e; ++k) {
                                        const auto lead {static_cast<std::uint8_t>(text[j])};
                                        j += lead < 0x80U ? std::size_t {1}
                                                          : detail::decode_codepoint_strict(text, j).length;
                                      }
                                      return k;
                                    };
      const bool        greedy  {prog_.hints.greedy_cp_class_plus};
      const std::size_t max_len {prog_.hints.greedy_cp_class_max};
      // A TEMPLATE parameter, so the guard below compiles away entirely for every pattern that does
      // not carry a dropped leading `\b` -- which is nearly all of them. Two weaker versions were
      // measured and REFUSED first, both against calibrated layout floors: written inline per iteration
      // it charged every class-scan row, because it re-read the hint struct on every span emitted;
      // hoisting it to a runtime local still charged five of them. Only
      // `if constexpr` leaves those rows compiling to what they compiled to before.
      bool              wb_edge {WbEdge && start > 0};
      std::size_t       n       {0};
      std::size_t       i       {start};
      while (i < text.size()) {
        const auto  lead {static_cast<std::uint8_t>(text[i])};
        std::size_t w    {0};
        if (asc[lead] != 0U) {
          w = 1;
        }
        else if (lead >= 0x80U) {
          w = width(i);
        }
        if (w == 0) {
          ++i;
          continue;
        }
        std::size_t end {i + w};
        if (greedy) {
          while (end < text.size()) {
            const auto l2 {static_cast<std::uint8_t>(text[end])};
            if (asc[l2] != 0U) {
              ++end;
              continue;
            }
            if (l2 < 0x80U) {
              break;
            }
            const std::size_t w2 {width(end)};
            if (w2 == 0) {
              break;
            }
            end += w2;
          }
        }
        else if (max_len != 0) {
          for (std::size_t k {1}; k < max_len && end < text.size(); ++k) {
            const auto l2 {static_cast<std::uint8_t>(text[end])};
            if (asc[l2] != 0U) {
              ++end;
              continue;
            }
            if (l2 < 0x80U) {
              break;
            }
            const std::size_t w2 {width(end)};
            if (w2 == 0) {
              break;
            }
            end += w2;
          }
        }
        // the DROP rule window-edge guard, and it fires at exactly ONE position per walk. A candidate reached
        // by scanning forward past a non-member byte is provably preceded by one, so the DROP rule’s
        // redundancy argument (a maximal run can only start where the preceding character is
        // non-word) holds for it unconditionally. The single exception is the first candidate when
        // it coincides with `start` and `start > 0`, because a caller-supplied `pos` does NOT assert
        // that `text[pos - 1]` is absent or non-word -- see pattern_hints::wb_lead_maximal_run. On a
        // refill, `start` is the previous span's end, whose byte the scan just rejected, so the
        // condition cannot hold there; this is the same test, at the same position, that
        // run_cp_class_loop applies.
        // The guard, and it is a REGISTER test that goes false after the first span. Written the
        // obvious way -- the whole condition inline, per iteration -- it read `prog_.hints` out of the
        // hint struct on every span emitted and charged every class-scan row above its own calibrated
        // floor. Hoisted, those rows are back inside noise. The assertion evaluator is the FREE
        // function given this filler's own `text`, not the member wrapper: the wrapper reads `text_`,
        // which `run()` binds and a filler never does, so calling it here segfaults in word_before on
        // a null view (ASan caught it the first time). `ascii_word` mirrors the wrapper exactly for a
        // non-flipped site.
        if constexpr (WbEdge) {
          if (wb_edge) {
            wb_edge = false; // can only ever be the FIRST candidate -- see the pre-loop initialiser
            if (i == start
                && !detail::assertion_holds(assert_kind::word_boundary, text, i, !prog_.unicode_word)) {
              i = end; // no genuine boundary here: skip this whole run, as the general route does
              continue;
            }
          }
        }
        if (min_len > 1 && count_cps(i, end) < min_len) {
          i = end;
          continue;
        }
        out[n] = cp_span {.start = i, .end = end};
        ++n;
        i = end;
        if (n == cap) {
          break;
        }
      }
      return n;
    }

    /*!
     * \brief Writes a buffered span into a caller's slots exactly as the per-match path would.
     * \param[out] out_slots Slots to fill.
     * \param[in]  s         Match start.
     * \param[in]  e         Match end.
     */
    template <typename OutSlots>
    constexpr void write_cp_span_slots(OutSlots&   out_slots,
                                       std::size_t s,
                                       std::size_t e)
    {
      fill_span_slots(out_slots, s, e);
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
    // The gcc-only outline of the >= 0x80 path in run_cp_class_loop: split into
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
      // Minimum run length in CODE POINTS (not bytes) for the `X{k,}` desugaring --
      // see run_class_loop's own doc comment; 1 for the original bare shape (dead branch below).
      const std::size_t min_len  {prog_.hints.greedy_cp_class_min};
      const std::size_t cp_index {static_cast<std::size_t>(prog_.hints.greedy_cp_class)};
#if defined(__GNUC__) && !defined(__clang__)
#include "real/engine/cpclass_gcc_loop.hpp"
#else
      const std::uint8_t* const  asc {cp_ascii_table(cp_index)};
      // Membership of a non-ASCII code point (>= 0x80): page path inlined here so European streams
      // (`\p{N}`, accented) never pay the sparse-hi body; CJK/astral → \ref cp_member_high.
      const auto member_hi = [&](char32_t cp) -> bool {
                               if (cp <= cp_page_max) {
                                 return cp_member_page(cp_index, cp);
                               }
                               return cp_member_high(cp_index, cp);
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
      // Success uses fill_span_slots (ensure_size, no npos fill). Fail still assigns (seam +
      // general-path slot parity when !matched).
      /*!
       * \brief Whether a class member starts at \p i — membership only, no width.
       *
       * The leftmost scan below needs one bit per byte, and asking \c width for it built a three-field
       * decode result, tested `valid`, re-branched on `cp < 0x80` and mapped a length back to the bit
       * `asc[lead]` already held. A strict decode of a byte below 0x80 is exactly
       * `{cp = lead, length = 1, valid = true}`, so that table entry IS the answer — the same shape
       * \ref run_class_loop's own `in_class` has, which is why its scan costs a fraction of this one.
       *
       * Kept separate from \c width rather than folded into it: `extend_run` needs the length, and one
       * lambda returning a width cannot narrow to a bool for the scan. Measured with each pattern ALONE in
       * its translation unit, the code-point rows gain substantially and the byte-class and literal rows are
       * byte-identical. Isolating the scan on a corpus with NO member at all, this route cost several times
       * the byte-class route for the same work before this.
       */
      const auto in_class = [&](std::size_t i) -> bool {
                              const auto lead {static_cast<std::uint8_t>(text[i])};
                              // Table FIRST, width test only on a miss. `asc` is a full 256-entry row, and a
                              // code-point class never sets a bit at or above 0x80 -- its `ascii` half is
                              // exactly that -- so a hit here is necessarily a single-byte member and the
                              // `< 0x80` test cannot change the answer. Ordering it after the table takes it
                              // off the accepted-byte path, where it was the second branch per byte.
                              if (asc[lead] != 0U) {
                                return true;
                              }
                              if (lead < 0x80U) {
                                return false; // an ASCII byte this class does not hold
                              }
                              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
                              return dc.valid && member_hi(dc.cp);
                            };
      const auto extend_run = [&](std::size_t match_start) -> std::size_t {
                                const std::size_t first {width(match_start)};
                                if (first == 0) {
                                  return npos;
                                }
                                std::size_t match_end {match_start + first};
                                if (prog_.hints.greedy_cp_class_plus) {
                                  while (match_end < text.size()) {
                                    const auto lead {static_cast<std::uint8_t>(text[match_end])};
                                    // Table FIRST — see in_class above for why the `< 0x80` test is sound
                                    // to move off the accepted-byte path. This is the run extension, so it
                                    // is the loop that runs once per matched byte of the whole corpus.
                                    if (asc[lead] != 0U) {
                                      ++match_end;
                                      continue;
                                    }
                                    if (lead < 0x80U) {
                                      break; // an ASCII byte this class does not hold: the run ends
                                    }
                                    const detail::decoded_codepoint dc {
                                      detail::decode_codepoint_strict(text, match_end)};
                                    if (!dc.valid || !member_hi(dc.cp)) {
                                      break;
                                    }
                                    match_end += dc.length;
                                  }
                                }
                                // A COUNTED repeat is mutually exclusive with the greedy loop -- `X{k}`
                                // emits no self-loop -- so it sits in the `else` and the test above stays
                                // exactly the one that was there. Tested FIRST instead, it cost `\w+` and
                                // a couple of percent of a word-boundary walk's instructions, on
                                // patterns that can never reach it.
                                else if (const std::size_t max_len {prog_.hints.greedy_cp_class_max};
                                         max_len != 0) {
                                  for (std::size_t n {1}; n < max_len && match_end < text.size(); ++n) {
                                    const auto lead {static_cast<std::uint8_t>(text[match_end])};
                                    // Table FIRST — same soundness argument as in_class above.
                                    if (asc[lead] != 0U) {
                                      ++match_end;
                                      continue;
                                    }
                                    if (lead < 0x80U) {
                                      break; // an ASCII byte this class does not hold
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
      const auto fail = [&]() {
                          out_slots.assign(prog_.slot_count, npos);
                          return false;
                        };

      // The limit a trailing `\Z`/`$` imposes. The recognizer peeled that assertion out of the
      // program, so this is the only thing left enforcing it -- and `$` (kind 2) also accepts the
      // position just before ONE final newline, which is why `^X$` is not fullmatch(X).
      const std::size_t end_limit {
        prog_.hints.greedy_cp_class_end == 2 && !text.empty() && text.back() == '\n'
          ? text.size() - 1
          : text.size()};

      // Counts code points in [s, e) -- only walked when min_len > 1 (the {k,} shape); the
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

      // the WRAP rule: `\b`/`\B` on subset cp-class (e.g. `\b\d+\b`) — try successive runs.
      if (prog_.hints.wb_lead != 0 || prog_.hints.wb_trail != 0) {
        if (mode == run_mode::full || mode == run_mode::prefix) {
          // `extend_run` decodes at its argument, which requires a byte to be there; anchored modes
          // have no forward scan to establish that, so the window edge must be tested here (the
          // search branch below gets it from its own scan). The route's minimum is at least one code
          // point by construction, so an exhausted window can never match.
          if (start >= text.size()) {
            return fail();
          }
          const std::size_t match_end {extend_run(start)};
          if (match_end == npos || (mode == run_mode::full && match_end != text.size()) ||
              (prog_.hints.greedy_cp_class_end != 0 && match_end != end_limit) ||
              !wb_boundaries_ok(start, match_end) ||
              (min_len > 1 && count_cps(start, match_end) < min_len)) {
            return fail();
          }
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
          const std::size_t match_end {extend_run(match_start)};
          if (match_end != npos && wb_boundaries_ok(match_start, match_end) &&
              (min_len <= 1 || count_cps(match_start, match_end) >= min_len)) {
            fill_span_slots(out_slots, match_start, match_end);
            return true;
          }
          pos = match_end == npos ? match_start + 1 : match_end;
        }
        return fail();
      }

      // the DROP rule window-edge guard, mode::full/prefix: anchored at `start` with no retry available --
      // see pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
      if ((mode == run_mode::full || mode == run_mode::prefix) && prog_.hints.wb_lead_maximal_run &&
          start > 0 && start < text.size() && width(start) != 0 &&
          !assertion_holds(assert_kind::word_boundary, start, false)) {
        return fail();
      }
      std::size_t match_start {start};
      std::size_t match_end   {};
      while (true) {
        if (mode == run_mode::search) {
          while (match_start < text.size() && !in_class(match_start)) {
            ++match_start;
          }
          if (match_start >= text.size()) {
            return fail();
          }
          // the DROP rule window-edge guard: a candidate found by scanning forward past a non-class
          // code point is provably preceded by one, so the DROP rule’s redundancy argument holds
          // unconditionally there. The ONE exception is the very first candidate when it
          // coincides with `start` itself (no forward scan occurred) AND `start > 0` -- see
          // pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
          if (prog_.hints.wb_lead_maximal_run && match_start == start && match_start > 0 &&
              !assertion_holds(assert_kind::word_boundary, match_start, false)) {
            const std::size_t skip {extend_run(match_start)};
            if (skip == npos) {
              return fail(); // malformed sequence right at the window edge: nothing to skip to
            }
            match_start = skip; // no genuine boundary here: skip this whole run
            continue;
          }
        }
        if (match_start >= text.size()) {
          return fail();
        }
        // The first code point must match: this path is only chosen for `\w`/`\w+` (never nullable).
        match_end = extend_run(match_start);
        if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
          return fail();
        }
        // A maximal run shorter than the required minimum can never satisfy `X{k,}` starting
        // here -- in search mode, skip past the whole (too-short) run and try the next one;
        // anchored modes have no retry, so fail outright (mirrors run_class_loop's own min-check).
        if (min_len > 1 && count_cps(match_start, match_end) < min_len) {
          if (mode != run_mode::search) {
            return fail();
          }
          match_start = match_end;
          continue;
        }
        // Same retry shape for the end anchor: a maximal run that stops short of the limit can never
        // be the match, so skip past it. Placed in the existing loop rather than replaced by a
        // backward walk -- walking back through UTF-8 means decoding, and this scan's handling of a
        // malformed sequence is already pinned by the seam differential.
        if (prog_.hints.greedy_cp_class_end != 0 && match_end != end_limit) {
          if (mode != run_mode::search || match_end <= match_start) {
            return fail();
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
     * \brief Shared driver: a possessive class+/++ loop, bare/suffixed (\ref
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
     *
     * \param[in]  text       Subject.
     * \param[in]  start      Byte offset to begin at.
     * \param[in]  mode       Anchoring: full, prefix or search.
     * \param[out] out_slots  Capture slots, filled on a match.
     * \param[in]  in_class   Membership test, per \c InClass.
     * \param[in]  scan_end   Maximal-run scanner, per \c ScanEnd.
     * \param[in]  last_width Last-atom width, per \c LastWidth.
     * \return True on a match.
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
      // how this bug -- present since the original klass/klass_cp fast path -- surfaced
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
        // arms both together) -- suffix_ok / write_success above already cover it exactly.
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
      // the DROP rule window-edge guard: see pattern_hints::wb_lead_maximal_run's own doc comment. Applies
      // only when `start` itself is the candidate AND is actually in-class (a zero-length body at
      // a non-class `start` has no "run" for the DROP rule’s argument to be about in the first place).
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

    /*!
     * \brief R2 (phase Raffinement): possessive literal-byte +/++ loop (`byte_loop_possessive`,
     *        e.g. `a++`) -- the asymmetry class_ref's typing made natural to close: this opcode was
     *        already emitted and executed by the general VM, but had no dedicated recognizer or
     *        runner, so `a++` fell back to the general VM despite the class/cp-class family
     *        already having one. See \ref run_possessive_loop_generic for the shared algorithm.
     * \param[in]  text      Subject.
     * \param[in]  start     Byte offset to begin at.
     * \param[in]  mode      Anchoring: full, prefix or search.
     * \param[out] out_slots Capture slots, filled on a match.
     * \return True on a match.
     */
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

    /*!
     * \brief Possessive class+/++ loop over a BYTE class (`klass_loop_possessive`).
     *        See \ref run_possessive_loop_generic for the shared algorithm.
     * \param[in]  text      Subject.
     * \param[in]  start     Byte offset to begin at.
     * \param[in]  mode      Anchoring: full, prefix or search.
     * \param[out] out_slots Capture slots, filled on a match.
     * \return True on a match.
     */
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

    /*!
     * \brief Possessive class+/++ loop over a CODE-POINT class
     *        (`klass_cp_loop_possessive`). Mirrors \ref run_cp_class_loop's decode/membership
     *        primitives, except that the scan predicate is now split by compiler (see `in_class` below):
     *        clang/MSVC read the ASCII table directly, gcc keeps the width round trip. Both directions are
     *        measured, and gcc's is the counter-intuitive one.
     *        See \ref run_possessive_loop_generic for the shared algorithm.
     * \param[in]  text      Subject.
     * \param[in]  start     Byte offset to begin at.
     * \param[in]  mode      Anchoring: full, prefix or search.
     * \param[out] out_slots Capture slots, filled on a match.
     * \return True on a match.
     */
    template <typename OutSlots>
    constexpr bool run_possessive_cp_class_loop(std::string_view  text,
                                                std::size_t       start,
                                                run_mode          mode,
                                                OutSlots&         out_slots)
    {
      prof::tick_route(prog_.hints.possessive_prefix_size > 0 ? prof::route::possessive_delimited
                                                               : prof::route::possessive_cp_class_loop);
      const std::size_t         cp_index {static_cast<std::size_t>(prog_.hints.possessive_class.index)};
      const std::uint8_t* const asc      {cp_ascii_table(cp_index)};
      const auto                member_hi = [&](char32_t cp) -> bool {
                                              if (cp <= cp_page_max) {
                                                return cp_member_page(cp_index, cp);
                                              }
                                              return cp_member_high(cp_index, cp);
                                            };
      const auto cp_width = [&](std::size_t i) -> std::size_t {
                              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
                              if (!dc.valid) {
                                return 0;
                              }
                              const bool m {dc.cp < 0x80U ? asc[dc.cp] != 0U : member_hi(dc.cp)};
                              return m ? dc.length : 0;
                            };
#if defined(__GNUC__) && !defined(__clang__)
      // gcc keeps the width round trip. Measured, and it is not the shape one would guess: the
      // ASCII-direct predicate below is sharply SLOWER on a scan with no member, and slower again on one
      // with members, while REDUCING its instruction count -- fewer
      // instructions, more time, which is the same trap cpclass_gcc.hpp's own note documents for this loop family.
      const auto in_class = [&](std::size_t i) { return i < text.size() && cp_width(i) != 0; };
#else
      // Membership only, no width, for the leftmost scan -- the sibling byte-class runner's `in_class` is
      // one table load, and this one went through `cp_width`: a three-field decode result, a `valid` test,
      // a re-branch on `cp < 0x80` and a length mapped back to the bit `asc[lead]` already held.
      // arm64/clang, find_iter over 64 KiB, each pattern ALONE in its TU (best of 25, three repeats):
      // the possessive code-point rows substantially; isolating the scan on a corpus with no member at all
      // halves it, which is exact parity with the greedy cp-class route.
      //
      // No bounds check, which is the sibling byte-class runner's contract too: every call site in
      // run_possessive_loop_generic guards `< text.size()` before asking. Carrying one here costs, and
      // was unreachable -- zero executions over the whole suite.
      const auto in_class = [&](std::size_t i) -> bool {
                              const auto lead {static_cast<std::uint8_t>(text[i])};
                              // Table FIRST — same soundness argument as run_cp_class_loop's in_class.
                              if (asc[lead] != 0U) {
                                return true;
                              }
                              if (lead < 0x80U) {
                                return false; // an ASCII byte this class does not hold
                              }
                              const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
                              return dc.valid && member_hi(dc.cp);
                            };
#endif
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

    /*!
     * \brief Fixed-shape body match from \ref pattern_hints::body_pc, then B1 `\b`/`\B` wrap.
     * \tparam SkipSaves Skip capture writes when the caller only needs the span.
     * \param[in] text Subject.
     * \param[in] s    Candidate match start.
     * \return Match end offset, or \ref npos when the body or the boundary wrap fails.
     */
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
          // Span-only write — ensure_size, no npos fill (both slots rewritten).
          ensure_slot_size(out_slots, 2);
          out_slots[0] = match_start;
          out_slots[1] = match_end;
          return true;
        }
        ++match_start;
      }
      return false;
    }

    /*!
     * \brief Search route for a HETEROGENEOUS fixed shape: vector-prefilter two positions, verify each
     *        survivor with the ordinary fixed-body walk, hand the sub-block tail to \ref fast_search.
     *
     * **Its own route, dispatched from \ref run — deliberately NOT a branch inside \ref
     * run_fixed_shape.** Hosting this block there was measured on callgrind to cost measurably more
     * INSTRUCTIONS** on heterogeneous shapes that never enter it (`[0-9]{2}:[0-9]{2}`, which the
     * `rare_byte` veto declines): not cycles, not layout luck — the block changed that function's
     * optimization decisions and its scalar path paid. Two variants were tried inside it, inline and
     * `noinline`; the `noinline` one halved the cost but also cut the win,
     * so neither was clean. Out here, `run_fixed_shape`'s body is byte-identical to before and only
     * patterns that actually take this route see new code — the same isolation \ref
     * run_class_loop_trailing_la buys for the class loop.
     *
     * `noinline` for the mirror reason: \ref run's own body must not grow (the guard there is one
     * compare). Search mode only — anchored modes have a single candidate and go straight to the walk.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin searching at.
     * \param[out] out_slots Receives the matched span on success, `npos` on failure (seam parity with
     *                       \ref run_fixed_shape's own `fail()`).
     * \return `true` if the sequence matched.
     */
    template <typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool run_pair_filtered_shape(std::string_view text,
                                 std::size_t      start,
                                 OutSlots&        out_slots)
    {
      // The same verify run_fixed_shape uses, `\b` conditions included — not a reimplementation.
      const auto at {[&](std::size_t s) {
                       return match_fixed_body_wb</*SkipSaves=*/ false>(text, s);
                     }};
      std::size_t       resume {start};
      std::size_t       end    {npos};
      const std::size_t found  {
        simd_fixed_shape_pair_scan(text, start, prog_.hints, at, end, resume)};
      if (found != npos) {
        ensure_slot_size(out_slots, 2); // == run_fixed_shape's write_span
        out_slots[0] = found;
        out_slots[1] = end;
        return true;
      }
      if (fast_search(text, resume, at, out_slots)) { // the < 16-candidate tail
        return true;
      }
      out_slots.assign(2, npos);
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
        // Success rewrites both spans; fail assigns for seam parity.
        const auto write_span = [&](std::size_t s, std::size_t e) {
                                  ensure_slot_size(out_slots, 2);
                                  out_slots[0] = s;
                                  out_slots[1] = e;
                                };
        const auto fail = [&]() {
                            out_slots.assign(2, npos);
                            return false;
                          };
        const auto at {[&](std::size_t s) {
                         return match_fixed_body_wb</*SkipSaves=*/ false>(text, s);
                       }};
        if (mode != run_mode::search) {
          const std::size_t match_end {at(start)};
          if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
            return fail();
          }
          write_span(start, match_end);
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
              if (!fast_search(text, resume, at, out_slots)) {
                return fail();
              }
              return true;
            }
            const std::size_t e {found + prog_.hints.fixed_shape_simd_len};
            if (wb_boundaries_ok(found, e)) {
              write_span(found, e);
              return true;
            }
            pos = found + 1; // body matched but `\b` failed — try next candidate
          }
          return fail();
        }
#endif
        if (!fast_search(text, start, at, out_slots)) {
          return fail();
        }
        return true;
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
     * \brief Verifies a fixed code-point shape forward from \p s, filling capture slots as it goes.
     *
     * The shape is a sequence of code-point atoms and literal bytes with no loop
     * (\ref pattern_hints::il_cp_shape_eligible), so one linear walk decides the whole match and every
     * `save` lands on the position the walk has reached — no engine, and no separate capture pass.
     * \param[in]  text      Subject.
     * \param[in]  s         Candidate match start.
     * \param[out] out_slots Receives the slots (untouched unless the walk succeeds).
     * \return The match end, or \ref real::npos if the shape does not hold at \p s.
     */
    template <typename OutSlots>
    [[nodiscard]] constexpr std::size_t match_cp_shape(std::string_view text,
                                                       std::size_t      s,
                                                       OutSlots&        out_slots) const
    {
      std::size_t at {s};
      for (std::size_t pc {0}; pc < prog_.code.size();) {
        const instr& instruction {prog_.code[pc]};
        if (instruction.op == opcode::save) {
          out_slots[static_cast<std::size_t>(instruction.arg16)] = at;
          ++pc;
        }
        else if (instruction.op == opcode::match) {
          return at;
        }
        else if (at >= text.size()) {
          return npos;
        }
        else if (instruction.op == opcode::byte) {
          if (static_cast<std::uint8_t>(text[at]) != instruction.arg8) {
            return npos;
          }
          ++at;
          ++pc;
        }
        else if (instruction.op == opcode::klass) {
          if (!prog_.classes[instruction.arg16].test(static_cast<std::uint8_t>(text[at]))) {
            return npos;
          }
          ++at;
          ++pc;
        }
        else { // klass_cp: one whole code point, then past the four-slot construct
          const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, at)};
          if (!dc.valid || !cp_class_holds(prog_.cp_classes[instruction.arg16], dc.cp)) {
            return npos;
          }
          at += dc.length;
          pc += 4;
        }
      }
      return npos;
    }

    /*!
     * \brief Fills capture slots for a `class+ <literal> class+` match, by anchor rather than by offset.
     *
     * The two-run shape has no fixed widths, so \ref fill_fixed_saves's running offset does not apply — but
     * every `save` in it still lands on one of four positions, and which one is decided by where the save
     * sits relative to the two loops and the literal. Walking the program once per MATCH is the same trick
     * \ref fill_fixed_saves uses, and the program is a dozen instructions.
     * \param[in]  s        Match start (the prefix run's beginning).
     * \param[in]  h        The literal's own start.
     * \param[in]  lit_end  One past the literal.
     * \param[in]  e        Match end (the suffix run's end).
     * \param[out] out_slots Slots to fill.
     */
    template <typename OutSlots>
    constexpr void fill_two_run_saves(std::size_t s,
                                      std::size_t h,
                                      std::size_t lit_end,
                                      std::size_t e,
                                      OutSlots&   out_slots) const
    {
      if (prog_.slot_count <= 2) {
        return;
      }
      std::size_t anchor        {s};
      bool        after_literal {false};
      for (const instr& instruction : prog_.code) {
        if (instruction.op == opcode::save) {
          out_slots[static_cast<std::size_t>(instruction.arg16)] = anchor;
        }
        else if (instruction.op == opcode::byte) {
          anchor        = lit_end; // the literal's bytes; saves after them open at its end
          after_literal = true;
        }
        else if (instruction.op == opcode::split) {
          anchor = after_literal ? e : h; // a loop just closed: the prefix ends at h, the suffix at e
        }
      }
    }

    /*!
     * \brief Batched twin of \ref run_codepoint_class — fills up to \p cap maximal spans in ONE call.
     *
     * The `.`/negated-class shape was the only class scan without a batch filler, so it paid a full
     * route entry PER MATCH where the byte- and code-point-class routes pay one per sixteen. Measured
     * on their own fast paths: the code-point rows cost several times the byte-class one per match -- and
     * `fields [^,]+` and `.` are the two weakest rows in docs/BENCHMARKS.md against PCRE2-JIT.
     *
     * A NEW function rather than a flag threaded through the existing one: an earlier attempt to widen
     * a shared scan lambda with one extra branch nearly doubled the property-class rows on the paths
     * that did not even use it. Nothing here is on any other route's codegen.
     *
     * Search semantics only, which is what the batched walk uses -- \ref basic_match_iterator excludes
     * anchored shapes from batching, and `run_mode::full` keeps \ref run_codepoint_class.
     *
     * \tparam Cascade Select the memchr-cascade run scan, chosen once per walk.
     * \param[in]  text  The subject.
     * \param[in]  start Byte offset to begin at.
     * \param[out] out   Receives the spans.
     * \param[in]  cap   Capacity of \p out.
     * \return How many spans were written (0 = no further match).
     */
    template <bool Cascade>
    constexpr std::size_t fill_codepoint_class_spans(std::string_view text,
                                                     std::size_t      start,
                                                     cp_span*         out,
                                                     std::size_t      cap)
    {
      const std::uint8_t* const ascii {
        class_table(static_cast<std::size_t>(prog_.hints.codepoint_class_ascii))};
      const auto cont = [&](std::size_t i) {
                          const auto cont_byte {static_cast<std::uint8_t>(text[i])};
                          return cont_byte >= 0x80 && cont_byte <= 0xBF;
                        };
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
      std::size_t n {0};
      std::size_t i {start};
      while (n < cap && i < text.size()) {
        std::size_t w {width(i)};
        while (w == 0) {
          ++i;
          if (i >= text.size()) {
            return n;
          }
          w = width(i);
        }
        const std::size_t match_start {i};
        std::size_t       match_end   {i + w};
        if (prog_.hints.codepoint_class_plus) {
          const auto scalar_scan = [&]() {
                                     while (match_end < text.size()) {
                                       const std::size_t cw {width(match_end)};
                                       if (cw == 0) {
                                         break;
                                       }
                                       match_end += cw;
                                     }
                                   };
          if constexpr (Cascade) {
            if (!std::is_constant_evaluated()) {
              const std::size_t stop {find_bytes_cascade(text, match_end, prog_.hints.stop_set.data(),
                                                         prog_.hints.stop_set_size)};
              const std::size_t p1   {stop == npos ? text.size() : stop};
              bool              bad  {false};
              while (match_end < p1) {
                const std::size_t high {first_high_byte(text, match_end, p1)};
                if (high == p1) {
                  break;
                }
                match_end = high;
                const std::size_t cw {width(match_end)};
                if (cw == 0) {
                  bad = true;
                  break;
                }
                match_end += cw;
              }
              if (!bad) {
                match_end = p1;
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
        out[n].start = match_start;
        out[n].end   = match_end;
        ++n;
        i = match_end;
      }
      return n;
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
      // Success rewrites both span slots; fail assigns for seam parity.
      const auto fail = [&]() {
                          out_slots.assign(2, npos);
                          return false;
                        };

      const auto cont = [&](std::size_t i) {
                          const auto cont_byte {static_cast<std::uint8_t>(text[i])};
                          return cont_byte >= 0x80 && cont_byte <= 0xBF;
                        };
      // Byte length of a matching codepoint at i, or 0 for no match. ASCII stays a direct table
      // hit; the 3-/4-byte cases bounds-check their FIRST continuation byte against
      // utf8_second_byte_bounds_table (charclass.hpp) instead of the generic [0x80,0xBF] `cont`
      // check -- that generic check accepted overlong (E0 80 80 / F0 80 80 80) and encoded-
      // surrogate (ED A0 80) sequences as one code point. A table lookup, not a full decode: reusing
      // decode_codepoint_strict (which accumulates the code point via
      // shifts and checks it against min_cp/the surrogate block after the fact) costs measurably
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

      // The recompute below is DELIBERATE, and removing it was measured and refused. The search loop
      // stops on a non-zero width and could hand it over -- callgrind agrees it is redundant, and
      // carrying it (`while (... && (first_width = width(i)) == 0)`) cut total instructions on `[^,]+`
      // by a fifth (the lambda being a real call rather than
      // inlined). It also made this path SLOWER, reproducibly on two independent builds: `\w+`
      // and the property rows with them, while the row it TARGETED barely moved.
      // Fewer instructions is not faster; the extra variable live across the loop
      // costs more than the call it saves. Do not "simplify" this again without timing it.
      std::size_t match_start {start};
      if (mode == run_mode::search) {
        while (match_start < text.size() && width(match_start) == 0) {
          ++match_start;
        }
      }
      if (match_start >= text.size()) {
        return fail();
      }
      const std::size_t first_width {width(match_start)};
      if (first_width == 0) {
        return fail();
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
            // SWAR: the next ASCII stop bounds the whole run (an ASCII byte can never lie inside
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
        return fail();
      }
      ensure_slot_size(out_slots, 2);
      out_slots[0] = match_start;
      out_slots[1] = match_end;
      return true;
    }

    /*!
     * \brief Multi-literal search via the automaton \ref ac_ready hands back, cached per regex in
     *        \ref detail::regex_immutables.
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
     * `noinline`, deliberately NOT `cold` — same reasoning as \ref ac_ready (called on
     * every AC-eligible search, so it must stay fully optimized); only kept OUT of `run()`'s own
     * body, which is what an isolation A/B on that ISA traced the negated-class regression
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
      // Called only from the dispatch site's own state_.ac_state.has_value() guard, but that
      // invariant is invisible across the call boundary to static analysis -- an explicit local
      // check keeps this function's own contract self-contained (defensive, not defensive-in-name).
      const ac_automaton* const ac {ac_ready()};
      if (ac == nullptr) {
        out_slots.assign(2, npos);
        return false;
      }
      const auto wb_ok = [&](std::size_t s, std::size_t e) { return wb_boundaries_ok(s, e); };
      const auto m {ac->search(text, start, wb_ok)};
      if (!m.matched) {
        out_slots.assign(2, npos);
        return false;
      }
      // Span-only (slot_count 2) — ensure_size, no npos fill; both slots rewritten.
      ensure_slot_size(out_slots, 2);
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
     *
     * \note **At density this route is almost entirely per-match RETURN, and that is measured rather
     *       than inferred.** Holding the pattern and the bytes fixed and varying ONLY how often a match
     *       must be emitted fits a straight line across five densities: a constant per match of return,
     *       plus a scan cost for the whole subject that is negligible beside it. Even a sparse-prose row
     *       is mostly return -- which is why it loses to the backtracking references while its own scan
     *       is nearly free.
     *
     *       So the opportunity here is a BATCH FILLER, exactly as for the class routes, and the
     *       recoverable amount is the one the class routes actually recovered when batched -- most of the
     *       per-match constant. Not attempted yet, and two things make it the heaviest
     *       item on that list rather than the obvious next one -- the search body below is a SIMD
     *       block scan with a carried mask plus a scalar tail plus a non-SIMD fallback, so a filler
     *       reproduces all three; and a new filler body is the change shape that charged unrelated
     *       rows every time it was tried during the batching work (docs/MEASUREMENT.md §5.4, §5.5).
     *       Judge it on BOTH instruments if it is built.
     */
    template <typename OutSlots>
    constexpr bool run_alternation(std::string_view text,
                                   std::size_t      start,
                                   run_mode         mode,
                                   OutSlots&        out_slots)
    {
      // Success rewrites both span slots; fail assigns for seam/general-path parity.
      const auto write_span = [&](std::size_t s, std::size_t e) {
                                ensure_slot_size(out_slots, 2);
                                out_slots[0] = s;
                                out_slots[1] = e;
                              };
      const auto fail = [&]() {
                          out_slots.assign(2, npos);
                          return false;
                        };
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
          return fail();
        }
        write_span(start, match_end);
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
              write_span(pos + lane, me);
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
              write_span(pos, me);
              return true;
            }
          }
        }
        return fail();
      }
#endif
      if (!fast_search(text, start, [&](std::size_t match_start) { return match_at(match_start, false); }, out_slots)) {
        return fail();
      }
      return true;
    }

    /*!
     * \brief Fills up to \p cap `fixed_alternation` matches from \p start without leaving the route.
     *
     * The measurement that motivates it is recorded on \ref run_alternation -- holding the pattern and the
     * bytes fixed and varying only how often a match must be emitted fits a per-match constant of return
     * against a whole-subject scan cost that is negligible beside it. This route was even more
     * return-dominated than the class routes the same filler treatment already rescued.
     *
     * Scope is the SMALL-SET shape only (2..8 distinct branch first bytes, \ref
     * pattern_hints::small_set_size), which is what the mask scan below needs. An alternation outside
     * that range takes `run_alternation`'s `fast_search` fallback, and the caller declines to batch it
     * rather than have this body grow a second scan -- adding bodies to this translation unit is the
     * change shape that charged unrelated rows repeatedly during the batching work
     * (docs/MEASUREMENT.md §5.4).
     *
     * The scan is deliberately a COPY of run_alternation's rather than a refactor of it. Both were
     * available; the existing route is measured and working, and relocating its hot body risks a
     * regression there that would be worse than this filler's whole gain. If the duplication proves
     * costly on either instrument, the refactor is the fallback, not the other way round.
     *
     * \param[in]  text  The subject.
     * \param[in]  start Where to begin.
     * \param[out] out   Buffer for the spans found.
     * \param[in]  cap   Capacity of \p out; the walk stops there and resumes from the last end.
     * \return How many spans were written.
     */
    constexpr std::size_t fill_alternation_spans(std::string_view text,
                                                 std::size_t      start,
                                                 cp_span*         out,
                                                 std::size_t      cap)
    {
      const auto& code {prog_.code};
      // Leftmost-FIRST among branches, read from the split chain in source order, exactly as
      // run_alternation's own `match_at` does -- same helper calls, same order, same wb test.
      const auto match_at = [&](std::size_t match_start) -> std::size_t {
                              std::size_t pc {prog_.hints.body_pc == 0
                                                ? std::size_t {1}
                                                : static_cast<std::size_t>(prog_.hints.body_pc)};
                              while (true) {
                                const bool        is_split {code[pc].op == opcode::split};
                                const std::size_t branch   {is_split
                                                            ? static_cast<std::size_t>(code[pc].primary_target)
                                                            : pc};
                                const std::size_t me {match_byte_klass_run(text, branch, match_start)};
                                if (me != npos && wb_boundaries_ok(match_start, me)) {
                                  return me;
                                }
                                if (!is_split) {
                                  return npos;
                                }
                                pc = static_cast<std::size_t>(code[pc].secondary_target);
                              }
                            };
      const std::size_t           cnt {prog_.hints.small_set_size};
      std::array<std::uint8_t, 8> mem {};
      for (std::size_t i = 0; i < cnt; ++i) {
        mem[i] = static_cast<std::uint8_t>(prog_.hints.small_set[i]);
      }
      const std::size_t sz  {text.size()};
      std::size_t       n   {0};
      std::size_t       pos {start};
      while (pos < sz && n < cap) {
        std::size_t hit {npos};
        std::size_t end {npos};
        // Block scan. A match may end inside a later block, so the walk always RESUMES from the
        // match end rather than continuing the current mask -- the mask's carry is worth having
        // within a block of misses, not across an emitted span.
        //
        // GUARDED, and the guard is not decoration: `mask_t` and its accessors only exist behind this
        // condition (simd.hpp), and the first version of this filler copied run_alternation's block
        // WITHOUT copying its `#if`. Every CI leg here defines one of the two macros, so the omission
        // was invisible locally and on both benchmark legs; a translation unit with neither failed to
        // compile with "'mask_t' was not declared in this scope". Where the block is absent the scalar
        // loop below covers the whole subject rather than only a tail -- same answers, no vectors.
#if defined(__ARM_NEON) || defined(__SSE2__)
        if (!std::is_constant_evaluated()) {
          for (; pos + 16 <= sz; pos += 16) {
            std::array<std::uint8_t, 16> buf {};
            std::memcpy(buf.data(), text.data() + pos, 16);
            mask_t mask                      {load_members_mask(buf.data(), mem.data(), cnt)};
            while (!empty(mask)) {
              const std::size_t lane {first_lane(mask)};
              const std::size_t me   {match_at(pos + lane)};
              if (me != npos) {
                hit = pos + lane;
                end = me;
                break;
              }
              mask = clear_first(mask);
            }
            if (hit != npos) {
              break;
            }
          }
        }
#endif
        if (hit == npos) {
          // The whole scan when no vector block ran (no SIMD, or constant evaluation), the tail
          // otherwise -- run_alternation draws the same boundary.
          for (; pos < sz; ++pos) {
            const std::uint8_t b      {static_cast<std::uint8_t>(text[pos])};
            bool               member {false};
            for (std::size_t i = 0; i < cnt; ++i) {
              if (b == mem[i]) {
                member = true;
                break;
              }
            }
            if (member) {
              const std::size_t me {match_at(pos)};
              if (me != npos) {
                hit = pos;
                end = me;
                break;
              }
            }
          }
        }
        if (hit == npos) {
          break;
        }
        out[n] = cp_span {.start = hit, .end = end};
        ++n;
        // An alternation branch is a non-empty byte/klass run, so `end > hit` always and the walk
        // cannot stall; the find_iter empty-match rule has nothing to apply here.
        pos = end;
      }
      return n;
    }

    /*!
     * \brief Is the exact-literal route the one `run()` would take, in its one-search subset?
     *
     * Mirrors `run()`'s cascade for the same reason lazy_dfa_is_the_route does. Only three kinds of route
     * sit ABOVE this one -- the byte-class loop, the code-point class loop, and the three possessive
     * loops -- so the list is short; everything below it (inner literal, fixed shape, the DFAs) is
     * territory this route already wins and must keep.
     *
     * The `literal_one_search` bit carries the rest by itself: it is set only when the program has no
     * capture, no assertion, no anchor, a literal of two bytes or more, and `prefix_size ==
     * exact_literal_len`. That is exactly the subset where the answer is `find_prefix` plus two stores,
     * with nothing to confirm and no occurrence to retry.
     *
     * \param[in] hints The program's shape hints.
     * \return True when no earlier route in the cascade claims this shape.
     */
    [[nodiscard]] static constexpr bool exact_literal_is_the_route(const pattern_hints& hints) noexcept
    {
      return hints.exact_literal_len > 0
             && hints.literal_one_search                         // no capture / assertion / anchor, len >= 2
             && hints.greedy_class_loop < 0                      // the byte-class loop sits above
             && hints.greedy_cp_class < 0                        // so does the code-point one
             && hints.possessive_class.kind == class_kind::none; // and the three possessive loops
    }

    /*!
     * \brief A two-slot sink, for a filler that must call a route function expecting a slot container.
     *
     * The batched routes all arm on `slot_count == 2`, so the whole-match span is every slot the program
     * has. This exists so \ref fill_inner_literal_spans can reuse \ref run_inner_literal verbatim -- one
     * mechanism rather than a copy of its confirm logic -- without pulling `real::detail::small_vec` into
     * this header, which `storage.hpp` cannot do (it includes this one), and without putting a 256-byte
     * inline buffer on `refill_batch`'s frame.
     *
     * It satisfies exactly what the route touches: `assign`, `resize`, `size` and indexing.
     */
    struct slot_pair
    {
      std::size_t slots[2] {npos, npos}; //!< `[0]` is the match start, `[1]` the match end.

      /*!
       * \brief Sets both slots to \p value.
       * \param[in] n     The route's `slot_count`; must be 2, and is ignored.
       * \param[in] value The value written to both slots (the route passes \ref real::npos).
       */
      constexpr void assign(std::size_t n,
                            std::size_t value) noexcept
      {
        static_cast<void>(n);
        slots[0] = value;
        slots[1] = value;
      }

      /*!
       * \brief No-op: the pair is already at its final size.
       * \param[in] n The requested size; must be 2, and is ignored.
       */
      constexpr void resize(std::size_t n) noexcept
      {
        static_cast<void>(n);
      }

      /*!
       * \brief The slot count.
       * \return Always 2.
       */
      [[nodiscard]] constexpr std::size_t size() const noexcept
      {
        return 2;
      }

      /*!
       * \brief Slot access.
       * \param[in] i Slot index, 0 or 1.
       * \return A reference to that slot.
       */
      [[nodiscard]] constexpr std::size_t& operator[](std::size_t i) noexcept
      {
        return slots[i];
      }

      /*!
       * \brief Slot access, const.
       * \param[in] i Slot index, 0 or 1.
       * \return A const reference to that slot.
       */
      [[nodiscard]] constexpr const std::size_t& operator[](std::size_t i) const noexcept
      {
        return slots[i];
      }
    };

    /*!
     * \brief Is the inner-literal route the one `run()` would take for this program?
     *
     * Mirrors that route's own gate, and only the routes with their own `run_*` body above it in the
     * cascade -- the two class loops, the three possessive loops, the exact literal. A scan STRATEGY is
     * not a route: that distinction is what the lazy-DFA predicate got wrong at first (see there), and
     * nothing of the kind applies here anyway.
     *
     * `prefix_code` is required non-empty unconditionally, which is conservative rather than exact: the
     * gate requires it only where the state confirms by reverse, which is the dynamic storage. A static
     * regex with an inner literal and no prefix program therefore keeps the per-match walk. Batching it is
     * separate work with its own measurement, not a widening of this line.
     *
     * \param[in] prog The compiled program view.
     * \return True when no earlier route in the cascade claims this shape.
     */
    [[nodiscard]] static constexpr bool inner_literal_is_the_route(const program_view& prog) noexcept
    {
      const pattern_hints& hints {prog.hints};
      return hints.inner_literal_len > 0
             && hints.inner_literal_prefix >= 1                  // a literal at offset 0 is a PREFIX and keeps find_prefix
             && !hints.fixed_shape                               // the route's own gate: IL never beats the fixed-shape scan
             && !prog.prefix_code.empty()                        // the reverse start-finder must exist
             && hints.exact_literal_len == 0                     // exact-literal search sits above
             && hints.greedy_class_loop < 0                      // the byte-class loop sits above
             && hints.greedy_cp_class < 0                        // so does the code-point one
             && hints.possessive_class.kind == class_kind::none; // and the three possessive loops
    }

    /*!
     * \brief Is the lazy-DFA route the one `run()` would actually take for this program?
     *
     * MIRRORS `run()`'s CASCADE, and it has to: a batched walk bypasses `run()` entirely, so batching a
     * shape that some EARLIER route claims does not merely fail to help, it takes the pattern off a
     * faster route. Written first as "whatever the four shape recognizers did not claim", which cost
     * an exact-literal row heavily, well above its own floor at every paired draw: a
     * plain literal has no class loop and no fixed alternation, so it fell through to here and left its
     * `memmem` behind. The conditions below are therefore stated positively, one per route that sits
     * above the lazy DFA in the cascade, and NOT as a residue.
     *
     * \warning **Adding a route to `run()` above the lazy DFA means adding its hint here.** Nothing
     *          enforces that automatically; the failure mode is a silent slowdown on exactly the shape
     *          the new route was written for, and the only instrument that sees it is a per-row layout
     *          judgement on a row that exercises that shape.
     *
     * \param[in] hints The program's shape hints.
     * \return True when no earlier route in the cascade claims this shape.
     */
    [[nodiscard]] static constexpr bool lazy_dfa_is_the_route(const pattern_hints& hints) noexcept
    {
      // WHAT BELONGS HERE IS A ROUTE, NOT A SCAN STRATEGY, and the first version of this predicate confused
      // the two. `rare_disc` and a literal `prefix_size` are branches of \ref next_candidate -- the
      // candidate-scan this filler itself calls, whose per-haystack sticky state is reset inside that same
      // function -- so excluding them declined shapes that take THIS route anyway and get their scan for
      // free: `https?://` (rare_disc = 58, prefix_size = 4) billed 0.996 engine entries per match while
      // both clauses stood. What remains is one clause per route with its own `run_*` body above this one
      // in the cascade.
      return hints.exact_literal_len == 0     // exact-literal search (`charlie`)
             && hints.inner_literal_len == 0  // inner-literal memmem (`\d+\.\d+`)
             && !hints.literal_one_search     // single-occurrence literal walk
             && !hints.fixed_shape            // fixed-width shape (`[0-9a-f]{8}`)
             && hints.fs_pair_width == 0      // the fixed-shape PAIR route
             && !hints.fixed_alternation;     // run_alternation / Aho-Corasick territory
    }

    /*!
     * \brief Fills up to \p cap exact-literal matches from \p start without re-entering the route gate.
     *
     * REOPENS A DOCUMENTED REFUSAL, and the reason is recorded in \ref run_literal_one_search — this filler
     * was written, measured and refused once. It was never wrong -- `exhaustive-compat` was byte-identical
     * over 3 218 434 cases and a both-ways differential agreed on every span -- and it read `literal`
     * heavily at every paired draw. It was refused for what it charged elsewhere: five rows above their
     * own floors at every draw, with nearly every row in the panel leaning positive.
     * The mechanism was pinned by machine code rather than argued -- no scan loop changed; `refill_batch`
     * grew 379 -> 389 instructions and `count_matches` 610 -> 606, and `count_matches` is what every row
     * measures -- and the note closes by saying a reopening needs a filler that does not enlarge
     * `refill_batch`.
     *
     * What reopens it is not a cheaper flag but a CONTRARY MEASUREMENT: a fifth route was since added to
     * `refill_batch`, enlarging it, and the judgement showed no cross-row toll at all (12 of 19 medians
     * positive, p = 0.36). What charged the rows in that work was the shape of `advance`'s HOT path -- one
     * extra comparison there cost most of the panel's rows at a significant p, and moving it into the branch
     * reached once per walk removed it entirely. So "enlarging refill_batch charges every row" is not a
     * law, and the original refusal deserves one re-test under the current shape.
     *
     * NO PARTIAL STATE, unlike the lazy-DFA filler: `find_prefix` scans to the end of the subject, so an
     * empty return means no occurrence remains anywhere ahead. Exhaustion is proven, and the caller's
     * "empty buffer ends the walk" reading is correct here.
     *
     * \param[in]  text  The subject.
     * \param[in]  start Where to begin.
     * \param[out] out   Buffer for the spans found.
     * \param[in]  cap   Capacity of \p out; the walk stops there and resumes from the last end.
     * \return How many spans were written.
     */
    std::size_t fill_exact_literal_spans(std::string_view text,
                                         std::size_t      start,
                                         cp_span        * out,
                                         std::size_t      cap)
    {
      const std::size_t      len {static_cast<std::size_t>(prog_.hints.exact_literal_len)};
      const std::string_view lit {prog_.hints.prefix.data(), len};
      std::size_t            n   {0};
      std::size_t            pos {start};
      while (n < cap) {
        const std::size_t cand {find_prefix(text, pos, lit)};
        if (cand == npos) {
          break;
        }
        out[n] = cp_span {.start = cand, .end = cand + len};
        ++n;
        // Non-overlapping, as the walk requires. The hint guarantees len >= 2, so the position always
        // advances and the loop cannot stall on a zero-width answer.
        pos = cand + len;
      }
      return n;
    }

    /*!
     * \brief Fills up to \p cap inner-literal matches from \p start without re-entering the route gate.
     *
     * The route bills **one engine entry per match** (1.001 on a prose corpus) where every batched route
     * bills one per `batch_cap`, and the cost is that return rather than the scan: the per-match figure is
     * flat across densities, which is what a per-match CONSTANT looks like, and at the dense end that
     * constant is essentially the whole row.
     *
     * IT CALLS \ref run_inner_literal RATHER THAN COPYING IT, which is the opposite of what
     * \ref fill_alternation_spans chose, and for a reason that differs in kind: that filler's twin is a
     * mask scan whose hot body relocating would risk the working route, while this route's per-call work is
     * a linearity backstop, a density guard, a warm/cold size floor and a reverse confirm -- four pieces of
     * state whose duplication is exactly how a batched walk and a per-match walk come to disagree. The
     * route is already written to be re-entered per match in a walk (its own comment calls `start` "the
     * finditer resume"), so calling it in a loop asks nothing new of it. The per-haystack reset is shared
     * through \ref il_reset_on_new_haystack for the same reason.
     *
     * \p partial follows the lazy-DFA filler's contract, and this route needs it more: it ABANDONS -- on
     * the density guard, on the linearity backstop, on the size floor, or when there is no way to place a
     * candidate's start -- and every one of those leaves matches ahead that another route will find. Only a
     * memmem that ran out of candidates proves exhaustion, and that is the one branch which clears it.
     *
     * \param[in]  text    The subject.
     * \param[in]  start   Where to begin.
     * \param[out] out     Buffer for the spans found.
     * \param[in]  cap     Capacity of \p out; the walk stops there and resumes from the last end.
     * \param[out] partial True unless the subject was proven spent; see above.
     * \param[out] disarm  Set when the route has ABANDONED this haystack, meaning every further attempt
     *                      on it is wasted work. The caller must then stop calling this filler for the
     *                      rest of the walk. Without it the walk pays one failed refill per match on top
     *                      of the real work: measured 3634 attempts against 7 on the veto matrix's dense
     *                      date cell, which took `date dense` from 2.617 to 2.883 -- the route slower
     *                      than the core it replaces, which is exactly what that cell vetoes. The sticky
     *                      abandon was doing its job; the walk was not listening.
     * \return How many spans were written.
     */
    std::size_t fill_inner_literal_spans(std::string_view text,
                                         std::size_t      start,
                                         cp_span        * out,
                                         std::size_t      cap,
                                         bool           & partial,
                                         bool           & disarm)
    {
      partial = true;
      // Same guard as il_reset_on_new_haystack, and the same reason: a static tier that does not want IL
      // has no `il_abandoned` to read. Such a storage never arms this route either (the caller's condition
      // needs a `prefix_code`, which those programs do not carry), so declining here is unreachable rather
      // than a behaviour choice -- it exists to keep the template well-formed.
      if constexpr (!requires(State & st) {
        st.il_abandoned;
      }) {
        static_cast<void>(text);
        static_cast<void>(start);
        static_cast<void>(out);
        static_cast<void>(cap);
        disarm = true;
        return 0;
      }
      else {
        // NO RESET HERE. `run()`'s gate owns the per-haystack reset, and doing it here too makes the two
        // callers ping-pong: this filler cleared an abandon the gate had just set, so a route that had
        // given up on this haystack was retried on EVERY match. Measured on the veto matrix's dense date
        // cell: 3637 entries against 7, and `date dense` went 2.617 -> 2.883 (route slower than the core
        // it replaces, which is what that cell vetoes). Declining until the gate resets costs one wasted
        // refill per haystack.
        if (state_.il_abandoned) {
          disarm = true; // and the caller stops asking: see \p disarm
          return 0;
        }
        slot_pair   scratch;
        std::size_t n   {0};
        std::size_t pos {start};
        while (n < cap && pos <= text.size()) {
          bool       abandon {false};
          const bool matched {run_inner_literal(text, pos, scratch, abandon, /*density_gate=*/ false)};
          if (abandon) {
            return n;        // a guard tripped; partial stays set and the caller re-enters through the gate
          }
          if (!matched) {
            partial = false; // PROVEN spent: memmem found no further candidate anywhere ahead
            return n;
          }
          out[n] = cp_span {.start = scratch[0], .end = scratch[1]};
          ++n;
          // A match contains the required literal, so it is never empty and the walk cannot stall; the
          // find_iter empty-match rule has nothing to apply.
          pos = scratch[1];
        }
        return n;
      }
    }

    /*!
     * \brief Fills up to \p cap lazy-DFA matches from \p start without re-entering the route gate.
     *
     * The fifth batched route, and the one the other four made conspicuous. A pattern whose branches are
     * not all literals -- `[a-z]+|[0-9]+`, the plain tokenizer idiom -- matches none of the four
     * shape recognizers and lands here, where it was billing ONE engine entry per match against a quarter
     * of one for every batched route -- and running an order of magnitude slower than a plain class loop
     * for the same match count. The excess fits a per-match constant at two independent densities, which
     * is the tell: the DFA scan is not the cost, the return is.
     *
     * ONLY THE ANCHORED-FROM-CANDIDATE SUB-SCAN, deliberately. \ref try_shared_lazy_dfa_search has a
     * second sub-scan (`forward_end` then `reverse_start`) for when the first bytes do not carry the
     * search, and reproducing it here would put a second body in this translation unit -- the change
     * shape that repeatedly charged unrelated rows during the batching work (docs/MEASUREMENT.md §5.4).
     * Declining it costs nothing, because of \p partial.
     *
     * WHY \p partial EXISTS, and why the other four fillers need no such thing. Returning zero spans is
     * how a filler says "the subject is spent", and \ref basic_match_iterator::advance ends the walk on
     * it. For the four shape routes that is sound: their scan covers the whole subject, so nothing found
     * means nothing there. This route can stop with matches still to come -- the fallback sub-scan's
     * territory, fewer than \ref lazy_dfa_min_input bytes left, no shared DFAs built yet -- and ending
     * the walk there would drop them. So \p partial is set unless exhaustion was PROVEN (no candidate
     * remains in the whole subject), and the caller resumes on the per-match path, which re-enters the
     * full gate. Pessimistic by construction: only one branch clears it.
     *
     * \param[in]  text    The subject.
     * \param[in]  start   Where to begin.
     * \param[out] out     Buffer for the spans found.
     * \param[in]  cap     Capacity of \p out; the walk stops there and resumes from the last end.
     * \param[out] partial True unless the subject was proven spent; see above.
     * \return How many spans were written.
     */
    std::size_t fill_lazy_dfa_spans(std::string_view text,
                                    std::size_t      start,
                                    cp_span        * out,
                                    std::size_t      cap,
                                    bool           & partial)
    {
      partial = true;
      std::size_t n    {0};
      const bool  used {
        with_search_dfas([&](lazy_dfa& fwd, reverse_dfa& rev) {
                           // The reverse DFA serves the fallback sub-scan only, which this filler declines; naming it
                           // keeps the callback signature `with_search_dfas` hands out.
                           static_cast<void>(rev);
                           fwd.begin_scan();
                           std::size_t pos {start};
                           while (n < cap && pos <= text.size() && text.size() - pos >= lazy_dfa_min_input) {
                             std::size_t hit {npos};
                             std::size_t end {npos};
                             std::size_t c   {pos};
                             while (true) {
                               c = next_candidate(text, c, pos);
                               if (c > text.size()) {
                                 partial = false; // PROVEN spent: no candidate byte remains anywhere ahead
                                 return;
                               }
                               const auto anchored {fwd.anchored_end(text, c)};
                               prefilter_note_scan(anchored.scanned_to - c);
                               if (anchored.end != npos) {
                                 hit = c;
                                 end = anchored.end;
                                 break;
                               }
                               if (anchored.scanned_to >= text.size()) {
                                 return; // the fallback sub-scan's territory; partial stays set
                               }
                               ++c;
                             }
                             if (end == hit) {
                               // A zero-width match carries the find_iter empty-match rule (`forbid_empty_until_`),
                               // which the batched span path does not apply. The caller's arming condition already
                               // excludes a nullable pattern, so this is a belt rather than a road -- and if it ever
                               // trips, stopping is the answer that stays correct.
                               return;
                             }
                             out[n] = cp_span {.start = hit, .end = end};
                             ++n;
                             pos = end;
                           }
                         })};
      if (!used) {
        n = 0; // no shared DFAs on this regex yet: nothing found and nothing proven
      }
      return n;
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
      // Exact-literal programs write every live slot via save ops (whole-match + each capture);
      // group-0 end is always cand+len. Size once without a full npos fill (dead on find_iter reuse).
      // Assert-fail below still assign(npos) for seam/!matched consumers.
      ensure_slot_size(out_slots, prog_.slot_count);
      std::size_t consumed {};
      for (const instr& instruction : prog_.code) {
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
      if (prog_.slot_count >= 2) {
        out_slots[1] = cand + len; // group 0 end — always the full literal (unconditional)
      }
      return true;
    }

    /*!
     * \brief The whole exact-literal search in one \ref find_prefix, for a \ref
     *        pattern_hints::literal_one_search program (see \ref run_exact_literal's own call site for
     *        why each per-match step of the general loop is redundant there).
     *
     * `noinline` deliberately, and it is the *hot* path — not the usual cold-code reason. Keeping this
     * body inside \ref run_exact_literal grew that function, which shares an inlining unit with
     * \ref run and therefore with the class loops: `[^,]+` (\ref run_codepoint_class) measured a
     * reproducible regression from the growth alone, the same front-end codegen-luck hazard
     * documented on \ref run and fixed the same way (\ref run_class_loop_trailing_la,
     * `try_shared_lazy_dfa_search`). Out of line, `[^,]+` returns to its exact pre-change ns/B while
     * this path keeps its win — the one measured cost is a 9-byte literal giving back ~3 points of a
     * gain nearly intact -- out of line it keeps almost all of what inlining bought, and one literal row
     * is identical either way. Restoring a common route beats the last points of an uncommon one.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin searching at.
     * \param[in]  len       The literal's length (`hints.exact_literal_len`, >= 2 by the hint).
     * \param[out] out_slots Receives `[cand, cand + len]` on success.
     * \return `true` if the literal occurs at or after \p start.
     *
     * \note **A span filler for this shape WAS refused, and is now in place — the refusal was overturned by
     *       measurement, not by argument.** The history is kept because it is the clearest case this
     *       repository has of a refusal that was right when taken and wrong later, and of what changed. The
     *       route bills one entry per match (`dog`: 2001 entries against 2000 matches) where every batched
     *       route bills one per `batch_cap`, and this subset is the ideal candidate: the
     *       `literal_one_search` hint already excludes captures, assertions, anchors and one-byte
     *       literals, so a filler is `find_prefix` plus two stores, with no confirm and no retry.
     *       Correctness was never the problem — `exhaustive-compat` returned byte-identical counts
     *       (3 218 434 cases, 4 548 documented divergences, 0 serious) and a both-ways differential over
     *       the batch seam agreed on every span.
     *
     *       The trade is what settles it, and it is lopsided in BOTH directions: one row gains heavily
     *       while most of the others lose a little, each above its own floor. The gain lands on a row
     *       already ahead of the backtracking references; the costs land on rows near parity with them,
     *       several of which are recent wins.
     *
     *       **The mechanism was then pinned by comparing machine code rather than argued, and it is not
     *       the diffuse "per-unit inline budget" this note first blamed.** Of 398 function bodies in the
     *       consumer unit, five changed and NONE of them is a scan loop: every filler, and `advance`,
     *       are byte-identical. What moved is `refill_batch` (379 → 389 instructions), the iterator's
     *       constructor, and `count_matches` (610 → 606) — and `count_matches` is what
     *       `benchmarks/bench_minimal.cpp` measures for every row. So the rows that "regressed" do not do
     *       more work; the shared entry point they all pass through was recompiled.
     *
     *       Two follow-ups were tried against that mechanism and both failed, which is why the refusal
     *       stood at the time rather than waiting on one more idea. Folding the flag away cannot help: the
     *       added `bool` lands in existing padding, `sizeof` the iterator is unchanged at 8664 either way.
     *       Replacing the dispatch chain with a `switch` on a dense enum does not help either — clang
     *       emits a branch tree rather than a jump table, and the variant reproduced the SAME
     *       379 → 389 and 610 → 606 for no gain at all. Outlining the constructor's cold eligibility
     *       half (\ref real::basic_match_iterator::decide_batching) kept `count_matches` byte-identical on
     *       its own but NOT with this filler on top, so the note closed by asking for a filler that does
     *       not enlarge `refill_batch`.
     *
     *       **WHAT OVERTURNED IT.** Not a cheaper flag: the diagnosis was right and the condition it named
     *       came true on its own. `count_matches` has since been cut from 610 instructions to 377 — its two
     *       cold halves were outlined (`decide_batching`, and the trailing-lookaround walk's counter) for
     *       unrelated reasons — and at that size the filler no longer moves it at all. Re-measured on the
     *       machine-code instrument first, as this note's own method requires: of 407 function bodies in the
     *       consumer unit, THREE change size — `refill_batch` 391 → 401, the cold `decide_batching`
     *       160 → 187, and `count_matches` **377 → 377**. Enlarging `refill_batch` was never the mechanism;
     *       recompiling the entry point every row measures was.
     *
     *       The layout judgement then agreed, 25 rows against recalibrated floors, 24 paired draws:
     *       the exact-literal row heavily at every paired draw, the ONLY row judged REAL, and the five
     *       rows the first attempt charged are now indistinguishable from zero — every one
     *       indistinguishable. No cross-row toll either: 13 of 21 medians positive, p = 0.38, against
     *       14 of 15 leaning positive the first time. A fifth batched route had also been added to
     *       `refill_batch` shortly before, enlarging it, and charged nothing measurable — which is what
     *       made re-testing this defensible rather than hopeful.
     */
    template <typename OutSlots>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool run_literal_one_search(std::string_view text,
                                std::size_t      start,
                                std::size_t      len,
                                OutSlots&        out_slots)
    {
      const std::size_t cand {find_prefix(text, start, std::string_view(prog_.hints.prefix.data(), len))};
      if (cand == npos) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      ensure_slot_size(out_slots, prog_.slot_count); // find_prefix guarantees cand + len <= text.size()
      out_slots[0] = cand;
      out_slots[1] = cand + len;
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
     *
     * \note **A one-byte whole-pattern literal is NOT redirected to the batched single-class route,
     *       and that is measured rather than an oversight.** `e` and `[e]` are the same language, and
     *       `single_class` is batched where this route is not, so the redirect looks free -- the same
     *       argument that made the bare-possessive redirect a clear win. It is not free here, because
     *       which route wins depends on the SUBJECT, not the pattern: for a byte that occurs often the
     *       batched class wins by a wide margin, and for a byte that occurs rarely the literal wins --
     *       because `memchr` skips whole regions, which is worth more than batching when matches are
     *       rare. Sparse one-byte literals are at least as common as dense ones, so a blanket redirect
     *       would trade a large dense win for a real sparse loss.
     *
     *       So the shape of the answer is a DENSITY GATE -- what \ref ac_density_favours_automaton
     *       already is for Aho-Corasick -- not a recognition-time redirect. That is a design of its
     *       own, needing its own threshold measurement, and it is not attempted here.
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
      // One-search path: ONE `find_prefix` answers the whole search, because each per-match step the
      // general loop below takes is provably redundant for a `literal_one_search` program (the compiler
      // folded the eligibility into that one bit -- see pattern_hints::literal_one_search):
      //   * next_candidate's hint chain would take its `prefix_size >= 2` branch and call this very
      //     find_prefix (anchored_start / line_anchored / rare_disc are the only earlier branches, and
      //     the hint excludes all three);
      //   * literal_at would re-memcmp the bytes find_prefix just matched (same hints.prefix, and the
      //     hint requires prefix_size == exact_literal_len, so the whole literal was matched);
      //   * replay_literal would walk the whole program to rediscover a per-match-invariant answer --
      //     with no assert_position to evaluate and slot_count == 2, the saves it writes are exactly
      //     [0] = cand and [1] = cand + len.
      // Everything the hint excludes (a group, any assertion, an anchor, a 1-byte literal) keeps the
      // general loop below verbatim, so no shape loses its retry-on-assertion-failure behaviour.
      if (!std::is_constant_evaluated() && prog_.hints.literal_one_search && prog_.slot_count == 2) {
        return run_literal_one_search(text, start, len, out_slots);
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
      // Rare discriminant (URL `https?://…`): memchr the rare mid-byte, back-verify optional
      // prefix — prefer over a weak literal prefix (`http`) when armed. Meta-seam for differentials.
      // Runtime-only: the seam is not constexpr (same shape as IL / lazy-DFA toggles).
      // Density abandon (sticky per haystack): dense `:` filler makes memchr+verify lose to prefix.
      if (!std::is_constant_evaluated() && hints.rare_disc >= 0 && !rare_disc_route_disabled()) {
        bool use_disc {true};
        if constexpr (requires(State & s) {
          s.rare_disc_abandoned;
        }) {
          if (state_.rare_disc_text != static_cast<const void*>(text.data())) {
            state_.rare_disc_abandoned = false;
            state_.rare_disc_text      = static_cast<const void*>(text.data());
          }
          use_disc = !state_.rare_disc_abandoned;
        }
        if (use_disc) {
          bool              density_abandon {false};
          const std::size_t cand            {find_rare_disc_candidate(text, pos, hints, &density_abandon)};
          if (density_abandon) {
            if constexpr (requires(State & s) {
              s.rare_disc_abandoned;
            }) {
              state_.rare_disc_abandoned = true;
            }
            // Fall through to prefix / first-byte below for this candidate.
          }
          else {
            return cand;
          }
        }
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
     *
     * \param[in] pos        Boundary position.
     * \param[in] ascii_word Restrict word-ness to ASCII (`re.A` / bytes mode).
     * \return `true` when the preceding code point is a word character.
     */
    [[nodiscard]] constexpr bool word_before(std::size_t pos,
                                             bool        ascii_word) const
    {
      return real::detail::word_before(text_, pos, ascii_word); // shared free function (assert_eval.hpp)
    }

    /*!
     * \brief Word-ness of the code point **starting at** \p pos — the right side of a boundary. False
     *        at the text end or on a malformed sequence; bytes / `re.A` stay byte-level.
     * \param[in] pos        Boundary position.
     * \param[in] ascii_word Restrict word-ness to ASCII (`re.A` / bytes mode).
     * \return `true` when the following code point is a word character.
     */
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
                  cp_class_matches_idx(instruction.arg16, dc.cp)) {
                advance_thread(clist, nlist, i,
                               pc + 1 + static_cast<std::int32_t>(4 - dc.length), pos + 1);
              }
            }
            break;
          case opcode::byte_loop_possessive:
          case opcode::klass_loop_possessive:
            // Tier 1: by the time a leaf reaches step(), add_thread's closure
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
              // The winning thread's group-0 span. Capture-free: the start is what the thread carries and
              // the end IS `pos` -- the walk that pushed this thread ran at this same position, which is
              // what `save 1` would have recorded. Otherwise: read the COW block.
              const bool               cf   {prog_.hints.capture_free_walk};
              const std::size_t* const won  {cf ? nullptr : thread_slots(clist, i)};
              const std::size_t        won0 {cf ? clist.slots[i] : won[0]};
              const std::size_t        won1 {cf ? pos : won[1]};
              // Reject an empty match forbidden at this position; a lower-priority
              // thread may still consume a byte and win a non-empty match here.
              if (pos == won0 && won0 < forbid_empty_until_) {
                break;
              }
              if (sem_ == match_semantics::longest) {
                // Leftmost-longest (POSIX / RE2 set_longest_match): keep the leftmost start, then the longest end
                // at that start. Record only a strictly-better match and do NOT cut — a lower-priority or later
                // thread may still extend it. Seeding has already stopped (matched), so no start past the
                // leftmost survives. A lazy quantifier therefore behaves greedily here (the longest end wins).
                const bool better {!matched
                                   || won0 < out_slots[0]
                                   || (won0 == out_slots[0] && won1 > out_slots[1])};
                if (better) {
                  if (cf) {
                    out_slots[0] = won0;
                    out_slots[1] = won1;
                  }
                  else {
                    for (std::uint16_t s = 0; s < slot_count; ++s) {
                      out_slots[s] = won[s];
                    }
                  }
                }
                matched = true;
                break; // the match thread dies; the rest of the list and later positions may lengthen it
              }
              if (cf) {
                out_slots[0] = won0;
                out_slots[1] = won1;
              }
              else {
                for (std::uint16_t s = 0; s < slot_count; ++s) {
                  out_slots[s] = won[s];
                }
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
     * \brief Tier 1's on-match capture write: if \p capture_start_slot is not -1, records
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
      if (capture_start_slot < 0 || prog_.hints.capture_free_walk) {
        // Capture-free: `clist.slots[i]` is group 0's START, not a block handle. Handing it to `cow_write`
        // would read a refcount off an offset. The second half of the guard was implicit while the flag
        // could only be set by the compiler — that guard demands `slot_count == 2`, so an armed Tier 1
        // capture and the flag could not coexist — and is written out because a CALLER may now set the
        // flag on a pattern that does have groups (\ref real::basic_regex::count_matches). Nothing is
        // lost: on such a walk no capture is read.
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
     *        \p nlist (COW). The closure takes its own reference on the thread's capture block — no slot
     *        copy; the block is shared until a `save` copies it on write.
     *
     * \param[in]     clist    Current list, holding the thread to advance.
     * \param[in,out] nlist    Next list, receiving the continuation's closure.
     * \param[in]     i        Thread index within \p clist.
     * \param[in]     next_pc  Program counter the thread continues at.
     * \param[in]     next_pos Text position the thread continues at.
     */
    constexpr void advance_thread(list_type&   clist,
                                  list_type&   nlist,
                                  std::size_t  i,
                                  std::int32_t next_pc,
                                  std::size_t  next_pos)
    {
      if (!prog_.hints.capture_free_walk) {
        state_.pool.incref(static_cast<std::uint32_t>(clist.slots[i])); // the new closure holds its own ref
      }
      // Capture-free: this is group 0's start, full width, and no ref exists to take.
      add_thread(nlist, next_pc, next_pos, clist.slots[i]);
    }

    /*!
     * \brief Pointer to thread \p i's `slot_count` capture values — its COW block's slots (COW). Used by
     *        the `match` case to read out the winner.
     *
     * \param[in] clist List holding the thread.
     * \param[in] i     Thread index within \p clist.
     * \return Pointer to the thread's first capture slot.
     */
    [[nodiscard]] constexpr const std::size_t* thread_slots(list_type&  clist,
                                                            std::size_t i)
    {
      return state_.pool.slots(static_cast<std::uint32_t>(clist.slots[i]));
    }

    /*!
     * \brief Tests a decoded code point against a `klass_cp` class: ASCII bitmap below 0x80; above,
     *        \ref cp_member_hi when a class index is known at runtime (page + sparse hi table), else
     *        pure binary search of the class's range slice (constexpr / const paths). The class is
     *        already the effective set, so this is a plain positive membership test.
     * \param[in] cc The code-point class (from `prog_.cp_classes`).
     * \param[in] cp The decoded code point.
     * \return Whether \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_class_matches(const detail::cp_class& cc,
                                                  char32_t                cp) const
    {
      if (cp < 0x80U) {
        return cc.ascii.test(static_cast<std::uint8_t>(cp));
      }
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
      return lo < static_cast<std::size_t>(cc.range_begin) + cc.range_count &&
             cp >= prog_.cp_ranges[lo].lo && cp <= prog_.cp_ranges[lo].hi;
    }

    /*!
     * \brief Membership by class index (ASCII + European page + sparse hi / bsearch).
     * \param[in] cp_index Index of the code-point class.
     * \param[in] cp       The decoded code point.
     * \return Whether \p cp is a member.
     */
    [[nodiscard]] constexpr bool cp_class_matches_idx(std::size_t cp_index,
                                                      char32_t    cp)
    {
      if (cp < 0x80U) {
        return prog_.cp_classes[cp_index].ascii.test(static_cast<std::uint8_t>(cp));
      }
      return cp_member_hi(cp_index, cp);
    }

    /*!
     * \brief Adds \p pc0 and its whole epsilon closure to \p list — the one closure walk (COW). Each
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
     * \param[in]     initial       What the seed carries: capture-free, group 0's START — full width, which
     *                              is why this is a `std::size_t` and not the `std::uint32_t` an `eps_entry`
     *                              field would have been. Otherwise the block the walk starts on, on which
     *                              the caller passes an already-owned ref.
     */
    constexpr void add_thread(list_type&   list,
                              std::int32_t pc0,
                              std::size_t  pos,
                              std::size_t  initial)
    {
      auto& pool  {state_.pool};
      auto& stack {state_.stack};
      // CAPTURE-FREE WALK (\ref pattern_hints::capture_free_walk): a thread's whole capture state is group
      // 0's start, so no refcounted block travels along and the pool is never touched. The start is a
      // full-width `std::size_t` LOCAL rather than a field of `eps_entry` for two reasons: that field is a
      // `std::uint32_t`, which would silently truncate an offset past 4 GiB, and widening it would grow the
      // per-call epsilon stack -- the `mark` experiment measured a comparable growth of this state against
      // the per-call rows. A single local is correct because `save 0` is the program's FIRST instruction
      // (the guard checks exactly that): every thread one call adds therefore shares one start, either the
      // one passed in or `pos` if the walk crossed the head.
      const bool  cf    {prog_.hints.capture_free_walk};
      std::size_t start {initial};
      stack.clear();
      stack.push_back({.pc = pc0, .block = cf ? 0U : static_cast<std::uint32_t>(initial)});
      while (!stack.empty()) {
        const auto          entry {stack.back()};
        stack.pop_back();
        const std::int32_t  pc    {entry.pc};
        const std::uint32_t block {entry.block}; // this frame owns 1 ref
        if (list.seen(pc)) {
          if (!cf) {
            pool.decref(block);
          }
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
            if (!cf) {
              detail::prof::tick_event(detail::prof::event::pool_incref);
              pool.incref(block); // one held ref -> two pushed frames
            }
            stack.push_back({.pc = instruction.secondary_target, .block = block});
            stack.push_back({.pc = instruction.primary_target, .block = block});
            break;
          case opcode::save:
            {
              if (cf) {
                // Slot 0 is the start; slot 1 is the end, which needs no storage -- it IS `pos` when the
                // `match` opcode is reached, because the walk that pushes a thread and the step that runs
                // it share one position.
                if (instruction.arg16 == 0U) {
                  start = pos;
                }
                stack.push_back({.pc = pc + 1, .block = block});
                break;
              }
              // The one write: copy-on-write off the block if shared, then record pos in the slot.
              detail::prof::tick_event(detail::prof::event::pool_cow_write);
              const std::uint32_t written {pool.cow_write(block, instruction.arg16, pos)};
              stack.push_back({.pc = pc + 1, .block = written});
            }
            break;
          case opcode::assert_position:
            if (assertion_holds(static_cast<assert_kind>(instruction.arg8), pos, instruction.arg16 != 0U)) {
              stack.push_back({.pc = pc + 1, .block = block});
            }
            else if (!cf) {
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
              else if (!cf) {
                pool.decref(block);
              }
            }
            else if (!cf) {
              pool.decref(block); // unreachable (this walk is instantiated only for the pool-bearing state)
            }
            break;
          case opcode::byte:
          case opcode::klass:
          case opcode::klass_cp:
          case opcode::match:
            list.pcs.push_back(pc);
            // Capture-free: the thread carries group 0's START. Otherwise the block handle, ref transferred.
            list.slots.push_back(cf ? start : static_cast<std::size_t>(block));
            break;
          case opcode::byte_loop_possessive:
            // Tier 1: the match/no-match decision is made HERE, at insertion
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
              list.slots.push_back(cf ? start : static_cast<std::size_t>(block));
            }
            else {
              stack.push_back({.pc = instruction.secondary_target, .block = block});
            }
            break;
          case opcode::klass_loop_possessive:
            if (pos < text_.size() &&
                prog_.classes[instruction.arg16].test(static_cast<std::uint8_t>(text_[pos]))) {
              list.pcs.push_back(pc);
              list.slots.push_back(cf ? start : static_cast<std::size_t>(block));
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
                matched_here = dc.valid && cp_class_matches_idx(instruction.arg16, dc.cp);
              }
              if (matched_here) {
                list.pcs.push_back(pc);
                list.slots.push_back(cf ? start : static_cast<std::size_t>(block));
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
     * \brief Releases the block references a list's threads hold (COW), before the list is reset or the
     *        run returns. This is the one decref site paired with the incref at each step→closure boundary
     *        — the classic double-free locus, kept single.
     *
     * \param[in] list List whose threads' block references are dropped.
     */
    constexpr void cow_release_blocks(list_type& list)
    {
      if (prog_.hints.capture_free_walk) {
        return; // the threads carry a start, not a block: there is no reference to drop
      }
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
      // Peephole: a single-width body compiles to exactly [one consuming op; match] (code_length 2). Test it
      // directly, skipping the sub-VM scaffolding entirely -- several times cheaper on the common
      // single-class assertion. Negation is over the RESULT (applied below), so an empty or boundary
      // position flips right.
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

    /*!
     * \brief L1 peephole — does the single consuming op \p body match the code point / byte AT \p pos (ahead)?
     *        Mirrors the per-op logic of \ref lookahead_matches for a one-instruction sub-program.
     * \param[in] body The sub-program's single consuming instruction.
     * \param[in] pos  Position the lookaround is evaluated at.
     * \return True when \p body accepts what starts at \p pos; false at the text end.
     */
    [[nodiscard]] constexpr bool single_class_ahead(const instr& body,
                                                    std::size_t  pos)
    {
      if (pos >= text_.size()) {
        return false; // nothing ahead: the body cannot match — (?=…) false / (?!…) true (negated by the caller)
      }
      if (body.op == opcode::klass_cp) {
        const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text_, pos)};
        return dc.valid && cp_class_matches_idx(body.arg16, dc.cp);
      }
      const auto b {static_cast<std::uint8_t>(text_[pos])};
      return body.op == opcode::byte ? b == body.arg8 : prog_.classes[body.arg16].test(b);
    }

    /*!
     * \brief L1 peephole — does \p body match the code point / byte ending EXACTLY at \p pos (behind)?
     *        The defining lookbehind trap: the match must END at \p pos, so the code point is the one whose
     *        aligned start s gives `s + length == pos` (byte mode: `pos - 1`).
     * \param[in] body The sub-program's single consuming instruction.
     * \param[in] pos  Position the lookaround is evaluated at.
     * \return True when \p body accepts the atom ending at \p pos; false at the text start.
     */
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
               && cp_class_matches_idx(body.arg16, dc.cp);
      }
      const auto b {static_cast<std::uint8_t>(text_[pos - 1])};
      return body.op == opcode::byte ? b == body.arg8 : prog_.classes[body.arg16].test(b);
    }

    /*!
     * \brief Lookahead: does the sub-pattern match a prefix starting at \p pos?
     *
     * Forward Pike simulation from \p pos, bounded to `l_max` bytes, stopping at the first
     * `match` (the sub is capture-free, so any reached `match` is a witness).
     *
     * \param[in] sub The lookaround sub-program.
     * \param[in] pos Position the lookaround is evaluated at.
     * \return True when the sub matches somewhere in the forward window.
     */
    [[nodiscard]] constexpr bool lookahead_matches(const lookaround_sub& sub,
                                                   std::size_t           pos)
    {
      const std::size_t code_size {prog_.code.size()};
      thread_list*      clist     {&lookaround_state().lists[0]};
      thread_list*      nlist     {&lookaround_state().lists[1]};
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
            if (dc.valid && cp_class_matches_idx(in.arg16, dc.cp)) {
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
     *
     * \param[in] sub The lookaround sub-program.
     * \param[in] pos Position the sub must end exactly at.
     * \return True when some candidate start in the window fullmatches up to \p pos.
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
     *
     * \param[in] code_offset Entry program counter of the sub-program.
     * \param[in] start       Candidate start offset.
     * \param[in] pos         Offset the sub must end exactly at.
     * \return True when the sub matches `[start, pos)` exactly.
     */
    [[nodiscard]] constexpr bool sub_fullmatch_window(std::int32_t code_offset,
                                                      std::size_t  start,
                                                      std::size_t  pos)
    {
      const std::size_t code_size {prog_.code.size()};
      thread_list*      clist     {&lookaround_state().lists[0]};
      thread_list*      nlist     {&lookaround_state().lists[1]};
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
            if (dc.valid && cp_class_matches_idx(in.arg16, dc.cp)) {
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
     * `state_.lookaround->stack`, never the main `state_`. Linearity: `mark_seen` dedups
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
      auto& stack {lookaround_state().stack};
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
