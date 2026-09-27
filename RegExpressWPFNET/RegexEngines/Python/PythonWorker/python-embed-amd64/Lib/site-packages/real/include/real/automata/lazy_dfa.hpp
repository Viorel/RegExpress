/*!
 * \file lazy_dfa.hpp
 * \brief A lazy, priority-preserving forward DFA over the Pike program (the kFirstMatch forward pass + cache).
 *
 * DISTINCT from \ref real::dfa (`<real/dfa.hpp>`): that one is a capture-free *maximal-munch* recognizer
 * over unordered NFA-state sets (a lexer's rule dispatch). This one memoizes the *leftmost-first* Pike
 * closure — its DFA states are **ordered** NFA-state sets, so a `kFirstMatch` forward pass reports the same
 * match boundary the Pike VM would. It reuses only the byte-class idea (an alphabet smaller than 256), not
 * that engine's subset construction.
 *
 * \note The forward pass (`forward_end`), the reverse start-finder (`reverse_dfa`) and the byte-program
 *       that makes a Unicode `klass_cp` DFA-representable are wired into the matcher: pike.hpp routes an
 *       eligible search through them (forward end + reverse start, then the Pike VM on the located window).
 *       Dynamic only: the cache is mutable, so it never participates in constant evaluation.
 */
#ifndef REAL_LAZY_DFA_HPP
#define REAL_LAZY_DFA_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <algorithm>
#include <array>
#include <atomic>
#include <ranges>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "real/core/program.hpp"
#include "real/automata/utf8_ranges.hpp"

namespace real::detail {

  /*!
   * \brief Test seam: force the matcher off the lazy-DFA route onto the pure Pike VM, so a differential can
   *        assert that routed and unrouted searches give identical results within one binary. Not for
   *        production use — the routing is transparent by contract, and this only exists to prove it.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& lazy_dfa_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the general loop off the bounded backtracker onto the Pike VM, so a
   *        differential can assert that both give the same answer on every small subject. Not for production
   *        use -- the backtracker reproduces the VM's priority order by contract, and this proves it.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& bounded_backtrack_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the inner-literal search route onto the core search, so a
   *        differential can assert routed and unrouted searches agree. Not for production use — the route is
   *        transparent by contract (its reverse bound never advances mid-search, so it cannot miss a leftmost
   *        match), and this only exists to prove it.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& inner_literal_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force off the rare-discriminant prefilter (`https?://` memchr-`:` route)
   *        onto prefix/first-byte search, so a differential can assert routed and unrouted agree.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& rare_disc_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the inner-literal small-haystack guard off, so the route fires on any size. In
   *        production the guard uses a cold floor (\ref regex_immutables::il_min_haystack) on the first
   *        candidate-scan and \ref il_warm_floor thereafter (shared reverse DFA in \ref shared_dfa_slot).
   *        Correctness suites use tiny inputs, so they set this to exercise the route rather than the core
   *        fallback. Not for production use.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& inner_literal_guard_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the trailing-lookaround class+ route onto the pure Pike VM, so a
   *        differential can assert routed and unrouted searches agree. Not for production use — the route is
   *        transparent by contract (same leftmost-first spans as the general loop on the eligible shape).
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& trailing_la_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the heterogeneous fixed-shape pair-filter route onto the
   *        ordinary \c run_fixed_shape walk, so a differential can assert routed and unrouted agree.
   *        The route is transparent by contract (it only *filters* candidates; the same
   *        `match_fixed_body_wb` verify decides every one of them), and this seam is what proves it.
   *        Not for production use.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& fixed_shape_pair_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the fixed-shape walk (\c run_fixed_shape) onto the general
   *        Pike loop, so a differential can assert routed and unrouted agree.
   *
   *        This route had NO seam, and that gap was not harmless: it is what `date`
   *        (`[0-9]{4}-[0-9]{2}-[0-9]{2}`, docs/BENCHMARKS.md §A's weakest row) actually takes, and the
   *        test that claimed to cover the shape reached for \ref inner_literal_route_disabled instead --
   *        which does nothing for a `fixed_shape` pattern, because the inner-literal gate excludes them.
   *        Both arms of that differential therefore ran the same code and asserted nothing. Verified with
   *        the route counters: routed and unrouted both billed `fixed_shape` 3092 times over 3091
   *        matches.
   *        Not for production use.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& fixed_shape_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test/profile seam: skip dedicated class-scan fast paths (byte class-loop, cp-class-loop,
   *        and codepoint_class / negated-class `.`/`[^,]+`) so a pattern that would take them falls
   *        through to lazy-DFA / general (dispatch-optimality audit; matrix4d class-scan rows). Not for
   *        production — same contract as the other route-disabled seams.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& class_fastpath_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test/profile seam : force the matcher off the possessive-loop fast paths
   *        (bare/suffixed/delimited `X*+`/`X++`) onto the general VM, so a differential can assert
   *        route-auto and forced-general agree on every input — the route-agreement pattern applied to the new
   *        recognizers. Not for production use — same contract as the other route-disabled seams.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& possessive_fastpath_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam : force the matcher off the Aho-Corasick multi-literal route (past the
   *        branch-count threshold) onto the existing \ref pattern_hints::fixed_alternation
   *        `run_alternation` path, so a differential can assert routed and unrouted searches agree.
   *        Not for production use — same contract as the other route-disabled seams.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& aho_corasick_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam : take the Aho-Corasick DENSITY gate out, so the route is chosen on branch
   *        count alone — the behaviour that shipped before the gate existed.
   *
   * Distinct from \ref aho_corasick_route_disabled, and both are needed to say anything about the
   * gate: that one answers "cascade or automaton", this one answers "who decided". Without it a
   * harness setting `aho_corasick_route_disabled() = false` is not forcing the automaton at all, it
   * is merely declining to forbid it, and the gate then routes the subject wherever it likes — so a
   * column labelled "AC on" silently becomes a column measuring the gate. That happened, and the
   * numbers looked like a regression in the automaton rather than a mislabelled arm.
   *
   * \return Reference to the process-wide seam flag; set it to true to route on branch count alone.
   */
  inline bool& ac_density_gate_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test observability : whether the inner-literal density gate last abandoned the route.
   *
   * The gate decides only which of two routes runs, and both produce identical spans by contract --
   * which is what leaves `tests/engine/test_il_density_gate.cpp` unable to see it at all. That file
   * pins semantic transparency, correctly and thoroughly, and therefore cannot react to
   * \ref real::detail::pike_vm::il_density_milli_threshold moving: the spans are equal whichever way
   * the gate goes. That is why the constant has no test guarding its value -- there is nothing semantic
   * to assert, and this seam is what makes the two routes comparable at all.
   *
   * One store, on a path that already writes two sticky fields beside it. Atomic because every search
   * that abandons writes it, from whatever thread runs the search: a plain `bool` is a data race between
   * two threads searching at once. The store is relaxed, which compiles to the plain store it replaces.
   *
   * \return Reference to the process-wide flag; clear it before a search to arm it.
   */
  inline std::atomic<bool>& il_density_last_abandoned()
  {
    static std::atomic<bool> abandoned {false};
    return abandoned;
  }

  /*!
   * \brief What the AC density gate last decided; `ac_density_last_verdict()` below reports it.
   */
  enum class ac_verdict : std::uint8_t
  {
    not_consulted = 0, //!< The gate has not run since the last reset.
    cascade,           //!< Candidates too sparse: keep the memchr cascade.
    automaton          //!< Candidates dense enough: take the Aho-Corasick walk.
  };

  /*!
   * \brief Test observability : the AC density gate's most recent verdict.
   *
   * The gate decides only which of two routes runs, and both produce identical spans by contract --
   * which is exactly what makes it untestable from the outside. The obvious substitute, asserting
   * that the guarded run is much faster than the forced one, is not portable across build
   * configurations: the margin is ~5.9x optimised and 1.4x under ASan/UBSan, because sanitizer
   * overhead is additive per operation and so dilutes the advantage of a route whose whole merit is
   * SKIPPING bytes. That assertion turned the sanitize leg red while the engine was correct. Timing
   * belongs in `benchmarks/ac_regime.cpp`; a test asserts the decision.
   *
   * One store per search, on the same path as the guard fields it reports on. Atomic for the reason
   * \ref il_density_last_abandoned is: searches on different threads all write it. Relaxed, so the store
   * costs what the plain one did.
   *
   * \return Reference to the process-wide verdict; assign \ref ac_verdict::not_consulted to arm it.
   */
  inline std::atomic<ac_verdict>& ac_density_last_verdict()
  {
    static std::atomic<ac_verdict> verdict {ac_verdict::not_consulted};
    return verdict;
  }

  /*!
   * \brief A byte-level program derived from a Pike program for the DFA passes: every `klass_cp` construct
   *        is expanded into UTF-8 byte-range split/klass chains, so the whole thing is byte-transition-only
   *        and a forward DFA can represent it. The Pike program itself is untouched (byte-identity); this
   *        is a private recognition view the DFAs own. `eligible` is false when an op no DFA can represent
   *        (a position assertion or a lookaround) is present — the caller then keeps the Pike VM.
   */
  struct byte_program
  {
    std::vector<instr>      code;                   //!< The expanded byte-only instruction stream.
    std::vector<char_class> classes;                //!< Byte classes it indexes, including those the trie expansion added.
    bool                    eligible       {true};  //!< Representable by the byte DFAs / Tier-A one-pass.
    bool                    has_assertions {false}; //!< A Tier-B build kept `assert_position` ops (else stripped/declined).
    bool                    unicode_word   {false}; //!< Program default word-ness for `\b \B \< \>` (Tier-B edge conditions).
  };

  /*!
   * \brief One node of a minimal deterministic UTF-8 trie for a code-point class. Its transitions are byte
   *        ranges that are pairwise **disjoint**, so at most one edge matches any byte — that determinism is
   *        what makes the byte-program one-pass-friendly. A target `>= 0` is a node id; `-1` is accept (a
   *        code point ends here — the run continues at the construct's successor).
   */
  struct utf8_trie_node
  {
    /*!
     * \brief Outgoing edges: a byte range paired with its target, `-1` meaning accept. Pairwise disjoint.
     *
     * \note **One heap block per node, and it dominates a cold first search, measured end to end
     *       (2026.09.15, benchmarks/alloc_cold_probe.cpp): the tries are 98.5 % of
     *       build_byte_program's 3 171 allocations -- `\w`'s alone is 2 757 -- and the build is
     *       95.6 % of a cold first search's 3 317 for `\w+\d+`.** Flattening it into a pool is not
     *       a local edit: two sources share
     *       the count, this vector and the `bounds`/`tails` pair `builder::build` allocates at every
     *       level -- and build is RECURSIVE, so those cannot share one scratch buffer. The shape that
     *       works is a stack-disciplined arena, each level taking a slice and releasing it on return,
     *       plus a flat transition pool with the memo's hash and compare working over spans.
     *
     *       The root cause sits above all of it: for a class like `\w`, almost everything this trie
     *       recognises is code points a pure-ASCII subject cannot contain, and the subject IS known at
     *       search time. An ASCII-first expansion would delete the work rather than make it cheaper --
     *       see the design note above \ref build_byte_program.
     */
    std::vector<std::pair<utf8_byte_range, std::int32_t>> trans;
  };

  /*! \brief A minimal deterministic UTF-8 trie for a code-point class. `root == -1` means the class is empty. */
  struct utf8_trie
  {
    std::vector<utf8_trie_node> nodes;      //!< Node pool; ids in \ref utf8_trie_node::trans index it.
    std::int32_t                root {-1};  //!< Entry node id, or `-1` for an empty class.
  };

