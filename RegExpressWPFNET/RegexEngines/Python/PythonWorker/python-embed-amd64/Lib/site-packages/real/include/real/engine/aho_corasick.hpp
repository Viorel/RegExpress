/*!
 * \file aho_corasick.hpp
 * \brief Aho-Corasick multi-literal engine for large pure-literal alternations.
 *
 * A dense-trie automaton (goto-function-as-total-transition-table), built lazily per program from
 * the compiled byte/klass op sequence of a \ref real::detail::pattern_hints::fixed_alternation
 * -shaped program (see prefilter.hpp's `is_fixed_alternation`) once its branch count reaches the
 * threshold where a single O(n) automaton walk beats \ref
 * real::detail::pattern_hints::small_set's 2..8-member memchr-cascade scan — that scan has no fast
 * path at all past 8 distinct first bytes. It complements small_set/fixed_alternation rather than
 * replacing them: an eligible pattern under the threshold stays on its usual route.
 *
 * Leftmost-first semantics: earliest match start wins; among matches starting at the same
 * position, the FIRST-LISTED branch (smallest declared id) wins, matching REAL's own thread-
 * priority alternation semantics exactly — held to it by a differential rather than by assertion
 * (tests/engine/test_fastpath_seam_matrix.cpp, seam_run_aho_corasick).
 *
 * Storage is a std::vector<ac_node> pool throughout — no raw new/delete anywhere in this file.
 */
#ifndef REAL_AHO_CORASICK_HPP
#define REAL_AHO_CORASICK_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include "real/core/charclass.hpp"
#include "real/core/program.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <span>
#include <string_view>
#include <vector>

namespace real::detail {

  /*!
   * \brief One Aho-Corasick trie/DFA node: a dense 256-entry goto row plus fail/output links.
   *
   * \ref goto_ starts as a sparse trie edge set (missing = -1) during \ref ac_automaton::build
   * and ends as a TOTAL transition function (goto-function-as-DFA): every entry is a valid state
   * index once construction finishes, so a search-time lookup is a single array read with no
   * fail-chain walk. Dense only, no sparse or hybrid row: this engine is built for literal
   * alternations of tens of nodes, where the whole table is a few kilobytes, and a sparse row would
   * trade that certain cost for a lookup that is no longer one read.
   */
  struct ac_node
  {
    std::array<std::int32_t, 256> goto_       {};   //!< Trie edge (build) / total DFA transition (post-build).
    std::int32_t                  fail        {0};  //!< Fail link: the longest proper suffix of this state that is also a trie prefix.
    std::int32_t                  pattern_id  {-1}; //!< Smallest branch id ending here, or -1.
    std::int32_t                  pattern_len {0};  //!< Byte length of the pattern ending here; 0 when none does.
    std::int32_t                  output_link {-1}; //!< Next (strictly shorter) pattern-ending state on the fail chain.

    /*!
     * \brief A trie node with no edges yet: every \ref goto_ entry is -1 until \ref ac_automaton::build
     *        makes the row total.
     */
    ac_node()
    {
      goto_.fill(-1);
    }
  };

  /*!
   * \brief Dense Aho-Corasick automaton for a `fixed_alternation` program's branch set.
   *
   * Built once per compiled program (see \ref build_ac_automaton), then reused across every match
   * on that program via the state cache in \ref pike_state — the same build-once-per-program
   * discipline the lazy DFA and the inner-literal prefix follow.
   */
  class ac_automaton
  {
  public:

    /*!
     * \brief Adds one branch's literal byte-set sequence, tagged with its source declaration
     *        order (\p id).
     *
     * Multiple calls with the same \p id are legal and expected: icase fan-out means one source
     * branch containing a `klass` op at some position expands into every concrete byte string
     * that op accepts, all sharing the branch's single id — the smallest-id-wins tie-break in
     * \ref search then treats every expansion of one branch as equally (and correctly) that one
     * branch's priority, regardless of which concrete spelling matched.
     *
     * \param[in] bytes One concrete byte string the branch accepts.
     * \param[in] id    The branch's declaration order.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((cold)) // construction-only (once per program): keep this out of the hot TU
                          // neighborhood of \ref search, which runs per match.
#endif
    void add_literal(const std::vector<std::uint8_t>&  bytes,
                     std::int32_t                      id)
    {
      std::int32_t state {0};
      for (const std::uint8_t byte : bytes) {
        // A COPY, not a reference: emplace_back below can reallocate nodes_'s backing storage, and
        // any reference or pointer into the pool taken before it dangles afterwards. Read the edge
        // out by value, then write the new index back through a FRESH index into nodes_.
        // A reference here compiles, passes an ordinary differential, and is a use-after-free.
        std::int32_t next {nodes_[static_cast<std::size_t>(state)].goto_[byte]};
        if (next == -1) {
          next = static_cast<std::int32_t>(nodes_.size());
          nodes_.emplace_back();
          nodes_[static_cast<std::size_t>(state)].goto_[byte] = next;
        }
        state = next;
      }
      ac_node& end {nodes_[static_cast<std::size_t>(state)]};
      if (end.pattern_id == -1 || id < end.pattern_id) {
        end.pattern_id  = id;
        end.pattern_len = static_cast<std::int32_t>(bytes.size());
      }
      max_pattern_len_ = std::max(max_pattern_len_, static_cast<std::int32_t>(bytes.size()));
    }

    /*!
     * \brief Standard single-pass BFS goto-function-as-DFA construction.
     *
     * Every state's fail link points to a strictly shallower state, so by the time a state is
     * dequeued its fail state's `goto_` row is already total — letting every missing edge be
     * filled in place as `goto_[state][c] = goto_[fail(state)][c]`, with no separate
     * fail-chain-walk pass.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((cold)) // construction-only, same reasoning as add_literal above.
#endif
    void build()
    {
      std::queue<std::int32_t> q;
      for (std::int32_t c {}; c < 256; ++c) {
        const std::int32_t child {nodes_[0].goto_[static_cast<std::size_t>(c)]};
        if (child == -1) {
          nodes_[0].goto_[static_cast<std::size_t>(c)] = 0;
        }
        else {
          nodes_[static_cast<std::size_t>(child)].fail = 0;
          q.push(child);
        }
      }
      while (!q.empty()) {
        const std::int32_t state      {q.front()};
        q.pop();
        const std::int32_t fail_state {nodes_[static_cast<std::size_t>(state)].fail};
        for (std::int32_t c {}; c < 256; ++c) {
          const std::int32_t child {nodes_[static_cast<std::size_t>(state)].goto_[static_cast<std::size_t>(c)]};
          if (child == -1) {
            nodes_[static_cast<std::size_t>(state)].goto_[static_cast<std::size_t>(c)] =
              nodes_[static_cast<std::size_t>(fail_state)].goto_[static_cast<std::size_t>(c)];
          }
          else {
            ac_node& child_node {nodes_[static_cast<std::size_t>(child)]};
            child_node.fail = nodes_[static_cast<std::size_t>(fail_state)].goto_[static_cast<std::size_t>(c)];
            const ac_node& f    {nodes_[static_cast<std::size_t>(child_node.fail)]};
            child_node.output_link = (f.pattern_id != -1) ? child_node.fail : f.output_link;
            q.push(child);
          }
        }
      }
    }

    /*! \brief One AC search outcome: whether/where/which branch matched. */
    struct match_result
    {
      bool         matched    {}; //!< Whether anything matched; the other fields are meaningful only then.
      std::size_t  start      {}; //!< Match start offset.
      std::size_t  end        {}; //!< Match end offset.
      std::int32_t pattern_id {}; //!< Declaration order of the winning branch.
    };

    /*!
     * \brief Leftmost-first search over \p text starting at \p start.
     *
     * Earliest match start wins; among matches starting at the same position, the smallest
     * pattern id (first-listed branch) wins — REAL's own alternation semantics.
     *
     * \tparam WbOk  `bool(std::size_t match_start, std::size_t match_end)` — the caller's
     *               word-boundary check (e.g. `pike_vm::wb_boundaries_ok`); always-true when the
     *               alternation carries no `\b`/`\B` wrap.
     *
     * A trail `\b`/`\B` depends only on the END position (identical for every branch ending
     * there), but a LEAD `\b`/`\B` depends on the START position, which DOES differ across
     * branches sharing one output-link chain (a shorter suffix starts later) — so a wb-rejected
     * candidate is not necessarily the chain's last usable one. \p wb_ok is therefore checked
     * per candidate, walking deeper into the chain only when the shallower one fails it; when
     * \p wb_ok is trivially true (the common, unwrapped case) this still costs one check and
     * stops at the first (shallowest) hit, same as the unconstrained walk.
     *
     * \param[in] text  Subject.
     * \param[in] start Byte offset to begin scanning at.
     * \param[in] wb_ok The caller's word-boundary check, per \c WbOk.
     * \return The leftmost-first match, or a result whose \ref match_result::matched is false.
     */
    template <typename WbOk>
    [[nodiscard]] match_result search(std::string_view text,
                                      std::size_t      start,
                                      const WbOk&      wb_ok) const
    {
      std::int32_t state      {0};
      std::int64_t best_start {-1};
      std::int32_t best_id    {-1};
      std::int32_t best_len   {0};

      const auto consider = [&](std::int64_t end_pos, std::int32_t pattern_id, std::int32_t pattern_len) {
                              const std::int64_t start_pos {end_pos - pattern_len + 1};
                              if (best_start == -1 || start_pos < best_start ||
                                  (start_pos == best_start && pattern_id < best_id)) {
                                best_start = start_pos;
                                best_id    = pattern_id;
                                best_len   = pattern_len;
                              }
                            };

      for (std::size_t i {start}; i < text.size(); ++i) {
        const auto byte {static_cast<std::uint8_t>(text[i])};
        state = nodes_[static_cast<std::size_t>(state)].goto_[byte];
        // Walk the output-link chain from shallowest (longest, earliest start) to deepest, taking
        // the FIRST candidate whose (start, end) passes wb_ok — output_link chains are strictly
        // decreasing in depth, hence strictly increasing start_pos, so the shallowest wb-passing
        // entry is always the best one this position can offer (nothing deeper can have an
        // earlier start). One check in the common (no-wb) case. The chain is walked forward only and
        // the scan never returns to an earlier byte, so the search stays linear in the subject even
        // when the branch set nests suffixes deeply.
        std::int32_t node_idx {state};
        while (node_idx != -1) {
          const ac_node& n {nodes_[static_cast<std::size_t>(node_idx)]};
          if (n.pattern_id != -1) {
            const std::int64_t end_pos   {static_cast<std::int64_t>(i)};
            const std::int64_t start_pos {end_pos - n.pattern_len + 1};
            if (wb_ok(static_cast<std::size_t>(start_pos), i + 1)) {
              consider(end_pos, n.pattern_id, n.pattern_len);
              break;
            }
          }
          node_idx = n.output_link;
        }
        // Leftmost-first early exit: once a match starts strictly before the earliest position a
        // NEW match could still begin, nothing later can beat it.
        if (best_start != -1 && best_start < static_cast<std::int64_t>(i) - max_pattern_len_ + 1) {
          break;
        }
      }
      if (best_start == -1) {
        return {};
      }
      return {.matched     = true,
              .start       = static_cast<std::size_t>(best_start),
              .end         = static_cast<std::size_t>(best_start + best_len),
              .pattern_id  = best_id};
    }

