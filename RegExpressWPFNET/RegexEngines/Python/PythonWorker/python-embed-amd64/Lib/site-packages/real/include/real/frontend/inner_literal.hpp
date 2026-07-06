/*!
 * \file frontend/inner_literal.hpp
 * \brief Extracts a *required inner literal* from a pattern's AST — the substring that every match must
 *        contain (`(\w+)@(\w+)` -> `@`, `key=(\w+)` -> `key=`, `\d{4}-\d{2}` -> `-`). It is the memmem
 *        candidate an inner-literal prefilter scans for: find the literal, then confirm the surrounding
 *        pattern from that candidate. Pure over the node pool; **inert** — nothing routes on it yet.
 */
#ifndef REAL_FRONTEND_INNER_LITERAL_HPP
#define REAL_FRONTEND_INNER_LITERAL_HPP

#include <real/version.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <real/engine/prefilter.hpp> // byte_frequency (a lower tier — frontend may use the runtime)
#include <real/frontend/ast.hpp>

namespace real::detail {

  //! \brief The best required inner literal of a pattern (the memmem candidate). `len == 0` means the pattern
  //!        declined: an alternation, an optional (`?`/`*`/`{0,n}`), a lookaround or an anchor at the level
  //!        walked, or simply no literal run — anything that would make a required literal unsound.
  struct inner_literal
  {
    std::array<std::uint8_t, 16> bytes              {};
    std::uint8_t                 len                {0};
    std::uint32_t                score              {0};  //!< Selectivity: higher = rarer/longer = fewer memmem candidates.
    std::int32_t                 prefix_child_count {-1}; //!< Top-level concat children BEFORE the literal — the
    //!< sub-pattern IL.1 reverse-matches from a candidate to find the match start. 0 = the literal is at the
    //!< head (reverse is the identity: start = candidate). -1 = the literal is nested in a group/repeat, so no
    //!< clean top-level prefix boundary exists (the prefix-reverse does not apply; the memmem candidate still does).

    [[nodiscard]] constexpr bool found() const
    {
      return len > 0;
    }
  };

  //! \brief The most bytes an inner literal keeps (a longer memmem target is diminishing returns and storage).
  inline constexpr std::size_t inner_literal_max {16};

  namespace inner_literal_detail {

    //! \brief Extraction state threaded through the walk: the growing byte run, the best literal so far, and
    //!        the top-level concat child each run began at (so the winner carries its prefix boundary).
    struct walk_state
    {
      std::vector<std::uint8_t> run;
      inner_literal             best;
      std::int32_t              run_top  {-1}; //!< Top-level child where the current run began (-1 = nested).
      std::int32_t              best_top {-1}; //!< Top-level child where the winning run began.
    };

    //! \brief Selectivity of a byte run: the sum of per-byte rarity (`2000 - byte_frequency`). A sum (rather
    //!        than the rarest byte alone) approximates the product of per-byte match probabilities, so it
    //!        rewards both a rare byte and a longer literal — the two things that shrink the candidate count.
    constexpr std::uint32_t score_run(std::span<const std::uint8_t> run)
    {
      std::uint32_t s {0};
      for (const std::uint8_t b : run) {
        s += static_cast<std::uint32_t>(2000U - byte_frequency(b));
      }
      return s;
    }

    //! \brief Score the current byte run and keep it (with its prefix boundary) if it beats `best`, then clear
    //!        it (capped at \ref inner_literal_max).
    constexpr void flush(walk_state& st)
    {
      if (!st.run.empty()) {
        const std::size_t   len {st.run.size() < inner_literal_max ? st.run.size() : inner_literal_max};
        const std::uint32_t s   {score_run(std::span<const std::uint8_t>(st.run.data(), len))};
        if (s > st.best.score) {
          st.best.len   = static_cast<std::uint8_t>(len);
          st.best.score = s;
          for (std::size_t i = 0; i < len; ++i) {
            st.best.bytes[i] = st.run[i];
          }
          st.best_top = st.run_top;
        }
        st.run.clear();
      }
    }