  /*!
   * \brief Builds the minimal deterministic trie recognising a code-point class's UTF-8 byte sequences.
   *
   * The naive expansion emits one alternation branch per UTF-8 range, and different ranges can share a lead
   * byte with different continuations — two threads then cross that lead byte, which is byte-level
   * non-determinism (a Unicode `\w` is thus never one-pass). This instead splits overlapping ranges into
   * disjoint per-node transitions and hash-conses identical suffix sub-tries (Daciuk), yielding a
   * deterministic automaton: it makes Unicode `\w \d \s` one-pass, and shrinks the byte-program dramatically
   * (a `\w` collapses from thousands of instructions to a few hundred shared nodes, which the lazy DFA also
   * profits from).
   *
   * \param[in] cc        The code-point class to recognise.
   * \param[in] cp_ranges The program's range pool, which \c cc slices.
   * \return The trie; its \ref utf8_trie::root is `-1` when the class is empty.
   */
  constexpr utf8_trie build_utf8_trie(const cp_class&             cc,
                                      std::span<const code_range> cp_ranges)
  {
    // Sequences land in ONE buffer with an offset per sequence, not in a vector of vectors. Each sequence
    // is 1 to 4 ranges, so as its own vector it reallocates at 1, 2 and 4 -- three heap blocks averaging a
    // handful of bytes each, per sequence, and a Unicode class has thousands. Spans are taken only once
    // the pool is complete, so no growth can invalidate one.
    std::vector<utf8_byte_range> seq_pool;
    std::vector<std::size_t>     seq_at {0};
    for (int b = 0; b < 0x80;) { // ASCII: each contiguous run of set bits is a one-byte sequence
      if (cc.ascii.test(static_cast<std::uint8_t>(b))) {
        const int lo {b};
        while (b < 0x80 && cc.ascii.test(static_cast<std::uint8_t>(b))) {
          ++b;
        }
        seq_pool.push_back({.lo = static_cast<std::uint8_t>(lo), .hi = static_cast<std::uint8_t>(b - 1)});
        seq_at.push_back(seq_pool.size());
      }
      else {
        ++b;
      }
    }
    for (std::uint32_t k = 0; k < cc.range_count; ++k) { // non-ASCII: canonical byte-range sequences
      const code_range& r {cp_ranges[cc.range_begin + k]};
      for (const utf8_byte_seq& s : utf8_range_sequences(r.lo, r.hi)) {
        for (std::size_t j = 0; j < s.length; ++j) {
          seq_pool.push_back(s.parts[j]);
        }
        seq_at.push_back(seq_pool.size());
      }
    }

    utf8_trie trie;
    if (seq_at.size() == 1) {
      return trie;
    }
    //! \brief A sequence's remaining byte ranges, as a view. The sequences in `seqs` outlive the whole
    //!        recursion, so a suffix needs no copy -- `subspan(1)` replaces what would otherwise be a
    //!        fresh vector per candidate per interval, once per level of a recursion over a whole class.
    using seq_view = std::span<const utf8_byte_range>;

    struct builder
    {
      std::vector<utf8_trie_node>& nodes;
      // Hash-cons index over FNV(trans), as an INTRUSIVE chain: `memo_head[bucket]` is a node id or -1,
      // `memo_next[id]` the next id in that bucket. One inner vector per bucket would cost one allocation
      // per bucket per trie, and more memory than the table they index. Head insertion is safe because a
      // bucket only ever holds one
      // representative per distinct `trans` -- an identical node returns the existing id and is not inserted
      // -- so the walk order cannot change the result.
      // The engine headers avoid std::hash / std::unordered_map, whose out-of-line libc++ symbols (e.g.
      // __hash_memory) drift across toolchains; an in-house FNV over the transitions hash-conses with no
      // string key per node.
      std::vector<std::int32_t>&   memo_head;
      std::vector<std::int32_t>&   memo_next;

      static constexpr std::uint64_t hash_trans(const std::vector<std::pair<utf8_byte_range, std::int32_t>>& trans)
      {
        std::uint64_t h {fnv1a_offset_basis};
        for (const std::pair<utf8_byte_range, std::int32_t>& t : trans) {
          h = (h ^ t.first.lo) * fnv1a_prime;
          h = (h ^ t.first.hi) * fnv1a_prime;
          h = (h ^ static_cast<std::uint32_t>(t.second)) * fnv1a_prime;
        }
        return h;
      }

      static constexpr bool trans_equal(const std::vector<std::pair<utf8_byte_range, std::int32_t>>& a,
                                        const std::vector<std::pair<utf8_byte_range, std::int32_t>>& b)
      {
        if (a.size() != b.size()) {
          return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
          if (a[i].first.lo != b[i].first.lo || a[i].first.hi != b[i].first.hi || a[i].second != b[i].second) {
            return false;
          }
        }
        return true;
      }

      // Takes a span, not a vector: the recursive call then hands its child a prefix of its OWN `tails`
      // rather than filling a second vector with the non-empty subset. Both vectors are also sized up
      // front -- neither can exceed a bound known on entry -- so a level allocates twice and never
      // re-allocates, against a growth series per interval per level. The trie's own vectors were the
      // build's second-largest allocation source after the sequence pool.
      constexpr std::int32_t build(std::span<const seq_view> in) // all non-empty sequences
      {
        std::vector<int> bounds;
        bounds.reserve(in.size() * 2U);
        for (const seq_view& s : in) {
          bounds.push_back(s[0].lo);
          bounds.push_back(s[0].hi + 1);
        }
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

        utf8_trie_node        node;
        std::vector<seq_view> tails;
        tails.reserve(in.size());
        for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
          const int lo   {bounds[i]};
          const int hi   {bounds[i + 1] - 1};
          tails.clear();
          bool all_empty {true};
          for (const seq_view& s : in) {
            if (s[0].lo <= lo && hi <= s[0].hi) { // this disjoint interval sits inside sequence s's first range
              const seq_view tail {s.subspan(1)};
              all_empty = all_empty && tail.empty();
              tails.push_back(tail);
            }
          }
          if (tails.empty()) {
            continue;
          }
          std::int32_t child {-1};
          if (!all_empty) { // UTF-8 is prefix-free, so within one interval the tails share a length
            // Compact the non-empty tails to the front, order preserved, and recurse on that prefix.
            // The child builds its own `tails`, so it never aliases this one.
            std::size_t kept {0};
            for (const seq_view& t : tails) {
              if (!t.empty()) {
                tails[kept] = t;
                ++kept;
              }
            }
            child = build(std::span<const seq_view> {tails.data(), kept});
          }
          node.trans.emplace_back(utf8_byte_range {.lo = static_cast<std::uint8_t>(lo), .hi = static_cast<std::uint8_t>(hi)}, child);
        }

        std::int32_t& head {memo_head[hash_trans(node.trans) % memo_head.size()]};
        for (std::int32_t existing = head; existing >= 0;
             existing = memo_next[static_cast<std::size_t>(existing)]) {
          if (trans_equal(nodes[static_cast<std::size_t>(existing)].trans, node.trans)) {
            return existing; // an identical suffix sub-trie already exists (Daciuk sharing)
          }
        }
        const auto id {static_cast<std::int32_t>(nodes.size())};
        nodes.push_back(std::move(node));
        memo_next.push_back(head);
        head = id;
        return id;
      }
    };
    constexpr std::size_t     trie_memo_buckets {1024}; // chained buckets, sized for the bounded trie
    std::vector<std::int32_t> memo_head(trie_memo_buckets, -1);
    std::vector<std::int32_t> memo_next;
    const std::size_t         seq_count {seq_at.size() - 1};
    memo_next.reserve(seq_count);
    std::vector<seq_view>     roots;
    roots.reserve(seq_count);
    for (std::size_t i = 0; i < seq_count; ++i) {
      roots.emplace_back(seq_pool.data() + seq_at[i], seq_at[i + 1] - seq_at[i]);
    }
    builder b {trie.nodes, memo_head, memo_next};
    trie.root = b.build(roots);
    return trie;
  }

  /*!
   * \brief The instruction count \ref emit_utf8_trie writes: an empty class is one dead `klass`; otherwise
   *        each node is a split-guarded chain of `k` byte ranges (`3k - 1` instructions).
   * \param[in] trie The trie to measure.
   * \return Instructions the emission will occupy.
   */
  constexpr std::size_t utf8_trie_emit_size(const utf8_trie& trie)
  {
    if (trie.root < 0) {
      return 1;
    }
    std::size_t n {0};
    for (const utf8_trie_node& node : trie.nodes) {
      n += (3 * node.trans.size()) - 1;
    }
    return n;
  }

  /*!
   * \brief Intern table for UTF-8 edge byte ranges, keyed by the exact 16-bit `(lo << 8) | hi`.
   *
   * An open-addressed probe table over a flat buffer rather than `std::unordered_map`, because this
   * builder must also run inside a constant expression: `static_storage` builds its byte program at
   * compile time, and no node-based standard container is constexpr-constructible before C++23.
   *
   * A slot holds `(key << 16) | (index + 1)`, so a zero slot means empty and the range `0x00..0x00`
   * stays a legal key. The table grows at half load rather than capping, so a pattern with many
   * distinct Unicode classes degrades in speed and never in correctness — a fixed capacity would
   * either fill and spin or silently stop interning.
   */
  struct range_intern_table
  {
    static constexpr std::uint16_t absent {0xFFFFU};                               //!< \ref find's miss answer (never a valid index: it would be slot 0x10000).

    std::vector<std::uint32_t> slots      {std::vector<std::uint32_t>(1024U, 0U)}; //!< Power of two; 1024 covers the ~476 ranges a `\w`-heavy program interns without a rehash.
    std::size_t                count      {0};                                     //!< Occupied slots, for the load factor.

    /*!
     * \brief Probe start for \p key: Fibonacci hashing — the key times 2^64/phi, keeping the high bits.
     *
     * The keys cluster hard and arrive in near-runs (`lo` walks a trie node's disjoint ranges in order,
     * and the continuation range `0x80..0xBF` sits on nearly every node), so the stride between
     * consecutive keys is what decides whether the table is used or a quarter of it is. Taking the high
     * bits of the 64-bit product gives a stride coprime with the table size; the 32-bit constant shifted
     * by a fixed amount does not — its stride shares a factor of 4 with 1024, so three of every four
     * buckets would be unreachable for a run of keys and the reachable quarter would be over 100% loaded
     * at the half-load rehash point. Wrapping is intended (it is the modular multiply).
     *
     * \param[in] key  The 16-bit packed byte range.
     * \param[in] mask `slots.size() - 1`, the table being a power of two.
     * \return The first bucket to probe.
     */
    [[nodiscard]] static constexpr std::size_t bucket(std::uint16_t key,
                                                      std::size_t   mask) noexcept
    {
      constexpr std::uint64_t phi_inverse {0x9E3779B97F4A7C15ULL}; // 2^64 / golden ratio, odd
      return static_cast<std::size_t>((static_cast<std::uint64_t>(key) * phi_inverse) >> 48U) & mask;
    }

    /*!
     * \brief Returns the interned class index for \p key, or \ref absent.
     * \param[in] key The 16-bit packed byte range.
     * \return The class index recorded for \p key, or \ref absent when it was never interned.
     */
    [[nodiscard]] constexpr std::uint16_t find(std::uint16_t key) const noexcept
    {
      const std::size_t mask {slots.size() - 1U};
      for (std::size_t i {bucket(key, mask)}; slots[i] != 0U; i = (i + 1U) & mask) {
        if (static_cast<std::uint16_t>(slots[i] >> 16U) == key) {
          return static_cast<std::uint16_t>((slots[i] & 0xFFFFU) - 1U);
        }
      }
      return absent;
    }

    /*!
     * \brief Records \p idx for \p key, doubling the table first when it would pass half load.
     * \param[in] key The 16-bit packed byte range.
     * \param[in] idx The class index to record for it.
     */
    constexpr void insert(std::uint16_t key,
                          std::uint16_t idx)
    {
      if ((count + 1U) * 2U > slots.size()) {
        std::vector<std::uint32_t> wider(slots.size() * 2U, 0U);
        const std::size_t          wide_mask {wider.size() - 1U};
        for (const std::uint32_t packed : slots) {
          if (packed != 0U) {
            std::size_t i {bucket(static_cast<std::uint16_t>(packed >> 16U), wide_mask)};
            while (wider[i] != 0U) {
              i = (i + 1U) & wide_mask;
            }
            wider[i] = packed;
          }
        }
        slots = std::move(wider);
      }
      const std::size_t mask {slots.size() - 1U};
      std::size_t       i    {bucket(key, mask)};
      while (slots[i] != 0U) {
        i = (i + 1U) & mask;
      }
      slots[i] = (static_cast<std::uint32_t>(key) << 16U) | (static_cast<std::uint32_t>(idx) + 1U);
      ++count;
    }
  };

  /*!
   * \brief Emits \p trie into \p bp as a deterministic split/klass/jump fragment, interning each edge's
   *        byte range through \p seen.
   *
   * Accept edges jump to \p after (the construct's successor); the root is emitted first, so the
   * fragment's entry is its base pc, and disjoint ranges mean at most one branch matches any byte.
   *
   * Every edge class here is a SINGLE range (`set_range` below), so `(lo, hi)` is an exact key and two
   * edges with the same range can share one interned class. Without that sharing a UTF-8 trie interns the
   * same range once per edge -- the continuation range `0x80..0xBF` sits on nearly every node -- and the
   * redundancy compounds across occurrences, since each `\w+` emits its own full trie. The caller owns
   * \p seen so it spans every occurrence in one program, which is where most of the duplication lives.
   *
   * Sharing an index is safe because a class index is only ever read as "which byte set" -- the
   * alphabet, `onepass`'s class_cover_, and the DFA all treat it that way; nothing uses it to tell two
   * `klass` instructions apart.
   *
   * \param[in,out] bp    Byte program the fragment is appended to.
   * \param[in]     trie  The trie to emit.
   * \param[in]     after Program counter the accept edges jump to (the construct's successor).
   * \param[in,out] seen  Byte-range intern table, shared across every occurrence in one program.
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only: see the note in prefilter.hpp's detect_fast_shapes
#endif
  constexpr void emit_utf8_trie(byte_program&        bp,
                                const utf8_trie&     trie,
                                std::int32_t         after,
                                range_intern_table&  seen)
  {
    if (trie.root < 0) {
      bp.code.push_back({.op = opcode::klass, .arg16 = static_cast<std::uint16_t>(bp.classes.size())});
      bp.classes.emplace_back(); // matches no byte: the run dies (an empty class matches nothing)
      return;
    }
    const auto                 base  {static_cast<std::int32_t>(bp.code.size())};
    std::vector<std::int32_t>  order {trie.root}; // root first, so the entry pc is `base`
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(trie.nodes.size()); ++i) {
      if (i != trie.root) {
        order.push_back(i);
      }
    }
    std::vector<std::int32_t> node_pc(trie.nodes.size(), 0);
    std::int32_t              off {base};
    for (const std::int32_t id : order) {
      node_pc[static_cast<std::size_t>(id)] = off;
      off                                  += (3 * static_cast<std::int32_t>(trie.nodes[static_cast<std::size_t>(id)].trans.size())) - 1;
    }
    for (const std::int32_t id : order) {
      const utf8_trie_node& node {trie.nodes[static_cast<std::size_t>(id)]};
      const auto            k    {static_cast<std::int32_t>(node.trans.size())};
      for (std::int32_t j = 0; j < k; ++j) {
        const auto&        edge   {node.trans[static_cast<std::size_t>(j)]};
        const std::int32_t target {edge.second < 0 ? after : node_pc[static_cast<std::size_t>(edge.second)]};
        const auto         here   {static_cast<std::int32_t>(bp.code.size())};
        if (j + 1 < k) {
          bp.code.push_back({.op = opcode::split, .primary_target = here + 1, .secondary_target = here + 3});
        }
        const auto key    {static_cast<std::uint16_t>((static_cast<unsigned>(edge.first.lo) << 8U)
                                                      | static_cast<unsigned>(edge.first.hi))};
        std::uint16_t idx {seen.find(key)};
        if (idx == range_intern_table::absent) {
          idx = static_cast<std::uint16_t>(bp.classes.size());
          char_class cc;
          cc.set_range(edge.first.lo, edge.first.hi);
          bp.classes.push_back(cc);
          seen.insert(key, idx);
        }
        bp.code.push_back({.op = opcode::klass, .arg16 = idx});
        bp.code.push_back({.op = opcode::jump, .primary_target = target});
      }
    }
  }

  //! Cap on the expanded byte-program's instruction count (the running `cur` total below, checked as it
  //! grows). Each `klass_cp` occurrence gets its OWN freshly-built UTF-8 trie here (unshared even when many
  //! occurrences reference the identical class — e.g. every copy of a `{k}`-repeated `\w`), so a large
  //! repeat count multiplies the trie's several-hundred-to-thousand-node size by k with no cache to amortize
  //! it. Left unbounded, that is O(k x trie size) wall-clock BEFORE onepass or the lazy DFA ever run (their
  //! own caps — \ref onepass::max_nodes, \ref onepass::max_minimize_work — sit downstream of this and never
  //! get a chance to bound it). The binding constraint is the sanitizer-instrumented fuzzing build, whose
  //! per-input timeout a `\w{k}` reaches while k is still small; the cap is set to keep that build's worst
  //! case well inside its timeout while comfortably exceeding every legitimate pattern. Exceeding it
  //! declines Tier-A/Tier-B (and so onepass/lazy-DFA) entirely, falling back to the general Pike VM, which
  //! matches straight off the (small, unexpanded) compiled program and needs no trie expansion at all.
  inline constexpr std::size_t max_byte_program_size {20000};

  /*!
   * \brief Builds the byte-level DFA program for \p prog (see \ref byte_program). A `klass_cp` at P (a four-
   *        instruction construct: the op plus three `utf8_cont` continuation slots) is replaced by the
   *        deterministic UTF-8 trie recognising its code-point class (\ref build_utf8_trie), converging on
   *        the mapped P+4; every other op is copied with its branch targets remapped. Two passes: the first
   *        builds each trie and sizes it to form the old→new pc map, the second emits. The first pass also
   *        enforces \p max_size (see \ref max_byte_program_size) as it accumulates `cur`, so a large repeated
   *        class declines before building any trie past the one that crosses the cap.
   *
   * \param[in] prog            The Pike program to expand.
   * \param[in] keep_assertions Tier-B: keep `assert_position` ops as edge conditions instead of declining
   *                            them (Tier-A's default).
   * \param[in] max_size        Expanded-program-size cap. Defaults to \ref max_byte_program_size; a smaller
   *                            value is a test hook to exercise the decline without a pattern that takes
   *                            seconds to build.
   * \return The expanded program, with \ref byte_program::eligible false when it declined.
   */
  // AN ASCII-RESTRICTED EXPANSION WAS PROTOTYPED AND THE GAP IS NOT CLOSE. Emitting each `klass_cp` as a
  // plain byte class built from `cp_class::ascii` -- which is already a `char_class`, so nothing needs
  // converting -- instead of expanding its UTF-8 trie takes a text-mode program from thousands of
  // instructions and hundreds of classes to a handful of each. Everything downstream is sized by that
  // program -- the lazy alphabet, the DFA's subset construction, the one-pass table -- so this does not
  // merely make the trie build cheaper, it makes the whole pipeline trivial. What it would close is the
  // order-of-magnitude gap that still separates a first text-mode search from its byte-class twin.
  //
  // THE OBVIOUS ROUTING IS REFUTED, MEASURED. A restricted program is correct only if the whole scanned
  // region is ASCII, so the naive design pre-scans the subject. That scan costs MORE than the search it
  // protects, and the gap widens with the subject, because the scan is O(n) against a search that skips.
  // Caching the verdict per haystack does not rescue the common case either, since the VM state is fresh
  // per `search()` and would re-scan; that is the same trap the compat regex_iterator was found in.
  //
  // What is left is for the restricted DFA to derail ITSELF: map every byte >= 0x80 to one
  // distinguished alphabet class whose transition is a sentinel meaning "cannot answer", so the scan
  // bails on first contact and the caller falls back to the general VM, which handles Unicode
  // natively. No pre-scan, and near-zero cost in a loop that already does one table lookup per byte.
  // It touches the DFA's state semantics, which is why it is designed here and not written here.
  //
  // WHAT IT NEEDS, and why the prototype stops at measuring: an ASCII-restricted program is valid
  // ONLY for an ASCII subject, so it needs a second cached program built lazily on the first
  // non-ASCII haystack, a per-haystack ASCII check (cheap, and cacheable exactly like the
  // inner-literal and Aho-Corasick density verdicts already are), and routing that can never hand an
  // ASCII program a subject that would fool it. That is a feature with a correctness obligation, not
  // a local edit -- and it subsumes the trie-arena work above rather than competing with it.
  //
  // THE EXPANSION IS BLIND TO THE SUBJECT, AND THAT IS WHERE THE COST IS. A text-mode class is expanded
  // here in full -- every code point it can match, as a UTF-8 trie -- before anything has looked at what
  // will be searched. The gap against the strict-ASCII equivalent of the same class scales with how much
  // of the class lies outside ASCII: two orders of magnitude for `\w`, one for `\d`, and small for `\s`,
  // which is nearly all ASCII to begin with. For a wide class that is essentially all of the cost, paid to
  // recognise code points a pure-ASCII subject cannot contain.
  //
  // The general Pike VM needs none of it: a bare `\w+` takes a route with no byte program at all. This
  // expansion exists only to feed the lazy DFA and the one-pass extractor.
  //
  // The shape that would fix it is an ASCII-first expansion upgraded on demand, since the subject IS known
  // at search time and its ASCII-ness is a cheap scan. That is an architectural change to WHEN the
  // immutables are built, not a tweak here.
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only: see the note in prefilter.hpp's detect_fast_shapes
#endif
  constexpr byte_program build_byte_program(const program_view& prog,
                                            bool                keep_assertions = false,
                                            std::size_t         max_size        = max_byte_program_size)
  {
    byte_program bp;
    bp.unicode_word = prog.unicode_word;
    if (prog.code.empty()) {
      // An empty program is not a recognizer, and saying `eligible` about one is worse than declining:
      // every consumer reads that flag as "the byte automata can run this", so a reverse pass built over
      // it answers npos at every position and the caller silently drops every candidate instead of
      // falling back to the VM. Reachable only by a caller that hands over a program it never compiled --
      // which is exactly what a storage with a cache but no inner-literal prefix sub-program would do.
      bp.eligible = false;
      return bp;
    }
    for (const instr& in : prog.code) {
      if (in.op == opcode::assert_lookaround) {
        bp.eligible = false; // no byte automaton can carry a bounded lookaround (Tier-B stops at assertions)
        return bp;
      }
      if (in.op == opcode::assert_position) {
        if (!keep_assertions || in.arg16 != 0) {
          bp.eligible = false; // Tier-A declines any assertion; Tier-B declines a word-ness-flipped one (scoped (?a:))
          return bp;
        }
        bp.has_assertions = true; // Tier-B: kept, to become an edge condition in the one-pass table
      }
      if (in.op == opcode::byte_loop_possessive || in.op == opcode::klass_loop_possessive ||
          in.op == opcode::klass_cp_loop_possessive) {
        // No byte automaton can carry a Tier 1 possessive loop: its `primary_target` is a
        // capture-slot index (or -1), not a branch target -- the generic copy below blindly
        // remaps every op's primary_target/secondary_target through the pc `map`, which would
        // silently corrupt that slot index into a bogus remapped pc for a captured loop. Decline
        // outright, exactly like a bounded lookaround (Tier-B stops at assertions).
        bp.eligible = false;
        return bp;
      }
    }
    bp.classes.assign(prog.classes.begin(), prog.classes.end()); // original classes keep their indices

    const std::size_t         n {prog.code.size()};
    std::vector<std::int32_t> map(n + 1, 0);                     // old pc -> new pc (n = the one-past end)
    // A trie PER CLASS, not per occurrence, and pointers into it per pc. A trie is a pure function of its
    // code-point class, so a pattern naming the same class twice -- `(\w+)X(\w+)` -- otherwise builds the
    // identical structure twice, and the cost of a first search on such a pattern is this build, not
    // construction and not steady-state matching. `by_class` is sized up front and never grows, so the
    // pointers cannot dangle.
    //
    // NOT shared across the program's TWO expansions -- Tier-A through ensure_immutables and Tier-B through
    // ensure_op_table, which calls it, so one cache on the latter's stack would cover both with nothing
    // outliving the build. That was written, wired and measured, and it does not pay: holding a wide
    // class's large tries alive across both builds costs more than rebuilding them, and the wide classes
    // are exactly the case worth fixing. Reverted.
    std::vector<utf8_trie>        by_class(prog.cp_classes.size());
    std::vector<bool>             class_built(prog.cp_classes.size(), false);
    std::vector<const utf8_trie*> tries(n, nullptr);              // the trie for each klass_cp pc
    std::size_t                   cur {0};
    for (std::size_t pc = 0; pc < n; ++pc) {
      map[pc] = static_cast<std::int32_t>(cur);
      if (prog.code[pc].op == opcode::klass_cp) {
        const std::size_t ci {static_cast<std::size_t>(prog.code[pc].arg16)};
        if (!class_built[ci]) {
          by_class[ci]    = build_utf8_trie(prog.cp_classes[ci], prog.cp_ranges);
          class_built[ci] = true;
        }
        tries[pc]   = &by_class[ci];
        cur        += utf8_trie_emit_size(*tries[pc]);
        if (cur > max_size) {
          bp.eligible = false; // a large repeated class (e.g. `\w{k}` for a big k): decline before building
          return bp;           // any further trie -- the general Pike VM needs no byte-program expansion.
        }
        map[pc + 1] = map[pc + 2] = map[pc + 3] = static_cast<std::int32_t>(cur); // continuation slots absorbed
        pc         += 3;                                                          // skip the construct's tail
      }
      else {
        ++cur;
      }
    }
    map[n] = static_cast<std::int32_t>(cur);

    // One intern cache for the whole program: the same UTF-8 range recurs across occurrences, not
    // just within one trie.
    range_intern_table range_intern;

    const auto remap {[&](std::int32_t t) {
                        return (t >= 0 && static_cast<std::size_t>(t) <= n) ? map[static_cast<std::size_t>(t)] : t;
                      }};
    for (std::size_t pc = 0; pc < n; ++pc) {
      const instr& in {prog.code[pc]};
      if (in.op == opcode::klass_cp) {
        emit_utf8_trie(bp, *tries[pc], map[pc + 4], range_intern);
        pc += 3;
      }
      else {
        instr out {in};
        out.primary_target   = remap(in.primary_target);
        out.secondary_target = remap(in.secondary_target);
        bp.code.push_back(out);
      }
    }
    return bp;
  }

  /*!
   * \brief Byte-class alphabet over a Pike program: bytes that satisfy exactly the same `byte`/`klass`
   *        predicates share a class, so the DFA transitions over classes instead of 256 raw bytes. The
   *        same reduction \ref real::dfa uses, computed here from the Pike program's own ops.
   */
  struct lazy_byte_alphabet
  {
    std::array<std::uint8_t, 256> of    {};    //!< byte -> class index.
    std::uint16_t                 count {0};   //!< number of distinct classes.
  };

  /*!
   * \brief Partition 0..255 by the program's consuming predicates (every `klass` test, every `byte`
   *        literal). Bytes with an identical signature collapse to one class.
   * \param[in] code    The program's instruction stream.
   * \param[in] classes The byte classes it indexes.
   * \return The alphabet: a byte-to-class map plus the class count.
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only: see the note in prefilter.hpp's detect_fast_shapes
#endif
  inline constexpr lazy_byte_alphabet compute_lazy_alphabet(std::span<const instr>      code,
                                                            std::span<const char_class> classes)
  {
    std::vector<char_class>    class_preds;
    std::vector<std::uint16_t> literal_preds;
    const auto                 push_unique {[](auto& vec, const auto& value) {
                                              for (const auto& existing : vec) {
                                                if (existing == value) {
                                                  return;
                                                }
                                              }
                                              vec.push_back(value);
                                            }};
    // Memoize by class index and by literal byte. `push_unique` is a linear scan with a full 256-bit
    // char_class compare, and it runs once per `klass` INSTRUCTION -- of which a Unicode trie emits several
    // times more than there are distinct classes, so most of those comparisons are re-deciding a class
    // already seen. Skipping an index already processed is a no-op by construction: its content was pushed
    // the first time.
    // This pays only because emit_utf8_trie now shares one interned class per byte range; with a fresh
    // index per edge the memo would never hit.
    // Distinct-class collection, hashed rather than linear-scanned. The linear scan compares whole
    // 256-bit char_class values, and on a Unicode program it runs tens of thousands of them to find almost
    // no duplicate at all -- interning already shares one class per byte range (see emit_utf8_trie), so the
    // array arrives nearly deduplicated and the scan is pure loss. Intrusive chain, not std::unordered_map: this function is constexpr and the
    // engine headers avoid std::hash, whose out-of-line libc++ symbols drift across toolchains. Insertion
    // order in `class_preds` is preserved, so sig_equal's early exit behaves identically.
    constexpr std::size_t     pred_buckets {512};
    std::vector<std::int32_t> pred_head(pred_buckets, -1);
    std::vector<std::int32_t> pred_next;
    std::vector<bool>         seen_class(classes.size(), false);
    std::array<bool, 256>     seen_byte {};
    for (const instr& in : code) {
      if (in.op == opcode::klass && in.arg16 < classes.size()) {
        if (!seen_class[in.arg16]) {
          seen_class[in.arg16] = true;
          // Inlined rather than a lambda: the analyzer cannot model a `[&]`-captured vector through the
          // call and reports a null-pointer call on every use of `class_preds`, and one call site does not
          // justify suppressing that.
          const char_class& cc {classes[in.arg16]};
          std::uint64_t     h  {fnv1a_offset_basis};
          for (const std::uint64_t w : cc.bits) {
            h = (h ^ w) * fnv1a_prime;
          }
          std::int32_t& head    {pred_head[static_cast<std::size_t>(h) % pred_buckets]};
          bool          present {false};
          for (std::int32_t i2 = head; i2 >= 0; i2 = pred_next[static_cast<std::size_t>(i2)]) {
            if (class_preds[static_cast<std::size_t>(i2)] == cc) {
              present = true;
              break;
            }
          }
          if (!present) {
            const auto id {static_cast<std::int32_t>(class_preds.size())};
            class_preds.push_back(cc);
            pred_next.push_back(head);
            head = id;
          }
        }
      }
      else if (in.op == opcode::byte) {
        if (!seen_byte[in.arg8]) {
          seen_byte[in.arg8] = true;
          push_unique(literal_preds, static_cast<std::uint16_t>(in.arg8));
        }
      }
    }
    // A program that carries position assertions needs every class uniform in the two properties they
    // read: whether a byte is a newline and whether it is an ASCII word byte. Two synthetic predicates
    // split the alphabet on them, so a class's properties can be read off any one of its bytes.
    if (std::ranges::any_of(code, [](const instr& in) { return in.op == opcode::assert_position; })) {
      char_class newline;
      newline.set(static_cast<std::uint8_t>('\n'));
      char_class word;
      word.set_range(static_cast<std::uint8_t>('a'), static_cast<std::uint8_t>('z'));
      word.set_range(static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('Z'));
      word.set_range(static_cast<std::uint8_t>('0'), static_cast<std::uint8_t>('9'));
      word.set(static_cast<std::uint8_t>('_'));
      char_class high;
      high.set_range(static_cast<std::uint8_t>(0x80U), static_cast<std::uint8_t>(0xFFU));
      class_preds.push_back(newline);
      class_preds.push_back(word);
      class_preds.push_back(high); // a Unicode word boundary is decided only between two ASCII bytes
    }
    // A byte's signature -- which predicates hold it -- does not depend on the classes formed so far, so
    // it is built ONCE per byte and then grouped. Comparing each byte against every open class instead
    // re-walked all the predicates per (byte, class) pair: O(256 * classes * predicates) with a whole
    // char_class scan inside, 9.2 M instructions per call on `(\w+)@(\w+)`'s 475 predicates. Building the
    // table is O(256 * predicates) and reads each class's bitmap directly.
    //
    // Class ids come out identical to the pairwise form: both walk bytes 0..255 in order and mint a new id
    // the first time a signature appears, and signature equality IS the old `sig_equal`. Downstream reads
    // `alpha.of` by value, so that identity is load-bearing, not incidental.
    const std::size_t          pred_count {class_preds.size() + literal_preds.size()};
    const std::size_t          sig_words  {((pred_count + 63U) / 64U) + 1U};
    std::vector<std::uint64_t> sig(256U * sig_words, 0);
    for (std::size_t p {0}; p < class_preds.size(); ++p) {
      const char_class&   cc {class_preds[p]};
      const std::size_t   w  {p >> 6U};
      const std::uint64_t m  {std::uint64_t {1} << (p & 63U)};
      // Only the bytes the class HOLDS, read straight off its bitmap, rather than asking `test` for all
      // 256. A `\w` predicate holds 63 of them, so this is 63 scatters against 256 tested branches per
      // predicate -- and `(\w+)@(\w+)` carries 475 predicates.
      for (std::size_t word {0}; word < cc.bits.size(); ++word) {
        for (std::uint64_t rest {cc.bits[word]}; rest != 0; rest &= rest - 1U) {
          const std::size_t b {(word * 64U) + static_cast<std::size_t>(std::countr_zero(rest))};
          sig[(b * sig_words) + w] |= m;
        }
      }
    }
    for (std::size_t i {0}; i < literal_preds.size(); ++i) {
      // A literal predicate holds exactly its own byte, so it sets one bit in one row.
      const std::size_t p {class_preds.size() + i};
      sig[(static_cast<std::size_t>(literal_preds[i]) * sig_words) + (p >> 6U)] |= std::uint64_t {1} << (p & 63U);
    }

    lazy_byte_alphabet        alpha;
    constexpr std::size_t     sig_buckets {512};
    std::vector<std::int32_t> sig_head(sig_buckets, -1);
    std::vector<std::int32_t> sig_next;  // parallel to `rep`, indexed by class id
    std::vector<std::uint8_t> rep;       // representative byte of each class id
    for (unsigned b {0}; b < 256U; ++b) {
      std::uint64_t h {fnv1a_offset_basis};
      for (std::size_t w {0}; w < sig_words; ++w) {
        h = (h ^ sig[(b * sig_words) + w]) * fnv1a_prime;
      }
      std::int32_t& head     {sig_head[static_cast<std::size_t>(h) % sig_buckets]};
      bool          assigned {false};
      for (std::int32_t i2 {head}; i2 >= 0; i2 = sig_next[static_cast<std::size_t>(i2)]) {
        const std::size_t other {rep[static_cast<std::size_t>(i2)]};
        bool              same  {true};
        for (std::size_t w {0}; w < sig_words; ++w) {
          if (sig[(b * sig_words) + w] != sig[(other * sig_words) + w]) {
            same = false;
            break;
          }
        }
        if (same) {
          alpha.of[b] = static_cast<std::uint8_t>(i2);
          assigned    = true;
          break;
        }
      }
      if (!assigned) {
        const auto id {static_cast<std::int32_t>(alpha.count)};
        rep.push_back(static_cast<std::uint8_t>(b));
        sig_next.push_back(head);
        head        = id;
        alpha.of[b] = static_cast<std::uint8_t>(alpha.count);
        ++alpha.count;
      }
    }
    return alpha;
  }

  /*!
   * \brief A tiny open-chaining hash set of interned PC-set state ids, keyed by their pc-set. Replaces a
   *        `std::unordered_map` so the DFAs stay **literal types** (a constexpr `real::regex` embeds one in
   *        its scratch state); all-`std::vector` storage is constexpr-constructible in C++20. Maps a
   *        candidate pc-set to its existing state id, or \ref not_found, comparing against the owner's pcs.
   */
  struct pc_set_cache
  {
    // SIZING, not behaviour: chaining absorbs any load factor, so no answer and no route
    // depends on this number -- only speed. The sabotage sweep will always read it as
    // unguarded, and correctly: there is nothing here for a test to hold.
    static constexpr std::size_t   bucket_count {2048};        //!< Fixed bucket count; chaining absorbs the load.
    static constexpr std::uint32_t not_found    {0xFFFFFFFFU}; //!< \ref find's miss answer (never a valid state id).

    std::vector<std::vector<std::uint32_t>> buckets;           //!< One chain of state ids per bucket.

    /*!
     * \brief An empty cache with all \ref bucket_count chains allocated.
     */
    constexpr pc_set_cache()
      : buckets(bucket_count)
    {}

    /*!
     * \brief FNV-1a over the pc-set, truncated to `size_t`.
     * \param[in] v The pc-set to hash.
     * \return Its hash; the caller takes it modulo \ref bucket_count.
     */
    static constexpr std::size_t hash(const std::vector<std::int32_t>& v)
    {
      // FNV-1a computed in a fixed 64-bit accumulator, truncated to size_t on return: the 64-bit
      // offset-basis brace-initialised straight into size_t narrows (an error) where size_t is 32-bit
      // (win32). Truncating a full FNV-64 keeps a well-distributed hash without width-specific constants.
      std::uint64_t h {fnv1a_offset_basis};
      for (const std::int32_t x : v) {
        h = (h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))) * fnv1a_prime;
      }
      return static_cast<std::size_t>(h);
    }

    /*!
     * \brief The state already interned for \p pcs, if any.
     * \param[in] pcs       The candidate pc-set.
     * \param[in] state_pcs The owner's per-state pc-sets, compared against on a bucket hit.
     * \return The matching state id, or \ref not_found.
     */
    [[nodiscard]] constexpr std::uint32_t find(const std::vector<std::int32_t>&               pcs,
                                               const std::vector<std::vector<std::int32_t>>&  state_pcs) const
    {
      for (const std::uint32_t id : buckets[hash(pcs) % bucket_count]) {
        if (state_pcs[id] == pcs) {
          return id;
        }
      }
      return not_found;
    }

    /*!
     * \brief Records \p id under \p pcs. The caller guarantees \p pcs is not already interned.
     * \param[in] pcs The state's pc-set.
     * \param[in] id  The state id to record.
     */
    constexpr void insert(const std::vector<std::int32_t>& pcs,
                          std::uint32_t                    id)
    {
      buckets[hash(pcs) % bucket_count].push_back(id);
    }

    /*!
     * \brief Empties every chain, keeping the bucket array allocated (paired with a state-cache flush).
     */
    constexpr void clear()
    {
      for (std::vector<std::uint32_t>& b : buckets) {
        b.clear();
      }
    }
  };

  /*!
   * \brief Whether a word assertion needs a code point's word-ness that one byte does not give: a side it
   *        reads is a non-ASCII byte, and the ASCII side does not settle it alone.
   *
   * Between two ASCII bytes a Unicode word boundary is an ASCII one: the VM reads the byte itself on each
   * side when it is below 0x80 (assert_eval.hpp). `\<` is false after an ASCII word byte or before an ASCII
   * non-word byte whatever the other side is, and `\>` likewise, so those need no code point.
   * \param[in] kind          The assertion.
   * \param[in] prev_word     The byte before is an ASCII word byte.
   * \param[in] prev_nonascii The byte before is not ASCII.
   * \param[in] next_word     The byte after is an ASCII word byte.
   * \param[in] next_nonascii The byte after is not ASCII.
   * \return True when only the VM can decide it.
   */
  [[nodiscard]] constexpr bool undecidable_word(assert_kind kind,
                                                bool        prev_word,
                                                bool        prev_nonascii,
                                                bool        next_word,
                                                bool        next_nonascii)
  {
    switch (kind) {
      case assert_kind::word_boundary:
      case assert_kind::not_word_boundary: return prev_nonascii || next_nonascii;
      case assert_kind::word_start:        return !prev_word && (next_nonascii || (prev_nonascii && next_word));
      case assert_kind::word_end:          return !next_word && (prev_nonascii || (next_nonascii && prev_word));
      case assert_kind::text_start:
      case assert_kind::text_end:
      case assert_kind::text_end_or_final_newline:
      case assert_kind::line_start:
      case assert_kind::line_end:          return false;
    }
    return false;
  }

  /*!
   * \brief A lazy priority-preserving forward DFA over a Pike program (the kFirstMatch forward pass).
   *
   * A DFA state is the ordered epsilon-closure of a set of program counters (the Pike thread list's PCs,
   * in split priority). \ref step transitions on a byte by consuming it from each PC and re-closing, then
   * interns the resulting ordered set into a cached state — the subset construction, memoized on demand.
   *
   * The cache is bounded: once it reaches \ref state_budget states it is flushed and rebuilt (states are
   * cheap to recompute; a bounded cache keeps memory flat). `state_budget` flushes crossed within one
   * scan (see \ref begin_scan) trips \ref thrashing. A DFA built to quit then ends the scan and reports a
   * quit, and its caller finishes that one search on the Pike VM, per-scan and linear, never re-attempting
   * per position.
   *
   * A program with an op no forward DFA can represent — a position assertion (`\b`, `^`, `$`), a
   * `klass_cp`, or a lookaround — is \ref eligible "ineligible"; this only builds the machinery, it does
   * not decide policy.
   */
  class lazy_dfa
  {
  public:

    static constexpr std::uint32_t dead_state     {0};           //!< The empty state: every transition from it stays here.
    static constexpr std::uint32_t no_transition  {0xFFFFFFFFU}; //!< A not-yet-computed cached transition.
    static constexpr std::uint32_t quit_state     {0xFFFFFFFEU}; //!< What resolve() gives when a Unicode word boundary meets a non-ASCII byte; never interned.
    static constexpr std::size_t   quit_pos       {npos - 1U};   //!< What forward_end() gives when its scan quit (see \ref anchored_result::quit).
    static constexpr std::uint32_t no_match_idx   {0xFFFFFFFFU}; //!< A state whose ordered set holds no accept.
    static constexpr std::size_t   state_budget   {4096};        //!< Cached states before a flush (the memory cap).
    static constexpr std::size_t   thrash_flushes {2};           //!< Flushes within one scan that trip \ref thrashing.

    /*!
     * \brief Cache-behaviour counters, for the policy tests and later tuning.
     */
    struct counters
    {
      std::size_t hits         {0};   //!< Transitions served from the cached row.
      std::size_t misses       {0};   //!< Transitions that had to run subset construction.
      std::size_t flushes      {0};   //!< Cache flushes over this object's lifetime.
      std::size_t scan_flushes {0};   //!< flushes in the current scan (reset by \ref begin_scan).
    };

    /*!
     * \brief Builds the (initially empty) lazy DFA over a Pike program.
     * \param[in] code    The program's instruction stream (must outlive this object — held as a span).
     * \param[in] classes The program's interned character classes (likewise held as a span).
     * \param[in] budget  Cached states before a flush; defaults to \ref state_budget. A smaller value is a
     *                    test hook to exercise eviction and thrash without a state-exploding pattern.
     * \param[in] shared_alpha A precomputed alphabet the caller shares per regex, or null to compute it
     *                    here. Recomputing is O(256 x classes) and a Unicode byte-program has thousands, so
     *                    the router passes the shared one rather than paying it on every scan.
     * \param[in] ascii_word Whether the program's word boundaries use ASCII word-ness (bytes mode, `(?a)`):
     *                    only then does one byte decide them. False, the default, declines any word boundary.
     * \param[in] byte_mode Whether a match may start at any byte. In text mode it may not start inside a code
     *                    point; with assertions an empty match could otherwise be found there (the scans
     *                    then do not seed at a continuation byte, as the VM does not).
     * \param[in] word_quit Let a scan quit, and say so, where the DFA cannot answer well: with Unicode
     *                    word-ness (\p ascii_word false), where a word boundary must be decided next to a
     *                    non-ASCII byte (between two ASCII bytes a Unicode word boundary is an ASCII one);
     *                    and once the cache thrashes (\ref thrashing), where building states costs more than
     *                    the VM would. The caller then asks the VM.
     */
    explicit constexpr lazy_dfa(std::span<const instr>      code,
                                std::span<const char_class> classes,
                                std::size_t                 budget       = state_budget,
                                const lazy_byte_alphabet*   shared_alpha = nullptr,
                                bool                        ascii_word   = false,
                                bool                        byte_mode    = true,
                                bool                        word_quit    = false)
      : code_ {code}, classes_ {classes},
        alpha_ {shared_alpha != nullptr ? *shared_alpha : compute_lazy_alphabet(code, classes)},
        eligible_ {compute_eligibility(code, ascii_word || word_quit)}, byte_mode_ {byte_mode},
        word_quit_ {word_quit && !ascii_word}, may_quit_ {word_quit},
        look_ {std::ranges::any_of(code, [](const instr& in) { return in.op == opcode::assert_position; })},
        plain_ {eligible_ && !look_}, budget_ {budget}
    {
      if (look_) {
        // The alphabet splits on newline, ASCII word bytes and non-ASCII bytes when the program carries
        // assertions (compute_lazy_alphabet), so any byte of a class tells that class's properties.
        for (unsigned b {0}; b < 256U; ++b) {
          class_ctx_[alpha_.of[b]] = (word_quit_ && b >= 0x80U) ? ctx_nonascii : ctx_of(static_cast<std::uint8_t>(b));
        }
      }
      flush();                 // seeds the dead state (0) and the start state (1)
    }

    /*!
     * \brief Whether the program can be represented at all (no assertion, `klass_cp` or lookaround).
     * \return False when the caller must keep the Pike VM.
     */
    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    /*!
     * \brief Width of one cached transition row.
     * \return The byte alphabet's class count.
     */
    [[nodiscard]] std::uint16_t num_classes() const
    {
      return alpha_.count;
    }

    /*!
     * \brief The state a scan starts in.
     * \return The start state's id (1; 0 is \ref dead_state).
     */
    [[nodiscard]] std::uint32_t start_state() const
    {
      return start_state_;
    }

    /*!
     * \brief Begin a search: clear the per-scan flush counter and the thrash flag. (No cache flush — the
     *        states carry over between searches on the same text, which is where the cache pays.)
     */
    void begin_scan()
    {
      stats_.scan_flushes = 0;
      thrashing_          = false;
    }

    /*!
     * \brief Whether this scan crossed \ref thrash_flushes flushes — the cache is not paying for it.
     * \return True once the caller should abandon the DFA and finish the search on the Pike VM.
     */
    [[nodiscard]] bool thrashing() const
    {
      return thrashing_;
    }

    /*!
     * \brief Cache-behaviour counters.
     * \return A reference to the live \ref counters, valid for this object's lifetime.
     */
    [[nodiscard]] const counters& stats() const
    {
      return stats_;
    }

    /*!
     * \brief The end offset of the leftmost-first match in \p text (`kFirstMatch`), or \ref real::npos.
     *
     * The forward pass the contract in the design guide (§7.6) specifies: an unanchored priority-ordered
     * closure that seeds a fresh thread at every position (at the lowest priority) until a match is found,
     * then reports the end of the **highest-priority** thread that reaches `match` — a lower-priority accept
     * is suppressed while a higher one lives. It is a single left-to-right pass over the ordered PC-sets, so
     * it is linear per search regardless of how the state space would explode under memoization. Eligible
     * programs only (an ineligible one returns \ref real::npos; the caller keeps the Pike VM). No captures:
     * this reports the end; the windowed Pike pass fills the span and applies the empty-match rule.
     *
     * A program with position assertions reads the text around \p start and past each position: the scan
     * starts at \p start in the whole text rather than in a slice of it, so `^`, `\b` or `$` see what is
     * really there (see \ref forward_end_look).
     *
     * \param[in] text  Subject.
     * \param[in] start Offset the search starts at (the first seed).
     * \return The match end, as an offset in \p text, or \ref real::npos when there is none (or the program
     *         is ineligible).
     */
    [[nodiscard]] std::size_t forward_end(std::string_view text,
                                          std::size_t      start = 0)
    {
      if (!plain_) {
        if (!eligible_) {
          return npos;
        }
        begin_scan();
        return forward_end_look(text, start);
      }
      begin_scan();
      std::uint32_t       state    {start_state_}; // the seed at the start (a re-seeding state)
      std::size_t         best_end {npos};
      bool                matched  {false};
      std::size_t         pos      {start};
      const std::uint16_t count    {alpha_.count};
      while (true) {
        const std::uint32_t midx {state_match_idx_[state]};
        if (midx != no_match_idx) {
          best_end = pos;               // the highest-priority accept lives at index midx; a higher thread may extend it
          matched  = true;
          state    = cut_cached(state); // drop the accept and every lower-priority thread after it (memoized)
          if (state == dead_state) {
            break; // nothing higher-priority survives to extend the match
          }
        }
        if (pos >= text.size() || state == dead_state) {
          break;
        }
        const std::uint8_t byte {static_cast<std::uint8_t>(text[pos])};
        // pre-match transitions re-seed (unanchored search continues); post-match ones do not (leftmost).
        // The cached edge read inline, as the anchored scan does; step()/step_seeded() only on a miss.
        const std::uint32_t cached {(matched ? trans_ : trans_seeded_)[(static_cast<std::size_t>(state) * count) + alpha_.of[byte]]};
        if (cached != no_transition) {
          state = cached;
        }
        else {
          state = matched ? step(state, byte) : step_seeded(state, byte);
        }
        ++pos;
      }
      return (thrashing_ && may_quit_) ? quit_pos : best_end;
    }

    /*! \brief \ref anchored_end's result: the match end (or \ref real::npos) and how far the walk got. */
    struct anchored_result
    {
      std::size_t end        {npos};  //!< Match end, or \ref real::npos.
      std::size_t scanned_to {0};     //!< Position the walk stopped at (see \ref anchored_end).
      bool        quit       {false}; //!< A Unicode word boundary met a non-ASCII byte: the answer is the VM's.
    };

    /*!
     * \brief The end offset of the leftmost-first match ANCHORED at \p start in \p text, or \ref
     *        real::npos.
     *
     * Identical walk to \ref forward_end except it never re-seeds: a single thread is seeded once, at
     * \p start (\ref step, never \ref step_seeded), so a match must begin exactly there. The caller
     * already knows \p start is a valid candidate (a prefilter hit) -- this skips the reverse pass
     * \ref forward_end normally needs to recover the start, since there is nothing left to recover.
     * Eligible programs only (an ineligible one returns \ref real::npos; the caller keeps the Pike VM).
     * No captures: this reports the end only, exactly like \ref forward_end.
     *
     * Does NOT call \ref begin_scan (unlike \ref forward_end): a caller trying several candidates in a
     * loop for one logical search calls \ref begin_scan itself, ONCE, before the loop -- resetting the
     * thrash flag per CANDIDATE rather than per search would mask real thrashing across the loop.
     *
     * The lean munch (A3): once find_iter has warmed a search up, the small state set a pattern like
     * `[a-z][a-z]+` actually visits is already fully cached -- every (state, class) transition and every
     * state's priority-cut already sit in \ref trans_ / \ref state_cut_. \ref step and \ref cut_cached
     * exist to COMPUTE and cache those on a miss; paying their call overhead plus the "is this already
     * built?" branch on every byte, when the answer is essentially always yes post-warm-up, is exactly
     * the residual cost profiling pinned here (step + cut_cached + this function itself). So the loop
     * below inlines the two lookups directly -- a flat class-then-transition read per byte, an
     * accept check against a local, no function call at all in the common case -- and falls back to the
     * real (state-building, cache-filling, flush-aware) \ref step / \ref cut_cached only on an actual
     * miss, which is rare once the small state set has been visited once. Same states, same tables, same
     * memoization \ref step / \ref cut_cached would have produced -- this is a leaner READ of them, not a
     * different automaton. One accounting gap: the inlined hits do not increment \ref counters::hits
     * (that counter is a step()/cut_cached()-callers' bookkeeping aid, not behavior -- \ref thrashing
     * and \ref flush only ever move on an actual miss, which still goes through the real functions).
     *
     * \param[in] text  The subject text.
     * \param[in] start Offset to anchor the match at (must be `<= text.size()`).
     * \return The match end and the position the walk stopped at (\ref anchored_result::scanned_to). On
     *         a miss (`end == real::npos`), `scanned_to == text.size()` means the walk consumed the
     *         whole remaining haystack without a dead state pruning it -- an *unbounded* reach (e.g.
     *         `.*` with no terminator ahead), as opposed to one the pattern's own structure bounded (a
     *         dead state hit before the end). A caller trying candidate after candidate (\ref
     *         real::detail::pike_vm's A2 route) uses this to tell "this candidate's reach is bounded,
     *         the next one is cheap too" apart from "every candidate from here will re-scan to the end"
     *         -- the O(n^2) regime.
     */
    [[nodiscard]] anchored_result anchored_end(std::string_view text,
                                               std::size_t      start)
    {
      // One test on the common path, as before programs with assertions had a scan of their own: this
      // runs once per candidate position, so a second one there is an instruction per byte scanned.
      if (!plain_) {
        if (!eligible_) {
          return {.end = npos, .scanned_to = start};
        }
        return anchored_end_look(text, start);
      }
      std::uint32_t       state    {start_state_};
      std::size_t         best_end {npos};
      std::size_t         pos      {start};
      const std::uint16_t count    {alpha_.count};
      while (true) {
        const std::uint32_t midx {state_match_idx_[state]};
        if (midx != no_match_idx) {
          best_end = pos;                                           // the highest-priority accept lives at index midx; a higher thread may extend it
          const std::uint32_t cut {state_cut_[state]};
          state = (cut != no_transition) ? cut : cut_cached(state); // already-memoized cut, or build + memoize it
          if (state == dead_state) {
            break; // nothing higher-priority survives to extend the match
          }
        }
        if (pos >= text.size() || state == dead_state) {
          break;
        }
        const std::uint8_t  byte  {static_cast<std::uint8_t>(text[pos])};
        const std::uint8_t  cls   {alpha_.of[byte]};
        const std::uint32_t trans {trans_[(static_cast<std::size_t>(state) * count) + cls]};
        state = (trans != no_transition) ? trans : step(state, byte); // anchored: never re-seed -- a match starts at `start` or not at all
        ++pos;
      }
      if (thrashing_ && may_quit_) {
        return {.end = npos, .scanned_to = pos, .quit = true};
      }
      return {.end = best_end, .scanned_to = pos};
    }

    /*!
     * \brief Whether the program carries position assertions: a caller that confirms a match found here by
     *        running the Pike VM over a slice must not cut the slice at the match end, where a `$` or a `\b`
     *        would read the cut as the end of the text.
     * \return True when it does.
     */
    [[nodiscard]] bool looks() const
    {
      return look_;
    }

    /*!
     * \brief Whether \p state accepts here (its ordered set contains a `match` PC).
     * \param[in] state The state id to test.
     * \return True when the state holds an accept.
     */
    [[nodiscard]] bool is_match(std::uint32_t state) const
    {
      return state_match_idx_[state] != no_match_idx;
    }

    /*!
     * \brief Transition \p state on \p byte to the next DFA state, computing and caching it on first use.
     *
     * \param[in] state The current state id.
     * \param[in] byte  The byte consumed.
     * \return The successor state, or \ref dead_state when no thread survives the byte. On a flush mid-step
     *         the id is a fresh post-flush one and the caller's \p state is stale.
     */
    std::uint32_t step(std::uint32_t state,
                       std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {trans_[(static_cast<std::size_t>(state) * alpha_.count) + cls]};   // by value: intern() below may realloc
      if (cached != no_transition) {
        ++stats_.hits;
        return cached;
      }
      ++stats_.misses;
      std::vector<std::int32_t> next;
      std::vector<char>         seen(code_.size(), 0);
      const std::uint8_t        ctx {class_ctx_[cls]};
      for (const std::int32_t pc : state_pcs_[state]) {
        if (consumes(pc, byte)) {
          close_any(pc + consumed_width(pc), next, seen, ctx);
        }
      }
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern_any(std::move(next), ctx)}; // may grow/flush the tables — do not hold a reference
      if (stats_.flushes == flushes_before) {
        trans_[(static_cast<std::size_t>(state) * alpha_.count) + cls] = result;   // no flush: `state` is still valid, so cache the edge
      }
      // On a flush mid-step the caller's `state` id is stale; `result` is a fresh post-flush id, and the
      // caller re-seeds. A scan that may quit stops here once the cache thrashes: the dead state ends its
      // loop, and the scan reports a quit.
      return (thrashing_ && may_quit_) ? dead_state : result;
    }

  private:

    /*!
     * \brief Like \ref step, but re-seeds: the unanchored-search variant appends pc 0's closure at the
     *        lowest priority, so a fresh thread starts at every position until a match is found. Cached in
     *        its own transition row (the pre-match state family).
     * \param[in] state The current state id.
     * \param[in] byte  The byte consumed.
     * \return The successor state, with a fresh thread appended at the lowest priority.
     */
    std::uint32_t step_seeded(std::uint32_t state,
                              std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {trans_seeded_[(static_cast<std::size_t>(state) * alpha_.count) + cls]};
      if (cached != no_transition) {
        ++stats_.hits;
        return cached;
      }
      ++stats_.misses;
      const std::vector<std::int32_t> pcs {state_pcs_[state]}; // copy: intern() below may realloc state_pcs_
      std::vector<std::int32_t>       next;
      std::vector<char>               seen(code_.size(), 0);
      const std::uint8_t              ctx {class_ctx_[cls]};
      for (const std::int32_t pc : pcs) {
        if (consumes(pc, byte)) {
          close_any(pc + consumed_width(pc), next, seen, ctx);
        }
      }
      close_any(0, next, seen, ctx); // re-seed at the lowest priority (deduped against the advanced threads)
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern_any(std::move(next), ctx)};
      if (stats_.flushes == flushes_before) {
        trans_seeded_[(static_cast<std::size_t>(state) * alpha_.count) + cls] = result;
      }
      return (thrashing_ && may_quit_) ? dead_state : result; // see step()
    }

    /*!
     * \brief The priority-cut at an accept: intern the prefix of \p state's ordered pc-set before index
     *        \p m (dropping the accept and every lower-priority thread).
     * \param[in] state The accepting state id.
     * \param[in] m     Index of the accept in that state's ordered pc-set.
     * \return The interned prefix state, or \ref dead_state when the prefix is empty.
     */
    std::uint32_t cut(std::uint32_t state,
                      std::uint32_t m)
    {
      // Copy the prefix element-by-element before interning, which may reallocate state_pcs_ (the intern
      // dangling-ref trap). A loop rather than an iterator-range copy — the latter trips a g++ false
      // -Werror=free-nonheap-object here.
      std::vector<std::int32_t> prefix;
      prefix.reserve(m);
      for (std::uint32_t i = 0; i < m; ++i) {
        prefix.push_back(state_pcs_[state][i]);
      }
      return intern(prefix);
    }

    /*!
     * \brief The priority-cut of \p state at its own accept, memoized. `cut` is O(state size) — it rebuilds
     *        and re-interns the prefix — and a Unicode `klass_cp` byte-program makes states thousands of PCs
     *        wide, so recomputing it once per match (per find_iter step) dominated. Cached per state, it is
     *        computed once and then O(1). The state's accept index is fixed, so the cut is deterministic.
     * \param[in] state The accepting state id.
     * \return The cut state, memoized after the first call.
     */
    std::uint32_t cut_cached(std::uint32_t state)
    {
      const std::uint32_t memo {state_cut_[state]};
      if (memo != no_transition) {
        return memo;
      }
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {cut(state, state_match_idx_[state])}; // intern may flush/realloc
      if (stats_.flushes == flushes_before) {
        state_cut_[state] = result; // no flush: `state` is still valid, memoise the edge
      }
      return (thrashing_ && may_quit_) ? dead_state : result; // see step()
    }

    /*!
     * \brief Scan \p code for an op no forward DFA can represent.
     * \param[in] code       The program's instruction stream.
     * \param[in] ascii_word Whether a word boundary's word-ness is ASCII (then a byte decides it).
     * \return True when every op is representable.
     */
    static constexpr bool compute_eligibility(std::span<const instr> code,
                                              bool                   ascii_word)
    {
      // Variable-width classes and lookarounds: no forward-DFA representation. Tier 1's possessive-loop
      // family has no consuming-edge representation here (consumes() below only recognizes byte/klass) —
      // treating it as a dead end (silently non-consuming) would be an outright wrong DFA. A position
      // assertion is represented (see close_look / resolve) unless it is a word boundary whose word-ness is
      // a code point's, which no single byte decides, or one scoped to the other word-ness.
      return std::ranges::none_of(code, [ascii_word](const instr& in) {
                                    if (in.op == opcode::assert_position) {
                                      const auto kind {static_cast<assert_kind>(in.arg8)};
                                      const bool word {kind == assert_kind::word_boundary || kind == assert_kind::not_word_boundary
                                                       || kind == assert_kind::word_start || kind == assert_kind::word_end};
                                      return in.arg16 != 0 || (word && !ascii_word);
                                    }
                                    return in.op == opcode::assert_lookaround || in.op == opcode::klass_cp
                                           || in.op == opcode::byte_loop_possessive || in.op == opcode::klass_loop_possessive
                                           || in.op == opcode::klass_cp_loop_possessive;
                                  });
    }

    // What a DFA state knows of the text before its position, and what a pending assertion needs of the
    // text after it. Contexts are bits; the look-ahead is a key: a byte class, the end of the text, or a
    // newline that is the text's last byte (Python's `$` holds before one).
    static constexpr std::uint8_t  ctx_start      {1};       //!< The position is the start of the text.
    static constexpr std::uint8_t  ctx_newline    {2};       //!< The byte before it is a newline.
    static constexpr std::uint8_t  ctx_word       {4};       //!< The byte before it is an ASCII word byte.
    static constexpr std::uint8_t  ctx_nonascii   {6};       //!< The byte before it is not ASCII (word_quit only): newline and word at once, which no ASCII byte is.
    static constexpr std::uint16_t key_unknown    {0xFFFFU}; //!< Closing inside a step: the next byte is not known yet.

    /*!
     * \brief Whether context \p ctx says the byte it describes is a newline.
     * \param[in] ctx Context bits.
     * \return True for a newline; false for a non-ASCII byte, which also carries the newline bit.
     */
    [[nodiscard]] static constexpr bool is_newline_ctx(std::uint8_t ctx)
    {
      return (ctx & ctx_nonascii) == ctx_newline;
    }

    /*!
     * \brief Whether context \p ctx says the byte it describes is an ASCII word byte.
     * \param[in] ctx Context bits.
     * \return True for an ASCII word byte.
     */
    [[nodiscard]] static constexpr bool is_word_ctx(std::uint8_t ctx)
    {
      return (ctx & ctx_nonascii) == ctx_word;
    }

    /*!
     * \brief Whether context \p ctx says the byte it describes is not ASCII.
     * \param[in] ctx Context bits.
     * \return True for a non-ASCII byte under word_quit.
     */
    [[nodiscard]] static constexpr bool is_nonascii_ctx(std::uint8_t ctx)
    {
      return (ctx & ctx_nonascii) == ctx_nonascii;
    }

    /*!
     * \brief The context a position has after \p b.
     * \param[in] b The byte before the position.
     * \return Its context bits.
     */
    [[nodiscard]] static constexpr std::uint8_t ctx_of(std::uint8_t b)
    {
      const bool word {(b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'};
      return static_cast<std::uint8_t>((b == '\n' ? ctx_newline : 0U) | (word ? ctx_word : 0U));
    }

    /*!
     * \brief The key the text gives a pending assertion at \p pos: the class of the byte there, or the end.
     * \param[in] text The subject.
     * \param[in] pos  The position.
     * \return The resolution key.
     */
    [[nodiscard]] std::uint16_t key_at(std::string_view text,
                                       std::size_t      pos) const
    {
      if (pos >= text.size()) {
        return static_cast<std::uint16_t>(alpha_.count);      // the end of the text
      }
      if (text[pos] == '\n' && pos + 1U == text.size()) {
        return static_cast<std::uint16_t>(alpha_.count + 1U); // a final newline
      }
      return alpha_.of[static_cast<std::uint8_t>(text[pos])];
    }

    /*!
     * \brief Whether an assertion that looks left holds with context \p ctx.
     * \param[in] kind The assertion.
     * \param[in] ctx  The position's context.
     * \return True when it holds; always false for an assertion that also looks right (it waits instead).
     */
    [[nodiscard]] static constexpr bool holds_behind(assert_kind  kind,
                                                     std::uint8_t ctx)
    {
      if (kind == assert_kind::text_start) {
        return (ctx & ctx_start) != 0U;
      }
      return (ctx & ctx_start) != 0U || is_newline_ctx(ctx); // line_start
    }

    /*!
     * \brief Whether an assertion that looks right holds, given the position's context and the key ahead.
     * \param[in] kind The assertion.
     * \param[in] ctx  The position's context.
     * \param[in] key  What follows (\ref key_at).
     * \return True when it holds.
     */
    [[nodiscard]] bool holds_ahead(assert_kind   kind,
                                   std::uint8_t  ctx,
                                   std::uint16_t key) const
    {
      const bool end        {key == alpha_.count};
      const bool final_nl   {key == alpha_.count + 1U};
      const bool next_nl    {final_nl || (key < alpha_.count && is_newline_ctx(class_ctx_[key]))};
      const bool next_word  {key < alpha_.count && is_word_ctx(class_ctx_[key])};
      const bool prev_word  {is_word_ctx(ctx)};
      if (word_quit_ && undecidable_word(kind, prev_word, is_nonascii_ctx(ctx), next_word,
                                         key < alpha_.count && is_nonascii_ctx(class_ctx_[key]))) {
        quit_hit_ = true; // resolve() turns the whole resolution into quit_state
        return false;
      }
      switch (kind) {
        case assert_kind::text_end:                  return end;
        case assert_kind::text_end_or_final_newline: return end || final_nl;
        case assert_kind::line_end:                  return end || next_nl;
        case assert_kind::word_boundary:             return prev_word != next_word;
        case assert_kind::not_word_boundary:         return prev_word == next_word;
        case assert_kind::word_start:                return !prev_word && next_word;
        case assert_kind::word_end:                  return prev_word && !next_word;
        case assert_kind::text_start:
        case assert_kind::line_start:                return holds_behind(kind, ctx);
      }
      return false;
    }

    /*!
     * \brief Whether \p kind looks only left, so a closure decides it without the next byte.
     * \param[in] kind The assertion.
     * \return True for `\A`, `^`.
     */
    [[nodiscard]] static constexpr bool looks_behind_only(assert_kind kind)
    {
      return kind == assert_kind::text_start || kind == assert_kind::line_start;
    }

    /*!
     * \brief Whether the instruction at \p pc is a consuming edge that accepts \p byte.
     * \param[in] pc   Program counter to test.
     * \param[in] byte The byte offered to it.
     * \return True for a `byte`/`klass` op accepting it; false for any non-consuming op.
     */
    [[nodiscard]] bool consumes(std::int32_t pc,
                                std::uint8_t byte) const
    {
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      if (in.op == opcode::byte) {
        return static_cast<std::uint8_t>(in.arg8) == byte;
      }
      if (in.op == opcode::klass) {
        return classes_[in.arg16].test(byte);
      }
      return false;   // match / anything else: not a consuming edge
    }

    /*!
     * \brief Instructions a consuming op occupies. Always 1 here: the byte program has no wider op left.
     * \return 1.
     */
    [[nodiscard]] static std::int32_t consumed_width(std::int32_t /*pc*/)
    {
      return 1;
    }

    /*!
     * \brief Append the ordered epsilon-closure of \p pc to \p out (split priority; save/jump crossed),
     *        collecting the consuming and `match` PCs. Uses \p seen to dedup within this closure.
     * \param[in]     pc   Program counter to close over.
     * \param[in,out] out  Ordered pc-set the closure is appended to.
     * \param[in,out] seen Per-pc visited marks, sized to the program, deduping within this closure.
     */
    constexpr void close_into(std::int32_t               pc,
                              std::vector<std::int32_t>& out,
                              std::vector<char>&         seen) const
    {
      // Structurally unreachable through the only two callers (step/step_seeded, both private): every pc
      // they pass is either 0 (the program start) or pc+1 of a consuming instruction's own valid pc, and a
      // well-formed program never ends on a byte/klass op with nothing after it (a `match` always follows
      // eventually) -- so pc+1 never runs off the end in practice. Kept as the defensive bound close_into's
      // OWN recursion-turned-stack-loop below also relies on (a split/jump target is trusted, not re-
      // checked, past this point); testing it would need a hand-crafted malformed program, not a pattern
      // the compiler can produce.
      if (pc < 0 || static_cast<std::size_t>(pc) >= code_.size()) {
        return;
      }
      // The work stack is a MEMBER, not a local. This runs once per pc of the source state, so a
      // local vector was one heap block per call -- and a single 8 KB first search was measured at
      // 10 408 allocations across the two DFAs (2026.08 landscape, `\w+@\w+` when the inner-literal
      // route confirmed through the DFA; see state_pcs_'s comment for why the route is named).
      // `mutable`: pure scratch, empty in and empty out.
      stack_.assign(1, pc);
      while (!stack_.empty()) {
        const std::int32_t cur {stack_.back()};
        stack_.pop_back();
        if (cur < 0 || static_cast<std::size_t>(cur) >= code_.size() || seen[static_cast<std::size_t>(cur)] != 0) {
          continue;
        }
        seen[static_cast<std::size_t>(cur)] = 1;
        const instr& in {code_[static_cast<std::size_t>(cur)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:
          case opcode::match:
            out.push_back(cur);
            break;
          case opcode::split:
            stack_.push_back(in.secondary_target);   // secondary pushed first -> primary explored first
            stack_.push_back(in.primary_target);
            break;
          case opcode::jump:
            stack_.push_back(in.primary_target);
            break;
          case opcode::save:
            stack_.push_back(cur + 1);
            break;
          default:
            break;   // assert / klass_cp / lookaround: only reachable for ineligible programs (not built)
        }
      }
    }

    /*!
     * \brief \ref close_into for a program with position assertions: an assertion that looks left is decided
     *        by \p ctx; one that looks right is decided by \p key when it is known, and otherwise waits in
     *        \p out as a pending pc, in its priority place, until \ref resolve knows what follows.
     * \param[in]     pc   Program counter to close over.
     * \param[in,out] out  Ordered pc-set the closure is appended to.
     * \param[in,out] seen Per-pc visited marks.
     * \param[in]     ctx  The position's context.
     * \param[in]     key  What follows the position, or \ref key_unknown.
     */
    constexpr void close_look(std::int32_t               pc,
                              std::vector<std::int32_t>& out,
                              std::vector<char>&         seen,
                              std::uint8_t               ctx,
                              std::uint16_t              key) const
    {
      stack_.assign(1, pc);
      while (!stack_.empty()) {
        const std::int32_t cur {stack_.back()};
        stack_.pop_back();
        if (cur < 0 || static_cast<std::size_t>(cur) >= code_.size() || seen[static_cast<std::size_t>(cur)] != 0) {
          continue;
        }
        seen[static_cast<std::size_t>(cur)] = 1;
        const instr& in {code_[static_cast<std::size_t>(cur)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:
          case opcode::match:
            out.push_back(cur);
            break;
          case opcode::split:
            stack_.push_back(in.secondary_target);
            stack_.push_back(in.primary_target);
            break;
          case opcode::jump:
            stack_.push_back(in.primary_target);
            break;
          case opcode::save:
            stack_.push_back(cur + 1);
            break;
          case opcode::assert_position:
            {
              const auto kind {static_cast<assert_kind>(in.arg8)};
              if (looks_behind_only(kind)) {
                if (holds_behind(kind, ctx)) {
                  stack_.push_back(cur + 1);
                }
              }
              else if (key == key_unknown) {
                out.push_back(cur); // pending: decided by what follows
              }
              else if (holds_ahead(kind, ctx, key)) {
                stack_.push_back(cur + 1);
              }
              break;
            }
          default:
            break;
        }
      }
    }

    /*!
     * \brief The closure a step takes: \ref close_look when the program carries assertions, else
     *        \ref close_into (which never reads \p ctx).
     * \param[in]     pc   Program counter to close over.
     * \param[in,out] out  Ordered pc-set.
     * \param[in,out] seen Per-pc visited marks.
     * \param[in]     ctx  The context after the byte just consumed.
     */
    constexpr void close_any(std::int32_t               pc,
                             std::vector<std::int32_t>& out,
                             std::vector<char>&         seen,
                             std::uint8_t               ctx) const
    {
      if (look_) {
        close_look(pc, out, seen, ctx, key_unknown);
      }
      else {
        close_into(pc, out, seen);
      }
    }

    /*!
     * \brief Interns \p pcs; a set holding a pending assertion also holds its context, as a negative
     *        sentinel at its end, because the same pcs with another context resolve differently.
     * \param[in] pcs The ordered pc-set.
     * \param[in] ctx Its position's context.
     * \return The state id.
     */
    constexpr std::uint32_t intern_any(std::vector<std::int32_t> pcs,
                                       std::uint8_t              ctx)
    {
      if (look_ && std::ranges::any_of(pcs, [this](std::int32_t pc) {
                                         return code_[static_cast<std::size_t>(pc)].op == opcode::assert_position;
                                       })) {
        pcs.push_back(-1 - static_cast<std::int32_t>(ctx));
      }
      return intern(pcs);
    }

    /*!
     * \brief \p state with its pending assertions decided by \p key: each is replaced, in its priority place,
     *        by the closure past it when it holds and by nothing when it does not. A state with nothing
     *        pending is its own resolution. Cached per (state, key).
     * \param[in] state The state.
     * \param[in] key   What follows its position (\ref key_at).
     * \return The resolved state, which holds no pending assertion.
     */
    std::uint32_t resolve(std::uint32_t state,
                          std::uint16_t key)
    {
      if (state_pending_[state] == 0U) {
        return state;
      }
      const std::size_t   slot   {(static_cast<std::size_t>(state) * (alpha_.count + 2U)) + key};
      const std::uint32_t cached {res_[slot]};
      if (cached != no_transition) {
        return cached;
      }
      std::vector<std::int32_t> pcs {state_pcs_[state]}; // copy: intern() below may realloc state_pcs_
      const auto                ctx {static_cast<std::uint8_t>(-1 - pcs.back())};
      pcs.pop_back();
      std::vector<std::int32_t> out;
      std::vector<char>         seen(code_.size(), 0);
      quit_hit_ = false;
      for (const std::int32_t pc : pcs) {
        const instr& in {code_[static_cast<std::size_t>(pc)]};
        if (in.op == opcode::assert_position) {
          if (holds_ahead(static_cast<assert_kind>(in.arg8), ctx, key)) {
            close_look(pc + 1, out, seen, ctx, key);
          }
        }
        else if (seen[static_cast<std::size_t>(pc)] == 0) {
          seen[static_cast<std::size_t>(pc)] = 1;
          out.push_back(pc);
        }
      }
      if (quit_hit_) {
        res_[slot] = quit_state; // the same state and key meet the same byte: memoizing it is exact
        return quit_state;
      }
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern(out)};
      if (stats_.flushes == flushes_before) {
        res_[slot] = result;
      }
      return result;
    }

    /*!
     * \brief The start state for a position with context \p ctx: the closure of pc 0 there. Cached per
     *        context; a program without assertions has one start whatever the context.
     * \param[in] ctx The start position's context.
     * \return Its state id.
     */
    std::uint32_t start_for(std::uint8_t ctx)
    {
      if (!look_) {
        return start_state_;
      }
      if (starts_[ctx] != no_transition) {
        return starts_[ctx];
      }
      std::vector<std::int32_t> pcs;
      std::vector<char>         seen(code_.size(), 0);
      close_look(0, pcs, seen, ctx, key_unknown);
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern_any(std::move(pcs), ctx)};
      if (stats_.flushes == flushes_before) {
        starts_[ctx] = result;
      }
      return result;
    }

    /*!
     * \brief The context of position \p pos in \p text.
     * \param[in] text The subject.
     * \param[in] pos  The position.
     * \return The start context at 0, else the context after the byte before \p pos.
     */
    [[nodiscard]] std::uint8_t ctx_at(std::string_view text,
                                      std::size_t      pos) const
    {
      return pos == 0 ? ctx_start : class_ctx_[alpha_.of[static_cast<std::uint8_t>(text[pos - 1U])]];
    }

    /*!
     * \brief \ref forward_end for a program with position assertions. Each position's state is first resolved
     *        by what follows it (\ref resolve), and the accept test and the step read the resolved state, so an
     *        accept that a `$` or a `\b` guards is decided with the text on both sides of it.
     * \param[in] text  Subject.
     * \param[in] start The first seed's position.
     * \return The match end in \p text, or \ref real::npos.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold)) // out of the hot scans' bodies: a program without assertions never calls it
#endif
    std::size_t forward_end_look(std::string_view text,
                                 std::size_t      start)
    {
      std::uint32_t       state    {start_for(ctx_at(text, start))};
      std::size_t         best_end {npos};
      bool                matched  {false};
      std::size_t         pos      {start};
      const std::uint16_t count    {alpha_.count};
      while (true) {
        // Only a state holding a pending assertion reads what follows it; the rest are their own resolution.
        std::uint32_t here {state};
        if (state_pending_[state] != 0U) {
          // The memoized resolution read inline, as the cached edges are; resolve() only on a miss.
          const std::uint16_t key  {key_at(text, pos)};
          const std::uint32_t memo {res_[(static_cast<std::size_t>(state) * (count + 2U)) + key]};
          here = memo != no_transition ? memo : resolve(state, key);
          if (here == quit_state) {
            return quit_pos;
          }
        }
        if (state_match_idx_[here] != no_match_idx) {
          best_end = pos;
          matched  = true;
          here     = cut_cached(here);
          if (here == dead_state) {
            break;
          }
        }
        // Before a match, a dead state is not the end: an assertion can kill every thread at one position
        // and let the next seed live (`^` after a newline), so the scan keeps seeding.
        if (pos >= text.size() || (here == dead_state && matched)) {
          break;
        }
        const auto byte {static_cast<std::uint8_t>(text[pos])};
        // In text mode a match does not start inside a code point: the step onto a continuation byte
        // carries the threads without a fresh seed.
        const bool seed            {!matched && (byte_mode_ || !starts_inside_code_point(text, pos + 1U))};
        // The cached edge read inline, as the plain scans do; step()/step_seeded() only on a miss.
        const std::uint32_t cached {(seed ? trans_seeded_ : trans_)[(static_cast<std::size_t>(here) * count) + alpha_.of[byte]]};
        if (cached != no_transition) {
          state = cached;
        }
        else {
          state = seed ? step_seeded(here, byte) : step(here, byte);
          if (thrashing_ && may_quit_) {
            return quit_pos; // before a match the dead state keeps seeding, so the loop would not end on it
          }
        }
        ++pos;
      }
      return best_end;
    }

    /*!
     * \brief Whether \p pos is inside a UTF-8 code point: the byte there is a continuation byte.
     * \param[in] text The subject.
     * \param[in] pos  The position.
     * \return True inside a code point; false at the end or at a code point's first byte.
     */
    [[nodiscard]] static constexpr bool starts_inside_code_point(std::string_view text,
                                                                 std::size_t      pos)
    {
      return pos < text.size() && (static_cast<std::uint8_t>(text[pos]) & 0xC0U) == 0x80U;
    }

    /*!
     * \brief \ref anchored_end for a program with position assertions (see \ref forward_end_look).
     * \param[in] text  Subject.
     * \param[in] start The anchor.
     * \return The match end and how far the walk got.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold)) // out of the hot scans' bodies: a program without assertions never calls it
#endif
    anchored_result anchored_end_look(std::string_view text,
                                      std::size_t      start)
    {
      std::uint32_t       state    {start_for(ctx_at(text, start))};
      std::size_t         best_end {npos};
      std::size_t         pos      {start};
      const std::uint16_t count    {alpha_.count};
      while (true) {
        std::uint32_t here {state};
        if (state_pending_[state] != 0U) {
          // The memoized resolution read inline, as the cached edges are; resolve() only on a miss.
          const std::uint16_t key  {key_at(text, pos)};
          const std::uint32_t memo {res_[(static_cast<std::size_t>(state) * (count + 2U)) + key]};
          here = memo != no_transition ? memo : resolve(state, key);
          if (here == quit_state) {
            // Even past an accept: whether a longer match wins is what the boundary would have told.
            return {.end = npos, .scanned_to = pos, .quit = true};
          }
        }
        if (state_match_idx_[here] != no_match_idx) {
          best_end = pos;
          const std::uint32_t cut {state_cut_[here]};
          here = cut != no_transition ? cut : cut_cached(here);
          if (here == dead_state) {
            break;
          }
        }
        if (pos >= text.size() || here == dead_state) {
          break;
        }
        const auto          byte   {static_cast<std::uint8_t>(text[pos])};
        const std::uint32_t cached {trans_[(static_cast<std::size_t>(here) * count) + alpha_.of[byte]]};
        state = cached != no_transition ? cached : step(here, byte);
        ++pos;
      }
      if (thrashing_ && may_quit_) {
        return {.end = npos, .scanned_to = pos, .quit = true};
      }
      return {.end = best_end, .scanned_to = pos};
    }

    /*!
     * \brief Intern an ordered pc-set into a state id (cached). Flushes the cache when the budget is hit.
     * \param[in] pcs The ordered pc-set.
     * \return Its state id, existing or freshly built; \ref dead_state for an empty set. A flush here
     *         invalidates every id the caller holds.
     */
    constexpr std::uint32_t intern(const std::vector<std::int32_t>& pcs)
    {
      if (pcs.empty()) {
        return dead_state;
      }
      const std::uint32_t found {cache_.find(pcs, state_pcs_)};
      if (found != pc_set_cache::not_found) {
        return found;
      }
      if (state_pcs_.size() >= budget_) {
        flush();
        return intern_fresh(pcs);   // rebuild from empty; the seeded start remains reachable
      }
      return intern_fresh(pcs);
    }

    /*!
     * \brief Append a new state for \p pcs: its two transition rows, its accept index and its empty cut memo.
     * \param[in] pcs The ordered pc-set, known not to be interned yet.
     * \return The new state's id.
     */
    constexpr std::uint32_t intern_fresh(const std::vector<std::int32_t>& pcs)
    {
      const auto id {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
      trans_.insert(trans_.end(), alpha_.count, no_transition);
      trans_seeded_.insert(trans_seeded_.end(), alpha_.count, no_transition);
      std::uint32_t match_idx {no_match_idx};
      bool          pending   {false};
      for (std::size_t i = 0; i < pcs.size(); ++i) {
        if (pcs[i] < 0) {
          continue; // the context sentinel of a state with pending assertions (intern_any)
        }
        const opcode op {code_[static_cast<std::size_t>(pcs[i])].op};
        if (op == opcode::assert_position) {
          pending = true; // an accept past it is not an accept yet: only resolve() decides it
          break;
        }
        if (op == opcode::match && match_idx == no_match_idx) {
          match_idx = static_cast<std::uint32_t>(i); // the highest-priority accept in this state
          if (!look_) {
            break; // nothing can be pending in a program without assertions
          }
        }
      }
      if (pending) {
        match_idx = no_match_idx;
      }
      state_match_idx_.push_back(match_idx);
      state_pending_.push_back(pending ? 1U : 0U);
      if (look_) {
        res_.insert(res_.end(), alpha_.count + 2U, no_transition);
      }
      state_cut_.push_back(no_transition); // the priority-cut result, memoized lazily on first use
      cache_.insert(pcs, id);
      return id;
    }

    /*!
     * \brief Empty the cache back to the dead + start states (the eviction: bounded memory).
     */
    constexpr void flush()
    {
      const bool first {state_pcs_.empty()};
      if (!first) {
        ++stats_.flushes;
        ++stats_.scan_flushes;
        if (stats_.scan_flushes >= thrash_flushes) {
          thrashing_ = true;
        }
      }
      state_pcs_.clear();
      trans_.clear();
      trans_seeded_.clear();
      state_match_idx_.clear();
      state_cut_.clear();
      state_pending_.clear();
      res_.clear();
      starts_.fill(no_transition);
      cache_.clear();
      // state 0 = dead (empty, self-looping), state 1 = start (closure of pc 0).
      state_pcs_.emplace_back();
      trans_.insert(trans_.end(), alpha_.count, dead_state);
      // Re-seeding out of the dead state is a real transition when an assertion can empty a start state:
      // the next position's seed may live. Without assertions no seed is ever empty, so it stays dead.
      trans_seeded_.insert(trans_seeded_.end(), alpha_.count, look_ ? no_transition : dead_state);
      state_match_idx_.push_back(no_match_idx);
      state_cut_.push_back(no_transition);
      state_pending_.push_back(0U);
      if (look_) {
        res_.insert(res_.end(), alpha_.count + 2U, dead_state);
      }
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      if (look_) {
        // The start of the text: the context forward_end and anchored_end at 0 ask for.
        close_look(0, start, seen, ctx_start, key_unknown);
        if (std::ranges::any_of(start, [this](std::int32_t pc) {
                                  return code_[static_cast<std::size_t>(pc)].op == opcode::assert_position;
                                })) {
          start.push_back(-1 - static_cast<std::int32_t>(ctx_start));
        }
        start_state_       = intern_fresh(start);
        starts_[ctx_start] = start_state_;
      }
      else {
        close_into(0, start, seen);
        start_state_ = intern_fresh(start);
      }
    }

    std::span<const instr>        code_;                                                        //!< The byte program, owned by the caller.
    std::span<const char_class>   classes_;                                                     //!< Its byte classes, likewise borrowed.
    lazy_byte_alphabet            alpha_;                                                       //!< Byte-to-class map; its count is the row stride.
    bool                          eligible_    {false};                                         //!< \ref compute_eligibility's verdict, fixed at construction.
    bool                          byte_mode_   {true};                                          //!< A match may start at any byte (else only at a code-point start).
    bool                          word_quit_   {false};                                         //!< Unicode word boundaries carried, quitting next to a non-ASCII byte.
    bool                          may_quit_    {false};                                         //!< A scan may quit: on a Unicode word boundary next to non-ASCII, and once its cache thrashes.
    mutable bool                  quit_hit_    {false};                                         //!< Set by holds_ahead() inside one resolve(): that resolution is quit_state.
    bool                          look_        {false};                                         //!< The program carries position assertions (the look paths).
    bool                          plain_       {false};                                         //!< Eligible and without assertions: the scans' one-test common path.
    std::array<std::uint8_t, 256> class_ctx_   {};                                              //!< Class -> the context after one of its bytes (look programs).
    std::array<std::uint32_t, 8>  starts_      {};                                              //!< Context -> start state, per \ref flush (look programs).
    std::uint32_t                 start_state_ {0};                                             //!< Id of the closure of pc 0, re-interned by each \ref flush.

    // A VECTOR OF VECTORS, one heap block per DFA state. What a first search pays here has been
    // measured twice, and the two measurements DISAGREE because they were taken on different route
    // landscapes. A measured number in a comment must name the ROUTE it was taken on, not only the
    // pattern: routes move (three moved the week this was written), and a number whose routing
    // premise changed reads as current long after it stopped applying. Both are stated with theirs.
    //
    // 2026.08, counted rather than timed: `\w+@\w+` on an 8 KB subject made 16 146 allocations
    // totalling 6.09 MB, against 187 and 163 KB for the ASCII twin `[a-z]+@[a-z]+` -- WHEN THE
    // INNER-LITERAL ROUTE CONFIRMED THROUGH THIS DFA. Attribution was by disabling routes: lazy
    // DFA off, the same search made 5 738 / 0.96 MB; inner-literal off as well, 91 / 117 KB. The
    // per-call locals in the transition path (`step`/`step_seeded`'s `next` and `seen`, the latter
    // also the memset callgrind put at 5 %) plus close_into's stack were essentially all of it.
    //
    // 2026.09.15, same instrument, this tree: `\w+@\w+` no longer reaches the DFA at all -- the
    // inner-literal route carries it whole (DFA knob on/off: 5 649 vs 5 647). A pattern that DOES
    // route here today -- `\w+\d+`, no required literal -- pays 3 317 allocations on the cold
    // first search and ZERO on every warm one, while interning only TWO states. Hoisting the
    // miss-path scratch (`next`/`seen` into members, a generation stamp for the zeroing) was
    // written, measured, and REVERTED on these numbers: it removed 36 of 3 315 cold allocations,
    // ~1 %, far below what this repository's layout instruments can resolve (±3 % floor, code
    // layout, not noise -- BENCHMARKS.md).
    //
    // ATTRIBUTED the same day (benchmarks/alloc_cold_probe.cpp, per-construction-site counting, the
    // parts summed against the total): build_byte_program is 3 171 of the 3 317 (95.6 %), and the
    // UTF-8 tries are 3 125 of that (98.5 % -- `\w` alone 2 757, `\d` 368); the shared alphabet 40,
    // the two DFA constructors 50, the remainder 56. The onepass op_table is NOT among them -- it
    // builds only for patterns with capture groups (pike.hpp's slot_count guard), and this pattern
    // has none. So the cold cost is the trie expansion, paid once per regex object; the miss path
    // is paid per transition. No bench row and no scaling test exercises the cold path -- the
    // consumer verdict lives in the probe's header.
    //
    // STILL TRUE, measured on the 16 146 landscape and worth keeping against the day the
    // scaffolding is fixed and the miss path matters again: reserving the OUTER vector saved 3
    // allocations of 16 146 and cost 98 KB, and FLATTENING this into one pool with an offset per
    // state -- the fix the trie builder uses -- changed the count by exactly ZERO. Whether they
    // still hold on today's scaffolding-dominated cost is unknown until that attribution exists.
    mutable std::vector<std::int32_t>                                          stack_;           //!< close_into's work stack, hoisted: it ran once per pc of the source state.
    std::vector<std::vector<std::int32_t>>                                     state_pcs_;       //!< state id -> ordered pc-set.
    std::vector<std::uint32_t>                                                 trans_;           //!< flat [state*stride + class] -> next, unseeded (post-match); stride = alpha_.count.
    std::vector<std::uint32_t>                                                 trans_seeded_;    //!< flat [state*stride + class] -> next, re-seeding (pre-match).
    std::vector<std::uint32_t>                                                 state_match_idx_; //!< state id -> index of its first accept, or no_match_idx.
    std::vector<std::uint32_t>                                                 state_cut_;       //!< state id -> memoized priority-cut result (no_transition = not yet computed).
    std::vector<std::uint8_t>                                                  state_pending_;   //!< state id -> it holds a pending assertion (look programs).
    std::vector<std::uint32_t>                                                 res_;             //!< flat [state*(count+2) + key] -> resolved state (look programs).
    pc_set_cache                                                               cache_;           //!< pc-set -> state id, the memo behind \ref intern.

    std::size_t budget_    {state_budget};                                                       //!< Cached states tolerated before a \ref flush.
    counters    stats_     {};                                                                   //!< Live counters, exposed by \ref stats.
    bool        thrashing_ {false};                                                              //!< Set once this scan crossed \ref thrash_flushes flushes.
  };

  /*!
   * \brief The start-finder companion to lazy_dfa. Given a match end, it finds the leftmost start (the design
   *        guide §7.6 contract). It runs the *inverted* program — the forward program's edges transposed,
   *        its consuming bytes kept — as a cached DFA over the text scanned right-to-left from the end,
   *        recording an accept each time it reaches the original start (`reverse-kLongest`: the furthest-back
   *        accept is the start). It needs no priority ordering — its states are plain unordered (sorted) PC
   *        sets and its rule is longest — so it is simpler than the forward pass. Dynamic only.
   */
  class reverse_dfa
  {
  public:

    static constexpr std::uint32_t dead_state    {0};           //!< The empty state: every transition from it stays here.
    static constexpr std::uint32_t no_transition {0xFFFFFFFFU}; //!< A not-yet-computed cached transition.
    static constexpr std::uint32_t quit_state    {0xFFFFFFFEU}; //!< What resolve() gives when a Unicode word boundary meets a non-ASCII byte; never interned.
    static constexpr std::size_t   quit_pos      {npos - 1U};   //!< What reverse_start() gives when its scan quit.
    static constexpr std::size_t   state_budget  {4096};        //!< Cached states before a flush (the memory cap).

    /*!
     * \brief Builds the (initially empty) reverse DFA, transposing the program's edges as it goes.
     * \param[in] code         The program's instruction stream (must outlive this object — held as a span).
     * \param[in] classes      The program's interned byte classes (likewise held as a span).
     * \param[in] budget       Cached states before a flush; defaults to \ref state_budget.
     * \param[in] shared_alpha A precomputed alphabet the caller shares per regex, or null to compute it here.
     * \param[in] ascii_word   Whether the program's word boundaries use ASCII word-ness: only then does one
     *                         byte decide them. False, the default, declines any word boundary.
     * \param[in] word_quit    With Unicode word-ness, carry the word boundaries and quit next to a non-ASCII
     *                         byte (see \ref lazy_dfa's).
     */
    explicit constexpr reverse_dfa(std::span<const instr>      code,
                                   std::span<const char_class> classes,
                                   std::size_t                 budget       = state_budget,
                                   const lazy_byte_alphabet*   shared_alpha = nullptr,
                                   bool                        ascii_word   = false,
                                   bool                        word_quit    = false)
      : code_ {code}, classes_ {classes},
        alpha_ {shared_alpha != nullptr ? *shared_alpha : compute_lazy_alphabet(code, classes)},
        eligible_ {compute_eligibility(code, ascii_word || word_quit)}, word_quit_ {word_quit && !ascii_word},
        look_ {std::ranges::any_of(code, [](const instr& in) { return in.op == opcode::assert_position; })},
        budget_ {budget}
    {
      if (look_) {
        for (unsigned b {0}; b < 256U; ++b) {
          class_ctx_[alpha_.of[b]] = (word_quit_ && b >= 0x80U) ? rctx_nonascii : right_ctx_of(static_cast<std::uint8_t>(b));
        }
      }
      // Transpose the program: rev_eps_[x] = the pcs with a forward epsilon edge to x; rev_consume_[x] = the
      // consuming pcs whose successor is x (a byte/klass at pc goes to pc+1).
      // TWO PASSES INTO FOUR BUFFERS, not a vector of vectors. The transpose is an adjacency list, and
      // as one inner vector per pc it was the single largest allocation COUNT in a first search: 7003
      // blocks for `\w+@\w+` over 8 KB, because `\w` expands to a byte program of thousands of
      // instructions and nearly every one gets a first push_back. Counting degrees, prefix-summing and
      // filling gives the same structure in four allocations. Measured by counting, not timing.
      const std::size_t n {code.size()};
      rev_eps_at_.assign(n + 2, 0);
      rev_consume_at_.assign(n + 2, 0);
      const auto bump {[](std::vector<std::uint32_t>& at, std::size_t x) {
                         if (x < at.size() - 1) {
                           ++at[x + 1]; // counted one slot right: the prefix sum below shifts it into place
                         }
                       }};
      for (std::int32_t pc = 0; pc < static_cast<std::int32_t>(n); ++pc) {
        const instr& in {code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:  bump(rev_consume_at_, static_cast<std::size_t>(pc) + 1); break;
          case opcode::split:
            bump(rev_eps_at_, static_cast<std::size_t>(in.primary_target));
            bump(rev_eps_at_, static_cast<std::size_t>(in.secondary_target));
            break;
          case opcode::jump:   bump(rev_eps_at_, static_cast<std::size_t>(in.primary_target)); break;
          case opcode::save:
          case opcode::assert_position: bump(rev_eps_at_, static_cast<std::size_t>(pc) + 1); break;
          case opcode::match:  match_pc_ = pc; break; // the reverse start
          default:             break;
        }
      }
      for (std::size_t i = 1; i < rev_eps_at_.size(); ++i) {
        rev_eps_at_[i]     += rev_eps_at_[i - 1];
        rev_consume_at_[i] += rev_consume_at_[i - 1];
      }
      rev_eps_pool_.assign(rev_eps_at_.back(), 0);
      rev_consume_pool_.assign(rev_consume_at_.back(), 0);
      std::vector<std::uint32_t> eps_cur {rev_eps_at_};
      std::vector<std::uint32_t> con_cur {rev_consume_at_};
      const auto                 put {[](std::vector<std::int32_t>& pool, std::vector<std::uint32_t>& cur,
                                         const std::vector<std::uint32_t>& at, std::size_t x, std::int32_t pc) {
                                        if (x < at.size() - 1) {
                                          pool[cur[x]++] = pc;
                                        }
                                      }};
      for (std::int32_t pc = 0; pc < static_cast<std::int32_t>(n); ++pc) {
        const instr& in {code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:
            put(rev_consume_pool_, con_cur, rev_consume_at_, static_cast<std::size_t>(pc) + 1, pc);
            break;
          case opcode::split:
            put(rev_eps_pool_, eps_cur, rev_eps_at_, static_cast<std::size_t>(in.primary_target), pc);
            put(rev_eps_pool_, eps_cur, rev_eps_at_, static_cast<std::size_t>(in.secondary_target), pc);
            break;
          case opcode::jump:
            put(rev_eps_pool_, eps_cur, rev_eps_at_, static_cast<std::size_t>(in.primary_target), pc);
            break;
          case opcode::save:
          case opcode::assert_position: // an edge the closure crosses only where the assertion holds
            put(rev_eps_pool_, eps_cur, rev_eps_at_, static_cast<std::size_t>(pc) + 1, pc);
            break;
          default:
            break;
        }
      }
      flush();
    }

    /*!
     * \brief Whether the program can be represented at all (no assertion, `klass_cp` or lookaround).
     * \return False when the caller must find the start another way.
     */
    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    /*!
     * \brief The leftmost start of the match ending at \p e, not before \p resume. Scans the text backward
     *        from \p e over the inverted program, keeping the furthest-back position that reaches the
     *        program start (reverse-`kLongest`). Precondition: a match ends at \p e; eligible programs only.
     *
     * \param[in] text   Subject.
     * \param[in] e      The known match end.
     * \param[in] resume Lower bound the backward scan will not cross.
     * \return The leftmost start at or after \p resume, or \ref real::npos when none was reached.
     */
    [[nodiscard]] std::size_t reverse_start(std::string_view text,
                                            std::size_t      e,
                                            std::size_t      resume)
    {
      if (look_) {
        return reverse_start_look(text, e, resume);
      }
      std::uint32_t       state {start_state_}; // rev-closure of the forward `match`
      std::size_t         best  {npos};
      std::size_t         pos   {e};
      const std::uint16_t count {alpha_.count};
      while (true) {
        if (state_has_start_[state] != 0) {
          best = pos; // reached the original start: [pos, e] matches; kLongest keeps the smallest pos
        }
        if (pos <= resume || state == dead_state) {
          break;
        }
        --pos;
        const auto          byte   {static_cast<std::uint8_t>(text[pos])};
        const std::uint32_t cached {trans_[(static_cast<std::size_t>(state) * count) + alpha_.of[byte]]};
        state = cached != no_transition ? cached : step(state, byte); // the cached edge inline; step() on a miss
      }
      return best;
    }

  private:

    // Backward, the text to the RIGHT of a position is what the scan has read, and the text to its left is
    // what it reads next. So the right side is a state's context and the left side is a pending
    // assertion's key: a byte class, or the start of the text.
    static constexpr std::uint8_t  rctx_end      {1};       //!< The position is the end of the text.
    static constexpr std::uint8_t  rctx_newline  {2};       //!< The byte after it is a newline.
    static constexpr std::uint8_t  rctx_word     {4};       //!< The byte after it is an ASCII word byte.
    static constexpr std::uint8_t  rctx_final_nl {8};       //!< The byte after it is a newline that ends the text.
    static constexpr std::uint8_t  rctx_nonascii {6};       //!< The byte after it is not ASCII (word_quit only): newline and word at once, which no ASCII byte is.

    /*!
     * \brief Whether right context \p ctx says the byte it describes is a newline.
     * \param[in] ctx Context bits.
     * \return True for a newline; false for a non-ASCII byte, which also carries the newline bit.
     */
    [[nodiscard]] static constexpr bool is_newline_ctx(std::uint8_t ctx)
    {
      return (ctx & rctx_nonascii) == rctx_newline;
    }

    /*!
     * \brief Whether right context \p ctx says the byte it describes is an ASCII word byte.
     * \param[in] ctx Context bits.
     * \return True for an ASCII word byte.
     */
    [[nodiscard]] static constexpr bool is_word_ctx(std::uint8_t ctx)
    {
      return (ctx & rctx_nonascii) == rctx_word;
    }

    /*!
     * \brief Whether right context \p ctx says the byte it describes is not ASCII.
     * \param[in] ctx Context bits.
     * \return True for a non-ASCII byte under word_quit.
     */
    [[nodiscard]] static constexpr bool is_nonascii_ctx(std::uint8_t ctx)
    {
      return (ctx & rctx_nonascii) == rctx_nonascii;
    }

    static constexpr std::uint16_t key_unknown   {0xFFFFU}; //!< Closing inside a step: the byte to the left is not read yet.
    // A set's entries: a pc reached; an assertion still to decide, encoded below every context sentinel
    // (a decided assertion is an ordinary member -- the consuming edge into it must stay findable); and at
    // most one context sentinel, `-1 - ctx`, whenever something is pending.
    static constexpr std::int32_t  pending_base  {-17};     //!< An undecided assertion at pc `pc` is `pending_base - pc`.

    /*!
     * \brief Whether \p entry encodes an undecided assertion.
     * \param[in] entry A set entry.
     * \return True for a pending assertion.
     */
    [[nodiscard]] static constexpr bool is_pending(std::int32_t entry)
    {
      return entry <= pending_base;
    }

    /*!
     * \brief The right context a position has when \p b follows it (not the text's last byte).
     * \param[in] b The byte after the position.
     * \return Its context bits.
     */
    [[nodiscard]] static constexpr std::uint8_t right_ctx_of(std::uint8_t b)
    {
      const bool word {(b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'};
      return static_cast<std::uint8_t>((b == '\n' ? rctx_newline : 0U) | (word ? rctx_word : 0U));
    }

    /*!
     * \brief The right context of position \p pos in the whole \p text.
     * \param[in] text The subject.
     * \param[in] pos  The position.
     * \return Its context bits.
     */
    [[nodiscard]] std::uint8_t right_ctx_at(std::string_view text,
                                            std::size_t      pos) const
    {
      if (pos >= text.size()) {
        return rctx_end;
      }
      const std::uint8_t ctx {class_ctx_[alpha_.of[static_cast<std::uint8_t>(text[pos])]]};
      return (text[pos] == '\n' && pos + 1U == text.size()) ? static_cast<std::uint8_t>(ctx | rctx_final_nl) : ctx;
    }

    /*!
     * \brief Whether \p kind needs the text to the left of the position (so backward it waits for the next
     *        byte); an assertion that looks only right is decided by the context.
     * \param[in] kind The assertion.
     * \return True for `\A`, `^` and the word boundaries.
     */
    [[nodiscard]] static constexpr bool looks_left(assert_kind kind)
    {
      return kind == assert_kind::text_start || kind == assert_kind::line_start || kind == assert_kind::word_boundary
             || kind == assert_kind::not_word_boundary || kind == assert_kind::word_start || kind == assert_kind::word_end;
    }

    /*!
     * \brief Whether an assertion that looks only right holds with right context \p ctx.
     * \param[in] kind The assertion (`\Z`, `$`, `(?m)$`).
     * \param[in] ctx  The position's right context.
     * \return True when it holds.
     */
    [[nodiscard]] static constexpr bool holds_right(assert_kind  kind,
                                                    std::uint8_t ctx)
    {
      switch (kind) {
        case assert_kind::text_end:                  return (ctx & rctx_end) != 0U;
        case assert_kind::text_end_or_final_newline: return (ctx & rctx_end) != 0U || (ctx & rctx_final_nl) != 0U;
        case assert_kind::line_end:                  return (ctx & rctx_end) != 0U || is_newline_ctx(ctx);
        default:                                     return false;
      }
    }

    /*!
     * \brief Whether an assertion that looks left holds, given the right context and the key to the left.
     * \param[in] kind The assertion.
     * \param[in] ctx  The position's right context.
     * \param[in] key  The class of the byte before the position, or `alpha_.count` at the start of the text.
     * \return True when it holds.
     */
    [[nodiscard]] bool holds_left(assert_kind   kind,
                                  std::uint8_t  ctx,
                                  std::uint16_t key) const
    {
      const bool start     {key == alpha_.count};
      const bool prev_nl   {!start && is_newline_ctx(class_ctx_[key])};
      const bool prev_word {!start && is_word_ctx(class_ctx_[key])};
      const bool next_word {is_word_ctx(ctx)};
      if (word_quit_ && undecidable_word(kind, prev_word, !start && is_nonascii_ctx(class_ctx_[key]), next_word,
                                         is_nonascii_ctx(ctx))) {
        quit_hit_ = true; // resolve() turns the whole resolution into quit_state
        return false;
      }
      switch (kind) {
        case assert_kind::text_start:        return start;
        case assert_kind::line_start:        return start || prev_nl;
        case assert_kind::word_boundary:     return prev_word != next_word;
        case assert_kind::not_word_boundary: return prev_word == next_word;
        case assert_kind::word_start:        return !prev_word && next_word;
        case assert_kind::word_end:          return prev_word && !next_word;
        default:                             return holds_right(kind, ctx);
      }
    }

    /*!
     * \brief \ref rev_closure for a program with position assertions: crossing back over an assertion needs
     *        it to hold at this position. One that looks only right is decided by \p ctx; one that looks
     *        left is decided by \p key when known, and otherwise stays in \p set as a pending pc that the
     *        closure does not cross until \ref resolve reads the byte to the left.
     * \param[in,out] set  The pc-set to close over, in place (sorted on return; unordered by design).
     * \param[in,out] seen Per-pc visited marks.
     * \param[in]     ctx  The position's right context.
     * \param[in]     key  The byte to the left, or \ref key_unknown.
     */
    constexpr void rev_closure_look(std::vector<std::int32_t>& set,
                                    std::vector<char>&         seen,
                                    std::uint8_t               ctx,
                                    std::uint16_t              key) const
    {
      stack_.assign(set.begin(), set.end());
      while (!stack_.empty()) {
        const std::int32_t pc {stack_.back()};
        stack_.pop_back();
        for (std::size_t k = rev_eps_at_[static_cast<std::size_t>(pc)];
             k < rev_eps_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_eps_pool_[k]};
          if (seen[static_cast<std::size_t>(pred)] != 0) {
            continue;
          }
          const instr& in {code_[static_cast<std::size_t>(pred)]};
          if (in.op == opcode::assert_position) {
            const auto kind {static_cast<assert_kind>(in.arg8)};
            if (looks_left(kind) && key == key_unknown) {
              seen[static_cast<std::size_t>(pred)] = 1;
              set.push_back(pending_base - pred); // undecided: its predecessors are reached once it is decided
              continue;
            }
            if (!(looks_left(kind) ? holds_left(kind, ctx, key) : holds_right(kind, ctx))) {
              continue; // the edge does not exist at this position
            }
          }
          seen[static_cast<std::size_t>(pred)] = 1;
          set.push_back(pred);
          stack_.push_back(pred);
        }
      }
      std::sort(set.begin(), set.end());
    }

    /*!
     * \brief Appends \p ctx to \p set as a negative sentinel when \p set holds a pending assertion: the same
     *        pcs resolve differently in another context, so the context is part of the state.
     * \param[in,out] set The sorted pc-set.
     * \param[in]     ctx Its position's right context.
     */
    static constexpr void mark_context(std::vector<std::int32_t>& set,
                                       std::uint8_t               ctx)
    {
      if (std::ranges::any_of(set, [](std::int32_t entry) { return is_pending(entry); })) {
        set.push_back(-1 - static_cast<std::int32_t>(ctx));
      }
    }

    /*!
     * \brief \p state with its pending assertions decided by \p key, the byte to the left (or the start):
     *        each that holds lets the closure continue past it. A state with nothing pending is its own
     *        resolution. Cached per (state, key).
     * \param[in] state The state.
     * \param[in] key   The class of the byte before its position, or `alpha_.count` at the start.
     * \return The resolved state.
     */
    std::uint32_t resolve(std::uint32_t state,
                          std::uint16_t key)
    {
      if (state_pending_[state] == 0U) {
        return state;
      }
      const std::size_t   slot   {(static_cast<std::size_t>(state) * (alpha_.count + 1U)) + key};
      const std::uint32_t cached {res_[slot]};
      if (cached != no_transition) {
        return cached;
      }
      std::vector<std::int32_t> pcs {state_pcs_[state]};                          // copy: intern may realloc
      const auto                ctx {static_cast<std::uint8_t>(-1 - pcs.back())}; // mark_context appends it last
      pcs.pop_back();
      std::vector<char>         seen(code_.size(), 0);
      std::vector<std::int32_t> set;
      quit_hit_ = false;
      for (const std::int32_t entry : pcs) {
        const std::int32_t pc {is_pending(entry) ? pending_base - entry : entry};
        seen[static_cast<std::size_t>(pc)] = 1;
        // A pending assertion the key decides true stays a member: the closure below reaches its predecessors.
        if (!is_pending(entry) || holds_left(static_cast<assert_kind>(code_[static_cast<std::size_t>(pc)].arg8), ctx, key)) {
          set.push_back(pc);
        }
      }
      rev_closure_look(set, seen, ctx, key); // no entry pending: the key decides every assertion it meets
      if (quit_hit_) {
        res_[slot] = quit_state; // the same state and key meet the same bytes: memoizing it is exact
        return quit_state;
      }
      set.erase(std::unique(set.begin(), set.end()), set.end());
      const std::size_t   flushes_before {flushes_};
      const std::uint32_t result         {intern(set)};
      if (flushes_ == flushes_before) {
        res_[slot] = result;
      }
      return result;
    }

    /*!
     * \brief The state that starts the backward scan at a match end with right context \p ctx: the backward
     *        closure of the forward `match` there.
     * \param[in] ctx The match end's right context.
     * \return Its state id.
     */
    std::uint32_t start_for(std::uint8_t ctx)
    {
      if (starts_[ctx] != no_transition) {
        return starts_[ctx];
      }
      std::vector<std::int32_t> set;
      std::vector<char>         seen(code_.size(), 0);
      if (match_pc_ >= 0) {
        seen[static_cast<std::size_t>(match_pc_)] = 1;
        set.push_back(match_pc_);
        rev_closure_look(set, seen, ctx, key_unknown);
      }
      mark_context(set, ctx);
      const std::size_t   flushes_before {flushes_};
      const std::uint32_t result         {intern(set)};
      if (flushes_ == flushes_before) {
        starts_[ctx] = result;
      }
      return result;
    }

    /*!
     * \brief One backward step for a program with assertions, with the right context given rather than read
     *        from the byte's class: the step onto the text's last byte, where a newline is final.
     * \param[in] state The current (resolved) state.
     * \param[in] byte  The byte consumed.
     * \param[in] ctx   The right context of the new position.
     * \return The predecessor state (not cached).
     */
    std::uint32_t step_with(std::uint32_t state,
                            std::uint8_t  byte,
                            std::uint8_t  ctx)
    {
      const std::vector<std::int32_t> pcs {state_pcs_[state]};
      std::vector<std::int32_t>       next;
      std::vector<char>               seen(code_.size(), 0);
      for (const std::int32_t pc : pcs) {
        for (std::size_t k = rev_consume_at_[static_cast<std::size_t>(pc)];
             k < rev_consume_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_consume_pool_[k]};
          if (consumes(pred, byte) && seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            next.push_back(pred);
          }
        }
      }
      rev_closure_look(next, seen, ctx, key_unknown);
      mark_context(next, ctx);
      return intern(next);
    }

    /*!
     * \brief \ref reverse_start for a program with position assertions: each position's state is resolved by
     *        the byte to its left before the accept test and the step, and the scan begins with the right
     *        context the whole text gives the match end.
     * \param[in] text   Subject.
     * \param[in] e      The known match end.
     * \param[in] resume Lower bound the backward scan will not cross.
     * \return The leftmost start at or after \p resume, or \ref real::npos.
     */
    std::size_t reverse_start_look(std::string_view text,
                                   std::size_t      e,
                                   std::size_t      resume)
    {
      std::uint32_t       state {start_for(right_ctx_at(text, e))};
      std::size_t         best  {npos};
      std::size_t         pos   {e};
      const std::uint16_t count {alpha_.count};
      while (true) {
        const std::uint16_t key  {pos == 0 ? alpha_.count : static_cast<std::uint16_t>(alpha_.of[static_cast<std::uint8_t>(text[pos - 1U])])};
        std::uint32_t       here {state};
        if (state_pending_[state] != 0U) {
          // The memoized resolution and the cached edge below are read inline; resolve() and step() only on
          // a miss (this loop runs once per byte of every match).
          const std::uint32_t memo {res_[(static_cast<std::size_t>(state) * (count + 1U)) + key]};
          here = memo != no_transition ? memo : resolve(state, key);
          if (here == quit_state) {
            return quit_pos;
          }
        }
        // No start lands inside a code point in text mode, with no test for it: every consuming path of a
        // text-mode program begins at an ASCII or lead byte, and an empty match sits at an end the forward
        // pass already aligned.
        if (state_has_start_[here] != 0) {
          best = pos;
        }
        if (pos <= resume || here == dead_state) {
          break;
        }
        --pos;
        const auto byte {static_cast<std::uint8_t>(text[pos])};
        // The class of a newline cannot say whether it is the text's last byte, which `$` asks; that one
        // step builds its state with the context read from the text.
        if (byte == '\n' && pos + 1U == text.size()) {
          state = step_with(here, byte, right_ctx_at(text, pos));
        }
        else {
          const std::uint32_t cached {trans_[(static_cast<std::size_t>(here) * count) + alpha_.of[byte]]};
          state = cached != no_transition ? cached : step(here, byte);
        }
      }
      return best;
    }

    /*!
     * \brief Saturate \p set with its backward epsilon-closure, then sort it into a canonical key.
     *        Unordered by design: the reverse rule is longest, so priority carries no meaning here.
     * \param[in,out] set  The pc-set to close over, in place.
     * \param[in,out] seen Per-pc visited marks, sized to the program.
     */
    constexpr void rev_closure(std::vector<std::int32_t>& set,
                               std::vector<char>&         seen) const
    {
      // Member stack, same reason as close_into's: one heap block per call otherwise. Seeded from the
      // whole set here rather than a single pc, since the reverse closure starts from all of them.
      stack_.assign(set.begin(), set.end());
      while (!stack_.empty()) {
        const std::int32_t pc {stack_.back()};
        stack_.pop_back();
        for (std::size_t k = rev_eps_at_[static_cast<std::size_t>(pc)];
             k < rev_eps_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_eps_pool_[k]};
          if (seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            set.push_back(pred);
            stack_.push_back(pred);
          }
        }
      }
      std::sort(set.begin(), set.end()); // unordered: a canonical (sorted) key, no priority
    }

    /*!
     * \brief Transition \p state backward over \p byte, computing and caching the edge on first use.
     * \param[in] state The current state id.
     * \param[in] byte  The byte consumed, read right-to-left.
     * \return The predecessor state, or \ref dead_state when nothing reaches back through \p byte.
     */
    std::uint32_t step(std::uint32_t state,
                       std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {trans_[(static_cast<std::size_t>(state) * alpha_.count) + cls]};
      if (cached != no_transition) {
        return cached;
      }
      const std::vector<std::int32_t> pcs {state_pcs_[state]}; // copy: intern may realloc
      std::vector<std::int32_t>       next;
      std::vector<char>               seen(code_.size(), 0);
      for (const std::int32_t pc : pcs) {
        for (std::size_t k = rev_consume_at_[static_cast<std::size_t>(pc)];
             k < rev_consume_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_consume_pool_[k]};
          if (consumes(pred, byte) && seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            next.push_back(pred);
          }
        }
      }
      if (look_) {
        rev_closure_look(next, seen, class_ctx_[cls], key_unknown);
        mark_context(next, class_ctx_[cls]);
      }
      else {
        rev_closure(next, seen);
      }
      // Same trap lazy_dfa::step guards against: intern() may flush() mid-call (state_pcs_/trans_ cleared
      // and rebuilt from scratch), which makes `state` -- the CALLER's index, captured before this call --
      // stale for the now-reset trans_. Only cache the edge back into trans_[state] when no flush happened
      // this call; a flush means the caller re-seeds anyway (reverse_start reads the RETURNED state, always
      // fresh), so the cache write is simply skipped rather than landing on a wrong or out-of-bounds slot.
      const std::size_t   flushes_before {flushes_};
      const std::uint32_t result         {intern(next)};
      if (flushes_ == flushes_before) {
        trans_[(static_cast<std::size_t>(state) * alpha_.count) + cls] = result;
      }
      return result;
    }

    /*!
     * \brief Whether the instruction at \p pc is a consuming edge that accepts \p byte.
     * \param[in] pc   Program counter to test.
     * \param[in] byte The byte offered to it.
     * \return True for a `byte`/`klass` op accepting it; false for any non-consuming op.
     */
    [[nodiscard]] bool consumes(std::int32_t pc,
                                std::uint8_t byte) const
    {
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      if (in.op == opcode::byte) {
        return static_cast<std::uint8_t>(in.arg8) == byte;
      }
      return in.op == opcode::klass && classes_[in.arg16].test(byte);
    }

    /*!
     * \brief Scan \p code for an op the transposed program cannot represent.
     * \param[in] code       The program's instruction stream.
     * \param[in] ascii_word Whether a word boundary's word-ness is ASCII (then a byte decides it).
     * \return True when every op is representable.
     */
    static constexpr bool compute_eligibility(std::span<const instr> code,
                                              bool                   ascii_word)
    {
      // Tier 1's possessive-loop family has no consuming-edge representation here (consumes() above only
      // recognizes byte/klass), as in the forward DFA. A position assertion is an edge the closure crosses
      // where it holds (rev_closure_look), unless no byte decides it.
      return std::ranges::none_of(code, [ascii_word](const instr& in) {
                                    if (in.op == opcode::assert_position) {
                                      const auto kind {static_cast<assert_kind>(in.arg8)};
                                      const bool word {kind == assert_kind::word_boundary || kind == assert_kind::not_word_boundary
                                                       || kind == assert_kind::word_start || kind == assert_kind::word_end};
                                      return in.arg16 != 0 || (word && !ascii_word);
                                    }
                                    return in.op == opcode::assert_lookaround || in.op == opcode::klass_cp
                                           || in.op == opcode::byte_loop_possessive || in.op == opcode::klass_loop_possessive
                                           || in.op == opcode::klass_cp_loop_possessive;
                                  });
    }

    /*!
     * \brief Intern a sorted pc-set into a state id (cached), recording whether it reaches the program start.
     * \param[in] pcs The sorted pc-set.
     * \return Its state id, existing or freshly built; \ref dead_state for an empty set.
     */
    constexpr std::uint32_t intern(const std::vector<std::int32_t>& pcs)
    {
      if (pcs.empty()) {
        return dead_state;
      }
      const std::uint32_t found {cache_.find(pcs, state_pcs_)};
      if (found != pc_set_cache::not_found) {
        return found;
      }
      if (state_pcs_.size() >= budget_) {
        flush();
      }
      const auto id {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
      trans_.insert(trans_.end(), alpha_.count, no_transition);
      bool has_start {false};
      bool pending   {false};
      for (const std::int32_t pc : pcs) {
        if (pc == 0) { // pc 0 is the program's save-0 start
          has_start = true;
        }
        if (is_pending(pc)) {
          pending = true;
        }
      }
      state_has_start_.push_back(has_start && !pending ? 1 : 0); // a pending state is not an accept until resolved
      state_pending_.push_back(pending ? 1U : 0U);
      if (look_) {
        res_.insert(res_.end(), alpha_.count + 1U, no_transition);
      }
      cache_.insert(pcs, id);
      return id;
    }

    /*!
     * \brief Empty the cache back to the dead + start states (the eviction: bounded memory).
     */
    constexpr void flush()
    {
      ++flushes_;
      state_pcs_.clear();
      trans_.clear();
      state_has_start_.clear();
      state_pending_.clear();
      res_.clear();
      starts_.fill(no_transition);
      cache_.clear();
      state_pcs_.emplace_back();                          // dead state 0
      trans_.insert(trans_.end(), alpha_.count, dead_state);
      state_has_start_.push_back(0);
      state_pending_.push_back(0U);
      if (look_) {
        res_.insert(res_.end(), alpha_.count + 1U, dead_state);
        return; // start states are per right context (start_for), built on first use
      }
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      if (match_pc_ >= 0) {
        seen[static_cast<std::size_t>(match_pc_)] = 1;
        start.push_back(match_pc_);
        rev_closure(start, seen);
      }
      start_state_ = intern(start);
    }

    std::span<const instr>                                                     code_;                       //!< The byte program, owned by the caller.
    std::span<const char_class>                                                classes_;                    //!< Its byte classes, likewise borrowed.
    lazy_byte_alphabet                                                         alpha_;                      //!< Byte-to-class map; its count is the row stride.
    bool                                                                       eligible_    {false};        //!< \ref compute_eligibility's verdict, fixed at construction.
    bool                                                                       word_quit_   {false};        //!< Unicode word boundaries carried, quitting next to a non-ASCII byte.
    mutable bool                                                               quit_hit_    {false};        //!< Set by holds_left() inside one resolve(): that resolution is quit_state.
    bool                                                                       look_        {false};        //!< The program carries position assertions (the look paths).
    std::array<std::uint8_t, 256>                                              class_ctx_   {};             //!< Class -> the right context a byte of it gives (look programs).
    std::array<std::uint32_t, 16>                                              starts_      {};             //!< Right context -> start state, per \ref flush (look programs).
    std::int32_t                                                               match_pc_    {-1};           //!< The forward `match` pc — this pass's start; -1 when absent.
    std::uint32_t                                                              start_state_ {0};            //!< Id of \ref match_pc_'s backward closure, re-interned by each \ref flush.
    std::size_t                                                                budget_      {state_budget}; //!< Cached states tolerated before a \ref flush.
    std::size_t                                                                flushes_     {0};            //!< bumped by flush(); step()'s stale-state guard against a mid-call reset.
    std::vector<std::int32_t>                                                  rev_eps_pool_;               //!< transposed epsilon edges, CSR-packed.
    std::vector<std::uint32_t>                                                 rev_eps_at_;                 //!< CSR offsets into \ref rev_eps_pool_, size code+2.
    std::vector<std::int32_t>                                                  rev_consume_pool_;           //!< transposed consuming edges, CSR-packed.
    std::vector<std::uint32_t>                                                 rev_consume_at_;             //!< CSR offsets into \ref rev_consume_pool_.
    mutable std::vector<std::int32_t>                                          stack_;                      //!< rev_closure's work stack, hoisted for the same reason.
    std::vector<std::vector<std::int32_t>>                                     state_pcs_;                  //!< state id -> sorted pc-set.
    std::vector<std::uint32_t>                                                 trans_;                      //!< flat [state*stride + class] -> next.
    std::vector<char>                                                          state_has_start_;            //!< state -> reaches the program start (an accept).
    std::vector<std::uint8_t>                                                  state_pending_;              //!< state -> holds a pending assertion (look programs).
    std::vector<std::uint32_t>                                                 res_;                        //!< flat [state*(count+1) + key] -> resolved state (look programs).
    pc_set_cache                                                               cache_;                      //!< pc-set -> state id, the memo behind \ref intern.
  };
} // namespace real::detail

#endif // REAL_LAZY_DFA_HPP
