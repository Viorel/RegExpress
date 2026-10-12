/*!
 * \file aho_corasick.hpp
 * \brief Aho-Corasick multi-literal engine for large pure-literal alternations.
 *
 * Built lazily per program from the byte/klass ops of a
 * \ref real::detail::pattern_hints::fixed_alternation -shaped program (see prefilter.hpp's
 * `is_fixed_alternation`) once its branch count reaches the threshold where one O(n) automaton walk beats
 * the first-byte scans; under it the pattern stays on its usual route.
 *
 * The trie is built over the program's byte CLASSES (bytes no branch position tells apart share one), so a
 * case-folded branch is one path, in a sparse first-child/next-sibling form sized by the node count. Fail
 * links are computed on it. Where the dense table fits \ref real::detail::ac_memory_budget, states are
 * numbered reporting-first and the table is allocated once at its exact size, with premultiplied ids
 * (index * stride): a step is one class lookup and one dependent load, "does this state report" one
 * compare. Otherwise the sparse trie is searched, the shallowest nodes given dense rows while the budget
 * lasts; past what the trie itself may take, nothing is built and the ordinary alternation route runs.
 *
 * Leftmost-first: earliest start wins, then the FIRST-LISTED branch (smallest id), as REAL's thread-priority
 * alternation does; held to it by a differential (tests/engine/test_fastpath_seam_matrix.cpp,
 * seam_run_aho_corasick). Storage is std::vector throughout, no raw new/delete.
 */
