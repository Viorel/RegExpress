/*!
 * \file onepass.hpp
 * \brief The one-pass builder: decides whether a pattern is *one-pass* and, if so, tabulates a deterministic
 *        capture-writing automaton over the byte-program.
 *
 * A pattern is **one-pass** (Brüggemann-Klein & Wood, "One-unambiguous regular languages"; RE2 `onepass.cc`)
 * when, matched anchored, at most one thread crosses any byte. Its capture slots then fill in one left-to-right
 * pass with no thread lists: at each node the byte read selects exactly one edge, whose conditions say which
 * slots take the current position. `(\w+)@(\w+)` is one-pass; `(\w+)_(\w+)` is not (`_` both extends group 1
 * and starts the separator).
 *
 * `pike_vm` walks the table through \ref real::detail::onepass::extract (the `onepass_full` and
 * `onepass_window` routes). The build runs over the byte program (Unicode `\w \d \s` already expanded to byte
 * ranges), so the one-pass check is byte-level.
 */
#ifndef REAL_ONEPASS_HPP
#define REAL_ONEPASS_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
#include <unordered_map> // REAL_ALLOW_STD_HASH -- see shared_dfa_map
#include <optional>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "real/engine/aho_corasick.hpp"
#include "real/engine/prefilter.hpp"
#include "real/engine/assert_eval.hpp"
#include "real/automata/lazy_dfa.hpp"
#include "real/core/program.hpp"

namespace real::detail {

  /*!
   * \brief One outgoing edge of a one-pass node, for a byte-class: the next node and the capture slots that
   *        take the current position as the byte is consumed. Two epsilon paths reaching the same class with
   *        a different edge is the one-pass conflict — the pattern is then rejected.
   */
  struct onepass_edge
  {
    std::uint32_t next        {0};     //!< Next node id (valid only when \ref assigned).
    std::uint64_t cap_mask    {0};     //!< Bit i set => write the current position into slot i on this edge.
    std::uint32_t assert_mask {0};     //!< Bit k (an \ref assert_kind) set => that assertion must hold to take this edge (Tier-B).
    bool          assigned    {false}; //!< Whether this byte-class has an edge from this node.
  };

  /*!
   * \brief One edge of the flattened table \ref onepass::extract walks: \ref onepass_edge with the target
   *        given as the offset of its row, so a step is one load from one array.
   */
  struct onepass_step
  {
    std::uint32_t row         {0}; //!< Offset of the target node's row in the flat table; \ref onepass::no_row when unassigned.
    std::uint32_t target      {0}; //!< The target node, with its \ref onepass::accept_rank in the top byte.
    std::uint16_t cap_mask    {0}; //!< As \ref onepass_edge::cap_mask (\ref onepass::max_slots bits at most).
    std::uint16_t assert_mask {0}; //!< As \ref onepass_edge::assert_mask (one bit per \ref assert_kind).
  };

  /*!
   * \brief A one-pass node: one edge per byte-class, plus whether the run may end here and with what
   *        captures. Nodes are the points the automaton can be in *between* byte reads.
   */
  struct onepass_node
  {
    std::vector<onepass_edge> edge;                      //!< Indexed by byte-class; emptied into \ref onepass_step rows once built.
    bool                      matches           {false}; //!< Reaching `match` from here (via epsilon).
    std::uint64_t             match_cap_mask    {0};     //!< Slots written when the match is taken.
    std::uint32_t             match_assert_mask {0};     //!< Assertions that must hold at the end for the match (Tier-B).
    bool                      edge_before_match {false}; //!< An edge was reached before the match in priority order.
    bool                      edge_after_match  {false}; //!< An edge was reached after it: the match outranks it.
  };

  /*!
   * \brief Builds and holds the one-pass classification (and table, when eligible) of a byte-program.
   *
   * Construction floods the program from the start: each node walks its epsilon-closure (`split`, `jump`,
   * `save`, assertions) accumulating masks, and each consuming instruction (`byte`, `klass`) writes the edge
   * for its byte-classes. A byte-class written twice with different edges, a second reachable `match` with
   * different captures, or an epsilon cycle (a nullable loop) means *not one-pass*, and the build bails with
   * a reason. Node, slot, memory and refinement-work caps bound a pathological program.
   */
  class onepass
  {
  public:

    static constexpr std::uint32_t no_node          {0xFFFFFFFFU};  //!< "No node yet" sentinel in the pc->node map.
    static constexpr std::uint32_t no_row           {0xFFFFFFFFU};  //!< \ref onepass_step::row of an unassigned edge.
    static constexpr std::uint8_t  rank_none        {0};            //!< \ref accept_rank of a node that does not accept.
    static constexpr std::uint8_t  rank_continue    {1};            //!< Accepts, and every edge outranks the match.
    static constexpr std::uint8_t  rank_match       {2};            //!< Accepts, and the match outranks every edge.
    static constexpr std::uint8_t  rank_mixed       {3};            //!< Accepts, with edges on both sides of the match.
    static constexpr std::size_t   max_nodes        {65000};        //!< Node cap, a memory/DoS bound.
    static constexpr std::size_t   max_slots        {10};           //!< Slot-pointer cap: group 0 + four user groups.
    static constexpr std::size_t   minimize_buckets {4096};         //!< Moore-refinement dedup buckets; sizing only (a collision costs a comparison).
    static constexpr std::size_t   max_table_bytes  {8U << 20};     //!< Table-memory cap (~8 MB): larger declines to the VM.
    //! Moore-refinement work cap (rounds x nodes x signature width); larger declines to the VM. A repeated
    //! large class (`\w{k}`) forms a chain needing ~k rounds of O(nodes x alphabet) each, quadratic in k:
    //! capping CUMULATIVE work, not rounds, lets a small automaton refine fully while a long chain declines early.
    static constexpr std::uint64_t max_minimize_work {100'000'000ULL};

    // onepass_step packs what its fields hold into these widths.
    static_assert(max_slots <= 16U, "a step's cap_mask holds one bit per slot in 16");
    static_assert(static_cast<unsigned>(assert_kind::line_end_cr) < 16U, "a step's assert_mask holds one bit per kind in 16");
    static_assert(max_nodes < (std::size_t {1} << 24U), "a step's target holds a node id below its rank byte");

    /*!
     * \brief Classifies \p bp and, when it is one-pass, builds the table. A smaller cap is a test hook.
     * \param[in] bp        The byte-program to classify.
     * \param[in] max_bytes Table-memory cap (\ref max_table_bytes).
     * \param[in] node_cap  Node-count cap (\ref max_nodes).
     * \param[in] work_cap  Moore-refinement work cap (\ref max_minimize_work).
     */
    explicit constexpr onepass(const byte_program&  bp,
                               std::size_t          max_bytes = max_table_bytes,
                               std::size_t          node_cap  = max_nodes,
                               std::uint64_t        work_cap  = max_minimize_work)
      : max_bytes_ {max_bytes}, node_cap_ {node_cap}, work_cap_ {work_cap},
        ascii_word_ {!bp.unicode_word}
    {
      if (!bp.eligible) {
        bail("the byte-program is itself ineligible (a lookaround, or a word-ness-flipped assertion)");
        return;
      }
      build(bp);
      // code_ and classes_ borrow bp, which pike_vm::ensure_op_table's Tier-B branch passes as a
      // block-local: empty them so a read after construction sees an empty span, never a dangling one.
      code_    = {};
      classes_ = {};
    }

