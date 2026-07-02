/*!
 * \file dfa.hpp
 * \brief `real::dfa` — a maximal-munch DFA over a set of patterns (opt-in).
 *
 * A lexer matches many rules at every position; running each rule's Pike VM in
 * turn is linear but re-scans the input once per candidate rule. `real::dfa`
 * fuses a set of patterns into one deterministic automaton that recognizes the
 * winning rule in a single left-to-right pass — the same maximal-munch decision
 * (longest match; ties to the earliest rule), reached far faster when many rules
 * share leading bytes. It is built from the patterns' compiled programs, runs at
 * run time (the tables are heap-allocated once and then immutable), and is the
 * accelerated rule-dispatch path SciLex opts into.
 *
 * Scope: a pattern is DFA-able iff its program holds no zero-width assertion other
 * than a leading `\A`/`^` (a no-op under anchored scanning). A pattern with any
 * other assertion (`$`, `\b`, multiline `^`/`$`, …) is **not** representable as a
 * pure DFA; the constructor throws \ref real::dfa_error rather than silently
 * mis-recognizing — the caller keeps such rules on the Pike VM. Lazy/greedy makes
 * no difference to a DFA: it recognizes the pattern's *language* and takes the
 * longest match, which is the lexer's munch for greedy rules but **not** for lazy
 * ones (whose `match()` is the shortest) — so the caller must only feed DFA-faithful
 * (greedy, assertion-free) rules. Include this header explicitly; `real.hpp` does not.
 */
#ifndef REAL_DFA_HPP
#define REAL_DFA_HPP

#include "version.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "real.hpp"

namespace real {

  /*!
   * \brief Thrown when a pattern cannot be represented as a DFA.
   *
   * The cause is always a zero-width assertion other than a leading `\A`/`^`
   * (e.g. `$`, `\b`, `\B`, multiline anchors). `real::dfa` never falls back
   * silently — a violated contract is an error the caller handles (e.g. by
   * keeping that rule on the Pike VM).
   */
  class dfa_error : public std::runtime_error
  {
  public:

    /*!
     * \brief Builds the error.
     * \param[in] message Human-readable cause.
     */
    explicit dfa_error(const std::string& message)
      : std::runtime_error(message)
    {}
  };

  /*!
   * \brief The outcome of \ref dfa::match — which rule won, and how many bytes it spans.
   */
  struct dfa_match
  {
    std::uint32_t rule_index; //!< Index of the winning pattern, in the order passed to the ctor.
    std::size_t   length;     //!< Byte length of the (non-empty) match.
  };

  namespace detail {

    //! \brief A flattened NFA instruction (global PCs, global class index).
    struct dfa_instr
    {
      opcode        op        {};
      std::uint8_t  arg8      {};
      std::uint32_t klass     {0};  //!< Global class index (op == klass).
      std::int64_t  primary   {-1}; //!< Global target (split/jump).
      std::int64_t  secondary {-1}; //!< Global secondary target (split).
    };

    //! \brief The union NFA over all the patterns, flattened into one address space.
    struct dfa_nfa
    {
      std::vector<dfa_instr>    code;
      std::vector<char_class>   classes;
      std::vector<std::int64_t> accept_rule; //!< accept_rule[pc] = rule index if match, else -1.
      std::vector<std::size_t>  entry;       //!< entry[rule] = global pc of its pc 0.
      std::size_t               rule_count {0};
    };

