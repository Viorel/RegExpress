/*!
 * \file onepass.hpp
 * \brief The one-pass builder: decides whether a pattern is *one-pass* and, if so, tabulates a deterministic
 *        capture-writing automaton over the byte-program.
 *
 * A pattern is **one-pass** (Brüggemann-Klein & Wood, "One-unambiguous regular languages"; RE2 `onepass.cc`)
 * when, matched anchored, at most one thread crosses any byte — the non-determinism is contained. For such a
 * pattern the capture slots can be filled in a single left-to-right pass with no thread lists at all: at each
 * node, the byte read selects exactly one outgoing edge, whose recorded conditions say which slots take the
 * current position. `(\w+)@(\w+)` is one-pass (inside `\w+` an `@` cannot extend the run, so there is no
 * ambiguity); `(\w+)_(\w+)` is not (`_` is itself a `\w`, so a `_` both extends group 1 and starts the
 * separator — a genuine conflict).
 *
 * This header is the **builder only** (the arc's first slice): it classifies a pattern and, when one-pass,
 * produces the node table. Nothing runs matching through it yet. It builds over the L2.5 byte_program
 * (Unicode `\w \d \s` already expanded to byte ranges), so the one-pass check runs at the byte level. The
 * table format here is a readable struct, not RE2's packed `uint32`; packing is a runtime concern (a later
 * slice) that the differential would catch either way.
 */
#ifndef REAL_ONEPASS_HPP
#define REAL_ONEPASS_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <mutex>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "real/engine/assert_eval.hpp"
#include "real/automata/lazy_dfa.hpp"
#include "real/core/program.hpp"

namespace real::detail {

  //! \brief One outgoing edge of a one-pass node, for a byte-class: the next node and the capture slots that
  //!        take the current position as the byte is consumed. Two epsilon paths reaching the same class with
  //!        a different edge is the one-pass conflict — the pattern is then rejected.
  struct onepass_edge
  {
    std::uint32_t next        {0};     //!< Next node id (valid only when \ref assigned).
    std::uint64_t cap_mask    {0};     //!< Bit i set => write the current position into slot i on this edge.
    std::uint32_t assert_mask {0};     //!< Bit k (an \ref assert_kind) set => that assertion must hold at the position to take this edge (Tier-B).
    bool          assigned    {false}; //!< Whether this byte-class has an edge from this node.
  };

  //! \brief A one-pass node: one edge per byte-class, plus whether the run may end here and with what
  //!        captures. Nodes are the points the automaton can be in *between* byte reads.
  struct onepass_node
  {
    std::vector<onepass_edge> edge;                      //!< Indexed by byte-class.
    bool                      matches           {false}; //!< Reaching `match` from here (via epsilon).
    std::uint64_t             match_cap_mask    {0};     //!< Slots written when the match is taken.
    std::uint32_t             match_assert_mask {0};     //!< Assertions that must hold at the end for the match (Tier-B).
  };

  /*!
   * \brief Builds and holds the one-pass classification (and table, when eligible) of a byte-program.
   *
   * Construction floods the program from the start: for each node it walks the epsilon-closure (`split`,
   * `jump`, `save`) accumulating the capture mask, and every consuming instruction (`byte`, `klass`) writes
   * the edge for its byte-class(es). A byte-class written twice with a different edge, a second reachable
   * `match` with different captures, or an epsilon cycle (a nullable loop) each means *not one-pass* and the
   * build bails with a human-readable reason. Node and slot counts are capped, so a pathological program is
   * rejected rather than explored without bound.
   */
  class onepass
  {
  public:

    static constexpr std::uint32_t no_node          {0xFFFFFFFFU};  //!< "No node yet" sentinel in the pc->node map.
    static constexpr std::size_t   max_nodes        {65000};        //!< Node cap (RE2's), a memory/DoS bound.
    static constexpr std::size_t   max_slots        {10};           //!< Slot-pointer cap: group 0 + four user groups.
    static constexpr std::size_t   minimize_buckets {4096};         //!< Hash buckets for the Moore-refinement dedup.
    static constexpr std::size_t   max_table_bytes  {8U << 20};     //!< Table-memory cap (~8 MB): larger declines to the VM.