    /*!
     * \brief Whether the table was built and may be used.
     * \return `false` when the pattern is not one-pass or a cap was exceeded; \ref bail_reason then says why.
     */
    [[nodiscard]] bool eligible() const
    {
      return eligible_;
    }

    /*!
     * \brief Why the build declined, when it did.
     * \return The reason \ref bail recorded, or an empty string if \ref eligible is `true`.
     */
    [[nodiscard]] const std::string& bail_reason() const
    {
      return bail_reason_;
    }

    /*!
     * \brief Size of the built table.
     * \return Node count after minimization, or 0 if the build declined.
     */
    [[nodiscard]] std::size_t node_count() const
    {
      return nodes_.size();
    }

    /*!
     * \brief Width of each node's edge row.
     * \return The number of byte-equivalence classes the alphabet collapsed to.
     */
    [[nodiscard]] std::uint16_t num_classes() const
    {
      return alpha_.count;
    }

    /*!
     * \brief The nodes, for tests that pin the table's shape. A built table keeps its edges in \ref steps_
     *        alone, so each node's \ref onepass_node::edge row is empty once the build is over.
     * \return The nodes, indexed by node id; node 0 is the start.
     */
    [[nodiscard]] const std::vector<onepass_node>& nodes() const
    {
      return nodes_;
    }

    /*!
     * \brief The byte-class of \p byte, for a runtime that walks this table.
     * \param[in] byte The subject byte to classify.
     * \return Its index into a node's edge row, below \ref num_classes.
     */
    [[nodiscard]] std::uint8_t class_of(std::uint8_t byte) const
    {
      return alpha_.of[byte];
    }

    /*!
     * \brief The number of capture slots (group 0 start/end plus each group's).
     * \return Twice the group count plus two.
     */
    [[nodiscard]] std::size_t slot_count() const
    {
      return slot_count_;
    }

    /*!
     * \brief Fills \p out with the capture slots of the one-pass match on `text[s, e)`: fullmatch on the span
     *        the router located, anchored at \p s and accepting exactly at \p e. A slot no edge wrote is
     *        \ref real::npos.
     *
     * \param[in]  text The full subject, never a substring: assertions read `s - 1` and `e`.
     * \param[in]  s    Match start (anchor).
     * \param[in]  e    Match end (the run must accept here).
     * \param[out] out  Capture slots, sized to \ref slot_count.
     * \return `true` on a successful extraction; `false` (ineligible table, or the span does not match)
     *         leaves \p out unspecified.
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
      const onepass_step* const flat {steps_.data()};
      std::uint32_t             row  {0}; // node 0 is the start (the closure of pc 0)
      for (std::size_t pos = s; pos < e; ++pos) {
        const onepass_step& step {flat[row + alpha_.of[static_cast<std::uint8_t>(text[pos])]]};
        if (step.row == no_row) {
          return false;                                             // no edge for this byte
        }
        if (step.assert_mask != 0 && !asserts_hold(step.assert_mask, text, pos)) {
          return false;                                             // an edge assertion fails here (Tier-B)
        }
        for (std::uint64_t m = step.cap_mask; m != 0; m &= m - 1) {
          out[static_cast<std::size_t>(std::countr_zero(m))] = pos; // saves crossed before this byte take pos
        }
        row = step.row;
      }
      const std::uint32_t node {row / alpha_.count};
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

    /*!
     * \brief How a node accepts, from the priority order its closure was walked in.
     * \param[in] node The node.
     * \return \ref rank_none, \ref rank_continue, \ref rank_match or \ref rank_mixed. A node that accepts with no
     *         edge has nothing to continue with, so it ranks as \ref rank_match does.
     */
    [[nodiscard]] static constexpr std::uint8_t accept_rank(const onepass_node& node) noexcept
    {
      if (!node.matches) {
        return rank_none;
      }
      if (node.edge_before_match && node.edge_after_match) {
        return rank_mixed;
      }
      return node.edge_before_match ? rank_continue : rank_match;
    }

    /*!
     * \brief Whether \ref extract_leftmost applies: no node accepts with edges on both sides of its match.
     * \return False when the table is ineligible or some node is \ref rank_mixed.
     */
    [[nodiscard]] bool ends_known() const
    {
      return eligible_ && ends_known_;
    }

    /*!
     * \brief The leftmost-first match anchored at \p s, found and captured in one pass: no end needs to be known
     *        beforehand, unlike \ref extract.
     *
     * Each accepting node met is a candidate end. Where its edges outrank the match (\ref rank_continue) the
     * walk goes on and a later end replaces it, as backtracking would; where the match outranks them
     * (\ref rank_match) the walk stops. The groups are those of the last end kept: a slot written past it is
     * discarded.
     *
     * \pre \ref ends_known().
     * \param[in]  text  The full subject (assertions read around a position).
     * \param[in]  s     Match start.
     * \param[out] out   Capture slots, sized to \ref slot_count, filled on a match.
     * \param[out] reach How far the walk read, the match's end on a match.
     * \return The match's end, or \ref real::npos when none begins at \p s.
     */
    template <typename OutSlots>
    [[nodiscard]] std::size_t extract_leftmost(std::string_view text,
                                               std::size_t      s,
                                               OutSlots&        out,
                                               std::size_t&     reach) const
    {
      std::array<std::size_t, max_slots> cur  {};
      std::array<std::size_t, max_slots> kept {};
      cur.fill(npos);
      kept.fill(npos);
      std::uint32_t       stale  {0}; // slots written since the last end kept, or set by its match
      std::size_t         end    {npos};
      std::uint32_t       row    {0};
      std::uint32_t       target {start_};
      std::size_t         pos    {s};
      const onepass_step* flat   {steps_.data()};
      for (;;) {
        if (const std::uint32_t rank {target >> 24U}; rank != rank_none) {
          const onepass_node& node {nodes_[target & 0xFFFFFFU]};
          if (node.match_assert_mask == 0 || asserts_hold(node.match_assert_mask, text, pos)) {
            for (std::uint32_t m {stale}; m != 0; m &= m - 1) {
              kept[static_cast<std::size_t>(std::countr_zero(m))] = cur[static_cast<std::size_t>(std::countr_zero(m))];
            }
            stale = static_cast<std::uint32_t>(node.match_cap_mask);
            for (std::uint32_t m {stale}; m != 0; m &= m - 1) {
              kept[static_cast<std::size_t>(std::countr_zero(m))] = pos;
            }
            end = pos;
            if (rank == rank_match) {
              break;
            }
          }
        }
        if (pos == text.size()) {
          break;
        }
        const onepass_step& step {flat[row + alpha_.of[static_cast<std::uint8_t>(text[pos])]]};
        if (step.row == no_row || (step.assert_mask != 0 && !asserts_hold(step.assert_mask, text, pos))) {
          break;
        }
        for (std::uint32_t m {step.cap_mask}; m != 0; m &= m - 1) {
          cur[static_cast<std::size_t>(std::countr_zero(m))] = pos;
        }
        stale  |= step.cap_mask;
        row     = step.row;
        target  = step.target;
        ++pos;
      }
      reach = pos;
      if (end != npos) {
        out.assign(slot_count_, npos);
        for (std::size_t i {0}; i < slot_count_; ++i) {
          out[i] = kept[i];
        }
      }
      return end;
    }

