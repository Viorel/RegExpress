/*!
 * \file frontend/inner_literal.hpp
 * \brief Extracts a *required inner literal* from a pattern's AST — the substring that every match must
 *        contain (`(\w+)@(\w+)` -> `@`, `key=(\w+)` -> `key=`, `\d{4}-\d{2}` -> `-`). It is the memmem
 *        candidate an inner-literal prefilter scans for: find the literal, then confirm the surrounding
 *        pattern from that candidate. A pure function over the node pool: compiler.hpp records the
 *        result in `pattern_hints`, and `pike_vm::run` dispatches to `run_inner_literal` on it.
 */
#ifndef REAL_FRONTEND_INNER_LITERAL_HPP
#define REAL_FRONTEND_INNER_LITERAL_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <real/version.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <real/engine/prefilter.hpp> // byte_frequency (a lower tier — frontend may use the runtime)
#include <real/frontend/ast.hpp>

/*! \brief REAL's internal implementation. Not a stable API: anything here may change between releases. */
namespace real::detail {

  /*!
   * \brief The best required inner literal of a pattern (the memmem candidate).
   *
   * `len == 0` means the pattern declined: a non-literal alternation, an optional (`?`/`*`/`{0,n}`), a
   * lookaround or a non-wb anchor at the level walked, or simply no literal run — anything that would make a
   * required literal unsound. Top-level `\b`/`\B` are peeled: they set \ref wb_lead / \ref wb_trail and
   * \ref prefix_skip so the reverse-prefix excludes them (asserts are not byte-DFA-eligible) while
   * `confirm_at` still runs the full program (boundaries checked there).
   *
   * A pure-literal alternation (`info|error|warn`) does not abort the walk: it flushes and continues, so a
   * later required run (`req=`) can still arm. No branch's bytes are ever appended — none are shared.
   * Mono-byte optionals (`s?`) stay declined; see \ref real::detail::inner_literal_detail::walk for why
   * continuing past one is sound and still not wanted.
   */
  struct inner_literal
  {
    //! \brief The literal's bytes, the first \ref len of which are meaningful. Fixed capacity, so an
    //!        \ref inner_literal is copyable and needs no allocation; \ref inner_literal_max is that bound.
    std::array<std::uint8_t, 16> bytes              {};
    std::uint8_t                 len                {0};  //!< Bytes held in \ref bytes; 0 means the pattern declined.
    std::uint32_t                score              {0};  //!< Selectivity: higher = rarer/longer = fewer memmem candidates.
    //! \brief Top-level concat children BEFORE the literal — the sub-pattern the prefix-reverse matches
    //!        backwards from a candidate to find the match start. 0 = the literal is at the head (identity:
    //!        start = candidate). -1 = the literal is nested in a group/repeat, so no clean top-level prefix
    //!        boundary exists (the prefix-reverse does not apply; the memmem candidate still does).
    std::int32_t                 prefix_child_count {-1};

    //! \brief Leading top-level children to skip when building the reverse-prefix (peeled lead `\b`/`\B`).
    //!        \ref prefix_child_count is counted from the first non-peeled body child, so the reverse
    //!        program is `children[prefix_skip .. prefix_skip + prefix_child_count)`.
    std::int32_t                 prefix_skip        {0};
    std::uint8_t                 wb_lead            {0}; //!< 0/1/2 — peeled lead `\b`/`\B` (informational; confirm checks).
    std::uint8_t                 wb_trail           {0}; //!< 0/1/2 — peeled trail `\b`/`\B`.

    /*!
     * \brief Whether an inner literal was found at all.
     * \return `true` if \ref len is non-zero, i.e. the pattern did not decline.
     */
    [[nodiscard]] constexpr bool found() const
    {
      return len > 0;
    }
  };

  inline constexpr std::size_t inner_literal_max {16}; //!< The most bytes an inner literal keeps; past this a longer needle costs storage without shrinking the candidate set much.

  /*! \brief Helpers for \ref real::detail::extract_inner_literal; not part of any interface. */
  namespace inner_literal_detail {