    //! \param[in] bp        The byte-program to classify.
    //! \param[in] max_bytes Table-memory cap; larger tables decline. Defaults to \ref max_table_bytes; a
    //!                      smaller value is a test hook to exercise the cap without a huge pattern.
    //! \param[in] node_cap  Node-count cap (see \ref max_nodes). Defaults to \ref max_nodes; a smaller
    //!                      value is a test hook to exercise the cap without a 65000-node pattern.
    explicit constexpr onepass(const byte_program&  bp,
                               std::size_t          max_bytes = max_table_bytes,
                               std::size_t          node_cap  = max_nodes)
      : max_bytes_ {max_bytes}, node_cap_ {node_cap},
        ascii_word_ {!bp.unicode_word} // word-ness mode for \b \B \< \> in edge conditions (Tier-B)
    {
      if (!bp.eligible) {
        bail("the byte-program is itself ineligible (a lookaround, or a word-ness-flipped assertion)");
        return;
      }
      build(bp);
    }

    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    [[nodiscard]] const std::string& bail_reason() const
    {
      return bail_reason_;
    }

    [[nodiscard]] std::size_t node_count() const
    {
      return nodes_.size();
    }

    [[nodiscard]] std::uint16_t num_classes() const
    {
      return alpha_.count;
    }

    [[nodiscard]] const std::vector<onepass_node>& nodes() const
    {
      return nodes_;
    }

    //! \brief The byte-class of \p byte, for a runtime that walks this table.
    [[nodiscard]] std::uint8_t class_of(std::uint8_t byte) const
    {
      return alpha_.of[byte];
    }

    //! \brief The number of capture slots (group 0 start/end plus each group's).
    [[nodiscard]] std::size_t slot_count() const
    {
      return slot_count_;
    }

    /*!
     * \brief Fills \p out with the capture slots of the one-pass match on `text[s, e)` — the single left-to-
     *        right pass the whole arc is for, no thread lists. \p text is the **full** subject (never a
     *        substring: assertions look at `s - 1` and `e`). Anchored at \p s; this is *fullmatch-on-span*
     *        (the span the router located): it consumes to \p e and requires the run to accept exactly there.
     *        `\ref real::npos` marks a slot no edge wrote. Returns `false` (leaving \p out unspecified) if the
     *        pattern is ineligible or the span does not in fact match — which the caller has already ruled
     *        out for a router-supplied span.
     *
     * \param[in]  text The full subject.
     * \param[in]  s    Match start (anchor).
     * \param[in]  e    Match end (the run must accept here).
     * \param[out] out  Capture slots, sized to \ref slot_count.
     */
    template <typename OutSlots>
    [[nodiscard]] bool extract(std::string_view text,
                               std::size_t      s,
                               std::size_t      e,
                               OutSlots&        out) const
    {
      if (!eligible_) {
        return false;
      }
      out.assign(slot_count_, npos);
      std::uint32_t node {0}; // node 0 is the start (the closure of pc 0)
      for (std::size_t pos = s; pos < e; ++pos) {
        const std::uint8_t  cls  {alpha_.of[static_cast<std::uint8_t>(text[pos])]};
        const onepass_edge& edge {nodes_[node].edge[cls]};
        if (!edge.assigned) {
          return false;                                             // no outgoing edge for this byte — the span does not match
        }
        if (edge.assert_mask != 0 && !asserts_hold(edge.assert_mask, text, pos)) {
          return false;                                             // an assertion on this edge does not hold here (Tier-B)
        }
        for (std::uint64_t m = edge.cap_mask; m != 0; m &= m - 1) {
          out[static_cast<std::size_t>(std::countr_zero(m))] = pos; // saves crossed before this byte take pos
        }
        node = edge.next;
      }
      if (!nodes_[node].matches) {
        return false;                                           // reached e but not at an accept
      }
      if (nodes_[node].match_assert_mask != 0 && !asserts_hold(nodes_[node].match_assert_mask, text, e)) {
        return false;                                           // an end assertion ($, \b, \Z…) does not hold at e
      }
      for (std::uint64_t m = nodes_[node].match_cap_mask; m != 0; m &= m - 1) {
        out[static_cast<std::size_t>(std::countr_zero(m))] = e; // saves crossed to the match take e
      }
      return true;
    }