    //! \brief Walk one node, appending guaranteed-present literal bytes to `st.run`. `top_child` is the
    //!        top-level concat child index this node belongs to (-1 when nested in a group/repeat, so any run
    //!        it starts has no clean top-level prefix boundary). Returns `false` to DECLINE the whole
    //!        extraction — an alternation, an optional (`repeat` min 0), a lookaround or an anchor would make a
    //!        required inner literal unsound (a path could bypass it) or the pattern VM-routed. Every byte
    //!        appended is present in *every* match; the confirming scan then verifies the surrounding context.
    constexpr bool walk(const ast&   tree,
                        std::int32_t idx,
                        walk_state&  st,
                        std::int32_t top_child)
    {
      if (idx < 0) {
        return true;
      }
      const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
      switch (n.kind) {
        case node_kind::empty:
          return true;
        case node_kind::byte:
          if (st.run.empty()) {
            st.run_top = top_child; // this byte starts a run — remember where, for the prefix boundary
          }
          st.run.push_back(n.byte);
          return true;
        case node_kind::concat:
          // Only the ROOT concat's direct children are top-level (assigned indices in extract_inner_literal);
          // a nested concat's children are inside a group/repeat, hence not a top-level boundary.
          for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
            if (!walk(tree, c, st, -1)) {
              return false;
            }
          }
          return true;
        case node_kind::group:
          // A non-optional group (its optionality would be a `repeat` parent): descend; the run continues
          // across the group boundary, but a literal that STARTS inside it is nested (top_child -1).
          return walk(tree, n.child, st, -1);
        case node_kind::repeat: {
            if (n.min == 0) {
              return false; // ? * {0,n}: optional -> DECLINE (conservative v1)
            }
            flush(st);                        // the repeat's width is variable; break the run around it
            if (!walk(tree, n.child, st, -1)) { // a guaranteed literal inside the first (min) copy, nested
              return false;
            }
            flush(st);
            return true;
          }
        case node_kind::klass:
        case node_kind::any:
          flush(st); // a non-byte guaranteed segment breaks the run
          return true;
        case node_kind::alternation:
        case node_kind::lookaround:
        case node_kind::anchor:
          return false; // DECLINE
      }
      return false;
    }
  } // namespace inner_literal_detail

  //! \brief Extract the best required inner literal from a pattern's AST (a pure function on the node pool).
  //!        Returns an empty \ref inner_literal when the pattern declines. Inert: nothing routes on it yet.
  constexpr inner_literal extract_inner_literal(const ast& tree)
  {
    inner_literal_detail::walk_state st;
    if (tree.root < 0) {
      return inner_literal {};
    }
    const ast_node& root {tree.nodes[static_cast<std::size_t>(tree.root)]};
    if (root.kind == node_kind::concat) {
      std::int32_t i {0};
      for (std::int32_t c = root.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next, ++i) {
        if (!inner_literal_detail::walk(tree, c, st, i)) { // top-level child index = i
          return inner_literal {};
        }
      }
    }
    else if (!inner_literal_detail::walk(tree, tree.root, st, 0)) { // a lone top-level node is child 0
      return inner_literal {};
    }
    inner_literal_detail::flush(st); // the final run
    st.best.prefix_child_count = st.best_top;
    return st.best;
  }

  //! \brief Build the prefix sub-AST: the first \p count top-level concat children (\p count >= 1). Copies the
  //!        tree and truncates the root concat's child chain after the count-th child — the later children
  //!        become unreferenced and are simply never reached when it is compiled.
  inline ast build_prefix_ast(const ast&   tree,
                              std::int32_t count)
  {
    ast             prefix {tree};
    const ast_node& root   {prefix.nodes[static_cast<std::size_t>(prefix.root)]};
    std::int32_t    c      {root.child};
    for (std::int32_t i = 0; i + 1 < count; ++i) {
      c = prefix.nodes[static_cast<std::size_t>(c)].next;
    }
    prefix.nodes[static_cast<std::size_t>(c)].next = -1; // truncate after the count-th top-level child
    return prefix;
  }
} // namespace real::detail

#endif // REAL_FRONTEND_INNER_LITERAL_HPP