    /*!
     * \brief Whether every assertion in \p mask (a set of \ref assert_kind bits) holds at \p pos in \p text.
     * \param[in] mask The assertions an edge carries; an empty mask holds trivially.
     * \param[in] text The full subject the position is inside.
     * \param[in] pos  Byte offset to test the assertions at.
     * \return `true` if all of them hold.
     */
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

    /*!
     * \brief Reject as not one-pass, recording a category and the offending node / byte-class / pc.
     *
     * Locations stay integers, not formatted text, so the builder stays constexpr: a constexpr `real::regex`
     * embeds an (empty) one-pass table in its literal state.
     * \param[in] reason Category, surfaced by \ref bail_reason.
     * \param[in] node   Offending node id, or -1 when not applicable.
     * \param[in] klass  Offending byte-class, or -1.
     * \param[in] pc     Offending program counter, or -1.
     */
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

    /*!
     * \brief The first non-`jump` pc reachable from \p pc by following unconditional jumps.
     *
     * A `jump` is pure epsilon, so a jump's node and its target's get identical edges. \ref emit_utf8_trie
     * writes `klass` then `jump(target)` per trie edge: without this, the flood copies every shared trie
     * subgraph for \ref minimize to merge back; resolving the chain builds the shared node directly.
     *
     * Bounded by the code size: on a jump cycle the pc comes back as-is and \ref build_edges bails on it.
     *
     * \param[in] pc Starting pc.
     * \return The resolved pc (\p pc itself when it is not a jump, or on running out of steps).
     */
    [[nodiscard]] constexpr std::int32_t follow_jumps(std::int32_t pc) const
    {
      for (std::size_t steps = 0; steps < code_.size(); ++steps) {
        if (pc < 0 || static_cast<std::size_t>(pc) >= code_.size()
            || code_[static_cast<std::size_t>(pc)].op != opcode::jump) {
          return pc;
        }
        pc = code_[static_cast<std::size_t>(pc)].primary_target;
      }
      return pc;
    }

    /*!
     * \brief Get-or-create the node whose entry pc is \p pc, enqueueing a fresh one for the flood.
     * \param[in]     pc    Entry program counter the node stands for.
     * \param[in,out] queue Work list a newly created node is appended to.
     * \return The node's id, existing or just created.
     */
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

    /*!
     * \brief Builds the table: flood the byte program into nodes, write their edges, then minimize.
     *
     * Declines by calling \ref bail, which leaves \ref eligible false and \ref bail_reason set.
     * \param[in] bp The klass_cp-expanded byte program to compile.
     */
    constexpr void build(const byte_program& bp)
    {
      code_    = bp.code;
      classes_ = bp.classes;
      alpha_   = compute_lazy_alphabet(bp.code, bp.classes);

      // Per interned char-class, the byte-classes it consumes, computed once (O(classes x 256)): per
      // instruction it is O(nodes x classes) and dominates a Unicode \w build.
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
      // Allocated once: build_edges clears every entry it sets on each path that keeps eligible_ true (a bail
      // ends the loop), so it is all-zero at each iteration. Re-zeroing per node is O(node_cap x code size),
      // which dominates a large repeated class's build.
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
      minimize(); // (i) merge equivalent nodes
      if (!eligible_) {
        return; // minimize() hit its work cap: nodes_ is an unfinished flood, not a table.
      }
      // (ii) memory cap: even minimized, a pathological table declines to the Pike VM.
      std::size_t bytes {0};
      for (const onepass_node& nd : nodes_) {
        bytes += nd.edge.capacity() * sizeof(onepass_edge);
      }
      if (bytes > max_bytes_) {
        bail("one-pass table too large after minimization (MB)", static_cast<std::int32_t>(bytes >> 20));
        return;
      }
      const std::size_t width {alpha_.count};
      steps_.assign(nodes_.size() * width, onepass_step {.row = no_row});
      for (std::size_t n = 0; n < nodes_.size(); ++n) {
        for (std::size_t c = 0; c < width; ++c) {
          const onepass_edge& edge {nodes_[n].edge[c]};
          if (edge.assigned) {
            steps_[n * width + c] = {.row         = static_cast<std::uint32_t>(edge.next * width),
                                     .target      = ranked(edge.next),
                                     .cap_mask    = static_cast<std::uint16_t>(edge.cap_mask),
                                     .assert_mask = static_cast<std::uint16_t>(edge.assert_mask)};
          }
        }
        // The rows now live in steps_ alone: kept here as well, every eligible regex would carry its table twice.
        nodes_[n].edge = {};
        ends_known_    = ends_known_ && accept_rank(nodes_[n]) != rank_mixed;
      }
      start_ = ranked(0);
    }

    /*!
     * \brief A node id with its \ref accept_rank in the top byte, as \ref onepass_step::target holds it.
     * \param[in] node The node id, below 2^24 (\ref max_nodes is far below).
     * \return The packed value.
     */
    [[nodiscard]] constexpr std::uint32_t ranked(std::uint32_t node) const
    {
      return node | (static_cast<std::uint32_t>(accept_rank(nodes_[node])) << 24U);
    }