    //! \brief Whether every assertion in \p mask (a set of \ref assert_kind bits) holds at \p pos in \p text.
    [[nodiscard]] bool asserts_hold(std::uint32_t    mask,
                                    std::string_view text,
                                    std::size_t      pos) const
    {
      for (std::uint32_t m = mask; m != 0; m &= m - 1) {
        const auto kind {static_cast<assert_kind>(std::countr_zero(m))};
        if (!assertion_holds(kind, text, pos, ascii_word_)) {
          return false;
        }
      }
      return true;
    }

  private:

    //! \brief Reject as not one-pass, recording a category and the offending node / byte-class / pc (kept as
    //!        integers rather than formatted into the string so the whole builder stays constexpr — a
    //!        constexpr `real::regex` embeds an (empty) one-pass table in its literal state).
    constexpr void bail(const char*  reason,
                        std::int32_t node  = -1,
                        std::int32_t klass = -1,
                        std::int32_t pc    = -1)
    {
      eligible_    = false;
      bail_reason_ = reason;
      bail_node_   = node;
      bail_class_  = klass;
      bail_pc_     = pc;
    }

    //! \brief Get-or-create the node whose entry pc is \p pc, enqueueing a fresh one for the flood.
    constexpr std::uint32_t node_of(std::int32_t               pc,
                                    std::vector<std::int32_t>& queue)
    {
      std::uint32_t& id {pc_to_node_[static_cast<std::size_t>(pc)]};
      if (id == no_node) {
        id = static_cast<std::uint32_t>(nodes_.size());
        onepass_node fresh;
        fresh.edge.assign(alpha_.count, onepass_edge {});
        nodes_.push_back(std::move(fresh));
        queue.push_back(pc);
      }
      return id;
    }

    constexpr void build(const byte_program& bp)
    {
      code_    = bp.code;
      classes_ = bp.classes;
      alpha_   = compute_lazy_alphabet(bp.code, bp.classes);

      // For each interned char-class, the byte-classes it consumes — so a `klass` instruction writes only
      // those edges instead of scanning the whole alphabet. Computed once here (O(classes x 256)) rather
      // than per instruction (which was O(nodes x classes) and dominated a Unicode \w build).
      class_cover_.assign(classes_.size(), {});
      for (std::size_t i = 0; i < classes_.size(); ++i) {
        std::vector<std::uint16_t>& cover {class_cover_[i]};
        for (unsigned b = 0; b < 256U; ++b) {
          if (classes_[i].test(static_cast<std::uint8_t>(b))) {
            cover.push_back(alpha_.of[b]);
          }
        }
        std::ranges::sort(cover);
        cover.erase(std::ranges::unique(cover).begin(), cover.end());
      }

      std::size_t max_slot {0};
      for (const instr& in : bp.code) {
        if (in.op == opcode::save) {
          max_slot = std::max(max_slot, static_cast<std::size_t>(in.arg16));
        }
      }
      slot_count_ = max_slot + 1;
      if (slot_count_ > max_slots) {
        bail("too many capture slots: the general Pike VM keeps these", static_cast<std::int32_t>(slot_count_));
        return;
      }

      pc_to_node_.assign(bp.code.size(), no_node);
      std::vector<std::int32_t> queue;
      node_of(0, queue); // the start node
      while (!queue.empty() && eligible_) {
        const std::int32_t pc {queue.back()};
        queue.pop_back();
        std::vector<char> on_path(bp.code.size(), 0);
        build_edges(pc, 0, 0, on_path, pc_to_node_[static_cast<std::size_t>(pc)], queue);
        if (nodes_.size() > node_cap_) {
          bail("node cap exceeded", static_cast<std::int32_t>(nodes_.size()));
          return;
        }
      }
      if (!eligible_) {
        return;
      }
      minimize(); // (i) collapse equivalent nodes (the byte-program's trie sharing, lost in the flood, recovered)
      // (ii) memory cap: even minimized, a pathological table declines to the Pike VM rather than bloat the regex.
      std::size_t bytes {0};
      for (const onepass_node& nd : nodes_) {
        bytes += nd.edge.capacity() * sizeof(onepass_edge);
      }
      if (bytes > max_bytes_) {
        bail("one-pass table too large after minimization (MB)", static_cast<std::int32_t>(bytes >> 20));
      }
    }