    /*!
     * \brief Flattens \p programs into one union NFA, auditing DFA-ability.
     * \throws real::dfa_error if any program has an assertion other than a head text_start.
     */
    inline dfa_nfa dfa_flatten(std::span<const program_view> programs)
    {
      dfa_nfa nfa;
      nfa.rule_count = programs.size();
      for (std::size_t r = 0; r < programs.size(); ++r) {
        const program_view& prog  {programs[r]};
        const std::size_t   base  {nfa.code.size()};
        const std::size_t   cbase {nfa.classes.size()};
        nfa.entry.push_back(base);
        for (const char_class& cls : prog.classes) {
          nfa.classes.push_back(cls);
        }
        for (std::size_t i = 0; i < prog.code.size(); ++i) {
          const instr& in  {prog.code[i]};
          dfa_instr    out {.op = in.op, .arg8 = in.arg8};
          if (in.op == opcode::klass) {
            out.klass = static_cast<std::uint32_t>(cbase + in.arg16);
          }
          else if (in.op == opcode::split) {
            out.primary   = in.primary_target + static_cast<std::int64_t>(base);
            out.secondary = in.secondary_target + static_cast<std::int64_t>(base);
          }
          else if (in.op == opcode::jump) {
            out.primary = in.primary_target + static_cast<std::int64_t>(base);
          }
          else if (in.op == opcode::assert_position
                   && in.arg8 != static_cast<std::uint8_t>(assert_kind::text_start)) {
            // text_start (`\A`/`^`) is handled as a conditional ε in the closure (true
            // at the cursor, false after any byte — exactly its anchored meaning). Any
            // other assertion ($, \b, \B, multiline ^/$, …) cannot be a pure DFA.
            throw dfa_error("pattern has a zero-width assertion that no DFA can represent "
                            "(only \\A/^ is allowed)");
          }
          else if (in.op == opcode::assert_lookaround) {
            throw dfa_error("pattern has a lookaround, which no DFA can represent");
          }
          else if (in.op == opcode::klass_cp) {
            // A code-point predicate matches whole code points via a decode + range search, not a
            // byte transition, so it has no place in a byte-transition DFA.
            throw dfa_error("pattern has a Unicode code-point class (\\w/\\d/\\s in text mode), "
                            "which no DFA can represent");
          }
          nfa.code.push_back(out);
          nfa.accept_rule.push_back(in.op == opcode::match ? static_cast<std::int64_t>(r) : -1);
        }
      }
      return nfa;
    }

    //! \brief A set of NFA PCs as a bitset (one per DFA state during construction).
    using dfa_set = std::vector<std::uint64_t>;

    inline void dfa_set_bit(dfa_set&    s,
                            std::size_t i)
    {
      const std::size_t word {i >> 6U};
      if (word < s.size()) { // defensive bound, mirroring dfa_test_bit (the set is sized to fit)
        s[word] |= (std::uint64_t {1} << (i & 63U));
      }
    }

    inline bool dfa_test_bit(const dfa_set& s,
                             std::size_t    i)
    {
      const std::size_t word {i >> 6U};
      return word < s.size() && ((s[word] >> (i & 63U)) & 1U) != 0; // beyond the set ⇒ absent
    }

    //! \brief The epsilon-closure of \p seeds (a PC list), as a canonical PC bitset.
    //!        \p at_start follows a text_start assertion (true only at offset 0).
    inline dfa_set dfa_closure(const dfa_nfa&                    nfa,
                               const std::vector<std::uint32_t>& seeds,
                               bool                              at_start)
    {
      const std::size_t          words {(nfa.code.size() + 63U) / 64U};
      dfa_set                    present(words, 0);
      std::vector<std::uint32_t> stack;
      for (const std::uint32_t pc : seeds) {
        if (!dfa_test_bit(present, pc)) {
          dfa_set_bit(present, pc);
          stack.push_back(pc);
        }
      }
      while (!stack.empty()) {
        const std::uint32_t pc {stack.back()};
        stack.pop_back();
        const dfa_instr& in {nfa.code[pc]};
        const auto       visit {[&](std::int64_t target) {
                                  if (target >= 0 && !dfa_test_bit(present, static_cast<std::size_t>(target))) {
                                    dfa_set_bit(present, static_cast<std::size_t>(target));
                                    stack.push_back(static_cast<std::uint32_t>(target));
                                  }
                                }};
        switch (in.op) {
          case opcode::split: visit(in.primary); visit(in.secondary); break;
          case opcode::jump: visit(in.primary); break;
          case opcode::save: visit(static_cast<std::int64_t>(pc) + 1); break;
          case opcode::assert_position:
            if (at_start) { visit(static_cast<std::int64_t>(pc) + 1); } // text_start: ε only at offset 0
            break;
          case opcode::byte:
          case opcode::klass:
          case opcode::klass_cp:
          case opcode::match:
          case opcode::assert_lookaround: break; // terminal / unreachable (dfa_flatten rejects lookaround & klass_cp)
        }
      }
      return present;
    }