    /*!
     * \brief Extraction state threaded through the walk: the growing byte run, the best literal so far, and
     *        the top-level concat child each run began at (so the winner carries its prefix boundary).
     */
    struct walk_state
    {
      std::vector<std::uint8_t> run;           //!< Literal bytes accumulated since the last \ref flush.
      inner_literal             best;          //!< Best run scored so far; `best.len == 0` until one is kept.
      std::int32_t              run_top  {-1}; //!< Top-level child where the current run began (-1 = nested).
      std::int32_t              best_top {-1}; //!< Top-level child where the winning run began.
    };

    /*!
     * \brief Selectivity of a byte run.
     *
     * The sum of per-byte rarity (`2000 - byte_frequency`). A sum (rather than the rarest byte alone)
     * approximates the product of per-byte match probabilities, so it rewards both a rare byte and a longer
     * literal — the two things that shrink the candidate count.
     * \param[in] run The bytes to score.
     * \return The run's selectivity; higher is rarer and therefore better.
     */
    constexpr std::uint32_t score_run(std::span<const std::uint8_t> run)
    {
      std::uint32_t s {0};
      for (const std::uint8_t b : run) {
        s += static_cast<std::uint32_t>(2000U - byte_frequency(b));
      }
      return s;
    }

    /*!
     * \brief Score the current byte run and keep it (with its prefix boundary) if it beats `best`, then clear
     *        it (capped at \ref inner_literal_max).
     *
     * Prefer a true *inner* run (`run_top >= 1`) over a *head* run (`run_top == 0`) even with a slightly
     * lower score: the head is already filtered by \c extract_prefix / \c find_prefix, and the IL route only
     * fires for `prefix_child_count >= 1`. Among same-kind candidates, higher \ref score_run still wins.
     * \param[in,out] st The walk state whose \ref walk_state::run is scored and then cleared.
     */
    constexpr void flush(walk_state& st)
    {
      if (!st.run.empty()) {
        const std::size_t   len        {st.run.size() < inner_literal_max ? st.run.size() : inner_literal_max};
        const std::uint32_t s          {score_run(std::span<const std::uint8_t>(st.run.data(), len))};
        const bool          new_inner  {st.run_top >= 1};
        const bool          best_inner {st.best_top >= 1};
        const bool          take       {st.best.len == 0
                                        || (new_inner && !best_inner)
                                        || (new_inner == best_inner && s > st.best.score)};
        if (take) {
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

    /*!
     * \brief Whether \p idx is a pure fixed byte run: only `byte` / nested concat / group of the same.
     *
     * Used to decide whether an alternation may be skipped (flush+continue) without appending any branch
     * bytes — every branch is fixed-width literal text, so the reverse-prefix can still represent the alt as
     * a deterministic byte DFA. Anything else (klass, repeat, nested alt, lookaround, …) keeps the
     * conservative decline.
     * \param[in] tree The AST holding the node.
     * \param[in] idx  Node index; a negative index is the empty subtree and counts as pure.
     * \return `true` if every node in the subtree is a fixed byte, a concat of such, or a group of such.
     */
    [[nodiscard]] constexpr bool is_pure_byte_run(const ast&   tree,
                                                  std::int32_t idx) noexcept
    {
      if (idx < 0) {
        return true;
      }
      const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
      switch (n.kind) {
        case node_kind::empty:
        case node_kind::byte:
          return true;
        case node_kind::concat:
          for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
            if (!is_pure_byte_run(tree, c)) {
              return false;
            }
          }
          return true;
        case node_kind::group:
          return is_pure_byte_run(tree, n.child);
        case node_kind::repeat:
        case node_kind::klass:
        case node_kind::any:
        case node_kind::alternation:
        case node_kind::lookaround:
        case node_kind::anchor:
          return false;
      }
      return false;
    }

    /*!
     * \brief Walk one node, appending guaranteed-present literal bytes to \ref walk_state::run.
     *
     * Every byte appended is present in *every* match; the confirming scan then verifies the surrounding
     * context. Pure-literal alternations \ref flush and continue (no branch bytes) so a later unconditional
     * run can still arm. An optional declines the whole extraction, which is a choice and not a
     * requirement: flushing past it would be sound, since bytes after an optional are still required, but
     * it would take `https?://` off its head literal `http`, and a required HEAD is a stronger filter than
     * an inner scan for `://`.
     * \param[in]     tree      The AST holding the node.
     * \param[in]     idx       Node index; a negative index is the empty subtree and succeeds trivially.
     * \param[in,out] st        Walk state the run accumulates into.
     * \param[in]     top_child The top-level concat child index this node belongs to, or -1 when nested in a
     *                          group/repeat — a run starting there has no clean top-level prefix boundary.
     * \return `false` to DECLINE the whole extraction: a non-literal alternation, an optional (`repeat` with
     *         min 0), a lookaround or an anchor would make a required inner literal unsound, since a path
     *         could bypass it.
     */
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
              return false; // ? * {0,n}: DECLINE -- see this function's own doc for why, and why not flush
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
        case node_kind::alternation: {
            // Pure-literal alt (`info|error|warn`) — every branch is fixed bytes, so the alt is a
            // representable reverse-prefix segment. Flush (do not append any branch's bytes: none are
            // shared) and continue. A branch with klass/repeat/nested-alt declines the whole extract.
            for (std::int32_t b = n.child; b >= 0; b = tree.nodes[static_cast<std::size_t>(b)].next) {
              if (!is_pure_byte_run(tree, b)) {
                return false;
              }
            }
            flush(st);
            return true;
          }
        case node_kind::lookaround:
        case node_kind::anchor:
          // Top-level `\b`/`\B` are peeled by extract_inner_literal before the walk; any anchor that
          // still reaches here (mid-body wb, ^, $, nested) would make a required inner literal unsound
          // or a reverse-prefix non-byte — decline.
          return false;
      }
      return false;
    }