    /*!
     * \brief Moore partition refinement: merges nodes with the same accept, match masks and, per byte-class,
     *        the same edge (target partition and masks). The graph has cycles (`\w+`), so bottom-up
     *        hash-consing is not enough; refinement runs to a fixpoint. Merged nodes share their masks by
     *        construction, and each keeps a dense edge row.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((cold)) // build-time only, never on a search path
#endif
    constexpr void minimize()
    {
      const std::size_t          n       {nodes_.size()};
      std::vector<std::uint32_t> cls(n, 0);
      std::uint32_t              classes {0};
      // Per-round work is n x sig_width on the dense width (every edge row holds alpha_.count entries), fixed
      // for the call; the cap is on cumulative work (see max_minimize_work).
      const std::uint64_t sig_width  {4U + (3U * static_cast<std::uint64_t>(alpha_.count))};
      const std::uint64_t round_work {static_cast<std::uint64_t>(n) * sig_width};
      std::uint64_t       work_done  {0};
      // Scratch is flat and hoisted out of the loop, four allocations per call: nested vectors cost
      // n + minimize_buckets allocations a round, a capacity check per signature word, and a header per bucket.
      //
      // Buckets are intrusive chains (bucket_head + bucket_next). Head insertion cannot change the result: a
      // bucket holds ONE representative per distinct signature. bucket_next needs no reset: heads are reset
      // each round, so every node reached on a chain was written this round.
      //
      // Rows are SPARSE (variable length, hence row_at): an unassigned class adds the same (0, 0, 0) to every
      // dense row, and each sparse entry carries its class index, so no comparison changes. Rows also split by
      // what varies: class indices and masks are fixed for the call and collapse once into inv_id, a bijection
      // minted in node order, so a round compares (inv_id, cls[i], nexts...) with an identical partition and
      // numbering at every round.
      //
      // Invariant row at inv_at[i]: [0..2] accept rank, match_cap_mask, match_assert_mask, then per assigned
      //   class in class order: class index, cap_mask, assert_mask.
      // Round row at row_at[i]: [0] inv_id[i], [1] cls[i], then per assigned class in class order:
      //   cls[edge.next] + 1.
      std::vector<std::size_t> row_at(n + 1, 0);
      std::vector<std::size_t> inv_at(n + 1, 0);
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t assigned {0};
        for (const onepass_edge& e : nodes_[i].edge) {
          assigned += e.assigned ? 1U : 0U;
        }
        row_at[i + 1] = row_at[i] + 2U + assigned;
        inv_at[i + 1] = inv_at[i] + 3U + (3U * assigned);
      }

      // The invariant half, written once and then collapsed to one id per distinct tuple.
      std::vector<std::uint64_t> inv(inv_at[n], 0);
      for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t* v {inv.data() + inv_at[i]};
        v[0] = accept_rank(nodes_[i]);
        v[1] = nodes_[i].match_cap_mask;
        v[2] = nodes_[i].match_assert_mask;
        std::size_t w {3};
        for (std::size_t c = 0; c < nodes_[i].edge.size(); ++c) {
          const onepass_edge& e {nodes_[i].edge[c]};
          if (e.assigned) {
            v[w]     = c;
            v[w + 1] = e.cap_mask;
            v[w + 2] = e.assert_mask;
            w       += 3;
          }
        }
      }
      std::vector<std::uint64_t> inv_id(n, 0);
      {
        std::vector<std::uint32_t> inv_head(minimize_buckets, no_node);
        std::vector<std::uint32_t> inv_next(n, no_node);
        std::uint64_t              inv_count {0};
        for (std::size_t i = 0; i < n; ++i) {
          const std::span<const std::uint64_t> row   {inv.data() + inv_at[i], inv_at[i + 1] - inv_at[i]};
          std::uint32_t&                       head  {inv_head[sig_hash(row) % minimize_buckets]};
          bool                                 found {false};
          for (std::uint32_t j = head; j != no_node; j = inv_next[j]) {
            const std::span<const std::uint64_t> other {inv.data() + inv_at[j], inv_at[j + 1] - inv_at[j]};
            if (std::equal(row.begin(), row.end(), other.begin(), other.end())) {
              inv_id[i] = inv_id[j];
              found     = true;
              break;
            }
          }
          if (!found) {
            inv_id[i]   = inv_count++;
            inv_next[i] = head;
            head        = static_cast<std::uint32_t>(i);
          }
        }
      }

      std::vector<std::uint64_t> sigs(row_at[n], 0);
      // Assigned edges' targets, gathered once at their signature offsets: a round reads a packed run of node
      // ids instead of testing `assigned` per class (39.4 entries against 103 for `(\w+)@(\w+)`).
      std::vector<std::uint32_t> nexts(row_at[n], 0);
      for (std::size_t i = 0; i < n; ++i) {
        sigs[row_at[i]] = inv_id[i];
        std::size_t w {row_at[i] + 2U};
        for (const onepass_edge& e : nodes_[i].edge) {
          if (e.assigned) {
            nexts[w] = e.next;
            ++w;
          }
        }
      }
      std::vector<std::uint32_t> next_cls(n, 0);
      std::vector<std::uint32_t> bucket_head(minimize_buckets, no_node);
      std::vector<std::uint32_t> bucket_next(n, no_node);
      while (true) {
        // Counted on the DENSE width the cap was calibrated on: counting sparse work would turn declines
        // into tables, a route change rather than a speed change.
        if (work_done + round_work > work_cap_) {
          bail("one-pass minimization exceeded its work budget: not one-pass", static_cast<std::int32_t>(n));
          return;
        }
        work_done += round_work;
        for (std::size_t i = 0; i < n; ++i) {
          const std::size_t base {row_at[i]};
          const std::size_t stop {row_at[i + 1]};
          std::uint64_t*    s    {sigs.data() + base};
          s[1] = cls[i]; // s[0] is inv_id, written once above
          for (std::size_t w {base + 2U}; w < stop; ++w) {
            s[w - base] = static_cast<std::uint64_t>(cls[nexts[w]]) + 1U;
          }
        }
        next_cls.assign(n, 0);
        bucket_head.assign(minimize_buckets, no_node);
        std::uint32_t next {0};
        for (std::size_t i = 0; i < n; ++i) {
          const std::span<const std::uint64_t> row  {sigs.data() + row_at[i], row_at[i + 1] - row_at[i]};
          std::uint32_t&                       head {bucket_head[sig_hash(row) % minimize_buckets]};
          std::uint32_t                        id   {no_node};
          for (std::uint32_t j = head; j != no_node; j = bucket_next[j]) {
            const std::span<const std::uint64_t> other {sigs.data() + row_at[j], row_at[j + 1] - row_at[j]};
            if (std::equal(row.begin(), row.end(), other.begin(), other.end())) {
              id = next_cls[j];
              break;
            }
          }
          if (id == no_node) {
            next_cls[i]    = next++;
            bucket_next[i] = head;
            head           = static_cast<std::uint32_t>(i);
          }
          else {
            next_cls[i] = id;
          }
        }
        cls.swap(next_cls); // swap, not move: `next_cls` is reused next round (assign(n, 0) above)
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
        nn.edge_before_match = r.edge_before_match;
        nn.edge_after_match  = r.edge_after_match;
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

    /*!
     * \brief FNV-1a hash of a partition signature, for the constexpr-friendly bucket dedup.
     *
     * Accumulates in a fixed 64-bit width and truncates only at the return, so a 32-bit `size_t` (Win32)
     * never sees a narrowing brace-init of the 64-bit offset basis.
     * \param[in] v The signature words to hash.
     * \return The hash, truncated to `size_t`.
     */
    static constexpr std::size_t sig_hash(std::span<const std::uint64_t> v)
    {
      std::uint64_t h {fnv1a_offset_basis};
      for (const std::uint64_t x : v) {
        h = (h ^ x) * fnv1a_prime;
      }
      return static_cast<std::size_t>(h);
    }