    //! \brief Moore partition refinement of the one-pass automaton: merge nodes that are behaviourally
    //!        identical (same accept + match captures, and for every byte-class the same edge — target
    //!        partition AND capture mask). The one-pass graph has cycles (`\w+` loops), so bottom-up
    //!        hash-consing is not enough; refinement to a fixpoint is. Merged nodes have identical capture
    //!        masks by construction, so captures are unchanged. The dense per-node edge table is preserved,
    //!        so `extract`'s O(1) lookup is unchanged — the whole point of not going sparse.
    constexpr void minimize()
    {
      const std::size_t          n       {nodes_.size()};
      std::vector<std::uint32_t> cls(n, 0);
      std::uint32_t              classes {0};
      while (true) {
        std::vector<std::vector<std::uint64_t>> sigs(n);
        for (std::size_t i = 0; i < n; ++i) {
          std::vector<std::uint64_t>& s {sigs[i]};
          s.push_back(cls[i]);
          s.push_back(nodes_[i].matches ? 1U : 0U);
          s.push_back(nodes_[i].match_cap_mask);
          s.push_back(nodes_[i].match_assert_mask);
          for (const onepass_edge& e : nodes_[i].edge) {
            s.push_back(e.assigned ? static_cast<std::uint64_t>(cls[e.next]) + 1U : 0U);
            s.push_back(e.cap_mask);
            s.push_back(e.assert_mask);
          }
        }
        std::vector<std::uint32_t>              next_cls(n, 0);
        std::vector<std::vector<std::uint32_t>> buckets(minimize_buckets);
        std::uint32_t                           next {0};
        for (std::size_t i = 0; i < n; ++i) {
          std::vector<std::uint32_t>& bucket {buckets[sig_hash(sigs[i]) % minimize_buckets]};
          std::uint32_t               id     {no_node};
          for (const std::uint32_t j : bucket) {
            if (sigs[j] == sigs[i]) {
              id = next_cls[j];
              break;
            }
          }
          if (id == no_node) {
            next_cls[i] = next++;
            bucket.push_back(static_cast<std::uint32_t>(i));
          }
          else {
            next_cls[i] = id;
          }
        }
        cls = std::move(next_cls);
        if (next == classes) {
          break; // fixpoint: the partition stopped refining
        }
        classes = next;
      }

      // Rebuild with one node per class, the start (old node 0) renumbered to node 0.
      std::vector<std::int32_t> rep(classes, -1);
      for (std::size_t i = 0; i < n; ++i) {
        if (rep[cls[i]] < 0) {
          rep[cls[i]] = static_cast<std::int32_t>(i);
        }
      }
      std::vector<std::uint32_t> class_id(classes, no_node);
      class_id[cls[0]] = 0;
      std::uint32_t assigned {1};
      for (std::uint32_t c = 0; c < classes; ++c) {
        if (class_id[c] == no_node) {
          class_id[c] = assigned++;
        }
      }
      std::vector<onepass_node> merged(classes);
      for (std::uint32_t c = 0; c < classes; ++c) {
        const onepass_node& r  {nodes_[static_cast<std::size_t>(rep[c])]};
        onepass_node&       nn {merged[class_id[c]]};
        nn.matches           = r.matches;
        nn.match_cap_mask    = r.match_cap_mask;
        nn.match_assert_mask = r.match_assert_mask;
        nn.edge.assign(alpha_.count, onepass_edge {});
        for (std::uint16_t x = 0; x < alpha_.count; ++x) {
          if (r.edge[x].assigned) {
            nn.edge[x] = onepass_edge {.next        = class_id[cls[r.edge[x].next]],
                                       .cap_mask    = r.edge[x].cap_mask,
                                       .assert_mask = r.edge[x].assert_mask,
                                       .assigned    = true};
          }
        }
      }
      nodes_ = std::move(merged);
    }

    //! \brief FNV-1a hash of a partition signature (for the constexpr-friendly bucket dedup). Accumulates in
    //!        a fixed 64-bit width and truncates only at the return, so a 32-bit `size_t` (Win32) never sees
    //!        a narrowing brace-init of the 64-bit offset basis.
    static constexpr std::size_t sig_hash(const std::vector<std::uint64_t>& v)
    {
      std::uint64_t h {1469598103934665603ULL};
      for (const std::uint64_t x : v) {
        h = (h ^ x) * 1099511628211ULL;
      }
      return static_cast<std::size_t>(h);
    }