    //! \brief The move on the byte \p rep: ε-closure of the successors of every PC
    //!        in \p set that consumes \p rep.
    inline dfa_set dfa_move(const dfa_nfa& nfa,
                            const dfa_set& set,
                            std::uint8_t   rep)
    {
      std::vector<std::uint32_t> seeds;
      for (std::size_t pc = 0; pc < nfa.code.size(); ++pc) {
        if (!dfa_test_bit(set, pc)) {
          continue;
        }
        const dfa_instr& in      {nfa.code[pc]};
        const bool       consume {(in.op == opcode::byte && in.arg8 == rep)
                                  || (in.op == opcode::klass && nfa.classes[in.klass].test(rep))};
        if (consume) {
          seeds.push_back(static_cast<std::uint32_t>(pc + 1));
        }
      }
      return dfa_closure(nfa, seeds, false); // post-consumption: text_start is false here
    }

    //! \brief The accepting rule of a state set: the SMALLEST rule index among its
    //!        match PCs (the order tie-break), or -1 if none accept.
    inline std::int64_t dfa_accept_of(const dfa_nfa& nfa,
                                      const dfa_set& set)
    {
      std::int64_t best {-1};
      for (std::size_t pc = 0; pc < nfa.code.size(); ++pc) {
        if (dfa_test_bit(set, pc) && nfa.accept_rule[pc] >= 0
            && (best < 0 || nfa.accept_rule[pc] < best)) {
          best = nfa.accept_rule[pc];
        }
      }
      return best;
    }

    //! \brief Computes byte-equivalence classes: two bytes are equivalent iff they
    //!        satisfy the same consuming predicates (every klass test and every byte
    //!        literal). Reduces the alphabet so the DFA is built over classes, not 256.
    struct dfa_byte_classes
    {
      std::array<std::uint8_t, 256> of    {};
      std::array<std::uint8_t, 256> rep   {};
      std::size_t                   count {0};
    };

    inline dfa_byte_classes dfa_compute_classes(const dfa_nfa& nfa)
    {
      // Predicates as VALUES (the char_class itself, and byte literals), deduped —
      // self-contained, so the signature loop indexes only its own vectors (no
      // cross-vector nfa.classes[idx] the analyzer cannot prove in bounds).
      std::vector<char_class>    class_preds;
      std::vector<std::uint16_t> literal_preds;
      const auto                 push_unique {[](auto& vec, const auto& value) {
                                                for (std::size_t i = 0; i < vec.size(); ++i) {
                                                  if (vec[i] == value) { return; }
                                                }
                                                vec.push_back(value);
                                              }};
      for (std::size_t pc = 0; pc < nfa.code.size(); ++pc) {
        const dfa_instr& in {nfa.code[pc]};
        if (in.op == opcode::klass && in.klass < nfa.classes.size()) {
          push_unique(class_preds, nfa.classes[in.klass]);
        }
        else if (in.op == opcode::byte) {
          push_unique(literal_preds, static_cast<std::uint16_t>(in.arg8));
        }
      }
      const auto sig_equal {[&](unsigned a, unsigned b) {
                              for (std::size_t i = 0; i < class_preds.size(); ++i) {
                                if (class_preds[i].test(static_cast<std::uint8_t>(a))
                                    != class_preds[i].test(static_cast<std::uint8_t>(b))) {
                                  return false;
                                }
                              }
                              for (std::size_t i = 0; i < literal_preds.size(); ++i) {
                                const unsigned lit {literal_preds[i]};
                                if ((a == lit) != (b == lit)) { return false; }
                              }
                              return true;
                            }};
      dfa_byte_classes bc;
      for (unsigned b = 0; b < 256U; ++b) {
        bool assigned {false};
        for (std::size_t c = 0; c < bc.count; ++c) {
          if (sig_equal(b, bc.rep[c])) {
            bc.of[b] = static_cast<std::uint8_t>(c);
            assigned = true;
            break;
          }
        }
        if (!assigned) {
          bc.rep[bc.count] = static_cast<std::uint8_t>(b);
          bc.of[b]         = static_cast<std::uint8_t>(bc.count);
          ++bc.count;
        }
      }
      return bc;
    }

    //! \brief The baked DFA tables produced by \ref dfa_build.
    struct dfa_tables
    {
      std::array<std::uint8_t, 256> byte_class  {};
      std::size_t                   num_classes {0};
      std::vector<std::uint32_t>    trans;   //!< [state*num_classes + cls] -> next state (0 = dead).
      std::vector<std::uint32_t>    accept;  //!< accept[state] = rule index, or NO_RULE.
      std::uint32_t                 start      {0};
      std::size_t                   num_states {0};
      std::size_t                   rule_count {0};
    };

    inline constexpr std::uint32_t dfa_no_rule {std::numeric_limits<std::uint32_t>::max()};

