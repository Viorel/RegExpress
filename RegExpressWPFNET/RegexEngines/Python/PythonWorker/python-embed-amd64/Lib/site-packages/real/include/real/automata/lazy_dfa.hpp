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
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "real/core/program.hpp"
#include "real/automata/utf8_ranges.hpp"

namespace real::detail {

  //! \brief Test seam: force the matcher off the lazy-DFA route onto the pure Pike VM, so a differential can
  //!        assert that routed and unrouted searches give identical results within one binary. Not for
  //!        production use — the routing is transparent by contract, and this only exists to prove it.
  inline bool& lazy_dfa_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test seam: force the matcher off the inner-literal search route (IL.2) onto the core search, so a
  //!        differential can assert routed and unrouted searches agree. Not for production use — the route is
  //!        transparent by contract (its reverse bound never advances mid-search, so it cannot miss a leftmost
  //!        match), and this only exists to prove it.
  inline bool& inner_literal_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test seam: force off the rare-discriminant prefilter (`https?://` memchr-`:` route)
  //!        onto prefix/first-byte search, so a differential can assert routed and unrouted agree.
  inline bool& rare_disc_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test seam: force the inner-literal small-haystack guard off, so the route fires on any size. In
  //!        production the guard uses a cold floor (\ref regex_immutables::il_min_haystack) on the first
  //!        candidate-scan and \ref il_warm_floor thereafter (shared reverse DFA in \ref shared_dfa_slot).
  //!        Correctness suites use tiny inputs, so they set this to exercise the route rather than the core
  //!        fallback. Not for production use.
  inline bool& inner_literal_guard_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test seam: force the matcher off the trailing-lookaround class+ route onto the pure Pike VM, so a
  //!        differential can assert routed and unrouted searches agree. Not for production use — the route is
  //!        transparent by contract (same leftmost-first spans as the general loop on the eligible shape).
  inline bool& trailing_la_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test/profile seam: skip dedicated class-scan fast paths (byte class-loop, cp-class-loop,
  //!        and codepoint_class / negated-class `.`/`[^,]+`) so a pattern that would take them falls
  //!        through to lazy-DFA / general (dispatch-optimality audit; matrix4d class-scan rows). Not for
  //!        production — same contract as the other route-disabled seams.
  inline bool& class_fastpath_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test/profile seam : force the matcher off the possessive-loop fast paths
  //!        (bare/suffixed/delimited `X*+`/`X++`) onto the general VM, so a differential can assert
  //!        route-auto and forced-general agree on every input — the route-agreement pattern applied to the new
  //!        recognizers. Not for production use — same contract as the other route-disabled seams.
  inline bool& possessive_fastpath_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief Test seam : force the matcher off the Aho-Corasick multi-literal route (past the
  //!        branch-count threshold) onto the existing \ref pattern_hints::fixed_alternation
  //!        `run_alternation` path, so a differential can assert routed and unrouted searches agree.
  //!        Not for production use — same contract as the other route-disabled seams.
  inline bool& aho_corasick_route_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  //! \brief A byte-level program derived from a Pike program for the DFA passes: every `klass_cp` construct
  //!        is expanded into UTF-8 byte-range split/klass chains, so the whole thing is byte-transition-only
  //!        and a forward DFA can represent it. The Pike program itself is untouched (byte-identity); this
  //!        is a private recognition view the DFAs own. `eligible` is false when an op no DFA can represent
  //!        (a position assertion or a lookaround) is present — the caller then keeps the Pike VM.
  struct byte_program
  {
    std::vector<instr>      code;
    std::vector<char_class> classes;
    bool                    eligible       {true};  //!< Representable by the byte DFAs / Tier-A one-pass.
    bool                    has_assertions {false}; //!< A Tier-B build kept `assert_position` ops (else stripped/declined).
    bool                    unicode_word   {false}; //!< Program default word-ness for `\b \B \< \>` (Tier-B edge conditions).
  };

  //! \brief One node of a minimal deterministic UTF-8 trie for a code-point class. Its transitions are byte
  //!        ranges that are pairwise **disjoint**, so at most one edge matches any byte — that determinism is
  //!        what makes the byte-program one-pass-friendly. `child >= 0` is a node id; `child == -1` is accept
  //!        (a code point ends here — the run continues at the construct's successor).
  struct utf8_trie_node
  {
    std::vector<std::pair<utf8_byte_range, std::int32_t>> trans;
  };

  //! \brief A minimal deterministic UTF-8 trie for a code-point class. `root == -1` means the class is empty.
  struct utf8_trie
  {
    std::vector<utf8_trie_node> nodes;
    std::int32_t                root {-1};
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
   */
  inline utf8_trie build_utf8_trie(const cp_class&             cc,
                                   std::span<const code_range> cp_ranges)
  {
    std::vector<std::vector<utf8_byte_range>> seqs;
    for (int b = 0; b < 0x80;) { // ASCII: each contiguous run of set bits is a one-byte sequence
      if (cc.ascii.test(static_cast<std::uint8_t>(b))) {
        const int lo {b};
        while (b < 0x80 && cc.ascii.test(static_cast<std::uint8_t>(b))) {
          ++b;
        }
        seqs.push_back({utf8_byte_range {.lo = static_cast<std::uint8_t>(lo), .hi = static_cast<std::uint8_t>(b - 1)}});
      }
      else {
        ++b;
      }
    }
    for (std::uint32_t k = 0; k < cc.range_count; ++k) { // non-ASCII: canonical byte-range sequences
      const code_range& r {cp_ranges[cc.range_begin + k]};
      for (const utf8_byte_seq& s : utf8_range_sequences(r.lo, r.hi)) {
        std::vector<utf8_byte_range> seq;
        for (std::size_t j = 0; j < s.length; ++j) {
          seq.push_back(s.parts[j]);
        }
        seqs.push_back(std::move(seq));
      }
    }

    utf8_trie trie;
    if (seqs.empty()) {
      return trie;
    }
    struct builder
    {
      std::vector<utf8_trie_node>&            nodes;
      std::vector<std::vector<std::int32_t>>& memo; // hash-cons index: FNV(trans) bucket -> node ids. The
                                                    // engine headers avoid std::hash / std::unordered_map,
                                                    // whose out-of-line libc++ symbols (e.g. __hash_memory)
                                                    // drift across toolchains; an in-house FNV over the
                                                    // transitions hash-conses with no string key per node.

      static constexpr std::uint64_t hash_trans(const std::vector<std::pair<utf8_byte_range, std::int32_t>>& trans)
      {
        std::uint64_t h {1469598103934665603ULL};
        for (const std::pair<utf8_byte_range, std::int32_t>& t : trans) {
          h = (h ^ t.first.lo) * 1099511628211ULL;
          h = (h ^ t.first.hi) * 1099511628211ULL;
          h = (h ^ static_cast<std::uint32_t>(t.second)) * 1099511628211ULL;
        }
        return h;
      }

      static bool trans_equal(const std::vector<std::pair<utf8_byte_range, std::int32_t>>& a,
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

      std::int32_t build(const std::vector<std::vector<utf8_byte_range>>& in) // all non-empty sequences
      {
        std::vector<int> bounds;
        for (const std::vector<utf8_byte_range>& s : in) {
          bounds.push_back(s[0].lo);
          bounds.push_back(s[0].hi + 1);
        }
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

        utf8_trie_node node;
        for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
          const int                                 lo        {bounds[i]};
          const int                                 hi        {bounds[i + 1] - 1};
          std::vector<std::vector<utf8_byte_range>> tails;
          bool                                      all_empty {true};
          for (const std::vector<utf8_byte_range>& s : in) {
            if (s[0].lo <= lo && hi <= s[0].hi) { // this disjoint interval sits inside sequence s's first range
              std::vector<utf8_byte_range> tail(s.begin() + 1, s.end());
              all_empty = all_empty && tail.empty();
              tails.push_back(std::move(tail));
            }
          }
          if (tails.empty()) {
            continue;
          }
          std::int32_t child {-1};
          if (!all_empty) { // UTF-8 is prefix-free, so within one interval the tails share a length
            std::vector<std::vector<utf8_byte_range>> nonempty;
            for (std::vector<utf8_byte_range>& t : tails) {
              if (!t.empty()) {
                nonempty.push_back(std::move(t));
              }
            }
            child = build(nonempty);
          }
          node.trans.emplace_back(utf8_byte_range {.lo = static_cast<std::uint8_t>(lo), .hi = static_cast<std::uint8_t>(hi)}, child);
        }

        std::vector<std::int32_t>& bucket {memo[hash_trans(node.trans) % memo.size()]};
        for (const std::int32_t existing : bucket) {
          if (trans_equal(nodes[static_cast<std::size_t>(existing)].trans, node.trans)) {
            return existing; // an identical suffix sub-trie already exists (Daciuk sharing)
          }
        }
        const auto id {static_cast<std::int32_t>(nodes.size())};
        nodes.push_back(std::move(node));
        bucket.push_back(id);
        return id;
      }
    };
    constexpr std::size_t                  trie_memo_buckets {1024}; // chained buckets, sized for the bounded trie
    std::vector<std::vector<std::int32_t>> memo(trie_memo_buckets);
    builder                                b                 {trie.nodes, memo};
    trie.root = b.build(seqs);
    return trie;
  }

  //! \brief The instruction count \ref emit_utf8_trie writes: an empty class is one dead `klass`; otherwise
  //!        each node is a split-guarded chain of `k` byte ranges (`3k - 1` instructions).
  inline std::size_t utf8_trie_emit_size(const utf8_trie& trie)
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

  //! \brief Emits \p trie into \p bp as a deterministic split/klass/jump fragment; accept edges jump to
  //!        \p after (the construct's successor). The root is emitted first, so the fragment's entry is its
  //!        base pc. Disjoint ranges mean at most one branch matches any byte.
  inline void emit_utf8_trie(byte_program&    bp,
                             const utf8_trie& trie,
                             std::int32_t     after)
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
        char_class cc;
        cc.set_range(edge.first.lo, edge.first.hi);
        bp.code.push_back({.op = opcode::klass, .arg16 = static_cast<std::uint16_t>(bp.classes.size())});
        bp.classes.push_back(cc);
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
  //! get a chance to bound it). Calibrated empirically (arm64, the sanitizer-instrumented `make fuzz` build,
  //! whose -timeout=10 is the real constraint): `\w{5}a` -> 17154 instrs/1.3s, `\w{10}a` -> 34304/3.9s,
  //! `\w{15}a` -> 51454/7.1s already flirts with the timeout. 20000 keeps sanitized wall time around 1.5-2s
  //! (5x+ margin) while comfortably exceeding every legitimate pattern in the test suite. Exceeding it
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
   */
  inline byte_program build_byte_program(const program_view& prog,
                                         bool                keep_assertions = false,
                                         std::size_t         max_size        = max_byte_program_size)
  {
    byte_program bp;
    bp.unicode_word = prog.unicode_word;
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
    std::vector<utf8_trie>    tries(n);                          // the trie for each klass_cp pc (built once, reused when emitting)
    std::size_t               cur {0};
    for (std::size_t pc = 0; pc < n; ++pc) {
      map[pc] = static_cast<std::int32_t>(cur);
      if (prog.code[pc].op == opcode::klass_cp) {
        tries[pc]   = build_utf8_trie(prog.cp_classes[prog.code[pc].arg16], prog.cp_ranges);
        cur        += utf8_trie_emit_size(tries[pc]);
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

    const auto remap {[&](std::int32_t t) {
                        return (t >= 0 && static_cast<std::size_t>(t) <= n) ? map[static_cast<std::size_t>(t)] : t;
                      }};
    for (std::size_t pc = 0; pc < n; ++pc) {
      const instr& in {prog.code[pc]};
      if (in.op == opcode::klass_cp) {
        emit_utf8_trie(bp, tries[pc], map[pc + 4]);
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

  //! \brief Partition 0..255 by the program's consuming predicates (every `klass` test, every `byte`
  //!        literal). Bytes with an identical signature collapse to one class.
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
    for (const instr& in : code) {
      if (in.op == opcode::klass && in.arg16 < classes.size()) {
        push_unique(class_preds, classes[in.arg16]);
      }
      else if (in.op == opcode::byte) {
        push_unique(literal_preds, static_cast<std::uint16_t>(in.arg8));
      }
    }
    const auto sig_equal {[&](unsigned a, unsigned b) {
                            for (const char_class& cc : class_preds) {
                              if (cc.test(static_cast<std::uint8_t>(a)) != cc.test(static_cast<std::uint8_t>(b))) {
                                return false;
                              }
                            }
                            for (const std::uint16_t lit : literal_preds) {
                              if ((a == lit) != (b == lit)) {
                                return false;
                              }
                            }
                            return true;
                          }};
    lazy_byte_alphabet            alpha;
    std::array<std::uint8_t, 256> rep {};
    for (unsigned b = 0; b < 256U; ++b) {
      bool assigned {false};
      for (std::uint16_t c = 0; c < alpha.count; ++c) {
        if (sig_equal(b, rep[c])) {
          alpha.of[b] = static_cast<std::uint8_t>(c);
          assigned    = true;
          break;
        }
      }
      if (!assigned) {
        rep[alpha.count] = static_cast<std::uint8_t>(b);
        alpha.of[b]      = static_cast<std::uint8_t>(alpha.count);
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
    static constexpr std::size_t   bucket_count {2048};
    static constexpr std::uint32_t not_found    {0xFFFFFFFFU};

    std::vector<std::vector<std::uint32_t>> buckets;

    constexpr pc_set_cache()
      : buckets(bucket_count)
    {}

    static constexpr std::size_t hash(const std::vector<std::int32_t>& v)
    {
      // FNV-1a computed in a fixed 64-bit accumulator, truncated to size_t on return: the 64-bit
      // offset-basis brace-initialised straight into size_t narrows (an error) where size_t is 32-bit
      // (win32). Truncating a full FNV-64 keeps a well-distributed hash without width-specific constants.
      std::uint64_t h {1469598103934665603ULL};
      for (const std::int32_t x : v) {
        h = (h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))) * 1099511628211ULL;
      }
      return static_cast<std::size_t>(h);
    }

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

    constexpr void insert(const std::vector<std::int32_t>& pcs,
                          std::uint32_t                    id)
    {
      buckets[hash(pcs) % bucket_count].push_back(id);
    }

    constexpr void clear()
    {
      for (std::vector<std::uint32_t>& b : buckets) {
        b.clear();
      }
    }
  };

  /*!
   * \brief A lazy priority-preserving forward DFA over a Pike program (the kFirstMatch forward pass).
   *
   * A DFA state is the ordered epsilon-closure of a set of program counters (the Pike thread list's PCs,
   * in split priority). \ref step transitions on a byte by consuming it from each PC and re-closing, then
   * interns the resulting ordered set into a cached state — the subset construction, memoized on demand.
   *
   * The cache is bounded: once it reaches \ref state_budget states it is flushed and rebuilt (states are
   * cheap to recompute; a bounded cache keeps memory flat). `state_budget` flushes crossed within one
   * scan (see \ref begin_scan) trips \ref thrashing — the signal an eventual caller uses to abandon the
   * DFA and finish that one search on the Pike VM, per-scan and linear, never re-attempting per position.
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
    static constexpr std::uint32_t no_match_idx   {0xFFFFFFFFU}; //!< A state whose ordered set holds no accept.
    static constexpr std::size_t   state_budget   {4096};        //!< Cached states before a flush (the memory cap).
    static constexpr std::size_t   thrash_flushes {2};           //!< Flushes within one scan that trip \ref thrashing.

    //! \brief Cache-behaviour counters, for the policy tests and later tuning.
    struct counters
    {
      std::size_t hits         {0};
      std::size_t misses       {0};
      std::size_t flushes      {0};
      std::size_t scan_flushes {0};   //!< flushes in the current scan (reset by \ref begin_scan).
    };

    /*!
     * \brief Builds the (initially empty) lazy DFA over a Pike program.
     * \param[in] code    The program's instruction stream (must outlive this object — held as a span).
     * \param[in] classes The program's interned character classes (likewise held as a span).
     * \param[in] budget  Cached states before a flush; defaults to \ref state_budget. A smaller value is a
     *                    test hook to exercise eviction and thrash without a state-exploding pattern.
     */
    //! \param[in] shared_alpha A precomputed alphabet the caller shares per regex, or null to compute it here.
    //!        Recomputing is O(256 x classes) and a Unicode byte-program has thousands, so the router passes
    //!        the shared one rather than paying it on every scan.
    explicit constexpr lazy_dfa(std::span<const instr>      code,
                                std::span<const char_class> classes,
                                std::size_t                 budget       = state_budget,
                                const lazy_byte_alphabet*   shared_alpha = nullptr)
      : code_ {code}, classes_ {classes},
        alpha_ {shared_alpha != nullptr ? *shared_alpha : compute_lazy_alphabet(code, classes)},
        eligible_ {compute_eligibility(code)}, budget_ {budget}
    {
      flush();                 // seeds the dead state (0) and the start state (1)
    }

    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    [[nodiscard]] std::uint16_t num_classes() const
    {
      return alpha_.count;
    }

    [[nodiscard]] std::uint32_t start_state() const
    {
      return start_state_;
    }

    //! \brief Begin a search: clear the per-scan flush counter and the thrash flag. (No cache flush — the
    //!        states carry over between searches on the same text, which is where the cache pays.)
    void begin_scan()
    {
      stats_.scan_flushes = 0;
      thrashing_          = false;
    }

    [[nodiscard]] bool thrashing() const
    {
      return thrashing_;
    }

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
     */
    [[nodiscard]] std::size_t forward_end(std::string_view text)
    {
      if (!eligible_) {
        return npos;
      }
      begin_scan();
      std::uint32_t state    {start_state_}; // the seed at position 0 (a re-seeding state)
      std::size_t   best_end {npos};
      bool          matched  {false};
      std::size_t   pos      {0};
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
        state = matched ? step(state, byte) : step_seeded(state, byte);
        ++pos;
      }
      return best_end;
    }

    /*! \brief \ref anchored_end's result: the match end (or \ref real::npos) and how far the walk got. */
    struct anchored_result
    {
      std::size_t end;        //!< Match end, or \ref real::npos.
      std::size_t scanned_to; //!< Position the walk stopped at (see \ref anchored_end).
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
      if (!eligible_) {
        return {.end = npos, .scanned_to = start};
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
      return {.end = best_end, .scanned_to = pos};
    }

    //! \brief Whether \p state accepts here (its ordered set contains a `match` PC).
    [[nodiscard]] bool is_match(std::uint32_t state) const
    {
      return state_match_idx_[state] != no_match_idx;
    }

    /*!
     * \brief Transition \p state on \p byte to the next DFA state, computing and caching it on first use.
     *        Returns \ref dead_state when no thread survives the byte.
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
      for (const std::int32_t pc : state_pcs_[state]) {
        if (consumes(pc, byte)) {
          close_into(pc + consumed_width(pc), next, seen);
        }
      }
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern(next)}; // may grow/flush the tables — do not hold a reference
      if (stats_.flushes == flushes_before) {
        trans_[(static_cast<std::size_t>(state) * alpha_.count) + cls] = result;   // no flush: `state` is still valid, so cache the edge
      }
      // On a flush mid-step the caller's `state` id is stale; `result` is a fresh post-flush id, and the
      // caller re-seeds. (An eventual forward pass falls back to Pike once \ref thrashing trips.)
      return result;
    }

  private:

    //! \brief Like \ref step, but re-seeds: the unanchored-search variant appends pc 0's closure at the
    //!        lowest priority, so a fresh thread starts at every position until a match is found. Cached in
    //!        its own transition row (the pre-match state family).
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
      for (const std::int32_t pc : pcs) {
        if (consumes(pc, byte)) {
          close_into(pc + consumed_width(pc), next, seen);
        }
      }
      close_into(0, next, seen); // re-seed at the lowest priority (deduped against the advanced threads)
      const std::size_t   flushes_before {stats_.flushes};
      const std::uint32_t result         {intern(next)};
      if (stats_.flushes == flushes_before) {
        trans_seeded_[(static_cast<std::size_t>(state) * alpha_.count) + cls] = result;
      }
      return result;
    }

    //! \brief The priority-cut at an accept: intern the prefix of \p state's ordered pc-set before index
    //!        \p m (dropping the accept and every lower-priority thread). Returns \ref dead_state if empty.
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

    //! \brief The priority-cut of \p state at its own accept, memoized. `cut` is O(state size) — it rebuilds
    //!        and re-interns the prefix — and a Unicode `klass_cp` byte-program makes states thousands of PCs
    //!        wide, so recomputing it once per match (per find_iter step) dominated. Cached per state, it is
    //!        computed once and then O(1). The state's accept index is fixed, so the cut is deterministic.
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
      return result;
    }

    static constexpr bool compute_eligibility(std::span<const instr> code)
    {
      for (const instr& in : code) {
        if (in.op == opcode::assert_position || in.op == opcode::assert_lookaround
            || in.op == opcode::klass_cp || in.op == opcode::byte_loop_possessive
            || in.op == opcode::klass_loop_possessive || in.op == opcode::klass_cp_loop_possessive) {
          // Position assertions / variable-width classes: no forward-DFA representation. Tier 1's
          // possessive-loop family additionally has no consuming-edge representation at all here
          // (consumes() below only recognizes byte/klass) — treating it as a dead end (silently
          // non-consuming) would be an outright wrong DFA, not just an unrepresented shape.
          return false;
        }
      }
      return true;
    }

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

    [[nodiscard]] static std::int32_t consumed_width(std::int32_t /*pc*/)
    {
      return 1;
    }

    //! \brief Append the ordered epsilon-closure of \p pc to \p out (split priority; save/jump crossed),
    //!        collecting the consuming and `match` PCs. Uses \p seen to dedup within this closure.
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
      std::vector<std::int32_t> stack {pc};
      while (!stack.empty()) {
        const std::int32_t cur {stack.back()};
        stack.pop_back();
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
            stack.push_back(in.secondary_target);   // secondary pushed first -> primary explored first
            stack.push_back(in.primary_target);
            break;
          case opcode::jump:
            stack.push_back(in.primary_target);
            break;
          case opcode::save:
            stack.push_back(cur + 1);
            break;
          default:
            break;   // assert / klass_cp / lookaround: only reachable for ineligible programs (not built)
        }
      }
    }

    //! \brief Intern an ordered pc-set into a state id (cached). Flushes the cache when the budget is hit.
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

    constexpr std::uint32_t intern_fresh(const std::vector<std::int32_t>& pcs)
    {
      const auto id {static_cast<std::uint32_t>(state_pcs_.size())};
      state_pcs_.push_back(pcs);
      trans_.insert(trans_.end(), alpha_.count, no_transition);
      trans_seeded_.insert(trans_seeded_.end(), alpha_.count, no_transition);
      std::uint32_t match_idx {no_match_idx};
      for (std::size_t i = 0; i < pcs.size(); ++i) {
        if (code_[static_cast<std::size_t>(pcs[i])].op == opcode::match) {
          match_idx = static_cast<std::uint32_t>(i); // the highest-priority accept in this state
          break;
        }
      }
      state_match_idx_.push_back(match_idx);
      state_cut_.push_back(no_transition); // the priority-cut result, memoized lazily on first use
      cache_.insert(pcs, id);
      return id;
    }

    //! \brief Empty the cache back to the dead + start states (the eviction: bounded memory).
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
      cache_.clear();
      // state 0 = dead (empty, self-looping), state 1 = start (closure of pc 0).
      state_pcs_.emplace_back();
      trans_.insert(trans_.end(), alpha_.count, dead_state);
      trans_seeded_.insert(trans_seeded_.end(), alpha_.count, dead_state);
      state_match_idx_.push_back(no_match_idx);
      state_cut_.push_back(no_transition);
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      close_into(0, start, seen);
      start_state_ = intern_fresh(start);
    }

    std::span<const instr>      code_;
    std::span<const char_class> classes_;
    lazy_byte_alphabet          alpha_;
    bool                        eligible_    {false};
    std::uint32_t               start_state_ {0};

    std::vector<std::vector<std::int32_t>>                                    state_pcs_;          //!< state id -> ordered pc-set.
    std::vector<std::uint32_t>                                                trans_;              //!< flat [state*stride + class] -> next, unseeded (post-match); stride = alpha_.count.
    std::vector<std::uint32_t>                                                trans_seeded_;       //!< flat [state*stride + class] -> next, re-seeding (pre-match).
    std::vector<std::uint32_t>                                                state_match_idx_;    //!< state id -> index of its first accept, or no_match_idx.
    std::vector<std::uint32_t>                                                state_cut_;          //!< state id -> memoized priority-cut result (no_transition = not yet computed).
    pc_set_cache                                                              cache_;

    std::size_t budget_    {state_budget};
    counters    stats_     {};
    bool        thrashing_ {false};
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

    static constexpr std::uint32_t dead_state    {0};
    static constexpr std::uint32_t no_transition {0xFFFFFFFFU};
    static constexpr std::size_t   state_budget  {4096};

    explicit constexpr reverse_dfa(std::span<const instr>      code,
                                   std::span<const char_class> classes,
                                   std::size_t                 budget       = state_budget,
                                   const lazy_byte_alphabet*   shared_alpha = nullptr)
      : code_ {code}, classes_ {classes},
        alpha_ {shared_alpha != nullptr ? *shared_alpha : compute_lazy_alphabet(code, classes)},
        eligible_ {compute_eligibility(code)}, budget_ {budget}
    {
      // Transpose the program: rev_eps_[x] = the pcs with a forward epsilon edge to x; rev_consume_[x] = the
      // consuming pcs whose successor is x (a byte/klass at pc goes to pc+1).
      rev_eps_.assign(code.size(), {});
      rev_consume_.assign(code.size(), {});
      for (std::int32_t pc = 0; pc < static_cast<std::int32_t>(code.size()); ++pc) {
        const instr& in {code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::byte:
          case opcode::klass:
            rev_consume_[static_cast<std::size_t>(pc) + 1].push_back(pc);
            break;
          case opcode::split:
            rev_eps_[static_cast<std::size_t>(in.primary_target)].push_back(pc);
            rev_eps_[static_cast<std::size_t>(in.secondary_target)].push_back(pc);
            break;
          case opcode::jump:
            rev_eps_[static_cast<std::size_t>(in.primary_target)].push_back(pc);
            break;
          case opcode::save:
            rev_eps_[static_cast<std::size_t>(pc) + 1].push_back(pc);
            break;
          case opcode::match:
            match_pc_ = pc; // the reverse start
            break;
          default:
            break;
        }
      }
      flush();
    }

    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    /*!
     * \brief The leftmost start of the match ending at \p e, not before \p resume. Scans the text backward
     *        from \p e over the inverted program, keeping the furthest-back position that reaches the
     *        program start (reverse-`kLongest`). Precondition: a match ends at \p e; eligible programs only.
     */
    [[nodiscard]] std::size_t reverse_start(std::string_view text,
                                            std::size_t      e,
                                            std::size_t      resume)
    {
      std::uint32_t state {start_state_}; // rev-closure of the forward `match`
      std::size_t   best  {npos};
      std::size_t   pos   {e};
      while (true) {
        if (state_has_start_[state] != 0) {
          best = pos; // reached the original start: [pos, e] matches; kLongest keeps the smallest pos
        }
        if (pos <= resume || state == dead_state) {
          break;
        }
        --pos;
        state = step(state, static_cast<std::uint8_t>(text[pos]));
      }
      return best;
    }

  private:

    constexpr void rev_closure(std::vector<std::int32_t>& set,
                               std::vector<char>&         seen) const
    {
      std::vector<std::int32_t> stack {set};
      while (!stack.empty()) {
        const std::int32_t pc {stack.back()};
        stack.pop_back();
        for (const std::int32_t pred : rev_eps_[static_cast<std::size_t>(pc)]) {
          if (seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            set.push_back(pred);
            stack.push_back(pred);
          }
        }
      }
      std::sort(set.begin(), set.end()); // unordered: a canonical (sorted) key, no priority
    }

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
        for (const std::int32_t pred : rev_consume_[static_cast<std::size_t>(pc)]) {
          if (consumes(pred, byte) && seen[static_cast<std::size_t>(pred)] == 0) {
            seen[static_cast<std::size_t>(pred)] = 1;
            next.push_back(pred);
          }
        }
      }
      rev_closure(next, seen);
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

    [[nodiscard]] bool consumes(std::int32_t pc,
                                std::uint8_t byte) const
    {
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      if (in.op == opcode::byte) {
        return static_cast<std::uint8_t>(in.arg8) == byte;
      }
      return in.op == opcode::klass && classes_[in.arg16].test(byte);
    }

    static constexpr bool compute_eligibility(std::span<const instr> code)
    {
      for (const instr& in : code) {
        if (in.op == opcode::assert_position || in.op == opcode::assert_lookaround
            || in.op == opcode::klass_cp || in.op == opcode::byte_loop_possessive
            || in.op == opcode::klass_loop_possessive || in.op == opcode::klass_cp_loop_possessive) {
          // Tier 1's possessive-loop family has no consuming-edge representation here either
          // (consumes() above only recognizes byte/klass) -- same reasoning as the forward-DFA's
          // own compute_eligibility.
          return false;
        }
      }
      return true;
    }

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
      for (const std::int32_t pc : pcs) {
        if (pc == 0) { // pc 0 is the program's save-0 start
          has_start = true;
          break;
        }
      }
      state_has_start_.push_back(has_start ? 1 : 0);
      cache_.insert(pcs, id);
      return id;
    }

    constexpr void flush()
    {
      ++flushes_;
      state_pcs_.clear();
      trans_.clear();
      state_has_start_.clear();
      cache_.clear();
      state_pcs_.emplace_back();                          // dead state 0
      trans_.insert(trans_.end(), alpha_.count, dead_state);
      state_has_start_.push_back(0);
      std::vector<std::int32_t> start;
      std::vector<char>         seen(code_.size(), 0);
      if (match_pc_ >= 0) {
        seen[static_cast<std::size_t>(match_pc_)] = 1;
        start.push_back(match_pc_);
        rev_closure(start, seen);
      }
      start_state_ = intern(start);
    }

    std::span<const instr>                                                    code_;
    std::span<const char_class>                                               classes_;
    lazy_byte_alphabet                                                        alpha_;
    bool                                                                      eligible_    {false};
    std::int32_t                                                              match_pc_    {-1};
    std::uint32_t                                                             start_state_ {0};
    std::size_t                                                               budget_      {state_budget};
    std::size_t                                                               flushes_     {0}; //!< bumped by flush(); step()'s stale-state guard against a mid-call reset.
    std::vector<std::vector<std::int32_t>>                                    rev_eps_;         //!< transposed epsilon edges.
    std::vector<std::vector<std::int32_t>>                                    rev_consume_;     //!< transposed consuming edges (the pred consuming pcs).
    std::vector<std::vector<std::int32_t>>                                    state_pcs_;
    std::vector<std::uint32_t>                                                trans_;           //!< flat [state*stride + class] -> next.
    std::vector<char>                                                         state_has_start_; //!< state -> reaches the program start (an accept).
    pc_set_cache                                                              cache_;
  };
} // namespace real::detail

#endif // REAL_LAZY_DFA_HPP
