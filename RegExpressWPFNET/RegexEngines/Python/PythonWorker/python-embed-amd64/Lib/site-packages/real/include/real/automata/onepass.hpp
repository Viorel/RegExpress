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
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>).

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
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
    //! Moore-refinement work cap (rounds x nodes x per-node signature width). A repeated large class (e.g.
    //! `\w{k}`, whose Unicode trie floods thousands of nodes per copy) forms a *chain* of that many node
    //! groups, and Moore refinement needs one round per link of the chain to propagate a distinguishing byte
    //! all the way back to the start -- rounds ~ O(k), each O(nodes x alphabet), so total work is quadratic
    //! in k and unbounded as k grows (confirmed: `\w{30}` alone took ~2.1s on arm64/-O2 before this cap).
    //! Calibrated against the full test suite's own one-pass patterns, whose peak observed work is ~5.9M
    //! (5 rounds, 3762 nodes) -- this cap gives that a >16x margin while bounding worst-case wall time to a
    //! few hundred ms instead of seconds-to-unbounded. Larger declines to the VM, same as the other caps here.
    static constexpr std::uint64_t max_minimize_work {100'000'000ULL};

    //! \param[in] bp        The byte-program to classify.
    //! \param[in] max_bytes Table-memory cap; larger tables decline. Defaults to \ref max_table_bytes; a
    //!                      smaller value is a test hook to exercise the cap without a huge pattern.
    //! \param[in] node_cap  Node-count cap (see \ref max_nodes). Defaults to \ref max_nodes; a smaller
    //!                      value is a test hook to exercise the cap without a 65000-node pattern.
    //! \param[in] work_cap  Moore-refinement work cap (see \ref max_minimize_work). Defaults to \ref
    //!                      max_minimize_work; a smaller value is a test hook to exercise the cap without a
    //!                      pattern that takes hundreds of milliseconds to build.
    explicit constexpr onepass(const byte_program&  bp,
                               std::size_t          max_bytes = max_table_bytes,
                               std::size_t          node_cap  = max_nodes,
                               std::uint64_t        work_cap  = max_minimize_work)
      : max_bytes_ {max_bytes}, node_cap_ {node_cap}, work_cap_ {work_cap},
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
      // Allocated once, not once per queued node: build_edges's own DFS backtrack (its unconditional trailing
      // `on_path[pc] = 0`) restores every entry it touched to 0 before returning here, on every path that
      // keeps `eligible_` true -- the only paths that skip that clear are the ones that also bail() (turn
      // eligible_ false), which stops this loop on its next condition check regardless. So the array is
      // already all-zero at the top of each iteration; re-zeroing bp.code.size() elements per node (up to
      // node_cap_ times) was O(node_cap x code size) for no reason -- multiple seconds on a large repeated
      // class (e.g. `\w{100}`) before it ever reached minimize().
      std::vector<char> on_path(bp.code.size(), 0);
      while (!queue.empty() && eligible_) {
        const std::int32_t pc {queue.back()};
        queue.pop_back();
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
      if (!eligible_) {
        return; // minimize() itself declined (its own work cap): nodes_ is an unfinished flood, not a table.
      }
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
      // Per-round cost is n signatures of width (4 + 3 x alpha_.count) each -- fixed for the whole call, since
      // only `cls` (not n or the alphabet) changes between rounds. A chain-shaped automaton (a repeated large
      // class, e.g. `\w{k}`) needs ~k rounds to converge, so bound *cumulative* work rather than round count:
      // that lets small automata refine as many rounds as they need while still catching a large chain early.
      const std::uint64_t sig_width  {4U + (3U * static_cast<std::uint64_t>(alpha_.count))};
      const std::uint64_t round_work {static_cast<std::uint64_t>(n) * sig_width};
      std::uint64_t       work_done  {0};
      while (true) {
        if (work_done + round_work > work_cap_) {
          bail("one-pass minimization exceeded its work budget: not one-pass", static_cast<std::int32_t>(n));
          return;
        }
        work_done += round_work;
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
    std::uint64_t                           work_cap_   {max_minimize_work};
    bool                                    ascii_word_ {true};
    std::int32_t                            bail_node_  {-1};
    std::int32_t                            bail_class_ {-1};
    std::int32_t                            bail_pc_    {-1};
    bool                                    eligible_   {true};
    std::string                             bail_reason_;
  };

  //! \brief The per-regex immutable cache the router shares across every find_iter on a regex: the byte-
  //!        program (klass_cp expanded to the deterministic trie) and, when the pattern is one-pass, the
  //!        extractor table. Built under program-identity invalidation (\ref built_for), so a const regex
  //!        used from many threads builds race-free, and assign-onto-warmed rebuilds for the new program.
  //!
  struct regex_immutables; // forward — \ref erase_shared_dfas is called from the dtor
  inline void erase_shared_dfas(const regex_immutables* immut);

  //!        The mutable lazy-DFA transition caches live in a process-wide side table
  //!        (\ref shared_dfa_slot), keyed by this object's address and guarded by a per-slot mutex —
  //!        so this struct stays free of \c std::mutex / extra members (layout: \c once_flag →
  //!        \c atomic pointer, same footprint). Address-reuse of the immutables object invalidates
  //!        the slot via \ref reset_shared_dfas; program change at the same address is detected by
  //!        \ref built_for (see \ref pike_vm::ensure_immutables). The dtor erases this address's map
  //!        entry so match-time caches do not outlive the regex.
  struct regex_immutables
  {
    byte_program           byte_prog;          //!< klass_cp-expanded byte program (empty until built).
    lazy_byte_alphabet     alphabet;           //!< byte-class alphabet of byte_prog (shared by both DFAs, else recomputed per scan).
    std::optional<onepass> op_table;           //!< one-pass extractor, present iff the pattern is one-pass.
    byte_program           il_prefix_prog;     //!< IL: the inner-literal prefix's byte program (ineligible until built). Per-regex so the reverse DFA that spans it is a cheap shared wrapper, not a per-find_iter rebuild.
    std::size_t            il_min_haystack {}; //!< IL cold floor: first candidate-scan on this regex only fires at or above this size when the haystack HAS a match (0 = always). Warm scans use \ref il_warm_floor (shared reverse DFA in \ref shared_dfa_slot). Checked ONLY after the first memmem hit — no-match is never gated. Scaled by prefix byte-program size; see \ref pike_vm::run_inner_literal.
    //! \brief \c prog.code.data() this cache was built for, or null if never built / invalidated.
    //!        Hot path: one atomic load. Not \c once_flag — assignment reuses this object under a new
    //!        program; a spent once_flag would never rebuild (silent wrong matches).
    std::atomic<const void*> built_for {nullptr};

    // A copied regex is an independent regex: fresh unbuilt cache (built_for null). Copy/move of the
    // cache body is never transferred — pure runtime accelerator, cheap to rebuild.
    regex_immutables() = default;
    regex_immutables(const regex_immutables& /*other*/) noexcept
      : built_for {nullptr}
    {}

    regex_immutables(regex_immutables&& /*other*/) noexcept
      : built_for {nullptr}
    {}

    // Assignment keeps this object's cache storage but marks it invalid — the destination's program
    // is already the new one by the time storage assignment reaches here; we cannot rebuild from
    // the source's program (and must not inherit its built state). built_for=null forces
    // ensure_immutables to rebuild. Self-assignment-safe. NOLINT: deliberate non-copy of members.
    // NOLINTNEXTLINE(cert-oop54-cpp)
    regex_immutables& operator=(const regex_immutables& /*other*/) noexcept
    {
      built_for.store(nullptr, std::memory_order_relaxed);
      return *this;
    }

    // NOLINTNEXTLINE(cert-oop54-cpp)
    regex_immutables& operator=(regex_immutables&& /*other*/) noexcept
    {
      built_for.store(nullptr, std::memory_order_relaxed);
      return *this;
    }

    // Runtime erase of this regex's shared DFA slot (reclaims match-time caches). Constexpr paths
    // skip the map entirely — \c is_constant_evaluated so dynamic_storage::compile / static_assert stay valid.
    constexpr ~regex_immutables()
    {
      if (!std::is_constant_evaluated()) {
        erase_shared_dfas(this);
      }
    }
  };

  //! \brief Striped rebuild lock for \ref pike_vm::ensure_immutables (not on \ref regex_immutables —
  //!        layout isolation). Distinct from \ref shared_dfa_map_mu / slot.mu so \ref reset_shared_dfas
  //!        cannot self-deadlock. Different immutables rarely share a stripe.
  [[nodiscard]] inline std::mutex& immut_build_mu(const regex_immutables* immut)
  {
    static std::array<std::mutex, 64> stripes {};
    const std::size_t                 h       {std::hash<const void*> {}(immut)};
    return stripes[h % stripes.size()];
  }

  //! \brief Process-wide shared DFA transition caches keyed by \ref regex_immutables*.
  //!        Thread-safe: map insert/erase under \ref shared_dfa_map_mu, DFA warm/scan under \ref mu.
  //!        Slots are \c shared_ptr so a concurrent \ref erase_shared_dfas (dtor) cannot free a slot a
  //!        thread is still scanning — the slot dies when the last holder (map or TLS cache) releases.
  struct shared_dfa_slot
  {
    std::mutex                 mu;
    std::optional<lazy_dfa>    fwd;
    std::optional<reverse_dfa> rev;
    std::optional<reverse_dfa> il_prefix_rev;
    //! \brief True after this regex has been IL-candidate-scanned at least once (any size).
    //!        Cold first scan keeps the high \ref regex_immutables::il_min_haystack floor; warm
    //!        scans use \ref il_warm_floor. Not "il_prefix_rev is built" — a always-<floor corpus
    //!        would never build the reverse DFA and would never drop the floor (audit 3.1).
    std::atomic<bool>          il_warmed {false};

    //! \brief The regex this slot belongs to, or null once \ref erase_shared_dfas has retired it.
    //!
    //! This is what validates a thread's last-hit cache in \ref shared_dfa_for — a cached slot is still
    //! this regex's slot exactly while `owner == immut`. The predecessor was a single process-wide
    //! epoch counter bumped on every erase, which meant ANY regex's destruction invalidated EVERY
    //! thread's cache and sent them all back to \ref shared_dfa_map_mu — the global mutex the cache
    //! exists to avoid, once per inner-literal candidate. Ownership is per slot, so one regex dying
    //! now costs only the threads that were actually using it.
    std::atomic<const regex_immutables*> owner {nullptr};
  };

  //! \brief Warm-regime IL min haystack (bytes). Shared reverse DFA amortizes after scan 1; below this
  //!        even warm IL may not win (measured ~4 KB email dense collapse, 2026-07). Cold first scan
  //!        still uses \ref regex_immutables::il_min_haystack (~94 KB email).
  inline constexpr std::size_t il_warm_floor {4UL * 1024};

  inline std::mutex& shared_dfa_map_mu()
  {
    static std::mutex m;
    return m;
  }

  //! \brief Bumped on every \ref erase_shared_dfas so a TLS last-hit cache never serves a destroyed
  //!        regex's slot to a new immutables that reuses the same address.
  //! \brief Process-wide map. Intentionally never destroyed (leaky singleton): a static map would
  //!        tear down at exit while other statics' \c ~regex_immutables still call \ref erase_shared_dfas.
  //!        The OS reclaims the map at process exit — not an accumulating leak; entries are erased on dtor.
  inline std::unordered_map<const regex_immutables*, std::shared_ptr<shared_dfa_slot>>& shared_dfa_map()
  {
    static auto* m {
      new std::unordered_map<const regex_immutables*, std::shared_ptr<shared_dfa_slot>>};
    return *m;
  }

  //! \brief Retire this regex's slot (called from \c ~regex_immutables). Concurrent scans that still hold
  //!        a \c shared_ptr via TLS keep the slot object alive until they release; clearing
  //!        \ref shared_dfa_slot::owner is what makes their cached copy stop matching, so a new regex
  //!        landing on this address can never be served the retired slot.
  //!
  //! The owner store is release and \ref shared_dfa_for's check is acquire. That pairing is what a new
  //! regex at a REUSED address relies on: the allocator handing the address out again orders this
  //! erase before that construction, and a thread can only reach the new regex through some
  //! synchronization with its constructor, so the null is visible by the time it asks.
  //! The map entry drops under the lock; the last \c shared_ptr reference is released outside it.
  inline void erase_shared_dfas(const regex_immutables* immut)
  {
    std::shared_ptr<shared_dfa_slot> retired;
    {
      const std::lock_guard<std::mutex> lock  {shared_dfa_map_mu()};
      const auto                        found {shared_dfa_map().find(immut)};
      if (found == shared_dfa_map().end()) {
        return; // this regex never took a DFA route — nothing was ever inserted
      }
      retired = std::move(found->second);
      shared_dfa_map().erase(found);
    }
    retired->owner.store(nullptr, std::memory_order_release);
  }

  //! \brief Resolve the process-wide DFA slot for this regex (map insert under \ref shared_dfa_map_mu).
  //!        Thread-local last-hit cache: dense IL was paying map_mu per candidate without it; the cache
  //!        is not on \ref regex_immutables (layout isolation — x86 class-loop +6% suspect). Holds a
  //!        \c shared_ptr (not a raw pointer) so erase cannot UAF a live scan, and validates it against
  //!        the slot's own \ref shared_dfa_slot::owner — so an unrelated regex's destruction no longer
  //!        invalidates this thread's cache (the process-wide epoch counter it replaced did).
  [[nodiscard]] inline shared_dfa_slot& shared_dfa_for(regex_immutables* immut)
  {
    thread_local std::shared_ptr<shared_dfa_slot> cached_slot {};
    // One acquire load on a line this thread already owns. `owner == immut` subsumes the old separate
    // identity check: a retired slot's owner is null, and a slot only ever names one regex.
    if (cached_slot && cached_slot->owner.load(std::memory_order_acquire) == immut) {
      return *cached_slot;
    }
    const std::lock_guard<std::mutex> lock {shared_dfa_map_mu()};
    std::shared_ptr<shared_dfa_slot>& slot {shared_dfa_map()[immut]};
    if (!slot) {
      slot = std::make_shared<shared_dfa_slot>();
      slot->owner.store(immut, std::memory_order_relaxed); // published by this mutex's release
    }
    cached_slot = slot; // shared_ptr copy — free-safe if erase races
    return *slot;
  }

  //! \brief Drop any DFAs cached for \p immut (caller holds nothing; takes map + slot locks).
  //!        Invoked from \ref pike_vm::ensure_immutables rebuild so a reused immutables address — or
  //!        the same address under a new program — cannot keep a previous pattern's DFAs.
  inline void reset_shared_dfas(regex_immutables* immut)
  {
    shared_dfa_slot&                  slot {shared_dfa_for(immut)};
    const std::lock_guard<std::mutex> lock {slot.mu};
    slot.fwd.reset();
    slot.rev.reset();
    slot.il_prefix_rev.reset();
    slot.il_warmed.store(false, std::memory_order_relaxed);
  }

  //! \brief Test/audit: number of live shared-DFA map entries (process-wide). Not for production.
  [[nodiscard]] inline std::size_t shared_dfa_map_size_for_test()
  {
    const std::lock_guard<std::mutex> lock {shared_dfa_map_mu()};
    return shared_dfa_map().size();
  }
} // namespace real::detail

#endif // REAL_ONEPASS_HPP