    //! \brief Subset construction over byte-classes, then Moore minimization
    //!        (initial partition by accept tag, so distinct rule tags never merge).
    //! \param[in] programs  The flattened NFA programs.
    //! \param[in] state_cap Maximum DFA states before \ref dfa_error (a test hook; the
    //!                      default is the production cap \ref max_dfa_states).
    inline dfa_tables dfa_build(std::span<const program_view> programs,
                                std::size_t                   state_cap = max_dfa_states)
    {
      const dfa_nfa          nfa {dfa_flatten(programs)};
      const dfa_byte_classes bc  {dfa_compute_classes(nfa)};
      const std::size_t      nc  {bc.count};
      dfa_tables             out;
      out.byte_class  = bc.of;
      out.num_classes = nc;
      out.rule_count  = nfa.rule_count;

      std::vector<dfa_set>       sets;       // sets[s] = the NFA state set of DFA state s
      std::vector<std::int64_t>  accept_pre; // accept of each pre-min state
      const auto                 find_or_add {[&](dfa_set s) -> std::uint32_t { // by value: a definite object
                                                for (std::size_t i = 0; i < sets.size(); ++i) {
                                                  if (sets[i] == s) { return static_cast<std::uint32_t>(i); }
                                                }
                                                // False positive: `nfa` and `s` are live locals bound by
                                                // const-ref; the analyzer mis-models the std::vector here.
                                                // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
                                                const std::int64_t acc {dfa_accept_of(nfa, s)};
                                                sets.push_back(std::move(s));
                                                accept_pre.push_back(acc);
                                                if (sets.size() > state_cap) { // bound the 2^NFA worst case
                                                  throw dfa_error("DFA state count exceeded max_dfa_states; "
                                                                  "pattern is too complex for a DFA");
                                                }
                                                return static_cast<std::uint32_t>(sets.size() - 1);
                                              }};

      const std::size_t words {(nfa.code.size() + 63U) / 64U};
      sets.emplace_back(words, 0); // state 0 = dead (empty set)
      accept_pre.push_back(-1);
      std::vector<std::uint32_t> entry_seeds;
      for (const std::size_t e : nfa.entry) {
        entry_seeds.push_back(static_cast<std::uint32_t>(e));
      }
      out.start = find_or_add(dfa_closure(nfa, entry_seeds, true)); // offset 0: text_start holds

      std::vector<std::uint32_t> trans_pre;                         // [s*nc + c]
      for (std::size_t s = 0; s < sets.size(); ++s) { // sets grows as states are discovered
        for (std::size_t c = 0; c < nc; ++c) {
          trans_pre.push_back(find_or_add(dfa_move(nfa, sets[s], bc.rep[c])));
        }
      }
      const std::size_t n_pre {sets.size()};

      // Moore partition refinement; initial block by accept tag.
      std::vector<std::int64_t> block(n_pre, 0);
      for (std::size_t s = 0; s < n_pre; ++s) {
        block[s] = accept_pre[s] < 0 ? 0 : accept_pre[s] + 1;
      }
      std::size_t num_blocks {0};
      {
        std::vector<std::int64_t> seen;
        for (std::size_t s = 0; s < n_pre; ++s) {
          bool found {false};
          for (const std::int64_t b : seen) {
            if (b == block[s]) { found = true; break; }
          }
          if (!found) { seen.push_back(block[s]); }
        }
        num_blocks = seen.size();
      }
      for (bool changed = true; changed;) {
        changed = false;
        std::vector<std::vector<std::int64_t>> sigs;
        std::vector<std::int64_t>              new_block(n_pre, 0);
        for (std::size_t s = 0; s < n_pre; ++s) {
          std::vector<std::int64_t> sig;
          sig.reserve(nc + 1);
          sig.push_back(block[s]);
          for (std::size_t c = 0; c < nc; ++c) {
            sig.push_back(block[trans_pre[(s * nc) + c]]);
          }
          std::int64_t id {-1};
          for (std::size_t i = 0; i < sigs.size(); ++i) {
            if (sigs[i] == sig) { id = static_cast<std::int64_t>(i); break; }
          }
          if (id < 0) {
            id = static_cast<std::int64_t>(sigs.size());
            sigs.push_back(std::move(sig));
          }
          new_block[s] = id;
        }
        if (sigs.size() != num_blocks) {
          changed    = true;
          num_blocks = sigs.size();
          block      = std::move(new_block);
        }
      }

      // Emit minimized tables: one state per block, fields from a representative.
      out.num_states = num_blocks;
      std::vector<std::int64_t> rep_of_block(num_blocks, -1);
      for (std::size_t s = 0; s < n_pre; ++s) {
        std::int64_t& slot {rep_of_block[static_cast<std::size_t>(block[s])]};
        if (slot < 0) { slot = static_cast<std::int64_t>(s); }
      }
      out.trans.reserve(num_blocks * nc);
      out.accept.reserve(num_blocks);
      for (std::size_t b = 0; b < num_blocks; ++b) {
        const std::size_t rep {static_cast<std::size_t>(rep_of_block[b])};
        out.accept.push_back(accept_pre[rep] < 0 ? dfa_no_rule : static_cast<std::uint32_t>(accept_pre[rep]));
        for (std::size_t c = 0; c < nc; ++c) {
          out.trans.push_back(static_cast<std::uint32_t>(block[trans_pre[(rep * nc) + c]]));
        }
      }
      out.start = static_cast<std::uint32_t>(block[out.start]);
      return out;
    }
  } // namespace detail