    /*!
     * \brief Whether \p idx is a top-level `\b` or `\B` anchor — a peel candidate.
     * \param[in] tree The AST holding the node.
     * \param[in] idx  Node index; a negative index is not an anchor.
     * \return `true` for a word-boundary or not-word-boundary anchor node.
     */
    [[nodiscard]] constexpr bool is_top_wb_anchor(const ast&   tree,
                                                  std::int32_t idx) noexcept
    {
      if (idx < 0) {
        return false;
      }
      const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
      return n.kind == node_kind::anchor
             && (n.anchor == anchor_kind::word_boundary || n.anchor == anchor_kind::not_word_boundary);
    }

    /*!
     * \brief Encodes a peeled word-boundary anchor as the hint value \ref inner_literal::wb_lead and
     *        \ref inner_literal::wb_trail carry.
     * \param[in] k The anchor kind that was peeled.
     * \return 1 for `\b`, 2 for `\B`, 0 for any other anchor (nothing peeled).
     */
    [[nodiscard]] constexpr std::uint8_t wb_hint_from_anchor(anchor_kind k) noexcept
    {
      if (k == anchor_kind::word_boundary) {
        return 1;
      }
      if (k == anchor_kind::not_word_boundary) {
        return 2;
      }
      return 0;
    }
  } // namespace inner_literal_detail