    //! \brief Walk the epsilon-closure from \p pc, writing this node's edges. \p on_path detects epsilon
    //!        cycles (a nullable loop => not one-pass). \p cap_mask accumulates the slots crossed so far.
    constexpr void build_edges(std::int32_t               pc,
                               std::uint64_t              cap_mask,
                               std::uint32_t              assert_mask,
                               std::vector<char>&         on_path,
                               std::uint32_t              node_id,
                               std::vector<std::int32_t>& queue)
    {
      if (!eligible_) {
        return;
      }
      if (on_path[static_cast<std::size_t>(pc)] != 0) {
        bail("epsilon cycle (a nullable loop): not one-pass", -1, -1, pc);
        return;
      }
      on_path[static_cast<std::size_t>(pc)] = 1;
      const instr& in {code_[static_cast<std::size_t>(pc)]};
      switch (in.op) {
        case opcode::byte:
        case opcode::klass: {
            const std::uint32_t next {node_of(pc + 1, queue)};
            onepass_node&       node {nodes_[node_id]};
            // Only the byte-classes this instruction actually consumes (one for `byte`, a precomputed few
            // for `klass`) — never a scan over the whole alphabet, which made the build O(nodes x classes)
            // and dominated a Unicode \w find_iter.
            // Invariant: every node is sized to alpha_.count in node_of; cls is always
            // alpha_.of[byte] or a cover entry, so cls < edge.size(). The assert documents
            // that precondition. clang-analyzer still false-positives NullDereference on
            // edge[cls] (cannot see the sizing across the node_of indirection) — NOLINT is
            // the residual, not a naked suppress of a real bug.
            const auto write_edge {[&](std::uint16_t cls) {
                                     assert(static_cast<std::size_t>(cls) < node.edge.size());
                                     // NOLINTBEGIN(clang-analyzer-core.NullDereference)
                                     onepass_edge& slot {node.edge[cls]};
                                     if (slot.assigned
                                         && (slot.next != next || slot.cap_mask != cap_mask
                                             || slot.assert_mask != assert_mask)) {
                                       bail("byte-class conflict: not one-pass", static_cast<std::int32_t>(node_id),
                                            cls, pc);
                                       return false;
                                     }
                                     slot = onepass_edge {.next        = next,
                                                          .cap_mask    = cap_mask,
                                                          .assert_mask = assert_mask,
                                                          .assigned    = true};
                                     // NOLINTEND(clang-analyzer-core.NullDereference)
                                     return true;
                                   }};
            if (in.op == opcode::byte) {
              if (!write_edge(alpha_.of[static_cast<std::uint8_t>(in.arg8)])) {
                return;
              }
            }
            else {
              for (const std::uint16_t cls : class_cover_[in.arg16]) {
                if (!write_edge(cls)) {
                  return;
                }
              }
            }
            break; // a consuming instruction ends this epsilon path
          }
        case opcode::match: {
            onepass_node& node {nodes_[node_id]};
            if (node.matches && (node.match_cap_mask != cap_mask || node.match_assert_mask != assert_mask)) {
              bail("second distinct match: not one-pass", static_cast<std::int32_t>(node_id));
              return;
            }
            node.matches           = true;
            node.match_cap_mask    = cap_mask;
            node.match_assert_mask = assert_mask;
            break;
          }
        case opcode::split:
          build_edges(in.primary_target, cap_mask, assert_mask, on_path, node_id, queue);
          build_edges(in.secondary_target, cap_mask, assert_mask, on_path, node_id, queue);
          break;
        case opcode::jump:
          build_edges(in.primary_target, cap_mask, assert_mask, on_path, node_id, queue);
          break;
        case opcode::save:
          build_edges(pc + 1, cap_mask | (std::uint64_t {1} << in.arg16), assert_mask, on_path, node_id, queue);
          break;
        case opcode::assert_position:
          // Tier-B: the assertion becomes a condition on whatever edge (or match) this epsilon path reaches.
          // The build_byte_program Tier-B pass already rejected a word-ness-flipped assert, so arg16 is 0.
          build_edges(pc + 1, cap_mask, assert_mask | (std::uint32_t {1} << in.arg8), on_path, node_id, queue);
          break;
        default:
          // Structurally unreachable via the current construction, not tested directly: the constructor's
          // own `!bp.eligible` bail (above) already declines before build_edges ever runs whenever a
          // lookaround or a word-ness-flipped assertion is present, and build_byte_program converts every
          // klass_cp into byte/klass chains (never leaves one in an eligible program) -- so no eligible
          // byte-program can reach this case through any public construction path. Kept as a defensive
          // guard against a FUTURE opcode added to the switch's domain without a corresponding case here,
          // not a live decline path -- hand-crafting a byte_program with an illegal op just to hit it would
          // test the guard's plumbing, not anything the engine can actually produce.
          bail("unexpected op in byte-program (lookaround/klass_cp should be absent)");
          return;
      }
      on_path[static_cast<std::size_t>(pc)] = 0; // backtrack: only a cycle bails, a diamond is fine
    }