  /*!
   * \brief A maximal-munch DFA over an ordered set of patterns.
   *
   * Built once (heap-allocated tables), then immutable and cheap to copy-share.
   * \ref match returns the longest match at the cursor, breaking ties toward the
   * earliest pattern — the lexer's maximal munch, in one pass over the input.
   */
  class dfa
  {
  public:

    /*!
     * \brief Builds the DFA from compiled programs (the embedder path).
     * \param[in] programs The patterns' programs, in priority order (see \ref regex::raw_program).
     * \throws real::dfa_error if any program holds a non-head zero-width assertion.
     */
    explicit dfa(std::span<const detail::program_view> programs)
      : tables_(detail::dfa_build(programs))
    {}

    /*!
     * \brief Builds the DFA from regexes (a convenience over \ref regex::raw_program).
     * \param[in] patterns The patterns, in priority order; they must outlive this call.
     * \throws real::dfa_error if any pattern holds a non-head zero-width assertion.
     */
    explicit dfa(std::span<const regex> patterns)
      : dfa(views_of(patterns))
    {}

    /*!
     * \brief Matches the longest pattern anchored at the start of \p rest.
     *
     * Maximal munch: the longest match wins; on equal length the earliest pattern
     * (lowest index passed to the constructor) wins; an empty match never wins.
     *
     * \param[in] rest The text to match at its start.
     * \return The winning rule index and byte length, or `std::nullopt` if nothing
     *         non-empty matches.
     */
    [[nodiscard]] std::optional<dfa_match> match(std::string_view rest) const noexcept
    {
      std::uint32_t             state {tables_.start};
      std::optional<dfa_match>  best;
      for (std::size_t i = 0; i < rest.size();) {
        const auto          byte {static_cast<std::uint8_t>(rest[i])};
        const std::size_t   cls  {tables_.byte_class[byte]};
        state = tables_.trans[(static_cast<std::size_t>(state) * tables_.num_classes) + cls];
        if (state == 0U) { // dead state
          break;
        }
        ++i;
        const std::uint32_t rule {tables_.accept[state]};
        if (rule != detail::dfa_no_rule) {
          best = dfa_match {.rule_index = rule, .length = i};
        }
      }
      return best;
    }

    //! \brief The number of states in the minimized automaton (includes the dead state).
    [[nodiscard]] std::size_t state_count() const noexcept
    {
      return tables_.num_states;
    }

    //! \brief The number of patterns the DFA was built from.
    [[nodiscard]] std::size_t rule_count() const noexcept
    {
      return tables_.rule_count;
    }

    //! \brief The number of byte-equivalence classes (the reduced alphabet width).
    [[nodiscard]] std::size_t class_count() const noexcept
    {
      return tables_.num_classes;
    }

  private:

    //! \brief Materializes program views from \p patterns (helper for the regex ctor).
    static std::vector<detail::program_view> views_of(std::span<const regex> patterns)
    {
      std::vector<detail::program_view> views;
      views.reserve(patterns.size());
      for (const regex& pattern : patterns) {
        views.push_back(pattern.raw_program());
      }
      return views;
    }

    detail::dfa_tables tables_; //!< The immutable baked tables.
  };
} // namespace real

#endif // REAL_DFA_HPP