  /*!
   * \brief Extract the best required inner literal from a pattern's AST (a pure function on the node pool).
   *
   * Routed on by `pike_vm::run` in search mode, via its `run_inner_literal`.
   *
   * Leading and trailing top-level `\b`/`\B` are peeled — recorded in \ref inner_literal::wb_lead,
   * \ref inner_literal::wb_trail and \ref inner_literal::prefix_skip — so `\b\w+@\w+\b` keeps the `@`
   * route; a mid-body wb anchor still declines. `confirm_at` on the full program re-checks the boundaries.
   * \param[in] tree The parsed pattern.
   * \return The best required literal, or a default-constructed \ref inner_literal (`len == 0`) when the
   *         pattern declines.
   */
  constexpr inner_literal extract_inner_literal(const ast& tree)
  {
    inner_literal_detail::walk_state st;
    if (tree.root < 0) {
      return inner_literal {};
    }
    const ast_node& root {tree.nodes[static_cast<std::size_t>(tree.root)]};
    if (root.kind == node_kind::concat) {
      // Collect top-level child indices (small; patterns with hundreds of top-level siblings are not IL targets).
      std::vector<std::int32_t> kids;
      for (std::int32_t c = root.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
        kids.push_back(c);
      }
      std::size_t  lo       {0};
      std::size_t  hi       {kids.size()};
      std::uint8_t wb_lead  {0};
      std::uint8_t wb_trail {0};
      // Peel a single optional lead `\b`/`\B` (multiple consecutive wb asserts are declined — rare & ambiguous).
      if (lo < hi && inner_literal_detail::is_top_wb_anchor(tree, kids[lo])) {
        wb_lead = inner_literal_detail::wb_hint_from_anchor(
          tree.nodes[static_cast<std::size_t>(kids[lo])].anchor);
        ++lo;
      }
      if (lo < hi && inner_literal_detail::is_top_wb_anchor(tree, kids[hi - 1])) {
        wb_trail = inner_literal_detail::wb_hint_from_anchor(
          tree.nodes[static_cast<std::size_t>(kids[hi - 1])].anchor);
        --hi;
      }
      // Mid-body wb (or a second lead/trail left after the single peel) → decline.
      for (std::size_t i = lo; i < hi; ++i) {
        if (inner_literal_detail::is_top_wb_anchor(tree, kids[i])) {
          return inner_literal {};
        }
      }
      // Walk the body only; top_child indices are body-local (0 = first body child) so
      // prefix_child_count + prefix_skip rebuild the reverse prefix without the peeled lead wb.
      std::int32_t body_i {0};
      for (std::size_t i = lo; i < hi; ++i, ++body_i) {
        if (!inner_literal_detail::walk(tree, kids[i], st, body_i)) {
          return inner_literal {};
        }
      }
      inner_literal_detail::flush(st);
      st.best.prefix_child_count = st.best_top;
      st.best.prefix_skip        = static_cast<std::int32_t>(lo);
      st.best.wb_lead            = wb_lead;
      st.best.wb_trail           = wb_trail;
      return st.best;
    }
    // Lone top-level node: a bare `\b`/`\B` is not an IL pattern; other roots walk as child 0.
    if (inner_literal_detail::is_top_wb_anchor(tree, tree.root)) {
      return inner_literal {};
    }
    if (!inner_literal_detail::walk(tree, tree.root, st, 0)) {
      return inner_literal {};
    }
    inner_literal_detail::flush(st);
    st.best.prefix_child_count = st.best_top;
    return st.best;
  }

  /*!
   * \brief Build the prefix sub-AST: \p count top-level concat children starting after \p skip lead children.
   *
   * Copies the tree, re-roots the concat chain at the skip-th child, and truncates after \p count body
   * children — later siblings (literal + suffix + trail wb) become unreferenced.
   * \param[in] tree  The pattern whose prefix is wanted.
   * \param[in] count How many body children the prefix keeps; must be >= 1.
   * \param[in] skip  Peeled-lead count (\ref inner_literal::prefix_skip); 0 means nothing was peeled, so
   *                  the prefix is simply the first \p count children.
   * \return A copy of \p tree re-rooted and truncated to that prefix.
   */
  inline ast build_prefix_ast(const ast&   tree,
                              std::int32_t count,
                              std::int32_t skip = 0)
  {
    ast             prefix {tree};
    ast_node&       root   {prefix.nodes[static_cast<std::size_t>(prefix.root)]};
    std::int32_t    c      {root.child};
    for (std::int32_t i = 0; i < skip; ++i) {
      c = prefix.nodes[static_cast<std::size_t>(c)].next;
    }
    root.child = c; // drop peeled lead wb anchors from the prefix root
    for (std::int32_t i = 0; i + 1 < count; ++i) {
      c = prefix.nodes[static_cast<std::size_t>(c)].next;
    }
    prefix.nodes[static_cast<std::size_t>(c)].next = -1; // truncate after the count-th body child
    return prefix;
  }
} // namespace real::detail

#endif // REAL_FRONTEND_INNER_LITERAL_HPP