    std::span<const instr>                  code_;
    std::span<const char_class>             classes_;
    lazy_byte_alphabet                      alpha_;
    std::vector<std::vector<std::uint16_t>> class_cover_; //!< char-class index -> the byte-classes it consumes.
    std::vector<std::uint32_t>              pc_to_node_;  //!< pc -> node id (or no_node).
    std::vector<onepass_node>               nodes_;
    std::size_t                             slot_count_ {0};
    std::size_t                             max_bytes_  {max_table_bytes};
    std::size_t                             node_cap_   {max_nodes};
    bool                                    ascii_word_ {true};
    std::int32_t                            bail_node_  {-1};
    std::int32_t                            bail_class_ {-1};
    std::int32_t                            bail_pc_    {-1};
    bool                                    eligible_   {true};
    std::string                             bail_reason_;
  };

  //! \brief The per-regex immutable cache the router shares across every find_iter on a regex: the byte-
  //!        program (klass_cp expanded to the deterministic trie) and, when the pattern is one-pass, the
  //!        extractor table. Built exactly once, under \ref once, so a const regex used from many threads
  //!        (the binding shares the compiled object across GIL-released calls) builds it race-free. The
  //!        mutable DFA transition caches stay per-iterator — they warm per scan and would need a lock here.
  struct regex_immutables
  {
    byte_program           byte_prog;          //!< klass_cp-expanded byte program (empty until built).
    lazy_byte_alphabet     alphabet;           //!< byte-class alphabet of byte_prog (shared by both DFAs, else recomputed per scan).
    std::optional<onepass> op_table;           //!< one-pass extractor, present iff the pattern is one-pass.
    byte_program           il_prefix_prog;     //!< IL: the inner-literal prefix's byte program (ineligible until built). Per-regex so the reverse DFA that spans it is a cheap per-iterator wrapper, not a per-find_iter rebuild.
    std::size_t            il_min_haystack {}; //!< IL: on a haystack that HAS a candidate, the route only fires at or above this size (0 = always). The prefix reverse DFA's cache is per-iterator and re-warms per find_iter; below N that cost does not amortize and the core is faster. Checked ONLY after the first memmem hit, so a no-match haystack (memmem-only) is never gated. Scaled by the prefix byte-program size (its cache size); see \ref pike_vm::run_inner_literal.
    std::once_flag         once;               //!< guards the one-time build.

    // A copied regex is an independent regex: it gets its own fresh, unbuilt cache rather than sharing
    // (std::once_flag is not copyable anyway). Copy/move reset rather than transfer — the built state is a
    // pure runtime accelerator, cheap to rebuild.
    regex_immutables() = default;
    regex_immutables(const regex_immutables& /*other*/) noexcept
    {}

    regex_immutables(regex_immutables&& /*other*/) noexcept
    {}

    // Assignment keeps this object's own (fresh) cache — it deliberately copies/moves nothing, which is
    // inherently self-assignment-safe (there is no state to guard). NOLINT: the empty body is the contract.
    // NOLINTNEXTLINE(cert-oop54-cpp)
    regex_immutables& operator=(const regex_immutables& /*other*/) noexcept
    {
      return *this;
    }

    // NOLINTNEXTLINE(cert-oop54-cpp)
    regex_immutables& operator=(regex_immutables&& /*other*/) noexcept
    {
      return *this;
    }

    ~regex_immutables() = default;
  };
} // namespace real::detail

#endif // REAL_ONEPASS_HPP