    /*!
     * \brief States in the automaton, root included.
     * \return The node count.
     */
    [[nodiscard]] std::size_t node_count() const
    {
      return nodes_.size();
    }

  private:

    std::vector<ac_node>  nodes_           {ac_node {}}; //!< Pool storage; root at index 0. No raw new/delete.
    std::int32_t          max_pattern_len_ {1};          //!< Longest literal added, bounding how far back a match can start.
  };

  /*!
   * \brief Maximum concrete literal strings a single branch may expand into (icase klass fan-out).
   *
   * A branch whose combinatorial expansion would exceed this declines AC for the WHOLE pattern,
   * which then takes the ordinary \ref pattern_hints::fixed_alternation route. The alternative is a
   * trie whose size is the product of the branch's per-position member counts, with no bound.
   */
  inline constexpr std::size_t ac_max_branch_expansion = 64;

  /*!
   * \brief Builds an \ref ac_automaton from a `fixed_alternation`-shaped program's branch set.
   *
   * \p code and \p classes are the program's own instruction stream and class table; \p body_pc
   * is \ref pattern_hints::body_pc (the first branch/split pc, already validated by
   * `is_fixed_alternation` — this function trusts that shape and does not re-validate it: every
   * branch is a run of `byte`/`klass` ops terminated by either a `jump` (non-final branch) or
   * falling straight through (final branch), exactly the shape `is_fixed_alternation` already
   * proved the program has).
   *
   * \param[in] code    The program's instruction stream.
   * \param[in] classes Its byte classes.
   * \param[in] body_pc The first branch/split pc, per \ref pattern_hints::body_pc.
   * \return The built automaton, or `std::nullopt` if any branch's icase-fold expansion would exceed
   *         \ref ac_max_branch_expansion — the caller then takes the general alternation route.
   */
  [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // construction-only, same reasoning as ac_automaton::add_literal.
#endif
  inline std::optional<ac_automaton> build_ac_automaton(std::span<const instr>       code,
                                                        std::span<const char_class>  classes,
                                                        std::size_t                  body_pc)
  {
    ac_automaton  automaton;
    std::size_t   pc {body_pc};
    std::int32_t  id {};
    while (true) {
      const bool  is_split  {code[pc].op == opcode::split};
      std::size_t branch_pc {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};

      std::vector<std::vector<std::uint8_t>> positions;
      while (code[branch_pc].op == opcode::byte || code[branch_pc].op == opcode::klass) {
        std::vector<std::uint8_t> members;
        if (code[branch_pc].op == opcode::byte) {
          members.push_back(code[branch_pc].arg8);
        }
        else {
          const char_class& kc {classes[code[branch_pc].arg16]};
          for (int b {}; b <= 255; ++b) {
            if (kc.test(static_cast<std::uint8_t>(b))) {
              members.push_back(static_cast<std::uint8_t>(b));
            }
          }
        }
        positions.push_back(std::move(members));
        ++branch_pc;
      }

      std::vector<std::vector<std::uint8_t>> expansions {{}};
      for (const auto& members : positions) {
        if (expansions.size() * members.size() > ac_max_branch_expansion) {
          return std::nullopt;
        }
        std::vector<std::vector<std::uint8_t>> next;
        next.reserve(expansions.size() * members.size());
        for (const auto& prefix : expansions) {
          for (const auto member : members) {
            auto copy {prefix};
            copy.push_back(member);
            next.push_back(std::move(copy));
          }
        }
        expansions = std::move(next);
      }
      for (const auto& literal : expansions) {
        automaton.add_literal(literal, id);
      }
      ++id;

      if (!is_split) {
        break;
      }
      pc = static_cast<std::size_t>(code[pc].secondary_target);
    }
    automaton.build();
    return automaton;
  }
} // namespace real::detail

#endif // REAL_AHO_CORASICK_HPP