    /*!
     * \brief Walk the epsilon-closure from \p pc, writing this node's edges.
     * \param[in]     pc       Program counter to walk from.
     * \param[in]     cap_mask Capture slots crossed on the way here; accumulates down the closure.
     * \param[in]     assert_mask Assertions crossed on the way here, as \ref assert_kind bits.
     * \param[in,out] on_path     Per-pc marks detecting an epsilon CYCLE — a nullable loop is not one-pass.
     * \param[in]     node_id     The node whose edge row is being written.
     * \param[in,out] queue    Work list new nodes are appended to.
     */
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
            const std::uint32_t next {node_of(follow_jumps(pc + 1), queue)};
            onepass_node&       node {nodes_[node_id]};
            // Only the byte-classes this instruction consumes, never a whole-alphabet scan (see class_cover_).
            // cls < edge.size(): node_of sizes every row to alpha_.count and cls is alpha_.of[byte] or a cover
            // entry. clang-analyzer cannot see that sizing through node_of, hence the NOLINT.
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
                                     // The closure walks in priority order, so a match already reached outranks
                                     // this edge, and one reached later is outranked by it.
                                     (node.matches ? node.edge_after_match : node.edge_before_match) = true;
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
          // Unreachable from any public construction (the constructor bails on an ineligible program, and
          // build_byte_program expands every klass_cp); guards an opcode added without a case here.
          bail("unexpected op in byte-program (lookaround/klass_cp should be absent)");
          return;
      }
      on_path[static_cast<std::size_t>(pc)] = 0; // backtrack: only a cycle bails, a diamond is fine
    }

    std::span<const instr>                  code_;                           //!< The byte program being compiled; borrowed, not owned.
    std::span<const char_class>             classes_;                        //!< Its interned byte classes; borrowed alongside \ref code_.
    lazy_byte_alphabet                      alpha_;                          //!< Byte-equivalence classes: what \ref class_of answers with.
    std::vector<std::vector<std::uint16_t>> class_cover_;                    //!< char-class index -> the byte-classes it consumes.
    std::vector<std::uint32_t>              pc_to_node_;                     //!< pc -> node id (or no_node).
    std::vector<onepass_node>               nodes_;                          //!< The table, node 0 being the start; empty until built.
    std::size_t                             slot_count_ {0};                 //!< Capture slots the program uses (\ref slot_count).
    std::vector<onepass_step>               steps_;                          //!< \ref nodes_' edges in one array, row by row: what \ref extract walks.
    std::uint32_t                           start_      {0};                 //!< Node 0 packed as \ref onepass_step::target is.
    bool                                    ends_known_ {true};              //!< No node is \ref rank_mixed, so \ref extract_leftmost applies.

    std::size_t                             max_bytes_  {max_table_bytes};   //!< Table-memory cap (a constructor test hook).
    std::size_t                             node_cap_   {max_nodes};         //!< Node-count cap; same test-hook role.
    std::uint64_t                           work_cap_   {max_minimize_work}; //!< Moore-refinement work cap; same role.
    bool                                    ascii_word_ {true};              //!< ASCII word-ness for `\b \B \< \>` edge conditions unless `byte_program::unicode_word`.
    std::int32_t                            bail_node_  {-1};                //!< Node the decline was found at, or -1. Diagnostic only.
    std::int32_t                            bail_class_ {-1};                //!< Byte-class involved in the decline, or -1.
    std::int32_t                            bail_pc_    {-1};                //!< Program counter involved in the decline, or -1.
    bool                                    eligible_   {true};              //!< Cleared by \ref bail; read through \ref eligible.
    std::string                             bail_reason_;                    //!< Human-readable decline reason (\ref bail_reason).
  };

  struct regex_immutables;                                       // defined below; the dtor calls the next line
  inline void erase_shared_dfas(const regex_immutables* immut);

  /*!
   * \brief The per-regex immutable cache the router shares across every find_iter on a regex: the byte
   *        program (klass_cp expanded to the deterministic trie) and, when the pattern is one-pass, the
   *        extractor table.
   *
   * Each product is keyed by program identity (\ref built_for and its siblings), so a const regex shared by
   * threads builds race-free and an assignment onto a warmed regex rebuilds. The mutable lazy-DFA caches live
   * in a process-wide side table keyed by this object's address (\ref shared_dfa_slot), one set per scanning
   * thread (\ref dfa_lease), which keeps \c std::mutex out of this struct. Each rebuild in
   * \ref pike_vm::ensure_immutables drops the slot's DFAs (\ref reset_shared_dfas); the destructor erases the
   * map entry, so match-time caches never outlive the regex.
   */
  struct regex_immutables
  {
    byte_program           byte_prog;                     //!< klass_cp-expanded byte program (empty until built).
    lazy_byte_alphabet     alphabet;                      //!< byte-class alphabet of byte_prog, shared by both DFAs.
    byte_program           look_prog;                     //!< byte program keeping its position assertions, built only when byte_prog declined: the search DFAs run it.
    lazy_byte_alphabet     look_alphabet;                 //!< byte-class alphabet of look_prog.
    bool                   run_shape {false};             //!< The program is saves, atoms and greedy `atom+` loops only: pike_vm::match_run_shape reads its groups off a window.
    std::optional<onepass> op_table;                      //!< one-pass extractor, present iff the pattern is one-pass.
    byte_program           il_prefix_prog;                //!< IL: the inner-literal prefix's byte program (ineligible until built); per regex, so the reverse DFA over it is shared.
    std::size_t            il_min_haystack {};            //!< IL cold floor: the first candidate scan abandons below this size (0 = never), checked only after a literal hit; see \ref pike_vm::run_inner_literal.
    /*!
     * \brief Byte-indexed membership rows, filled on first use of each class and kept for the regex's life.
     *
     * Deriving a row into the VM state charges every short search (`search()` builds a fresh state); filling
     * every row at compile charges patterns that read few of their classes (a negated class interns a dozen).
     * Per class, on demand, per regex pays for neither.
     *
     * Thread safety: \ref rows_for keys the program the rows were sized for, as \ref built_for does. A row's
     * flag is release-stored after the row is filled under \ref immut_build_mu and acquire-loaded before it
     * is read; only the lock holder that saw the flag clear writes a row, so a published row is immutable.
     */
    std::vector<std::uint8_t>            class_rows;      //!< One 256-byte row per interned BYTE class.
    std::vector<std::uint8_t>            cp_ascii_rows;   //!< One 256-byte row per cp_class: its ASCII half.
    std::vector<std::uint64_t>           cp_page_rows;    //!< One 30-word bitmap per cp_class: `[U+0080, U+07FF]`.
    /*!
     * \brief "Row filled" flags: one bit per row, the three runs packed into one word, plus an overflow
     *        vector for indices that do not fit.
     *
     * Every `real::regex` construction pays for this block: separate `std::vector<std::atomic<char>>` runs
     * allocate even for a pattern with no cp_class, where bits allocate nothing. Read on a VM-state miss, not
     * per call. The overflow is a vector of atomics, not a `unique_ptr` array (non-constexpr destructor; this
     * struct must stay literal), and not `std::atomic_ref`, which a supported libc++ lacks.
     */
    std::atomic<std::uint64_t>     row_ready_bits    {0};
    std::vector<std::atomic<char>> row_ready_overflow;    //!< Flags for row indices at or past \ref row_ready_bit_capacity.
    std::size_t                    cp_ascii_ready_at {0}; //!< Where the cp_ascii run starts in the flag index space.
    std::size_t                    cp_page_ready_at  {0}; //!< As \ref cp_ascii_ready_at, for cp_page.
    //! \brief \c prog.code.data() the rows above were sized for, or null. Independent of \ref built_for, as
    //!        scan routes that never build the DFA caches need the rows.
    std::atomic<const void*> rows_for {nullptr};

    //! \brief The multi-literal automaton for a `fixed_alternation` past the branch threshold, or empty when
    //!        never built or declined (a pathological icase-fold expansion). Per regex, not per state: a state
    //!        is fresh per `search()` and would rebuild it every call. A vector of at most one element, not a
    //!        `unique_ptr`, keeps this type literal and the 432-byte header off regexes that never build one.
    std::vector<ac_automaton> ac;

    //! \brief \c prog.code.data() \ref ac was built for, or null. Not folded into \ref built_for, since only
    //!        the alternation route consults the automaton (see \ref op_table_for).
    std::atomic<const void*> ac_for {nullptr};

    //! \brief The alternation's probe pairs with their splats, or empty when never built. Per regex, not per
    //!        state: in the per-`search()` state gcc would zero the 512 bytes of splats every call
    //!        (check-state-zeroing). At most one element, on the heap, as \ref ac.
    std::vector<alternation_pairs> alt_pairs;

    std::atomic<const void*> alt_pairs_for {nullptr}; //!< \c prog.code.data() \ref alt_pairs was built for, or null (own identity atomic, as \ref ac_for).

    //! \brief \c prog.code.data() \ref op_table was built for, or null. Kept out of \ref built_for because
    //!        the extractor costs more than the byte program and lazy DFA together, and only routes that fill
    //!        captures through it need it.
    std::atomic<const void*> op_table_for {nullptr};

    //! \brief \c prog.code.data() this cache was built for, or null if never built / invalidated.
    //!        Hot path: one atomic load. Not \c once_flag — assignment reuses this object under a new
    //!        program; a spent once_flag would never rebuild (silent wrong matches).
    std::atomic<const void*> built_for                  {nullptr};

    static constexpr std::size_t row_ready_bit_capacity {64}; //!< How many flag indices \ref row_ready_bits covers; the rest live in \ref row_ready_overflow.

    // A relation, not a knob: row_ready shifts `1ULL << i` below this width and indexes the overflow from
    // it, so a mismatch is silent UB that the suite rarely reaches.
    static_assert(row_ready_bit_capacity
                  == sizeof(decltype(row_ready_bits)::value_type) * 8U,
                  "row_ready_bit_capacity must be exactly the bit width of row_ready_bits");

    /*!
     * \brief Reads the "filled" flag for flag index \p i, acquiring what the filling thread released.
     * \param[in] i Flag index: a class row, or a cp_ascii / cp_page row at its own run's offset.
     * \return `true` once the row's contents are visible to this thread.
     */
    [[nodiscard]] bool row_ready(std::size_t i) const noexcept
    {
      if (i < row_ready_bit_capacity) {
        return ((row_ready_bits.load(std::memory_order_acquire) >> i) & 1ULL) != 0ULL;
      }
      return row_ready_overflow[i - row_ready_bit_capacity].load(std::memory_order_acquire) != 0;
    }

    /*!
     * \brief Publishes the "filled" flag for flag index \p i.
     *
     * Called only by the thread holding \ref immut_build_mu, so the read-modify-write on the bit word cannot
     * race another writer.
     * \param[in] i Flag index, in the same space \ref row_ready reads.
     */
    void set_row_ready(std::size_t i) noexcept
    {
      if (i < row_ready_bit_capacity) {
        row_ready_bits.fetch_or(1ULL << i, std::memory_order_release);
      }
      else {
        row_ready_overflow[i - row_ready_bit_capacity].store(1, std::memory_order_release);
      }
    }

    regex_immutables() = default; //!< An empty cache: every identity key null, nothing built.

    /*!
     * \brief Copies as an EMPTY cache: a copied regex is an independent regex.
     *
     * The cache body is never transferred — it is a pure runtime accelerator and cheap to rebuild — so the
     * copy starts with \ref built_for and \ref rows_for null and rebuilds on its own first routed search.
     */
    regex_immutables(const regex_immutables& /*other*/) noexcept
      : rows_for {nullptr}, built_for {nullptr}
    {}

    /*!
     * \brief Moves as an empty cache, for the same reason as the copy constructor.
     */
    regex_immutables(regex_immutables&& /*other*/) noexcept
      : rows_for {nullptr}, built_for {nullptr}
    {}

    /*!
     * \brief Clears EVERY identity key, so nothing built for the old program survives an assignment.
     *
     * Copy-assigning the program's vector can reuse its buffer, so `code.data()` is unchanged and any key
     * left set still matches: its cache then serves the previous pattern's product, silently wrong in both
     * directions. Hence every key, not only those with a known reproducer. The automaton is also released:
     * its size follows the pattern's (up to \ref ac_memory_budget).
     */
    void invalidate_all() noexcept
    {
      ac.clear();
      ac.shrink_to_fit();
      built_for.store(nullptr, std::memory_order_relaxed);
      rows_for.store(nullptr, std::memory_order_relaxed);
      ac_for.store(nullptr, std::memory_order_relaxed);
      alt_pairs_for.store(nullptr, std::memory_order_relaxed);
      op_table_for.store(nullptr, std::memory_order_relaxed);
    }

    /*!
     * \brief Keeps this object's cache STORAGE but marks it invalid.
     *
     * The destination's program is already the new one by the time storage assignment reaches here, so this
     * cannot rebuild from the source's program and must not inherit its built state: clearing
     * \ref built_for forces `pike_vm`'s `ensure_immutables` to rebuild. Self-assignment-safe.
     * \return `*this`.
     */
    // NOLINTNEXTLINE(cert-oop54-cpp): deliberate non-copy of members, per the note above.
    regex_immutables& operator=(const regex_immutables& /*other*/) noexcept
    {
      invalidate_all();
      return *this;
    }

    /*!
     * \brief Invalidates as the copy assignment does, and for the same reason.
     * \return `*this`.
     */
    // NOLINTNEXTLINE(cert-oop54-cpp)
    regex_immutables& operator=(regex_immutables&& /*other*/) noexcept
    {
      invalidate_all();
      return *this;
    }

    /*!
     * \brief Erases this regex's shared DFA slot at run time; constant evaluation skips the map, so
     *        dynamic_storage::compile and static_assert stay valid.
     */
    constexpr ~regex_immutables()
    {
      if (!std::is_constant_evaluated()) {
        erase_shared_dfas(this);
      }
    }
  };

  /*!
   * \brief Striped rebuild lock for \ref pike_vm::ensure_immutables (not on \ref regex_immutables —
   *        layout isolation). Distinct from \ref shared_dfa_map_mu / \ref shared_dfa_slot::pool_mu so \ref reset_shared_dfas
   *        cannot self-deadlock. Different immutables rarely share a stripe.
   * \param[in] immut The cache whose stripe is wanted; hashed by address, never dereferenced.
   * \return The stripe guarding that cache's build.
   */
  [[nodiscard]] inline std::mutex& immut_build_mu(const regex_immutables* immut)
  {
    static std::array<std::mutex, 64> stripes {};
    const std::size_t                 h       {std::hash<const void*> {}(immut)}; // REAL_ALLOW_STD_HASH
    return stripes[h % stripes.size()];
  }

  /*!
   * \brief One thread's lazy DFAs for one regex: the transition caches a scan fills as it walks.
   *
   * A set is used by one thread at a time (see \ref dfa_lease), so a scan takes no lock. It is built
   * for one program: \ref generation names the \ref shared_dfa_slot::generation it was built under,
   * and a lease clears a set whose generation is stale before handing it out.
   */
  struct shared_dfa_set
  {
    std::optional<lazy_dfa>    fwd;                  //!< Forward lazy DFA, absent until a route first needs it.
    std::optional<reverse_dfa> rev;                  //!< Reverse lazy DFA, for finding a match start from its end.
    std::optional<reverse_dfa> il_prefix_rev;        //!< Reverse DFA over the inner-literal PREFIX sub-program only.
    std::uint64_t              generation {0};       //!< The slot generation these DFAs were built under.

    /*!
     * \brief Sets alive, counted for the tests that pin when a destroyed regex's DFAs are freed.
     * \return A reference to the process-wide counter (relaxed atomic).
     */
    static std::atomic<std::int64_t>& alive() noexcept
    {
      static std::atomic<std::int64_t> count {0};
      return count;
    }

    shared_dfa_set()
    {
#if defined(REAL_TEST_INSTRUMENT)
      alive().fetch_add(1, std::memory_order_relaxed);
#endif
    }

    shared_dfa_set(const shared_dfa_set&)            = delete;
    shared_dfa_set& operator=(const shared_dfa_set&) = delete;
    shared_dfa_set(shared_dfa_set&&)                 = delete;
    shared_dfa_set& operator=(shared_dfa_set&&)      = delete;

    ~shared_dfa_set()
    {
#if defined(REAL_TEST_INSTRUMENT)
      alive().fetch_sub(1, std::memory_order_relaxed);
#endif
    }
  };

  /*!
   * \brief Process-wide per-regex DFA state keyed by \ref regex_immutables*: a pool of
   *        \ref shared_dfa_set, one per thread using the regex, and the flags every thread shares.
   *
   * Thread-safe: map insert/erase under \ref shared_dfa_map_mu; the pool under \ref pool_mu, held only
   * to take or return a set, never during a scan. Slots are \c shared_ptr so a concurrent
   * \ref erase_shared_dfas (from a destructor) cannot free a slot a thread still holds a set from — the
   * slot dies when the last holder, map or thread-local lease, releases it.
   */
  struct shared_dfa_slot
  {
    std::mutex                                   pool_mu;         //!< Guards \ref free only.
    std::vector<std::unique_ptr<shared_dfa_set>> free;            //!< Sets no thread holds.
    std::atomic<std::uint64_t>                   generation {0};  //!< Moves when the program is rebuilt.
    //! \brief Set once this regex has been IL-candidate-scanned (any size): the first scan uses the cold
    //!        \ref regex_immutables::il_min_haystack, later ones \ref il_warm_floor. Keyed on a scan, not on
    //!        il_prefix_rev being built, which a corpus always below the floor would never reach.
    std::atomic<bool>          il_warmed {false};

    //! \brief The regex this slot belongs to, or null once \ref erase_shared_dfas has retired it. Validates a
    //!        thread's last-hit cache in \ref shared_dfa_for. Per slot, so one regex's destruction sends only
    //!        its own users back to \ref shared_dfa_map_mu (a global epoch would send every thread).
    std::atomic<const regex_immutables*> owner {nullptr};
  };

  //! \brief Warm-regime IL minimum haystack, in bytes: below it the candidate scan can cost more than the
  //!        route saves, even with the reverse DFA already built. A cold first scan uses the higher
  //!        \ref regex_immutables::il_min_haystack.
  inline constexpr std::size_t il_warm_floor {4UL * 1024};

  /*!
   * \brief The mutex guarding insert/erase on the process-wide \ref shared_dfa_slot map.
   *
   * Distinct from \ref shared_dfa_slot::pool_mu, which guards a slot's free sets; a scan holds neither, so
   * \ref erase_shared_dfas cannot deadlock against one.
   * \return The process-wide map mutex.
   */
  inline std::mutex& shared_dfa_map_mu()
  {
    static std::mutex m;
    return m;
  }

  /*!
   * \brief Process-wide map, deliberately never destroyed: other statics' \c ~regex_immutables still call
   *        \ref erase_shared_dfas at exit. Entries are erased per destructor, so nothing accumulates.
   *
   * The one `std::unordered_map` in these headers. The rule (see lazy_dfa.hpp's `hash_trans`) keeps
   * `std::hash` and `std::unordered_map`, whose out-of-line libc++ symbols drift across toolchains, off scan
   * paths; this map is consulted on a lease or a thread-cache miss, never per match nor in constant
   * evaluation. `check-layers` accepts `REAL_ALLOW_STD_HASH` on these lines as the exception.
   * \return The map, keyed by \ref regex_immutables address.
   */
  inline std::unordered_map<const regex_immutables*, std::shared_ptr<shared_dfa_slot>>& // REAL_ALLOW_STD_HASH
  shared_dfa_map()
  {
    static auto* m {
      new std::unordered_map<const regex_immutables*, std::shared_ptr<shared_dfa_slot>>}; // REAL_ALLOW_STD_HASH
    return *m;
  }

  /*!
   * \brief Resolve the process-wide DFA slot for this regex (map insert under \ref shared_dfa_map_mu).
   *
   * A thread-local last-hit cache fronts the map, else a dense inner-literal scan takes the map mutex once
   * per candidate. It is not on \ref regex_immutables, whose cache lines the class-loop path uses (widening
   * it is measurable). It holds a \c shared_ptr so an erase cannot free a slot under a scan, and validates
   * the hit against \ref shared_dfa_slot::owner.
   * \param[in] immut The regex whose slot is wanted.
   * \return Its slot, created on first use; never null.
   */
  [[nodiscard]] inline shared_dfa_slot& shared_dfa_for(regex_immutables* immut)
  {
    thread_local std::shared_ptr<shared_dfa_slot> cached_slot {};
    // A retired slot's owner is null and a slot names one regex only, so `owner == immut` is the whole check.
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

  /*!
   * \brief Drop any DFAs cached for \p immut (caller holds nothing; takes map + slot locks).
   *        Invoked from `pike_vm`'s `ensure_immutables` rebuild so a reused immutables address — or
   *        the same address under a new program — cannot keep a previous pattern's DFAs.
   * \param[in] immut The regex whose cached DFAs are dropped.
   */
  inline void reset_shared_dfas(regex_immutables* immut)
  {
    shared_dfa_slot& slot {shared_dfa_for(immut)};
    // A held set is cleared by its next lease, which sees the generation move; the free ones go now.
    slot.generation.fetch_add(1, std::memory_order_acq_rel);
    {
      const std::lock_guard<std::mutex> lock {slot.pool_mu};
      slot.free.clear();
    }
    slot.il_warmed.store(false, std::memory_order_relaxed);
  }

  /*!
   * \brief This thread's DFA set for one regex, for the lifetime of the lease: a scan through it takes
   *        no lock, so threads sharing a regex do not queue on its DFAs.
   *
   * Each thread keeps the set it last used, with its slot, and gives it back to that slot's pool when it
   * moves to another regex or exits; a repeat lease is an owner check and a generation check. A nested
   * lease (a scan started inside another scan) takes a separate set from the pool and returns it on
   * destruction, so the outer scan's set is never handed away under it.
   */
  class dfa_lease
  {
  public:

    /*!
     * \brief Leases a DFA set for \p immut, cleared if it was built for an earlier program.
     * \param[in] immut The regex whose DFAs are wanted.
     */
    explicit dfa_lease(regex_immutables* immut)
    {
      note(counter::dfa_leases_taken);
      cache& mine {thread_cache()};
      if (!mine.busy) {
        if (!mine.slot || mine.slot->owner.load(std::memory_order_acquire) != immut) {
          mine.give_back();
          mine.slot = slot_for(immut);
          mine.set  = take(*mine.slot);
        }
        mine.busy = true;
        slot_     = mine.slot.get();
        set_      = mine.set.get();
        cached_   = true;
      }
      else {
        nested_slot_ = slot_for(immut);
        nested_set_  = take(*nested_slot_);
        slot_        = nested_slot_.get();
        set_         = nested_set_.get();
      }
      const std::uint64_t generation {slot_->generation.load(std::memory_order_acquire)};
      if (set_->generation != generation) {
        set_->fwd.reset();
        set_->rev.reset();
        set_->il_prefix_rev.reset();
        set_->generation = generation;
      }
    }

    dfa_lease(const dfa_lease&)            = delete;
    dfa_lease& operator=(const dfa_lease&) = delete;
    dfa_lease(dfa_lease&&)                 = delete;
    dfa_lease& operator=(dfa_lease&&)      = delete;

    /*!
     * \brief Ends the lease: the cached set stays with this thread, a nested one goes back to its pool.
     */
    ~dfa_lease()
    {
      if (cached_) {
        thread_cache().busy = false;
      }
      else {
        give(*nested_slot_, std::move(nested_set_));
      }
    }

    /*!
     * \brief Frees this thread's cached set if it came from \p slot and no lease holds it: the slot's regex is
     *        gone, so no lease will ask for it again.
     * \param[in] slot The retired slot.
     */
    static void drop_thread_set(const shared_dfa_slot& slot) noexcept
    {
      cache& mine {thread_cache()};
      if (!mine.busy && mine.slot.get() == &slot) {
        mine.set.reset();
        mine.slot.reset();
      }
    }

    /*!
     * \brief The leased set.
     * \return The set.
     */
    [[nodiscard]] shared_dfa_set& operator*() const noexcept
    {
      return *set_;
    }

    /*!
     * \brief The leased set's members.
     * \return The set.
     */
    [[nodiscard]] shared_dfa_set* operator->() const noexcept
    {
      return set_;
    }

  private:

    /*!
     * \brief The set a thread keeps between leases, and the slot it returns to.
     */
    struct cache
    {
      std::shared_ptr<shared_dfa_slot> slot;          //!< The slot \ref set came from.
      std::unique_ptr<shared_dfa_set>  set;           //!< This thread's set for that slot's regex.
      bool                             busy {false};  //!< A lease holds \ref set.

      cache()                        = default;
      cache(const cache&)            = delete;
      cache& operator=(const cache&) = delete;
      cache(cache&&)                 = delete;
      cache& operator=(cache&&)      = delete;

      /*!
       * \brief Returns the set to its slot's pool.
       */
      void give_back()
      {
        if (slot && set) {
          give(*slot, std::move(set));
        }
        slot.reset();
        set.reset();
      }

      /*!
       * \brief A thread's set outlives none of its uses: it goes back to its pool when the thread ends.
       */
      ~cache()
      {
        give_back();
      }
    };

    /*!
     * \brief This thread's cache.
     * \return The cache.
     */
    static cache& thread_cache()
    {
      thread_local cache mine;
      return mine;
    }

    /*!
     * \brief The slot for \p immut, created on first use (a shared reference: it may be retired).
     * \param[in] immut The regex whose slot is wanted.
     * \return The slot.
     */
    static std::shared_ptr<shared_dfa_slot> slot_for(regex_immutables* immut)
    {
      const std::lock_guard<std::mutex> lock {shared_dfa_map_mu()};
      std::shared_ptr<shared_dfa_slot>& slot {shared_dfa_map()[immut]};
      if (!slot) {
        slot = std::make_shared<shared_dfa_slot>();
        slot->owner.store(immut, std::memory_order_relaxed); // published by this mutex's release
      }
      return slot;
    }

    /*!
     * \brief A free set from \p slot's pool, or a new one.
     * \param[in,out] slot The slot whose pool is drawn from.
     * \return The set.
     */
    static std::unique_ptr<shared_dfa_set> take(shared_dfa_slot& slot)
    {
      {
        const std::lock_guard<std::mutex> lock {slot.pool_mu};
        if (!slot.free.empty()) {
          std::unique_ptr<shared_dfa_set> set {std::move(slot.free.back())};
          slot.free.pop_back();
          return set;
        }
      }
      return std::make_unique<shared_dfa_set>();
    }

    /*!
     * \brief Returns \p set to \p slot's pool, or frees it when the pool cannot take it.
     *
     * Called from destructors, so it cannot throw: a lock that fails or a pool that cannot grow leaves
     * \p set to be freed here instead of pooled, which costs the next lease a rebuild and nothing else.
     * \param[in,out] slot The slot the set belongs to.
     * \param[in]     set  The set given back.
     */
    static void give(shared_dfa_slot&                slot,
                     std::unique_ptr<shared_dfa_set> set) noexcept
    {
      try {
        const std::lock_guard<std::mutex> lock {slot.pool_mu};
        // Checked under the pool's lock, which the retiring erase takes after it clears the owner: a set given
        // back after the drain sees the null and is freed with `set`, never pooled on a slot nothing reads.
        if (slot.owner.load(std::memory_order_acquire) != nullptr) {
          slot.free.push_back(std::move(set));
        }
      }
      catch (...) { // NOLINT(bugprone-empty-catch) -- the set is freed with `set`; see above
      }
    }

    shared_dfa_slot*                 slot_   {nullptr};     //!< The slot the set belongs to.
    shared_dfa_set*                  set_    {nullptr};     //!< The leased set.
    bool                             cached_ {false};       //!< The set is this thread's cached one.
    std::shared_ptr<shared_dfa_slot> nested_slot_;          //!< The slot of a nested lease.
    std::unique_ptr<shared_dfa_set>  nested_set_;           //!< The set of a nested lease.
  };

  /*!
   * \brief Retire this regex's slot (called from \c ~regex_immutables). Scans still holding the slot's
   *        \c shared_ptr keep it alive; clearing \ref shared_dfa_slot::owner stops their cached copy
   *        matching, so a new regex at this address is never served the retired slot.
   *
   * The release store of the owner pairs with \ref shared_dfa_for's acquire check: the allocator orders this
   * erase before a construction reusing the address, and a thread reaches the new regex only by
   * synchronizing with that constructor, so it sees the null. The map entry drops under the lock, the last
   * reference outside it; the pooled sets and this thread's cached one are freed here
   * (\ref dfa_lease::drop_thread_set).
   * \param[in] immut The regex being destroyed, whose slot is retired.
   */
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
    // Nothing leases from a retired slot again: free its pool and this thread's cached set now. Another
    // thread's cached set goes at its next lease or exit.
    std::vector<std::unique_ptr<shared_dfa_set>> drained;
    {
      const std::lock_guard<std::mutex> lock {retired->pool_mu};
      drained.swap(retired->free);
    }
    dfa_lease::drop_thread_set(*retired);
  }

  /*!
   * \brief Test/audit: number of live shared-DFA map entries (process-wide). Not for production.
   * \return The live entry count.
   */
  [[nodiscard]] inline std::size_t shared_dfa_map_size_for_test()
  {
    const std::lock_guard<std::mutex> lock {shared_dfa_map_mu()};
    return shared_dfa_map().size();
  }
} // namespace real::detail

#endif // REAL_ONEPASS_HPP