#ifndef REAL_AHO_CORASICK_HPP
#define REAL_AHO_CORASICK_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include "real/automata/lazy_dfa.hpp"
#include "real/core/charclass.hpp"
#include "real/core/program.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace real::detail {

  //! Bytes an automaton may hold by default: every program within \ref max_program_size whose alphabet is
  //! under 32 classes fits the dense table.
  inline constexpr std::size_t ac_memory_budget_default {std::size_t {32} << 20U};

  /*!
   * \brief Bytes an automaton may hold (the layout rule is in the file header); \ref ac_memory_budget_default
   *        unless a test shrinks it to reach the sparse form and the decline.
   * \return A reference to the process-wide budget.
   */
  inline std::size_t& ac_memory_budget()
  {
    static std::size_t budget {ac_memory_budget_default};
    return budget;
  }

  /*!
   * \brief Test seam: search the sparse trie even where the dense table fits.
   * \return A reference to the process-wide flag.
   */
  inline bool& ac_dense_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: at most this many dense rows in the sparse form below what the budget allows. The
   *        root keeps its row whatever the cap: a miss there has no fail link to fall along.
   * \return A reference to the process-wide cap; unbounded by default (the budget decides).
   */
  inline std::size_t& ac_sparse_row_cap()
  {
    static std::size_t cap {std::numeric_limits<std::size_t>::max()};
    return cap;
  }

  /*!
   * \brief Aho-Corasick automaton for a `fixed_alternation` program's branch set, built once per compiled
   *        program and reused across every match on it.
   */
  class ac_automaton
  {
  public:

    /*!
     * \brief Starts an empty trie over \p class_count byte classes.
     * \param[in] byte_class  The class of each byte.
     * \param[in] class_count How many classes there are: a dense row's width.
     */
    constexpr ac_automaton(const std::array<std::uint8_t, 256>& byte_class,
                           std::uint32_t                        class_count)
      : cls_ {byte_class}, stride_ {class_count}
    {
      trie_.push_back(trie_node {});
    }

    /*!
     * \brief Adds one class sequence of branch \p id. Several calls with one \p id are expected: a branch
     *        whose positions span classes the others tell apart expands into each, all sharing its id, and
     *        the smallest-id tie-break then reads every one as that branch.
     * \param[in] classes The sequence.
     * \param[in] id      The branch's declaration order.
     * \return False once the sparse trie would pass \ref ac_memory_budget -- the automaton is then not built.
     */
    bool add_literal(std::span<const std::uint8_t> classes,
                     std::int32_t                  id)
    {
      const std::size_t node_cap {ac_memory_budget() / sizeof(trie_node)};
      std::int32_t      state    {0};
      for (const std::uint8_t c : classes) {
        std::int32_t child {sparse_child(state, c)};
        if (child == -1) {
          if (trie_.size() >= node_cap) {
            return false;
          }
          child = static_cast<std::int32_t>(trie_.size());
          trie_node n {};
          n.cls          = c;
          n.next_sibling = trie_[static_cast<std::size_t>(state)].first_child;
          trie_.push_back(n);
          trie_[static_cast<std::size_t>(state)].first_child = child;
        }
        state = child;
      }
      trie_node& end {trie_[static_cast<std::size_t>(state)]};
      if (end.pattern_id == -1 || id < end.pattern_id) {
        end.pattern_id  = id;
        end.pattern_len = static_cast<std::int32_t>(classes.size());
      }
      max_pattern_len_ = std::max(max_pattern_len_, static_cast<std::int32_t>(classes.size()));
      return true;
    }

    /*!
     * \brief Computes the fail and output links on the sparse trie, then lays the automaton out, dense or
     *        sparse by \ref ac_memory_budget.
     */
    void build()
    {
      const std::size_t n {trie_.size()};
      trie_.shrink_to_fit();
      // BFS order and fail links on the sparse trie.
      std::vector<std::int32_t> order;
      order.reserve(n);
      order.push_back(0);
      trie_[0].fail = 0;
      for (std::size_t head {0}; head < order.size(); ++head) {
        const std::int32_t s {order[head]};
        for (std::int32_t u {trie_[static_cast<std::size_t>(s)].first_child}; u != -1;
             u = trie_[static_cast<std::size_t>(u)].next_sibling) {
          order.push_back(u);
          const std::uint8_t c {trie_[static_cast<std::size_t>(u)].cls};
          std::int32_t       f {0};
          if (s != 0) {
            f = trie_[static_cast<std::size_t>(s)].fail;
            while (true) {
              const std::int32_t g {sparse_child(f, c)};
              if (g != -1) {
                f = g;
                break;
              }
              if (f == 0) {
                break;
              }
              f = trie_[static_cast<std::size_t>(f)].fail;
            }
          }
          trie_node& un       {trie_[static_cast<std::size_t>(u)]};
          un.fail = f;
          const trie_node& fn {trie_[static_cast<std::size_t>(f)]};
          un.output_link = (fn.pattern_id != -1) ? f : fn.output_link;
        }
      }
      // The dense table and the three per-reporting-state arrays, at most one entry per node each.
      const std::size_t row_bytes {stride_ * sizeof(std::int32_t)};
      if (ac_dense_disabled() || (n * (row_bytes + (3U * sizeof(std::int32_t)))) > ac_memory_budget()) {
        sparse_ = true;
        // A miss falls along the fail chain toward the root, so rows go to the SHALLOWEST nodes first (BFS).
        const std::size_t sparse_bytes {n * sizeof(trie_node)};
        std::size_t       rows_left    {ac_memory_budget() > sparse_bytes ? (ac_memory_budget() - sparse_bytes) / row_bytes : 0U};
        rows_left = std::min(rows_left, ac_sparse_row_cap());
        std::size_t rows               {0};
        for (const std::int32_t u : order) {
          const bool want {trie_[static_cast<std::size_t>(u)].first_child != -1 && rows_left != 0U};
          if (u == 0 || want) {
            trie_[static_cast<std::size_t>(u)].row = static_cast<std::int32_t>(rows++);
            rows_left                             -= rows_left != 0U ? 1U : 0U;
          }
        }
        rows_.assign(rows * stride_, 0);
        for (const std::int32_t u : order) { // BFS: a fail target's row is total before it is read
          const trie_node& t {trie_[static_cast<std::size_t>(u)]};
          if (t.row < 0) {
            continue;
          }
          std::int32_t* row {rows_.data() + (static_cast<std::size_t>(t.row) * stride_)};
          for (std::uint32_t c {0}; c < stride_; ++c) {
            row[c] = u == 0 ? 0 : sparse_next(t.fail, static_cast<std::uint8_t>(c));
          }
          for (std::int32_t c {t.first_child}; c != -1; c = trie_[static_cast<std::size_t>(c)].next_sibling) {
            row[trie_[static_cast<std::size_t>(c)].cls] = c;
          }
        }
        node_count_ = n;
        return;
      }
      // Numbering: every state that reports something first, so the search's test is `id < match_limit_`.
      std::vector<std::int32_t> index(n, -1);
      std::int32_t              next {0};
      for (const std::int32_t u : order) {
        const trie_node& t {trie_[static_cast<std::size_t>(u)]};
        if (t.pattern_id != -1 || t.output_link != -1) {
          index[static_cast<std::size_t>(u)] = next++;
        }
      }
      const std::int32_t matches {next};
      for (const std::int32_t u : order) {
        if (index[static_cast<std::size_t>(u)] == -1) {
          index[static_cast<std::size_t>(u)] = next++;
        }
      }
      const auto stride {static_cast<std::int32_t>(stride_)};
      match_limit_ = matches * stride;
      start_       = index[0] * stride;
      out_pid_.assign(static_cast<std::size_t>(matches), -1);
      out_len_.assign(static_cast<std::size_t>(matches), 0);
      out_link_.assign(static_cast<std::size_t>(matches), -1);
      trans_.assign(n * stride_, 0); // exact: allocated once, at its final size
      for (const std::int32_t u : order) {
        const trie_node&   t   {trie_[static_cast<std::size_t>(u)]};
        const std::int32_t id  {index[static_cast<std::size_t>(u)]};
        std::int32_t*      row {trans_.data() + (static_cast<std::size_t>(id) * stride_)};
        if (u == 0) {
          std::fill(row, row + stride_, start_);
        }
        else {
          const std::int32_t* frow {trans_.data() + (static_cast<std::size_t>(index[static_cast<std::size_t>(t.fail)]) * stride_)};
          std::memcpy(row, frow, stride_ * sizeof(std::int32_t));
        }
        for (std::int32_t c {t.first_child}; c != -1; c = trie_[static_cast<std::size_t>(c)].next_sibling) {
          row[trie_[static_cast<std::size_t>(c)].cls] = index[static_cast<std::size_t>(c)] * stride;
        }
        if (id < matches) {
          out_pid_[static_cast<std::size_t>(id)]  = t.pattern_id;
          out_len_[static_cast<std::size_t>(id)]  = t.pattern_len;
          out_link_[static_cast<std::size_t>(id)] = t.output_link == -1 ? -1 : index[static_cast<std::size_t>(t.output_link)];
        }
      }
      node_count_ = n;
      std::vector<trie_node> {}.swap(trie_);
    }

    /*!
     * \brief One search's answer.
     */
    struct match_result
    {
      bool         matched    {}; //!< Whether some branch matched.
      std::size_t  start      {}; //!< The match's start.
      std::size_t  end        {}; //!< Its end.
      std::int32_t pattern_id {}; //!< The branch that matched.
    };

    /*!
     * \brief The leftmost-first match at or after \p start: earliest start, then smallest branch id.
     * \tparam WbOk Callable `(start, end) -> bool`: the pattern's lead/trail word-boundary test.
     * \param[in] text  The subject.
     * \param[in] start Where to search from.
     * \param[in] wb_ok The boundary test; a candidate it refuses is passed over.
     * \return The match, if any.
     */
    template <typename WbOk>
    [[nodiscard]] match_result search(std::string_view text,
                                      std::size_t      start,
                                      const WbOk&      wb_ok) const
    {
      return sparse_ ? search_sparse(text, start, wb_ok) : search_dense(text, start, wb_ok);
    }

    /*!
     * \brief The trie's node count.
     * \return The number of states.
     */
    [[nodiscard]] std::size_t node_count() const
    {
      return node_count_;
    }

    /*!
     * \brief Whether the automaton searches its sparse trie (the dense table did not fit, or was disabled).
     * \return True in the sparse form.
     */
    [[nodiscard]] bool is_sparse() const
    {
      return sparse_;
    }

    /*!
     * \brief Dense rows the sparse form was given.
     * \return The row count; 0 in the dense form.
     */
    [[nodiscard]] std::size_t sparse_rows() const
    {
      return rows_.size() / stride_;
    }

    /*!
     * \brief Heap bytes the automaton holds once built.
     * \return The bytes.
     */
    [[nodiscard]] std::size_t memory_bytes() const
    {
      return (trans_.capacity() + out_pid_.capacity() + out_len_.capacity() + out_link_.capacity()
              + rows_.capacity()) * sizeof(std::int32_t)
             + trie_.capacity() * sizeof(trie_node);
    }

  private:

    /*!
     * \brief One trie node: while the automaton is built, and searched in the sparse form after.
     */
    struct trie_node
    {
      std::int32_t first_child  {-1}; //!< First child, or -1.
      std::int32_t next_sibling {-1}; //!< Next child of the same parent, or -1.
      std::int32_t fail         {0};  //!< The longest proper suffix that is also a trie prefix.
      std::int32_t output_link  {-1}; //!< The next reporting node on the fail chain, or -1.
      std::int32_t pattern_id   {-1}; //!< The smallest branch id ending here, or -1.
      std::int32_t pattern_len  {0};  //!< The length of the sequence ending here.
      std::int32_t row          {-1}; //!< Sparse form: this node's total dense row, or -1.
      std::uint8_t cls          {0};  //!< The class on the edge into this node.
    };

    /*!
     * \brief Sparse form: the total transition from \p s on class \p c -- the node's dense row where it has
     *        one, else its children, else along the fail chain (which ends at the root, which has a row).
     * \param[in] s The state.
     * \param[in] c The class.
     * \return The next state.
     */
    [[nodiscard]] std::int32_t sparse_next(std::int32_t s,
                                           std::uint8_t c) const
    {
      while (true) {
        const trie_node& t {trie_[static_cast<std::size_t>(s)]};
        if (t.row >= 0) {
          return rows_[(static_cast<std::size_t>(t.row) * stride_) + c];
        }
        const std::int32_t child {sparse_child(s, c)};
        if (child != -1) {
          return child;
        }
        s = t.fail;
      }
    }

    /*!
     * \brief The child of \p s on class \p c.
     * \param[in] s The node.
     * \param[in] c The class.
     * \return The child, or -1.
     */
    [[nodiscard]] std::int32_t sparse_child(std::int32_t s,
                                            std::uint8_t c) const
    {
      for (std::int32_t u {trie_[static_cast<std::size_t>(s)].first_child}; u != -1;
           u = trie_[static_cast<std::size_t>(u)].next_sibling) {
        if (trie_[static_cast<std::size_t>(u)].cls == c) {
          return u;
        }
      }
      return -1;
    }

    /*!
     * \brief The best match so far: earliest start, then smallest branch id.
     */
    struct best_so_far
    {
      std::int64_t start {-1}; //!< Its start, or -1 before any.
      std::int32_t id    {-1}; //!< Its branch.
      std::int32_t len   {0};  //!< Its length.

      /*!
       * \brief Keeps the match ending at \p end_pos if it beats the one held.
       * \param[in] end_pos     Its last byte's position.
       * \param[in] pattern_id  Its branch.
       * \param[in] pattern_len Its length.
       */
      void consider(std::int64_t end_pos,
                    std::int32_t pattern_id,
                    std::int32_t pattern_len)
      {
        const std::int64_t start_pos {end_pos - pattern_len + 1};
        if (start == -1 || start_pos < start || (start_pos == start && pattern_id < id)) {
          start = start_pos;
          id    = pattern_id;
          len   = pattern_len;
        }
      }
    };

    /*!
     * \brief Dense form: offers \p best the longest match ending at \p i that \p wb_ok passes, down the
     *        output chain from \p state (longest first, so the first that passes starts earliest).
     * \tparam WbOk As \ref search.
     * \param[in]     state The reporting state reached at \p i (premultiplied).
     * \param[in]     i     The position of the byte just consumed.
     * \param[in,out] best  The best match so far.
     * \param[in]     wb_ok The boundary test.
     */
    template <typename WbOk>
    void report_dense(std::int32_t      state,
                      std::size_t       i,
                      best_so_far&      best,
                      const WbOk&       wb_ok) const
    {
      std::int32_t k {state / static_cast<std::int32_t>(stride_)};
      while (k != -1) {
        const std::int32_t pid {out_pid_[static_cast<std::size_t>(k)]};
        if (pid != -1) {
          const std::int32_t len {out_len_[static_cast<std::size_t>(k)]};
          const std::size_t  s   {i + 1U - static_cast<std::size_t>(len)};
          if (wb_ok(s, i + 1U)) {
            best.consider(static_cast<std::int64_t>(i), pid, len);
            return;
          }
        }
        k = out_link_[static_cast<std::size_t>(k)];
      }
    }

    /*!
     * \brief Dense form of \ref search.
     * \tparam WbOk As \ref search.
     * \param[in] text  The subject.
     * \param[in] start Where to search from.
     * \param[in] wb_ok The boundary test.
     * \return The match, if any.
     */
    template <typename WbOk>
    [[nodiscard]] match_result search_dense(std::string_view text,
                                            std::size_t      start,
                                            const WbOk&      wb_ok) const
    {
      const char* const          p     {text.data()};
      const std::size_t          sz    {text.size()};
      const std::int32_t* const  t     {trans_.data()};
      const std::uint8_t* const  cls   {cls_.data()};
      const std::int32_t         limit {match_limit_};
      std::int32_t               state {start_};
      best_so_far                best  {};
      std::size_t                i     {start};
      while (true) {
        for (; i < sz; ++i) {
          state = t[state + cls[static_cast<std::uint8_t>(p[i])]];
          if (state < limit) {
            break;
          }
        }
        if (i >= sz) {
          break;
        }
        report_dense(state, i, best, wb_ok);
        ++i;
        if (best.start != -1) {
          // Leftmost-first: a later match can still start earlier only while it can reach back to best.start.
          while (i < sz && static_cast<std::int64_t>(i) < best.start + max_pattern_len_) {
            state = t[state + cls[static_cast<std::uint8_t>(p[i])]];
            if (state < limit) {
              report_dense(state, i, best, wb_ok);
            }
            ++i;
          }
          break;
        }
      }
      if (best.start == -1) {
        return {};
      }
      return {.matched    = true,
              .start      = static_cast<std::size_t>(best.start),
              .end        = static_cast<std::size_t>(best.start + best.len),
              .pattern_id = best.id};
    }

    /*!
     * \brief Sparse form of \ref report_dense.
     * \tparam WbOk As \ref search.
     * \param[in]     node  The node reached at \p i.
     * \param[in]     i     The position of the byte just consumed.
     * \param[in,out] best  The best match so far.
     * \param[in]     wb_ok The boundary test.
     */
    template <typename WbOk>
    void report_sparse(std::int32_t node,
                       std::size_t  i,
                       best_so_far& best,
                       const WbOk&  wb_ok) const
    {
      std::int32_t k {trie_[static_cast<std::size_t>(node)].pattern_id != -1 ? node : trie_[static_cast<std::size_t>(node)].output_link};
      while (k != -1) {
        const trie_node&   tn  {trie_[static_cast<std::size_t>(k)]};
        const std::size_t  s   {i + 1U - static_cast<std::size_t>(tn.pattern_len)};
        if (wb_ok(s, i + 1U)) {
          best.consider(static_cast<std::int64_t>(i), tn.pattern_id, tn.pattern_len);
          return;
        }
        k = tn.output_link;
      }
    }

    /*!
     * \brief Sparse form of \ref search.
     * \tparam WbOk As \ref search.
     * \param[in] text  The subject.
     * \param[in] start Where to search from.
     * \param[in] wb_ok The boundary test.
     * \return The match, if any.
     */
    template <typename WbOk>
    [[nodiscard]] match_result search_sparse(std::string_view text,
                                             std::size_t      start,
                                             const WbOk&      wb_ok) const
    {
      const char* const p     {text.data()};
      const std::size_t sz    {text.size()};
      std::int32_t      state {0};
      best_so_far       best  {};
      for (std::size_t i {start}; i < sz; ++i) {
        state = sparse_next(state, cls_[static_cast<std::uint8_t>(p[i])]);
        const trie_node& tn {trie_[static_cast<std::size_t>(state)]};
        if (tn.pattern_id != -1 || tn.output_link != -1) {
          report_sparse(state, i, best, wb_ok);
        }
        if (best.start != -1 && best.start < static_cast<std::int64_t>(i) - max_pattern_len_ + 1) {
          break;
        }
      }
      if (best.start == -1) {
        return {};
      }
      return {.matched    = true,
              .start      = static_cast<std::size_t>(best.start),
              .end        = static_cast<std::size_t>(best.start + best.len),
              .pattern_id = best.id};
    }

    std::array<std::uint8_t, 256> cls_             {};      //!< Byte to class.
    std::uint32_t                 stride_          {1};     //!< The class count: a dense row's width.
    std::vector<std::int32_t>     trans_;                   //!< Dense form: `[state + class]` to the next state, premultiplied.
    std::int32_t                  match_limit_     {0};     //!< Dense form: states below it report a match.
    std::int32_t                  start_           {0};     //!< Dense form: the root's id.
    std::vector<std::int32_t>     out_pid_;                 //!< Dense form: a reporting state's branch, or -1.
    std::vector<std::int32_t>     out_len_;                 //!< Dense form: its length.
    std::vector<std::int32_t>     out_link_;                //!< Dense form: the next reporting state on its fail chain, or -1.
    std::vector<trie_node>        trie_;                    //!< The trie: while building, and searched in the sparse form.
    std::vector<std::int32_t>     rows_;                    //!< Sparse form: the dense rows granted, `[row * stride + class]`.
    std::size_t                   node_count_      {0};     //!< The trie's node count.
    std::int32_t                  max_pattern_len_ {1};     //!< The longest sequence: how far back a match can start.
    bool                          sparse_          {false}; //!< Searched through the trie.
  };

  /*!
   * \brief Maximum class sequences one branch may expand into (one per combination of the classes its
   *        positions span); past this the WHOLE pattern takes the ordinary
   *        \ref pattern_hints::fixed_alternation route. A case-folded letter is one class unless another
   *        branch tells its cases apart.
   */
  inline constexpr std::size_t ac_max_branch_expansion = 64;

  /*!
   * \brief Builds an \ref ac_automaton from a `fixed_alternation`-shaped program's branch set, over the
   *        program's own byte classes (every class it tests is a union of them, so no position is split).
   *
   * Trusts the shape `is_fixed_alternation` validated: every branch is a run of `byte`/`klass` ops ended by
   * a `jump` or, for the last, falling through.
   *
   * \param[in] code    The program's instruction stream.
   * \param[in] classes Its class table.
   * \param[in] body_pc The first branch/split pc (\ref pattern_hints::body_pc).
   * \return The automaton, or `std::nullopt` when a branch expands past \ref ac_max_branch_expansion or the
   *         trie past \ref ac_memory_budget -- the caller then takes the ordinary alternation route.
   */
  [[nodiscard]]
  REAL_BUILD_COLD // construction-only (once per program): out of the hot neighbourhood
  inline std::optional<ac_automaton> build_ac_automaton(std::span<const instr>       code,
                                                        std::span<const char_class>  classes,
                                                        std::size_t                  body_pc)
  {
    const lazy_byte_alphabet               alpha     {compute_lazy_alphabet(code, classes)};
    ac_automaton                           automaton {alpha.of, alpha.count};
    std::size_t                            pc        {body_pc};
    std::int32_t                           id        {};
    std::vector<std::vector<std::uint8_t>> positions;
    std::vector<std::vector<std::uint8_t>> expansions;
    std::vector<std::vector<std::uint8_t>> next;
    while (true) {
      const bool  is_split  {code[pc].op == opcode::split};
      std::size_t branch_pc {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};

      positions.clear();
      while (code[branch_pc].op == opcode::byte || code[branch_pc].op == opcode::klass) {
        std::vector<std::uint8_t> members;
        if (code[branch_pc].op == opcode::byte) {
          members.push_back(alpha.of[code[branch_pc].arg8]);
        }
        else {
          const char_class&     kc   {classes[code[branch_pc].arg16]};
          std::array<bool, 256> seen {};
          for (int b {}; b <= 255; ++b) {
            if (kc.test(static_cast<std::uint8_t>(b))) {
              const std::uint8_t c {alpha.of[static_cast<std::size_t>(b)]};
              if (!seen[c]) {
                seen[c] = true;
                members.push_back(c);
              }
            }
          }
        }
        positions.push_back(std::move(members));
        ++branch_pc;
      }

      expansions.assign(1, {});
      for (const auto& members : positions) {
        if (expansions.size() * members.size() > ac_max_branch_expansion) {
          return std::nullopt;
        }
        next.clear();
        for (const auto& prefix : expansions) {
          for (const auto member : members) {
            auto copy {prefix};
            copy.push_back(member);
            next.push_back(std::move(copy));
          }
        }
        expansions.swap(next);
      }
      for (const auto& literal : expansions) {
        if (!automaton.add_literal(literal, id)) {
          return std::nullopt;
        }
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
