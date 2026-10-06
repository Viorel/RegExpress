/*!
 * \file lazy_dfa.hpp
 * \brief A lazy, priority-preserving forward DFA over the Pike program (the kFirstMatch forward pass + cache).
 *
 * Not \ref real::dfa (`<real/dfa.hpp>`), a maximal-munch recognizer over unordered NFA-state sets. Here a
 * DFA state is an **ordered** NFA-state set memoizing the leftmost-first Pike closure, so the forward pass
 * reports the boundary the Pike VM would. pike.hpp routes an eligible search through the forward end, the
 * reverse start (`reverse_dfa`), then the VM on the located window. Dynamic only: the cache is mutable.
 */
#ifndef REAL_LAZY_DFA_HPP
#define REAL_LAZY_DFA_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
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

  //! \brief Cap on how far a jump chain is followed to a loop head (empty-iteration exit routing), shared
  //!        by every closure walk; a loop join reaches its split in one hop, so eight is headroom, not a knob.
  inline constexpr int max_loop_hops {8};

  /*!
   * \brief Test seam: force the matcher off the lazy-DFA route onto the pure Pike VM, so a differential can
   *        assert routed and unrouted searches agree in one binary. Not for production use (applies to every
   *        `*_disabled` seam below).
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& lazy_dfa_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! Default bytes one lazy DFA's state cache may hold per direction; past it the cache flushes, and a scan
  //! that keeps flushing quits to the VM. Above the 30 MB a 2 000-word alternation builds without thrashing.
  inline constexpr std::size_t lazy_dfa_default_byte_budget {std::size_t {64} << 20U};

  /*!
   * \brief Test seam: the byte budget the search DFAs are built with (read at each DFA's construction).
   * \return A reference to the process-wide budget; \ref lazy_dfa_default_byte_budget unless a test moved it.
   */
  inline std::size_t& lazy_dfa_byte_budget()
  {
    static std::size_t budget {lazy_dfa_default_byte_budget};
    return budget;
  }

  /*!
   * \brief Test seam: force the general loop off the bounded backtracker onto the Pike VM, so a
   *        differential can assert both agree on every small subject.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& bounded_backtrack_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the inner-literal search route onto the core search. The route
   *        cannot miss a leftmost match because its reverse bound never advances mid-search.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& inner_literal_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force off the rare-discriminant prefilter (`https?://` memchr-`:` route) onto
   *        prefix/first-byte search.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& rare_disc_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the inner-literal small-haystack guard off, so the route fires on any size and
   *        tiny correctness inputs exercise it. The guard uses \ref regex_immutables::il_min_haystack on the
   *        first candidate scan and \ref il_warm_floor thereafter.
   * \return Reference to the process-wide seam flag; set it to true to take the guard out.
   */
  inline bool& inner_literal_guard_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the trailing-lookaround class+ route onto the pure Pike VM.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& trailing_la_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the heterogeneous fixed-shape pair-filter route onto the
   *        ordinary \c run_fixed_shape walk. The route only filters; `match_fixed_body_wb` decides each candidate.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& fixed_shape_pair_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the fixed-shape walk (\c run_fixed_shape) onto the general
   *        Pike loop. \ref inner_literal_route_disabled does not reach a `fixed_shape` pattern (the
   *        inner-literal gate excludes it); a differential on one needs this seam.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& fixed_shape_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test/profile seam: skip the dedicated class-scan fast paths (byte class-loop, cp-class-loop,
   *        codepoint_class, negated-class `.`/`[^,]+`), so such a pattern falls through to lazy-DFA / general.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& class_fastpath_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test/profile seam: force the matcher off the possessive-loop fast paths
   *        (bare/suffixed/delimited `X*+`/`X++`) onto the general VM.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& possessive_fastpath_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: force the matcher off the Aho-Corasick multi-literal route onto the
   *        \ref pattern_hints::fixed_alternation `run_alternation` path.
   * \return Reference to the process-wide seam flag; set it to true to take the route out.
   */
  inline bool& aho_corasick_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: keep an alternation's block scans on its first bytes whatever the subject's density,
   *        so a differential can compare the pair filter with the first-byte scan.
   * \return Reference to the process-wide seam flag; set it to true to take the pair filter out.
   */
  inline bool& alternation_pairs_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: mask a dense alternation's blocks by its byte pairs rather than by the nibble
   *        fingerprint, so a differential can compare both filters.
   * \return Reference to the process-wide seam flag; set it to true to take the fingerprint out.
   */
  inline bool& alternation_nibbles_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: take the Aho-Corasick density gate out, so the route is chosen on branch count alone.
   *
   * `aho_corasick_route_disabled() = false` only declines to forbid the automaton; the gate still decides.
   * A harness forcing the automaton needs this seam too.
   *
   * \return Reference to the process-wide seam flag; set it to true to route on branch count alone.
   */
  inline bool& ac_density_gate_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test observability: whether the inner-literal density gate last abandoned the route.
   *
   * Both routes give identical spans, so no span test can see the gate or
   * \ref real::detail::pike_vm::il_density_milli_threshold; this flag is what a test asserts. Atomic because
   * concurrent searches all write it; the relaxed store costs a plain store.
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
   * \brief Test observability: the AC density gate's most recent verdict.
   *
   * Both routes give identical spans, so a test asserts this decision, never a speed-up: the margin is
   * ~5.9x optimised but 1.4x under ASan/UBSan (sanitizer cost is per operation, diluting a byte-skipping
   * route). Timing belongs in `benchmarks/ac_regime.cpp`. Atomic and relaxed as
   * \ref il_density_last_abandoned.
   *
   * \return Reference to the process-wide verdict; assign \ref ac_verdict::not_consulted to arm it.
   */
  inline std::atomic<ac_verdict>& ac_density_last_verdict()
  {
    static std::atomic<ac_verdict> verdict {ac_verdict::not_consulted};
    return verdict;
  }

  /*!
   * \brief A byte-level view of a Pike program for the DFA passes: every `klass_cp` is expanded into UTF-8
   *        byte-range split/klass chains, so a forward DFA can represent it; the Pike program is untouched.
   *        `eligible` is false when an op no DFA can represent is present — the caller keeps the Pike VM.
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
   * \brief One node of a minimal deterministic UTF-8 trie for a code-point class. Its byte-range transitions
   *        are pairwise **disjoint** (at most one edge matches a byte), which makes the byte-program
   *        one-pass-friendly. A target `>= 0` is a node id; `-1` is accept (the run continues at the
   *        construct's successor).
   */
  struct utf8_trie_node
  {
    /*!
     * \brief Outgoing edges: a byte range paired with its target, `-1` meaning accept. Pairwise disjoint.
     *
     * \note One heap block per node, ~98 % of build_byte_program's allocations (a cold first search's
     *       cost). A pool needs a stack-disciplined arena for the recursive builder; an ASCII-first
     *       expansion would remove the work instead (see \ref build_byte_program).
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
   * One branch per UTF-8 range would let ranges sharing a lead byte cross it on two threads (never
   * one-pass). Overlapping ranges are split into disjoint per-node transitions and identical suffix
   * sub-tries hash-consed (Daciuk): Unicode `\w \d \s` become one-pass, and `\w` shrinks from thousands of
   * instructions to a few hundred shared nodes.
   *
   * \param[in] cc        The code-point class to recognise.
   * \param[in] cp_ranges The program's range pool, which \c cc slices.
   * \return The trie; its \ref utf8_trie::root is `-1` when the class is empty.
   */
  constexpr utf8_trie build_utf8_trie(const cp_class&             cc,
                                      std::span<const code_range> cp_ranges)
  {
    // One buffer plus offsets, not a vector per sequence (three tiny heap blocks each, thousands per class).
    // Spans are taken only once the pool is complete, so no growth invalidates one.
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
    //! \brief A sequence's remaining byte ranges, as a view: the pool outlives the recursion, so a suffix
    //!        is `subspan(1)`, never a copy.
    using seq_view = std::span<const utf8_byte_range>;

    struct builder
    {
      std::vector<utf8_trie_node>& nodes;
      // Hash-cons index as an intrusive chain: `memo_head[bucket]` is a node id or -1, `memo_next[id]` the
      // next id in that bucket (no vector per bucket). Head insertion is order-independent: a bucket holds
      // one representative per distinct `trans`.
      std::vector<std::int32_t>&   memo_head;
      std::vector<std::int32_t>&   memo_next;

      // In-house FNV, never std::hash / std::unordered_map: their out-of-line libc++ symbols (e.g.
      // __hash_memory) drift across toolchains.
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

      // Takes a span so a child gets a prefix of this level's own `tails`; both vectors are reserved to a
      // bound known on entry, so a level allocates twice and never re-allocates.
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
            // Compact the non-empty tails to the front, order preserved; the child builds its own `tails`.
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
   * Open-addressed over a flat buffer, not `std::unordered_map`: `static_storage` builds its byte program
   * in a constant expression. A slot holds `(key << 16) | (index + 1)`, so zero means empty and
   * `0x00..0x00` stays a legal key. It grows at half load, never caps: a fixed capacity would fill and
   * spin or silently stop interning.
   */
  struct range_intern_table
  {
    static constexpr std::uint16_t absent {0xFFFFU};                               //!< \ref find's miss answer (never a valid index: it would be slot 0x10000).

    std::vector<std::uint32_t> slots      {std::vector<std::uint32_t>(1024U, 0U)}; //!< Power of two; 1024 covers the ~476 ranges a `\w`-heavy program interns without a rehash.
    std::size_t                count      {0};                                     //!< Occupied slots, for the load factor.

    /*!
     * \brief Probe start for \p key: Fibonacci hashing — the key times 2^64/phi, keeping the high bits.
     *
     * Keys arrive in near-runs, so the stride between consecutive keys decides the table's use. Keep the
     * 64-bit product: a shifted 32-bit constant has a stride sharing a factor of 4 with 1024, leaving
     * three buckets in four unreachable. Wrapping is the intended modular multiply.
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
   * The root is emitted first, so the entry is the base pc. Equal edges share one class (`0x80..0xBF` sits
   * on nearly every node): sound because a class index is only read as a byte set, never to tell `klass`
   * ops apart.
   *
   * \param[in,out] bp    Byte program the fragment is appended to.
   * \param[in]     trie  The trie to emit.
   * \param[in]     after Program counter the accept edges jump to (the construct's successor).
   * \param[in,out] seen  Byte-range intern table, shared across every occurrence in one program.
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only, never on a search path
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

  //! Branches from which a literal alternation is factored into a trie in the byte program; below it the
  //! flat alternation's states stay small.
  inline constexpr std::size_t alternation_trie_min_branches {64};

  /*!
   * \brief Test seam: build the byte program's literal alternations flat, so a differential can compare
   *        the trie against them.
   * \return A reference to the process-wide flag.
   */
  inline bool& alternation_trie_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief A literal alternation (every branch a run of `byte` ops converging on one exit) factored into a trie
   *        that keeps leftmost-first priority.
   *
   * Flat, every seeded state holds all N threads (a 14 500-word alternation: 15 000 pcs per state);
   * factored, one pc per live trie node.
   *
   * Priority is kept by chunks: a branch ending at a node closes its current chunk with an END item (a jump
   * to the exit), and a later branch merges only into the last chunk. Items of one chunk are on distinct
   * bytes, so no text reaches two; items of different chunks keep the branches' declared order.
   */
  struct literal_alt_trie
  {
    /*!
     * \brief One alternative of a node: a byte leading to a child, or the END of a branch.
     */
    struct item
    {
      std::int16_t byte  {-1}; //!< The byte, or -1 for END (the jump to the exit).
      std::int32_t child {-1}; //!< The child node of a byte item.
    };

    /*!
     * \brief One trie node: its alternatives in priority order.
     */
    struct node
    {
      std::vector<item> items;           //!< Alternatives, highest priority first.
      std::uint32_t     chunk_begin {0}; //!< First item a later branch may merge into.
    };

    std::vector<node>         nodes {node {}}; //!< The trie; node 0 is the root.
    std::vector<std::int32_t> order;           //!< Emission order, root first.
    std::vector<std::int32_t> node_pc;         //!< Each node's pc, relative to the trie's first.
    std::vector<bool>         falls;           //!< The node's last item falls through into its child (no jump).
    std::size_t               size {0};        //!< Instructions the trie emits.

    /*!
     * \brief Adds the branch whose bytes are `code[b, e)`.
     * \param[in] code The program.
     * \param[in] b    The branch's first `byte`.
     * \param[in] e    One past its last.
     */
    constexpr void insert(std::span<const instr> code,
                          std::size_t            b,
                          std::size_t            e)
    {
      std::size_t at {0};
      for (std::size_t pc {b}; pc < e; ++pc) {
        const auto   byte  {static_cast<std::int16_t>(code[pc].arg8)};
        std::int32_t child {-1};
        for (std::size_t i {nodes[at].chunk_begin}; i < nodes[at].items.size(); ++i) {
          if (nodes[at].items[i].byte == byte) {
            child = nodes[at].items[i].child;
            break;
          }
        }
        if (child < 0) {
          child                            = static_cast<std::int32_t>(nodes.size());
          nodes[at].items.push_back({.byte = byte, .child = child});
          nodes.emplace_back();
        }
        at = static_cast<std::size_t>(child);
      }
      node& end {nodes[at]};
      if (end.items.empty() || end.items.back().byte >= 0) { // a second END in a row is the same exit
        end.items.push_back({.byte = -1, .child = -1});
      }
      end.chunk_begin = static_cast<std::uint32_t>(end.items.size());
    }

    /*!
     * \brief Orders the nodes and sizes the emission: preorder with each node's last child right after it, so a
     *        chain of one-child nodes is a straight run of `byte` ops.
     */
    constexpr void layout()
    {
      std::vector<std::int32_t> stack {0};
      while (!stack.empty()) {
        const std::int32_t id {stack.back()};
        stack.pop_back();
        order.push_back(id);
        for (const item& it : nodes[static_cast<std::size_t>(id)].items) { // first to last: the last child pops next
          if (it.byte >= 0) {
            stack.push_back(it.child);
          }
        }
      }
      node_pc.assign(nodes.size(), 0);
      falls.assign(nodes.size(), false);
      std::size_t off {0};
      for (std::size_t k {0}; k < order.size(); ++k) {
        const auto  id {static_cast<std::size_t>(order[k])};
        const node& nd {nodes[id]};
        node_pc[id] = static_cast<std::int32_t>(off);
        std::size_t sz {nd.items.size() - 1U}; // a split before every item but the last
        for (const item& it : nd.items) {
          sz += it.byte >= 0 ? 2U : 1U;        // byte + jump, or the END jump
        }
        if (nd.items.back().byte >= 0 && k + 1U < order.size() && order[k + 1U] == nd.items.back().child) {
          falls[id] = true;
          --sz;
        }
        off += sz;
      }
      size = off;
    }

    /*!
     * \brief Emits the trie at the end of \p bp.
     * \param[in,out] bp    The byte program.
     * \param[in]     after The exit's pc in \p bp.
     */
    constexpr void emit(byte_program& bp,
                        std::int32_t  after) const
    {
      const auto base {static_cast<std::int32_t>(bp.code.size())};
      for (const std::int32_t id : order) {
        const node&       nd {nodes[static_cast<std::size_t>(id)]};
        const std::size_t k  {nd.items.size()};
        for (std::size_t j {0}; j < k; ++j) {
          const item&        it   {nd.items[j]};
          const auto         here {static_cast<std::int32_t>(bp.code.size())};
          const std::int32_t body {it.byte >= 0 ? 2 : 1};
          if (j + 1U < k) {
            bp.code.push_back({.op = opcode::split, .primary_target = here + 1, .secondary_target = here + 1 + body});
          }
          if (it.byte >= 0) {
            bp.code.push_back({.op = opcode::byte, .arg8 = static_cast<std::uint8_t>(it.byte)});
            if (j + 1U != k || !falls[static_cast<std::size_t>(id)]) {
              bp.code.push_back({.op = opcode::jump, .primary_target = base + node_pc[static_cast<std::size_t>(it.child)]});
            }
          }
          else {
            bp.code.push_back({.op = opcode::jump, .primary_target = after});
          }
        }
      }
    }
  };

  /*!
   * \brief The literal alternation starting at a `split`: its exit and each branch's bytes.
   */
  struct literal_alt_chain
  {
    std::size_t                                      exit {0}; //!< The pc every branch reaches.
    std::vector<std::pair<std::size_t, std::size_t>> words;    //!< Each branch's `[first byte, one past last)`.
  };

  /*!
   * \brief Whether `[pc, exit)` is a chain of `split`s whose branches are runs of `byte` ops jumping forward to
   *        one exit, the last branch falling through to it.
   * \param[in]  code The program.
   * \param[in]  pc   The candidate first `split`.
   * \param[out] out  The chain, when it is one.
   * \return True when it is.
   */
  constexpr bool detect_literal_alt(std::span<const instr> code,
                                    std::size_t            pc,
                                    literal_alt_chain&     out)
  {
    out.words.clear();
    if (code[pc].op != opcode::split || code[pc].primary_target != static_cast<std::int32_t>(pc) + 1) {
      return false;
    }
    std::size_t  s    {pc};
    std::int64_t exit {-1};
    while (true) {
      const instr& in {code[s]};
      if (in.op == opcode::split && in.primary_target == static_cast<std::int32_t>(s) + 1) {
        std::size_t j {s + 1U};
        while (j < code.size() && code[j].op == opcode::byte) {
          ++j;
        }
        if (j < code.size() && code[j].op == opcode::jump && (exit < 0 || code[j].primary_target == exit)
            && in.secondary_target == static_cast<std::int32_t>(j) + 1 && code[j].primary_target > static_cast<std::int32_t>(j)) {
          exit = code[j].primary_target;
          out.words.emplace_back(s + 1U, j);
          s = j + 1U;
          continue;
        }
      }
      break; // `s` opens the last branch: bytes up to the exit
    }
    if (exit < 0 || out.words.empty() || static_cast<std::size_t>(exit) < s) {
      return false;
    }
    for (std::size_t j {s}; j < static_cast<std::size_t>(exit); ++j) {
      if (code[j].op != opcode::byte) {
        return false;
      }
    }
    out.words.emplace_back(s, static_cast<std::size_t>(exit));
    out.exit = static_cast<std::size_t>(exit);
    return true;
  }

  //! Cap on the expanded byte-program's instruction count, checked as it grows. Each `klass_cp` occurrence
  //! emits its own copy of its class's trie, so `\w{k}` costs O(k x trie size) before the downstream caps
  //! (\ref onepass::max_nodes, \ref onepass::max_minimize_work) can act. Sized for the sanitized fuzzing
  //! build's per-input timeout; past it Tier-A/Tier-B decline and the general Pike VM runs unexpanded.
  inline constexpr std::size_t max_byte_program_size {20000};

  /*!
   * \brief Builds the byte-level DFA program for \p prog (see \ref byte_program). A `klass_cp` at P (the op
   *        plus three `utf8_cont` slots) is replaced by its class's deterministic UTF-8 trie
   *        (\ref build_utf8_trie), converging on the mapped P+4; every other op is copied with remapped
   *        targets. The first pass sizes each construct into the old→new pc map and enforces \p max_size;
   *        the second emits.
   *
   * \param[in] prog            The Pike program to expand.
   * \param[in] keep_assertions Tier-B: keep `assert_position` ops as edge conditions instead of declining
   *                            them (Tier-A's default).
   * \param[in] max_size        Expanded-program-size cap. Defaults to \ref max_byte_program_size; a smaller
   *                            value lets a test reach the decline cheaply.
   * \return The expanded program, with \ref byte_program::eligible false when it declined.
   */
  // The expansion is blind to the subject: a text-mode `\w` expands in full, most of a first text-mode
  // search. An ASCII-only expansion holds only on an ASCII subject: pre-scanning costs more than the search
  // (the VM state is fresh per search), and the viable shape (bytes >= 0x80 to a "cannot answer" class, a
  // second program on the first non-ASCII subject) changes DFA state semantics, not a local edit.
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only, never on a search path
#endif
  constexpr byte_program build_byte_program(const program_view& prog,
                                            bool                keep_assertions = false,
                                            std::size_t         max_size        = max_byte_program_size)
  {
    byte_program bp;
    bp.unicode_word = prog.unicode_word;
    if (prog.code.empty()) {
      // Never `eligible`: a reverse pass over an empty program answers npos everywhere, so the caller would
      // drop every candidate instead of falling back (a storage without an inner-literal prefix program).
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
        // A possessive loop's `primary_target` is a capture-slot index, not a branch target: the generic
        // copy below would remap it as a pc. Decline.
        bp.eligible = false;
        return bp;
      }
    }
    bp.classes.assign(prog.classes.begin(), prog.classes.end()); // original classes keep their indices

    const std::size_t         n {prog.code.size()};
    std::vector<std::int32_t> map(n + 1, 0);                     // old pc -> new pc (n = the one-past end)
    // One trie per class, pointed to per pc: `(\w+)X(\w+)` builds `\w` once. `by_class` never grows, so the
    // pointers cannot dangle. Do not share it across the Tier-A and Tier-B expansions: keeping wide tries
    // alive across both builds measured dearer than rebuilding them.
    std::vector<utf8_trie>        by_class(prog.cp_classes.size());
    std::vector<bool>             class_built(prog.cp_classes.size(), false);
    std::vector<const utf8_trie*> tries(n, nullptr);              // the trie for each klass_cp pc
    std::size_t                   cur {0};
    // Literal alternations of many branches, factored (see literal_alt_trie); run time only, the
    // compile-time storage keeps the flat program.
    std::vector<literal_alt_trie> alt_tries;
    std::vector<std::size_t>      alt_exit;
    std::vector<std::int32_t>     alt_at;
    bool                          alt_on {false}; // assigned, not initialized: a `const bool` initializer is itself constant-evaluated
    if (!std::is_constant_evaluated()) {
      alt_on = !alternation_trie_disabled();
    }
    if (alt_on) {
      alt_at.assign(n, -1);
    }
    literal_alt_chain chain;
    for (std::size_t pc = 0; pc < n; ++pc) {
      map[pc] = static_cast<std::int32_t>(cur);
      if (alt_on && prog.code[pc].op == opcode::split && detect_literal_alt(prog.code, pc, chain)
          && chain.words.size() >= alternation_trie_min_branches) {
        literal_alt_trie trie;
        for (const auto& [b, e] : chain.words) {
          trie.insert(prog.code, b, e);
        }
        trie.layout();
        alt_at[pc] = static_cast<std::int32_t>(alt_tries.size());
        alt_exit.push_back(chain.exit);
        for (std::size_t t {pc + 1U}; t < chain.exit; ++t) {
          map[t] = static_cast<std::int32_t>(cur);
        }
        cur += trie.size;
        alt_tries.push_back(std::move(trie));
        pc = chain.exit - 1U;
        continue;
      }
      if (prog.code[pc].op == opcode::klass_cp) {
        const std::size_t ci {static_cast<std::size_t>(prog.code[pc].arg16)};
        if (!class_built[ci]) {
          by_class[ci]    = build_utf8_trie(prog.cp_classes[ci], prog.cp_ranges);
          class_built[ci] = true;
        }
        tries[pc]   = &by_class[ci];
        cur        += utf8_trie_emit_size(*tries[pc]);
        if (cur > max_size) {
          bp.eligible = false; // a large repeated class (`\w{k}`): decline before building another trie;
          return bp;           // the general Pike VM needs no expansion.
        }
        map[pc + 1] = map[pc + 2] = map[pc + 3] = static_cast<std::int32_t>(cur); // continuation slots absorbed
        pc         += 3;                                                          // skip the construct's tail
      }
      else {
        ++cur;
      }
    }
    map[n] = static_cast<std::int32_t>(cur);

    range_intern_table range_intern; // whole-program: a UTF-8 range recurs across occurrences

    const auto remap {[&](std::int32_t t) {
                        return (t >= 0 && static_cast<std::size_t>(t) <= n) ? map[static_cast<std::size_t>(t)] : t;
                      }};
    for (std::size_t pc = 0; pc < n; ++pc) {
      const instr& in {prog.code[pc]};
      if (alt_on && alt_at[pc] >= 0) {
        const auto k {static_cast<std::size_t>(alt_at[pc])};
        alt_tries[k].emit(bp, map[alt_exit[k]]);
        pc = alt_exit[k] - 1U;
        continue;
      }
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
   * \brief Byte-class alphabet over a Pike program: bytes satisfying exactly the same `byte`/`klass`
   *        predicates share a class, so the DFA transitions over classes instead of 256 raw bytes.
   */
  struct lazy_byte_alphabet
  {
    std::array<std::uint8_t, 256> of    {};    //!< byte -> class index.
    std::uint16_t                 count {0};   //!< number of distinct classes.
  };

  /*!
   * \brief Whether \p in is an ECMAScript line assertion, whose line ends at `\r` as well as `\n`.
   * \param[in] in An instruction.
   * \return True for `line_start_cr` / `line_end_cr`.
   */
  [[nodiscard]] constexpr bool is_cr_line_assert(const instr& in)
  {
    return in.op == opcode::assert_position
           && (in.arg8 == static_cast<std::uint8_t>(assert_kind::line_start_cr)
               || in.arg8 == static_cast<std::uint8_t>(assert_kind::line_end_cr));
  }

  /*!
   * \brief Partition 0..255 by the program's consuming predicates (every `klass` test, every `byte`
   *        literal). Bytes with an identical signature collapse to one class.
   * \param[in] code    The program's instruction stream.
   * \param[in] classes The byte classes it indexes.
   * \return The alphabet: a byte-to-class map plus the class count.
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only, never on a search path
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
    // Memoized by class index and literal byte (a Unicode trie emits many `klass` ops per class; the memo
    // hits because emit_utf8_trie interns one class per byte range). Distinct classes are found by an FNV
    // intrusive chain, not a linear 256-bit compare, and not std::hash (see hash_trans); `class_preds`
    // keeps first-seen order.
    constexpr std::size_t     pred_buckets {512};
    std::vector<std::int32_t> pred_head(pred_buckets, -1);
    std::vector<std::int32_t> pred_next;
    std::vector<bool>         seen_class(classes.size(), false);
    std::array<bool, 256>     seen_byte {};
    for (const instr& in : code) {
      if (in.op == opcode::klass && in.arg16 < classes.size()) {
        if (!seen_class[in.arg16]) {
          seen_class[in.arg16] = true;
          // Inlined, not a lambda: the analyzer cannot model a `[&]`-captured vector and reports a
          // null-pointer call on every use of `class_preds`.
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
    // With position assertions every class must be uniform in newline-ness and ASCII word-ness (and
    // non-ASCII-ness): synthetic predicates split on them, so any byte of a class tells its properties.
    if (std::ranges::any_of(code, [](const instr& in) { return in.op == opcode::assert_position; })) {
      char_class newline;
      newline.set(static_cast<std::uint8_t>('\n'));
      if (std::ranges::any_of(code, is_cr_line_assert)) {
        newline.set(static_cast<std::uint8_t>('\r')); // an ECMAScript line ends at either
      }
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
    // A byte's signature (which predicates hold it) is built once per byte, O(256 * predicates), then
    // grouped; comparing each byte against every open class is O(256 * classes * predicates). Ids are
    // minted in byte order at a signature's first appearance: downstream reads `alpha.of` by value.
    const std::size_t          pred_count {class_preds.size() + literal_preds.size()};
    const std::size_t          sig_words  {((pred_count + 63U) / 64U) + 1U};
    std::vector<std::uint64_t> sig(256U * sig_words, 0);
    for (std::size_t p {0}; p < class_preds.size(); ++p) {
      const char_class&   cc {class_preds[p]};
      const std::size_t   w  {p >> 6U};
      const std::uint64_t m  {std::uint64_t {1} << (p & 63U)};
      // Scatter only the bytes the class holds, read off its bitmap, not `test` on all 256.
      for (std::size_t word {0}; word < cc.bits.size(); ++word) {
        for (std::uint64_t rest {cc.bits[word]}; rest != 0; rest &= rest - 1U) {
          const std::size_t b {(word * 64U) + static_cast<std::size_t>(std::countr_zero(rest))};
          sig[(b * sig_words) + w] |= m;
        }
      }
    }
    for (std::size_t i {0}; i < literal_preds.size(); ++i) {
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
   * \brief A chained hash set of interned state ids keyed by their pc-set: maps a candidate pc-set to its
   *        state id, or \ref not_found. All-`std::vector`, not `std::unordered_map`, so the DFAs stay literal
   *        types (a constexpr `real::regex` embeds one in its scratch state).
   */
  struct pc_set_cache
  {
    static constexpr std::size_t   bucket_count {2048};        //!< Sizing only: chaining absorbs any load, so no answer depends on it.
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
      // A 64-bit accumulator truncated on return: brace-initialising the 64-bit basis into a 32-bit
      // size_t (win32) is a narrowing error.
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
   * Between two ASCII bytes a Unicode word boundary is an ASCII one (assert_eval.hpp). `\<` is false after
   * an ASCII word byte or before an ASCII non-word byte whatever the other side, and `\>` mirrors it.
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
      case assert_kind::line_end:
      case assert_kind::line_start_cr:
      case assert_kind::line_end_cr:       return false;
    }
    return false;
  }

  /*!
   * \brief The pcs one closure computation has entered, by generation: starting one bumps the generation
   *        instead of clearing per pc, so a cache miss costs its closure, not the program's size (a large
   *        alternation's byte program runs to hundreds of thousands of instructions).
   */
  struct visit_marks
  {
    std::vector<std::uint32_t> mark;     //!< Per pc: the generation that last entered it.
    std::uint32_t              gen {0};  //!< The current computation's generation.

    /*!
     * \brief Starts a computation over \p size pcs: none is entered yet.
     * \param[in] size The program's size.
     */
    constexpr void begin(std::size_t size)
    {
      if (mark.size() != size) {
        mark.assign(size, 0U);
        gen = 0;
      }
      if (++gen == 0U) { // wrapped: the marks of 2^32 computations ago would read as entered
        std::ranges::fill(mark, 0U);
        gen = 1;
      }
    }

    /*!
     * \brief Whether \p pc was entered in this computation.
     * \param[in] pc The pc.
     * \return True when it was.
     */
    [[nodiscard]] constexpr bool test(std::int32_t pc) const
    {
      return mark[static_cast<std::size_t>(pc)] == gen;
    }

    /*!
     * \brief Marks \p pc entered in this computation.
     * \param[in] pc The pc.
     */
    constexpr void set(std::int32_t pc)
    {
      mark[static_cast<std::size_t>(pc)] = gen;
    }
  };

  /*!
   * \brief Whether a lazy DFA, forward or reversed, can represent every op of \p code.
   *
   * Variable-width classes and lookarounds have no DFA representation. Tier 1's possessive-loop family has no
   * consuming-edge representation either (each DFA's consumes() recognizes byte/klass only): treating it as a dead
   * end would be an outright wrong DFA. A position assertion is represented -- an edge the closure crosses
   * where it holds -- unless it is a word boundary whose word-ness is a code point's, which no single byte
   * decides, or one scoped to the other word-ness.
   * \param[in] code       The program's instruction stream.
   * \param[in] ascii_word Whether a word boundary's word-ness is ASCII (then a byte decides it).
   * \return True when every op is representable.
   */
  constexpr bool dfa_representable(std::span<const instr> code,
                                   bool                   ascii_word)
  {
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
   * \brief A lazy priority-preserving forward DFA over a Pike program (the kFirstMatch forward pass).
   *
   * A DFA state is the ordered epsilon-closure of a pc set (the Pike thread list in split priority);
   * \ref step consumes a byte from each pc, re-closes, and interns the ordered result: subset construction,
   * memoized on demand.
   *
   * The cache is bounded: past its state or byte budget it is flushed and rebuilt. A scan that keeps
   * flushing trips \ref thrashing; a DFA built to quit then reports a quit and its caller finishes that one
   * search on the Pike VM (per scan, never re-attempted per position).
   *
   * A program with an op the DFA cannot represent (a `klass_cp`, a lookaround, a possessive loop, a scoped
   * assertion, or a word boundary on code-point word-ness without `word_quit`) is \ref eligible
   * "ineligible"; this builds the machinery, it does not decide policy.
   */
  class lazy_dfa
  {
  public:

    static constexpr std::uint32_t dead_state     {0};           //!< The empty state: every transition from it stays here.
    static constexpr std::uint32_t no_transition  {0xFFFFFFFFU}; //!< A not-yet-computed cached transition.
    static constexpr std::uint32_t quit_state     {0xFFFFFFFEU}; //!< What resolve() gives when a Unicode word boundary meets a non-ASCII byte; never interned.
    static constexpr std::size_t   quit_pos       {npos - 1U};   //!< What forward_end() gives when its scan quit (see \ref anchored_result::quit).
    static constexpr std::uint32_t no_match_idx   {0xFFFFFFFFU}; //!< A state whose ordered set holds no accept.
    static constexpr std::uint32_t pending_idx    {0xFFFFFFFEU}; //!< A state whose accept waits on a pending assertion: only resolve() decides it.
    static constexpr std::size_t   state_budget   {65536};       //!< Cached states before a flush; the memory cap is \ref lazy_dfa_byte_budget.
    static constexpr std::size_t   thrash_flushes {2};           //!< Flushes within one scan that trip \ref thrashing, where the scan may not quit.
    //! Bytes read per cached state below which a DFA that may quit refuses its next flush and quits: a cache
    //! refilled faster than that costs more to rebuild than the VM costs to scan.
    static constexpr std::size_t   quit_bytes_per_state {10};

    /*!
     * \brief Cache-behaviour counters, for the policy tests.
     */
    struct counters
    {
      std::size_t hits            {0}; //!< Transitions served from the cached row.
      std::size_t misses          {0}; //!< Transitions that had to run subset construction.
      std::size_t flushes         {0}; //!< Cache flushes over this object's lifetime.
      std::size_t scan_flushes    {0}; //!< flushes in the current scan (reset by \ref begin_scan).
      std::size_t byte_flushes    {0}; //!< Of \ref flushes, those the byte budget called, the state budget not reached.
      std::size_t refused_flushes {0}; //!< Flushes refused for want of progress: the scan quit instead.
    };

    /*!
     * \brief Builds the (initially empty) lazy DFA over a Pike program.
     * \param[in] code    The program's instruction stream (must outlive this object — held as a span).
     * \param[in] classes The program's interned character classes (likewise held as a span).
     * \param[in] budget  Cached states before a flush; defaults to \ref state_budget (smaller: a test hook).
     * \param[in] shared_alpha The regex's shared alphabet, or null to compute it here (O(256 x classes), so
     *                    the router passes the shared one).
     * \param[in] ascii_word Whether word boundaries use ASCII word-ness (bytes mode, `(?a)`): only then does
     *                    one byte decide them. False declines any word boundary.
     * \param[in] byte_mode Whether a match may start at any byte. In text mode the scans do not seed at a
     *                    continuation byte, as the VM does not.
     * \param[in] word_quit Let a scan quit, and say so, where the DFA cannot answer well: a Unicode word
     *                    boundary next to a non-ASCII byte, or once the cache thrashes (\ref thrashing). The
     *                    caller then asks the VM.
     * \param[in] raw_byte_starts The program was compiled with \ref flags::allow_raw_byte, so a lead opening
     *                    on a continuation byte is built as any other (\ref pattern_hints::raw_byte_starts).
     * \param[in] byte_budget Bytes the cached states may hold before a flush; see \ref lazy_dfa_byte_budget.
     */
    explicit constexpr lazy_dfa(std::span<const instr>      code,
                                std::span<const char_class> classes,
                                std::size_t                 budget          = state_budget,
                                const lazy_byte_alphabet*   shared_alpha    = nullptr,
                                bool                        ascii_word      = false,
                                bool                        byte_mode       = true,
                                bool                        word_quit       = false,
                                bool                        raw_byte_starts = false,
                                std::size_t                 byte_budget     = lazy_dfa_default_byte_budget)
      : code_ {code}, classes_ {classes},
        alpha_ {shared_alpha != nullptr ? *shared_alpha : compute_lazy_alphabet(code, classes)},
        eligible_ {dfa_representable(code, ascii_word || word_quit) && (byte_mode || raw_byte_starts || !opens_on_continuation(code, classes))},
        byte_mode_ {byte_mode},
        word_quit_ {word_quit && !ascii_word}, may_quit_ {word_quit},
        look_ {std::ranges::any_of(code, [](const instr& in) { return in.op == opcode::assert_position; })},
        plain_ {eligible_ && !look_}, stride_ {alpha_.count + 2U},
        // A state id is its row's offset: the last row must stay below the special ids, and a flush at the
        // budget still interns one state past it.
        budget_ {std::min(budget, (std::size_t {quit_state} / stride_) - 2U)}, byte_budget_ {byte_budget}
    {
      if (look_) {
        // compute_lazy_alphabet split classes on these properties, so any byte of a class tells them.
        const bool cr {std::ranges::any_of(code, is_cr_line_assert)};
        for (unsigned b {0}; b < 256U; ++b) {
          class_ctx_[alpha_.of[b]] = (word_quit_ && b >= 0x80U) ? ctx_nonascii : ctx_of(static_cast<std::uint8_t>(b), cr);
        }
      }
      flush();                 // seeds the dead state (0) and the start state (1)
    }

    /*!
     * \brief Whether the program can be represented at all (see `compute_eligibility`).
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
     * \brief Begin a search: clear the per-scan flush counter and the thrash flag. No cache flush: states
     *        carry over between searches, which is where the cache pays.
     */
    void begin_scan()
    {
      stats_.scan_flushes = 0;
      thrashing_          = false;
    }

    /*!
     * \brief Whether this scan stopped paying: its cache filled again before reading \ref quit_bytes_per_state
     *        bytes per state (or, for a DFA that may not quit, crossed \ref thrash_flushes flushes).
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
     * \brief Bytes the cached states hold now, as the byte budget counts them.
     * \return The bytes.
     */
    [[nodiscard]] std::size_t bytes() const
    {
      return bytes_;
    }

    /*!
     * \brief The end offset of the leftmost-first match in \p text (`kFirstMatch`), or \ref real::npos.
     *
     * Seeds a fresh lowest-priority thread at every position until a match, then reports the end of the
     * highest-priority thread reaching `match` (a lower-priority accept is suppressed while a higher one
     * lives). One left-to-right pass: linear per search. No captures: the windowed Pike pass fills the span
     * and applies the empty-match rule. With assertions the scan runs in the whole text, not a slice, so
     * `^`, `\b`, `$` see what is there (\ref scan_look).
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
        return scan_look<false>(text, start);
      }
      begin_scan();
      std::uint32_t       state    {start_state_}; // the seed at the start (a re-seeding state)
      std::size_t         best_end {npos};
      bool                matched  {false};
      std::size_t         pos      {start};
      scan_origin_ = start;
      while (true) {
        // Both tables carry the accept word: one row read per byte before a match.
        const std::uint32_t midx {(matched ? trans_ : trans_seeded_)[state + accept_col]};
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
        // Pre-match transitions re-seed, post-match ones do not (leftmost). The edge is read inline;
        // step()/step_seeded() only on a miss.
        const std::uint32_t cached {(matched ? trans_ : trans_seeded_)[state + trans_col + alpha_.of[byte]]};
        if (cached != no_transition) {
          state = cached;
        }
        else {
          miss_pos_ = pos;
          state     = matched ? step(state, byte) : step_seeded(state, byte);
        }
        ++pos;
      }
      window_bytes_ += pos - scan_origin_;
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
     * The \ref forward_end walk without re-seeding: one thread seeded at \p start (a prefilter hit), so no
     * reverse pass is needed. No captures. Does not call \ref begin_scan — a caller trying several candidates
     * for one search calls it once before its loop, since a per-candidate reset would mask thrashing.
     *
     * Cached transitions and cuts are read inline (\ref step / \ref cut_cached only on a miss), so hits do
     * not bump \ref counters::hits.
     *
     * \param[in] text  The subject text.
     * \param[in] start Offset to anchor the match at (must be `<= text.size()`).
     * \return The match end and the position the walk stopped at (\ref anchored_result::scanned_to). On
     *         a miss, `scanned_to == text.size()` means no dead state bounded the reach (`.*` with no
     *         terminator ahead): a caller trying candidate after candidate is then in the O(n^2) regime.
     */
    [[nodiscard]] anchored_result anchored_end(std::string_view text,
                                               std::size_t      start)
    {
      // One test on the common path: this runs once per candidate position.
      if (!plain_) {
        if (!eligible_) {
          return {.end = npos, .scanned_to = start};
        }
        return scan_look<true>(text, start);
      }
      std::uint32_t state    {start_state_};
      std::size_t   best_end {npos};
      std::size_t   pos      {start};
      scan_origin_ = start;
      while (true) {
        const std::uint32_t midx {trans_[state + accept_col]};
        if (midx != no_match_idx) {
          best_end = pos;                                           // the highest-priority accept lives at index midx; a higher thread may extend it
          const std::uint32_t cut {trans_[state + cut_col]};
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
        const std::uint32_t trans {trans_[state + trans_col + cls]};  // the state id is its row's offset: no multiply on the chain
        if (trans != no_transition) {
          state = trans;
        }
        else {
          miss_pos_ = pos;
          state     = step(state, byte); // anchored: never re-seed -- a match starts at `start` or not at all
        }
        ++pos;
      }
      window_bytes_ += pos - scan_origin_;
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
      const std::uint32_t word {trans_[state + accept_col]};
      return word != no_match_idx && word != pending_idx;
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
      const std::uint32_t cached {trans_[state + trans_col + cls]};   // by value: intern() below may realloc
      if (cached != no_transition) {
        ++stats_.hits;
        return cached;
      }
      ++stats_.misses;
      std::vector<std::int32_t> next;
      visit_marks&              seen {begin_visit()};
      const std::uint8_t        ctx  {class_ctx_[cls]};
      for (const std::int32_t pc : state_pcs_[state / stride_]) {
        if (consumes(pc, byte)) {
          close_any(pc + consumed_width(pc), next, seen, ctx);
        }
      }
      const std::size_t   flushes_before {epoch_};
      const std::uint32_t result         {intern_any(std::move(next), ctx)}; // may grow/flush the tables — do not hold a reference
      if (epoch_ == flushes_before) {
        trans_[state + trans_col + cls] = result;   // no flush: `state` is still valid, so cache the edge
      }
      // After a flush the caller's `state` is stale and `result` a fresh id. A scan that may quit gets the
      // dead state once the cache thrashes, which ends its loop and reports a quit.
      return (thrashing_ && may_quit_) ? dead_state : result;
    }

  private:

    /*!
     * \brief Starts a closure computation on this DFA's marks.
     * \return The marks, none entered.
     */
    constexpr visit_marks& begin_visit()
    {
      marks_.begin(code_.size());
      return marks_;
    }

    /*!
     * \brief Like \ref step, but appends pc 0's closure at the lowest priority (a fresh thread at every
     *        position until a match). Cached in its own pre-match transition table.
     * \param[in] state The current state id.
     * \param[in] byte  The byte consumed.
     * \return The successor state, with a fresh thread appended at the lowest priority.
     */
    std::uint32_t step_seeded(std::uint32_t state,
                              std::uint8_t  byte)
    {
      const std::uint8_t  cls    {alpha_.of[byte]};
      const std::uint32_t cached {trans_seeded_[state + trans_col + cls]};
      if (cached != no_transition) {
        ++stats_.hits;
        return cached;
      }
      ++stats_.misses;
      const std::vector<std::int32_t> pcs  {state_pcs_[state / stride_]}; // copy: intern() below may realloc state_pcs_
      std::vector<std::int32_t>       next;
      visit_marks&                    seen {begin_visit()};
      const std::uint8_t              ctx  {class_ctx_[cls]};
      for (const std::int32_t pc : pcs) {
        if (consumes(pc, byte)) {
          close_any(pc + consumed_width(pc), next, seen, ctx);
        }
      }
      close_any(0, next, seen, ctx); // re-seed at the lowest priority (deduped against the advanced threads)
      const std::size_t   flushes_before {epoch_};
      const std::uint32_t result         {intern_any(std::move(next), ctx)};
      if (epoch_ == flushes_before) {
        trans_seeded_[state + trans_col + cls] = result;
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
        prefix.push_back(state_pcs_[state / stride_][i]);
      }
      return intern(prefix);
    }

    /*!
     * \brief The priority-cut of \p state at its own accept, memoized: `cut` is O(state size) and Unicode
     *        byte-program states run thousands of pcs wide, so a per-match recompute dominated. Exact, since a
     *        state's accept index is fixed.
     * \param[in] state The accepting state id.
     * \return The cut state, memoized after the first call.
     */
    std::uint32_t cut_cached(std::uint32_t state)
    {
      const std::uint32_t memo {trans_[state + cut_col]};
      if (memo != no_transition) {
        return memo;
      }
      const std::size_t   flushes_before {epoch_};
      const std::uint32_t result         {cut(state, trans_[state + accept_col])}; // intern may flush/realloc
      if (epoch_ == flushes_before) {
        trans_[state + cut_col] = result; // no flush: `state` is still valid, memoise the edge
      }
      return (thrashing_ && may_quit_) ? dead_state : result; // see step()
    }

    /*!
     * \brief Whether a match can open on a UTF-8 continuation byte: a byte or class the start's closure reaches
     *        takes one. In text mode such a match may not start inside a code point, and neither the forward
     *        scan's seeds nor the reverse walk's start test for that; the VM does (pike_vm::seed_viable).
     * \param[in] code    The program's instruction stream.
     * \param[in] classes Its classes.
     * \return True when some first byte is `10xxxxxx`.
     */
    static constexpr bool opens_on_continuation(std::span<const instr>      code,
                                                std::span<const char_class> classes)
    {
      std::vector<std::uint8_t>  seen(code.size(), 0);
      std::vector<std::int32_t>  stack {0};
      const auto                 takes {[](const char_class& c) {
                                          for (unsigned b {0x80U}; b <= 0xBFU; ++b) {
                                            if (c.test(static_cast<std::uint8_t>(b))) {
                                              return true;
                                            }
                                          }
                                          return false;
                                        }};
      while (!stack.empty()) {
        const std::int32_t pc {stack.back()};
        stack.pop_back();
        if (pc < 0 || static_cast<std::size_t>(pc) >= code.size() || seen[static_cast<std::size_t>(pc)] != 0) {
          continue;
        }
        seen[static_cast<std::size_t>(pc)] = 1;
        const instr& in {code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::byte:
            if ((in.arg8 & 0xC0U) == 0x80U) {
              return true;
            }
            break;
          case opcode::klass:
            if (takes(classes[in.arg16])) {
              return true;
            }
            break;
          case opcode::split:
            stack.push_back(in.primary_target);
            stack.push_back(in.secondary_target);
            break;
          case opcode::jump:
            stack.push_back(in.primary_target);
            break;
          case opcode::save:
          case opcode::assert_position:
            stack.push_back(pc + 1);
            break;
          default:
            break; // match, or an op no forward DFA represents
        }
      }
      return false;
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
     * \param[in] b  The byte before the position.
     * \param[in] cr The program's line assertions are ECMAScript's, which end a line at `\r` too.
     * \return Its context bits.
     */
    [[nodiscard]] static constexpr std::uint8_t ctx_of(std::uint8_t b,
                                                       bool         cr)
    {
      const bool word {(b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'};
      const bool nl   {b == '\n' || (cr && b == '\r')};
      return static_cast<std::uint8_t>((nl ? ctx_newline : 0U) | (word ? ctx_word : 0U));
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
      // Every kind named, so a new one is a compile error here (-Wswitch) rather than a silent answer.
      switch (kind) {
        case assert_kind::text_start:    return (ctx & ctx_start) != 0U;
        case assert_kind::line_start:
        case assert_kind::line_start_cr: return (ctx & ctx_start) != 0U || is_newline_ctx(ctx); // the context says which bytes end a line
        case assert_kind::text_end:
        case assert_kind::text_end_or_final_newline:
        case assert_kind::line_end:
        case assert_kind::line_end_cr:
        case assert_kind::word_boundary:
        case assert_kind::not_word_boundary:
        case assert_kind::word_start:
        case assert_kind::word_end:      return false; // not decided by the left context alone
      }
      return false;
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
        case assert_kind::line_end:
        case assert_kind::line_end_cr:               return end || next_nl;
        case assert_kind::word_boundary:             return prev_word != next_word;
        case assert_kind::not_word_boundary:         return prev_word == next_word;
        case assert_kind::word_start:                return !prev_word && next_word;
        case assert_kind::word_end:                  return prev_word && !next_word;
        case assert_kind::text_start:
        case assert_kind::line_start:
        case assert_kind::line_start_cr:             return holds_behind(kind, ctx);
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
      switch (kind) {
        case assert_kind::text_start:
        case assert_kind::line_start:
        case assert_kind::line_start_cr:             return true;
        case assert_kind::text_end:
        case assert_kind::text_end_or_final_newline:
        case assert_kind::line_end:
        case assert_kind::line_end_cr:
        case assert_kind::word_boundary:
        case assert_kind::not_word_boundary:
        case assert_kind::word_start:
        case assert_kind::word_end:                  return false;
      }
      return false;
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
                              visit_marks&               seen) const
    {
      // Unreachable from a well-formed program (a pc is 0 or one past a consuming op, and `match` follows);
      // a defensive bound only.
      if (pc < 0 || static_cast<std::size_t>(pc) >= code_.size()) {
        return;
      }
      // The work stack is a mutable member, empty in and out: as a local it was one heap block per call,
      // 10 408 allocations in one 8 KB first search.
      stack_.assign(1, pc);
      while (!stack_.empty()) {
        const std::int32_t cur {stack_.back()};
        stack_.pop_back();
        if (cur < 0 || static_cast<std::size_t>(cur) >= code_.size() || seen.test(cur)) {
          continue;
        }
        seen.set(cur);
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
            stack_.push_back(loop_exit_target(in, seen));
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
     * \brief Where a `jump` leads within one step's closure, as in the VM: into a loop head the closure already
     *        entered at this position, the loop's exit. An empty iteration ends the loop there, in its priority
     *        place, before any branch that would consume.
     * \param[in] in   The `jump`.
     * \param[in] seen This step's visited marks.
     * \return The pc to continue at.
     */
    [[nodiscard]] constexpr std::int32_t loop_exit_target(const instr&             in,
                                                          const visit_marks&       seen) const
    {
      std::int32_t head {in.primary_target};
      for (int hops {0}; hops < max_loop_hops && seen.test(head)
           && code_[static_cast<std::size_t>(head)].op == opcode::jump;
           ++hops) {
        head = code_[static_cast<std::size_t>(head)].primary_target;
      }
      const instr& target {code_[static_cast<std::size_t>(head)]};
      return seen.test(head) && target.op == opcode::split ? target.secondary_target
                                                                                   : in.primary_target;
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
                              visit_marks&               seen,
                              std::uint8_t               ctx,
                              std::uint16_t              key) const
    {
      stack_.assign(1, pc);
      while (!stack_.empty()) {
        const std::int32_t cur {stack_.back()};
        stack_.pop_back();
        if (cur < 0 || static_cast<std::size_t>(cur) >= code_.size() || seen.test(cur)) {
          continue;
        }
        seen.set(cur);
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
            stack_.push_back(loop_exit_target(in, seen));
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
                             visit_marks&               seen,
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
      if (trans_[state + accept_col] != pending_idx) {
        return state;
      }
      const std::size_t   slot   {std::size_t {state} + key}; // a res_ row is as wide as a trans_ row
      const std::uint32_t cached {res_[slot]};
      if (cached != no_transition) {
        return cached;
      }
      std::vector<std::int32_t> pcs {state_pcs_[state / stride_]}; // copy: intern() below may realloc state_pcs_
      const auto                ctx {static_cast<std::uint8_t>(-1 - pcs.back())};
      pcs.pop_back();
      std::vector<std::int32_t> out;
      visit_marks&              seen {begin_visit()};
      quit_hit_ = false;
      for (const std::int32_t pc : pcs) {
        const instr& in {code_[static_cast<std::size_t>(pc)]};
        if (in.op == opcode::assert_position) {
          if (holds_ahead(static_cast<assert_kind>(in.arg8), ctx, key)) {
            close_look(pc + 1, out, seen, ctx, key);
          }
        }
        else if (!seen.test(pc)) {
          seen.set(pc);
          out.push_back(pc);
        }
      }
      if (quit_hit_) {
        res_[slot] = quit_state; // the same state and key meet the same byte: memoizing it is exact
        return quit_state;
      }
      const std::size_t   flushes_before {epoch_};
      const std::uint32_t result         {intern(out)};
      if (epoch_ == flushes_before) {
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
      visit_marks&              seen     {begin_visit()};
      close_look(0, pcs, seen, ctx, key_unknown);
      const std::size_t   flushes_before {epoch_};
      const std::uint32_t result         {intern_any(std::move(pcs), ctx)};
      if (epoch_ == flushes_before) {
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
     * \brief \ref forward_end and \ref anchored_end for a program with position assertions: the walk runs over
     *        the whole text, so `^`, `\b` and `$` see what is there, and a state holding a pending assertion
     *        resolves it on the key of what follows.
     * \tparam Anchored One thread seeded at \p start (\ref anchored_end). Otherwise every position up to the
     *                  first match seeds a thread (\ref forward_end), and a dead state before a match does not
     *                  end the walk: an assertion can kill every thread at one position and let the next seed
     *                  live (`^` after a newline).
     * \param[in] text  Subject.
     * \param[in] start The anchor, or the first seed's position.
     * \return Anchored, the match end and how far the walk got (\ref anchored_result::quit when a Unicode
     *         boundary met a non-ASCII byte or the cache thrashed); otherwise the match end, \ref real::npos or
     *         \ref quit_pos.
     */
    template <bool Anchored>
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline, cold)) // out of the hot scans' bodies: a program without assertions never calls it
#endif
    std::conditional_t<Anchored, anchored_result, std::size_t> scan_look(std::string_view text,
                                                                         std::size_t      start)
    {
      std::uint32_t state    {start_for(ctx_at(text, start))};
      std::size_t   best_end {npos};
      bool          matched  {false};
      std::size_t   pos      {start};
      scan_origin_ = start;
      while (true) {
        // Only a state holding a pending assertion reads what follows it; the rest are their own resolution.
        std::uint32_t here {state};
        std::uint32_t word {trans_[state + accept_col]};
        if (word == pending_idx) {
          // The memoized resolution read inline, as the cached edges are; resolve() only on a miss.
          const std::uint16_t key  {key_at(text, pos)};
          const std::uint32_t memo {res_[state + key]};
          here = memo != no_transition ? memo : resolve(state, key);
          if (here == quit_state) {
            // Even past an accept: whether a longer match wins is what the boundary would have told.
            return look_quit<Anchored>(pos);
          }
          word = trans_[here + accept_col]; // a resolution holds nothing pending
        }
        if (word != no_match_idx) {
          best_end = pos;
          matched  = true;
          if constexpr (Anchored) {
            const std::uint32_t cut {trans_[here + cut_col]};
            here = cut != no_transition ? cut : cut_cached(here);
          }
          else {
            here = cut_cached(here);
          }
          if (here == dead_state) {
            break;
          }
        }
        if (pos >= text.size() || (here == dead_state && (Anchored || matched))) {
          break;
        }
        const auto byte {static_cast<std::uint8_t>(text[pos])};
        // In text mode a match does not start inside a code point: the step onto a continuation byte
        // carries the threads without a fresh seed.
        const bool seed            {!Anchored && !matched && (byte_mode_ || !starts_inside_code_point(text, pos + 1U))};
        // The cached edge read inline, as the plain scans do; step()/step_seeded() only on a miss.
        const std::uint32_t cached {(seed ? trans_seeded_ : trans_)[here + trans_col + alpha_.of[byte]]};
        if (cached != no_transition) {
          state = cached;
        }
        else {
          miss_pos_ = pos;
          state     = seed ? step_seeded(here, byte) : step(here, byte);
          if constexpr (!Anchored) {
            if (thrashing_ && may_quit_) {
              return look_quit<Anchored>(pos); // the seeding dead state would not end the loop
            }
          }
        }
        ++pos;
      }
      // A cut that flushed hands back the dead state, which ends the loop as a match's end.
      window_bytes_ += pos - scan_origin_;
      if (thrashing_ && may_quit_) {
        return look_quit<Anchored>(pos);
      }
      if constexpr (Anchored) {
        return anchored_result {.end = best_end, .scanned_to = pos};
      }
      else {
        return best_end;
      }
    }

    /*!
     * \brief \ref scan_look's answer when the walk quits.
     * \tparam Anchored Which walk quit.
     * \param[in] pos Where it stopped.
     * \return The quit result in that walk's form.
     */
    template <bool Anchored>
    [[nodiscard]] static constexpr std::conditional_t<Anchored, anchored_result, std::size_t> look_quit(std::size_t pos)
    {
      if constexpr (Anchored) {
        return anchored_result {.end = npos, .scanned_to = pos, .quit = true};
      }
      else {
        static_cast<void>(pos);
        return quit_pos;
      }
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
        return found * stride_; // the cache keeps row indices (it is shared with reverse_dfa); an id is an offset
      }
      const bool by_states {state_pcs_.size() >= budget_};
      if (by_states || bytes_ >= byte_budget_) {
        // Past its first fill, a cache that filled again before reading ten bytes per state is not paying: the
        // scan quits to the VM and the full cache is kept, so the next search's first miss asks again.
        const std::size_t window {window_bytes_ + (miss_pos_ >= scan_origin_ ? miss_pos_ - scan_origin_ : 0U)};
        if (may_quit_ && stats_.flushes != 0U && window < quit_bytes_per_state * state_pcs_.size()) {
          ++stats_.refused_flushes;
          thrashing_ = true;
          ++epoch_; // the caller's state ids stay valid, but the dead state handed back must not be cached
          return dead_state;
        }
        stats_.byte_flushes += by_states ? 0U : 1U;
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
      const auto row          {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
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
        match_idx = pending_idx;
      }
      trans_.push_back(match_idx);
      trans_.push_back(no_transition); // the priority-cut result, memoized lazily on first use
      trans_.insert(trans_.end(), alpha_.count, no_transition);
      trans_seeded_.push_back(match_idx);
      trans_seeded_.push_back(no_transition);
      trans_seeded_.insert(trans_seeded_.end(), alpha_.count, no_transition);
      if (look_) {
        res_.insert(res_.end(), stride_, no_transition);
      }
      cache_.insert(pcs, row);
      bytes_ += (pcs.size() * sizeof(std::int32_t)) + sizeof(std::vector<std::int32_t>) + sizeof(std::uint32_t)
                + (std::size_t {look_ ? 3U : 2U} *stride_ * sizeof(std::uint32_t)); // the pc-set, its hash entry, its rows
      return row * stride_;
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
        ++epoch_;
        if (!may_quit_ && stats_.scan_flushes >= thrash_flushes) {
          thrashing_ = true; // informational where the scan may not quit; a scan that may, quits on its progress
        }
        window_bytes_ = 0;
        scan_origin_  = miss_pos_;
      }
      bytes_ = 0;
      state_pcs_.clear();
      trans_.clear();
      trans_seeded_.clear();
      res_.clear();
      starts_.fill(no_transition);
      cache_.clear();
      // state 0 = dead (empty, self-looping), then the start (closure of pc 0). The dead row's accept word
      // says no accept: a row filled with dead_state would read as an accept at index 0.
      state_pcs_.emplace_back();
      trans_.push_back(no_match_idx);
      trans_.push_back(no_transition);
      trans_.insert(trans_.end(), alpha_.count, dead_state);
      // Re-seeding out of the dead state is a real transition when an assertion can empty a start state:
      // the next position's seed may live. Without assertions no seed is ever empty, so it stays dead.
      trans_seeded_.push_back(no_match_idx);
      trans_seeded_.push_back(no_transition);
      trans_seeded_.insert(trans_seeded_.end(), alpha_.count, look_ ? no_transition : dead_state);
      if (look_) {
        res_.insert(res_.end(), stride_, dead_state);
      }
      std::vector<std::int32_t> start;
      visit_marks&              seen {begin_visit()};
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

    static constexpr std::uint32_t accept_col {0};      //!< A row's accept word: the first accept's index, \ref no_match_idx or \ref pending_idx.
    static constexpr std::uint32_t cut_col    {1};      //!< A row's memoized priority cut (\ref no_transition until built).
    static constexpr std::uint32_t trans_col  {2};      //!< A row's first transition; the accept word leads so a wide row keeps it on the first cache line.

    std::span<const instr>        code_;                //!< The byte program, owned by the caller.
    std::span<const char_class>   classes_;             //!< Its byte classes, likewise borrowed.
    lazy_byte_alphabet            alpha_;               //!< Byte-to-class map; its count plus two is the row stride.
    bool                          eligible_    {false}; //!< \ref dfa_representable's verdict, fixed at construction.
    bool                          byte_mode_   {true};  //!< A match may start at any byte (else only at a code-point start).
    bool                          word_quit_   {false}; //!< Unicode word boundaries carried, quitting next to a non-ASCII byte.
    bool                          may_quit_    {false}; //!< A scan may quit: on a Unicode word boundary next to non-ASCII, and once its cache thrashes.
    mutable bool                  quit_hit_    {false}; //!< Set by holds_ahead() inside one resolve(): that resolution is quit_state.
    bool                          look_        {false}; //!< The program carries position assertions (the look paths).
    bool                          plain_       {false}; //!< Eligible and without assertions: the scans' one-test common path.
    std::array<std::uint8_t, 256> class_ctx_   {};      //!< Class -> the context after one of its bytes (look programs).
    std::array<std::uint32_t, 8>  starts_      {};      //!< Context -> start state, per \ref flush (look programs).
    std::uint32_t                 start_state_ {0};     //!< Id of the closure of pc 0, re-interned by each \ref flush.

    // One heap block per DFA state. A first search's cold cost is build_byte_program's UTF-8 tries (~96 % of
    // its allocations, benchmarks/alloc_cold_probe.cpp), paid once per regex; warm searches allocate nothing.
    // Hoisting the miss-path scratch into members saved ~1 % of cold allocations, under the ±3 % layout
    // floor; reserving the outer vector or flattening it into one pool changed nothing.
    mutable std::vector<std::int32_t>                                          stack_;        //!< close_into's work stack, hoisted: it ran once per pc of the source state.
    std::vector<std::vector<std::int32_t>>                                     state_pcs_;    //!< state id -> ordered pc-set.
    std::vector<std::uint32_t>                                                 trans_;        //!< [state + accept_col] accept word, [state + cut_col] memoized cut, [state + trans_col + class] next, unseeded (post-match).
    std::vector<std::uint32_t>                                                 trans_seeded_; //!< The same rows re-seeding (pre-match); its accept word repeats trans_'s, its cut cell is unused.
    std::vector<std::uint32_t>                                                 res_;          //!< [state + key] -> resolved state (look programs); a key spans the row's count + 2 cells.
    pc_set_cache                                                               cache_;        //!< pc-set -> row index, the memo behind \ref intern.

    std::uint32_t stride_       {2};                                                          //!< Cells per row, alpha_.count + 2: a state id is its row's offset.
    std::size_t   budget_       {state_budget};                                               //!< Cached states tolerated before a \ref flush.
    std::size_t   byte_budget_  {lazy_dfa_default_byte_budget};                               //!< Cached bytes tolerated before a \ref flush.
    std::size_t   bytes_        {0};                                                          //!< Bytes the cached states hold (pc-sets, rows, hash entries).
    counters      stats_        {};                                                           //!< Live counters, exposed by \ref stats.
    std::size_t   epoch_        {0};                                                          //!< Bumped by every flush and every refused one: a caller's cached ids went stale.
    std::size_t   scan_origin_  {0};                                                          //!< Where the bytes the current scan has read are counted from.
    std::size_t   miss_pos_     {0};                                                          //!< Position of the scan's latest cache miss.
    std::size_t   window_bytes_ {0};                                                          //!< Bytes read since the last flush by scans that have ended.
    visit_marks   marks_        {};                                                           //!< The pcs the closure being computed has entered (\ref begin_visit).
    bool          thrashing_    {false};                                                      //!< Set once this scan stopped paying: a refused flush, or \ref thrash_flushes flushes where it may not quit.
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
    static constexpr std::size_t   state_budget  {65536};       //!< Cached states before a flush; the memory cap is \ref lazy_dfa_byte_budget.

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
     * \param[in] byte_budget  Bytes the cached states may hold before a flush; see \ref lazy_dfa_byte_budget.
     */
    explicit constexpr reverse_dfa(std::span<const instr>      code,
                                   std::span<const char_class> classes,
                                   std::size_t                 budget       = state_budget,
                                   const lazy_byte_alphabet*   shared_alpha = nullptr,
                                   bool                        ascii_word   = false,
                                   bool                        word_quit    = false,
                                   std::size_t                 byte_budget  = lazy_dfa_default_byte_budget)
      : code_ {code}, classes_ {classes},
        alpha_ {shared_alpha != nullptr ? *shared_alpha : compute_lazy_alphabet(code, classes)},
        eligible_ {dfa_representable(code, ascii_word || word_quit)}, word_quit_ {word_quit && !ascii_word},
        look_ {std::ranges::any_of(code, [](const instr& in) { return in.op == opcode::assert_position; })},
        budget_ {budget}, byte_budget_ {byte_budget}
    {
      if (look_) {
        const bool cr {std::ranges::any_of(code, is_cr_line_assert)};
        for (unsigned b {0}; b < 256U; ++b) {
          class_ctx_[alpha_.of[b]] = (word_quit_ && b >= 0x80U) ? rctx_nonascii : right_ctx_of(static_cast<std::uint8_t>(b), cr);
        }
      }
      // Transpose: rev_eps_[x] = the pcs with an epsilon edge to x; rev_consume_[x] = the consuming pcs whose
      // successor is x. Two passes into four flat buffers: a vector per pc was a first search's largest
      // allocation count (7003 blocks for `\w+@\w+` over 8 KB).
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
     * \brief Whether the program can be represented at all (see `compute_eligibility`).
     * \return False when the caller must find the start another way.
     */
    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    /*!
     * \brief Bytes the cached states hold now, as the byte budget counts them.
     * \return The bytes.
     */
    [[nodiscard]] std::size_t bytes() const
    {
      return bytes_;
    }

    /*!
     * \brief Flushes over this object's lifetime that the byte budget called, the state budget not reached.
     * \return The count.
     */
    [[nodiscard]] std::size_t byte_flushes() const
    {
      return byte_flushes_;
    }

    /*!
     * \brief The leftmost start of the match ending at \p e, not before \p resume. Scans the text backward
     *        from \p e over the inverted program, keeping the furthest-back position that reaches the
     *        program start (reverse-`kLongest`). Precondition: a match ends at \p e; eligible programs only.
     *
     * \param[in] text   Subject.
     * \param[in] e      The known match end.
     * \param[in] resume Lower bound the backward scan will not cross.
     * \param[out] read  When not null, the bytes the scan read: it runs until its state dies or \p resume,
     *                   past the start it returns.
     * \return The leftmost start at or after \p resume, or \ref real::npos when none was reached.
     */
    [[nodiscard]] std::size_t reverse_start(std::string_view text,
                                            std::size_t      e,
                                            std::size_t      resume,
                                            std::size_t*     read = nullptr)
    {
      if (look_) {
        return reverse_start_look(text, e, resume, read);
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
      if (read != nullptr) {
        *read = e - pos;
      }
      return best;
    }

  private:

    /*!
     * \brief Starts a closure computation on this DFA's marks.
     * \return The marks, none entered.
     */
    constexpr visit_marks& begin_visit()
    {
      marks_.begin(code_.size());
      return marks_;
    }

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
     * \param[in] b  The byte after the position.
     * \param[in] cr The program's line assertions are ECMAScript's, which end a line at `\r` too.
     * \return Its context bits.
     */
    [[nodiscard]] static constexpr std::uint8_t right_ctx_of(std::uint8_t b,
                                                             bool         cr)
    {
      const bool word {(b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'};
      const bool nl   {b == '\n' || (cr && b == '\r')};
      return static_cast<std::uint8_t>((nl ? rctx_newline : 0U) | (word ? rctx_word : 0U));
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
      switch (kind) {
        case assert_kind::text_start:
        case assert_kind::line_start:
        case assert_kind::line_start_cr:
        case assert_kind::word_boundary:
        case assert_kind::not_word_boundary:
        case assert_kind::word_start:
        case assert_kind::word_end:                  return true;
        case assert_kind::text_end:
        case assert_kind::text_end_or_final_newline:
        case assert_kind::line_end:
        case assert_kind::line_end_cr:               return false;
      }
      return false;
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
        case assert_kind::line_end:
        case assert_kind::line_end_cr:               return (ctx & rctx_end) != 0U || is_newline_ctx(ctx);
        case assert_kind::text_start:
        case assert_kind::line_start:
        case assert_kind::line_start_cr:
        case assert_kind::word_boundary:
        case assert_kind::not_word_boundary:
        case assert_kind::word_start:
        case assert_kind::word_end:                  return false; // decided on the left, by holds_left
      }
      return false;
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
        case assert_kind::line_start:
        case assert_kind::line_start_cr:     return start || prev_nl;
        case assert_kind::word_boundary:     return prev_word != next_word;
        case assert_kind::not_word_boundary: return prev_word == next_word;
        case assert_kind::word_start:        return !prev_word && next_word;
        case assert_kind::word_end:          return prev_word && !next_word;
        case assert_kind::text_end:
        case assert_kind::text_end_or_final_newline:
        case assert_kind::line_end:
        case assert_kind::line_end_cr:       return holds_right(kind, ctx);
      }
      return false;
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
                                    visit_marks&               seen,
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
          if (seen.test(pred)) {
            continue;
          }
          const instr& in {code_[static_cast<std::size_t>(pred)]};
          if (in.op == opcode::assert_position) {
            const auto kind {static_cast<assert_kind>(in.arg8)};
            if (looks_left(kind) && key == key_unknown) {
              seen.set(pred);
              set.push_back(pending_base - pred); // undecided: its predecessors are reached once it is decided
              continue;
            }
            if (!(looks_left(kind) ? holds_left(kind, ctx, key) : holds_right(kind, ctx))) {
              continue; // the edge does not exist at this position
            }
          }
          seen.set(pred);
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
      std::vector<std::int32_t> pcs  {state_pcs_[state]};                          // copy: intern may realloc
      const auto                ctx  {static_cast<std::uint8_t>(-1 - pcs.back())}; // mark_context appends it last
      pcs.pop_back();
      visit_marks&              seen {begin_visit()};
      std::vector<std::int32_t> set;
      quit_hit_ = false;
      for (const std::int32_t entry : pcs) {
        const std::int32_t pc {is_pending(entry) ? pending_base - entry : entry};
        seen.set(pc);
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
      visit_marks&              seen {begin_visit()};
      if (match_pc_ >= 0) {
        seen.set(match_pc_);
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
      const std::vector<std::int32_t> pcs  {state_pcs_[state]};
      std::vector<std::int32_t>       next;
      visit_marks&                    seen {begin_visit()};
      for (const std::int32_t pc : pcs) {
        for (std::size_t k = rev_consume_at_[static_cast<std::size_t>(pc)];
             k < rev_consume_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_consume_pool_[k]};
          if (consumes(pred, byte) && !seen.test(pred)) {
            seen.set(pred);
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
     * \param[out] read  As \ref reverse_start's.
     * \return The leftmost start at or after \p resume, or \ref real::npos.
     */
    std::size_t reverse_start_look(std::string_view text,
                                   std::size_t      e,
                                   std::size_t      resume,
                                   std::size_t*     read)
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
            if (read != nullptr) {
              *read = e - pos;
            }
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
      if (read != nullptr) {
        *read = e - pos;
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
                               visit_marks&               seen) const
    {
      stack_.assign(set.begin(), set.end());
      while (!stack_.empty()) {
        const std::int32_t pc {stack_.back()};
        stack_.pop_back();
        for (std::size_t k = rev_eps_at_[static_cast<std::size_t>(pc)];
             k < rev_eps_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_eps_pool_[k]};
          if (!seen.test(pred)) {
            seen.set(pred);
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
      const std::vector<std::int32_t> pcs  {state_pcs_[state]}; // copy: intern may realloc
      std::vector<std::int32_t>       next;
      visit_marks&                    seen {begin_visit()};
      for (const std::int32_t pc : pcs) {
        for (std::size_t k = rev_consume_at_[static_cast<std::size_t>(pc)];
             k < rev_consume_at_[static_cast<std::size_t>(pc) + 1]; ++k) {
          const std::int32_t pred {rev_consume_pool_[k]};
          if (consumes(pred, byte) && !seen.test(pred)) {
            seen.set(pred);
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
      // intern() may flush, leaving the caller's `state` stale: cache the edge only when no flush happened
      // (the caller reads the returned state).
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
      const bool by_states {state_pcs_.size() >= budget_};
      // Past the dead and start states only: flush() re-interns the start through here, and on a budget the
      // start alone fills it would flush again for ever.
      if (by_states || (bytes_ >= byte_budget_ && state_pcs_.size() > 2U)) {
        byte_flushes_ += by_states ? 0U : 1U;
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
      bytes_ += (pcs.size() * sizeof(std::int32_t)) + sizeof(std::vector<std::int32_t>) + sizeof(std::uint32_t) + 2U
                + (std::size_t {alpha_.count} *sizeof(std::uint32_t)) + (look_ ? (alpha_.count + 1U) * sizeof(std::uint32_t) : 0U);
      return id;
    }

    /*!
     * \brief Empty the cache back to the dead + start states (the eviction: bounded memory).
     */
    constexpr void flush()
    {
      ++flushes_;
      bytes_ = 0;
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
      visit_marks&              seen {begin_visit()};
      if (match_pc_ >= 0) {
        seen.set(match_pc_);
        start.push_back(match_pc_);
        rev_closure(start, seen);
      }
      start_state_ = intern(start);
    }

    std::span<const instr>                                                     code_;                                        //!< The byte program, owned by the caller.
    std::span<const char_class>                                                classes_;                                     //!< Its byte classes, likewise borrowed.
    lazy_byte_alphabet                                                         alpha_;                                       //!< Byte-to-class map; its count is the row stride.
    bool                                                                       eligible_     {false};                        //!< \ref dfa_representable's verdict, fixed at construction.
    bool                                                                       word_quit_    {false};                        //!< Unicode word boundaries carried, quitting next to a non-ASCII byte.
    mutable bool                                                               quit_hit_     {false};                        //!< Set by holds_left() inside one resolve(): that resolution is quit_state.
    bool                                                                       look_         {false};                        //!< The program carries position assertions (the look paths).
    std::array<std::uint8_t, 256>                                              class_ctx_    {};                             //!< Class -> the right context a byte of it gives (look programs).
    std::array<std::uint32_t, 16>                                              starts_       {};                             //!< Right context -> start state, per \ref flush (look programs).
    std::int32_t                                                               match_pc_     {-1};                           //!< The forward `match` pc — this pass's start; -1 when absent.
    std::uint32_t                                                              start_state_  {0};                            //!< Id of \ref match_pc_'s backward closure, re-interned by each \ref flush.
    std::size_t                                                                budget_       {state_budget};                 //!< Cached states tolerated before a \ref flush.
    visit_marks                                                                marks_        {};                             //!< The pcs the closure being computed has entered (\ref begin_visit).
    std::size_t                                                                flushes_      {0};                            //!< bumped by flush(); step()'s stale-state guard against a mid-call reset.
    std::size_t                                                                byte_budget_  {lazy_dfa_default_byte_budget}; //!< Cached bytes tolerated before a \ref flush.
    std::size_t                                                                bytes_        {0};                            //!< Bytes the cached states hold.
    std::size_t                                                                byte_flushes_ {0};                            //!< Flushes the byte budget called, the state budget not reached.
    std::vector<std::int32_t>                                                  rev_eps_pool_;                                //!< transposed epsilon edges, CSR-packed.
    std::vector<std::uint32_t>                                                 rev_eps_at_;                                  //!< CSR offsets into \ref rev_eps_pool_, size code+2.
    std::vector<std::int32_t>                                                  rev_consume_pool_;                            //!< transposed consuming edges, CSR-packed.
    std::vector<std::uint32_t>                                                 rev_consume_at_;                              //!< CSR offsets into \ref rev_consume_pool_.
    mutable std::vector<std::int32_t>                                          stack_;                                       //!< The closures' work stack, a member to spare a heap block per call.
    std::vector<std::vector<std::int32_t>>                                     state_pcs_;                                   //!< state id -> sorted pc-set.
    std::vector<std::uint32_t>                                                 trans_;                                       //!< flat [state*stride + class] -> next.
    std::vector<char>                                                          state_has_start_;                             //!< state -> reaches the program start (an accept).
    std::vector<std::uint8_t>                                                  state_pending_;                               //!< state -> holds a pending assertion (look programs).
    std::vector<std::uint32_t>                                                 res_;                                         //!< flat [state*(count+1) + key] -> resolved state (look programs).
    pc_set_cache                                                               cache_;                                       //!< pc-set -> state id, the memo behind \ref intern.
  };
} // namespace real::detail

#endif // REAL_LAZY_DFA_HPP
