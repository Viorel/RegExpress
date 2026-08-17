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
 * \note NOT the internal `real::detail::lazy_dfa` (`automata/lazy_dfa.hpp`): that one is a private,
 *       *priority-preserving* forward DFA that finds a single pattern's match boundary for the Pike route.
 *       This `real::dfa` is a public, capture-free *maximal-munch* recognizer over a whole rule set.
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

#include "real/version.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "real/core/config.hpp"
#include "real/real.hpp"

namespace real {

  /*!
   * \brief Thrown when a pattern cannot be represented as a DFA.
   *
   * Four causes: a zero-width assertion other than a leading `\A`/`^` (`$`,
   * `\b`, `\B`, multiline anchors), a lookaround, a Unicode code-point class
   * (`\w`/`\d`/`\s` in text mode — use byte classes), or a possessive
   * quantifier / atomic group. `real::dfa` never falls back silently — a
   * violated contract is an error the caller handles (e.g. by keeping that
   * rule on the Pike VM).
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

  /*! \brief DFA construction internals: subset construction over a flattened NFA. Not a stable API. */
  namespace detail {

    /*! \brief A flattened NFA instruction (global PCs, global class index). */
    struct dfa_instr
    {
      opcode        op        {};   //!< The instruction's opcode.
      std::uint8_t  arg8      {};   //!< Literal byte (op == byte).
      std::uint32_t klass     {0};  //!< Global class index (op == klass).
      std::int64_t  primary   {-1}; //!< Global target (split/jump).
      std::int64_t  secondary {-1}; //!< Global secondary target (split).
    };

    /*! \brief The union NFA over all the patterns, flattened into one address space. */
    struct dfa_nfa
    {
      std::vector<dfa_instr>    code;           //!< Every rule's instructions, concatenated.
      std::vector<char_class>   classes;        //!< Every rule's byte classes, concatenated.
      std::vector<std::int64_t> accept_rule;    //!< accept_rule[pc] = rule index if match, else -1.
      std::vector<std::size_t>  entry;          //!< entry[rule] = global pc of its pc 0.
      std::size_t               rule_count {0}; //!< Rules flattened in, i.e. patterns handed to \ref dfa_flatten.
    };

    /*!
     * \brief Cap on a pattern's expanded byte program before subset construction runs on it.
     *
     * \ref max_dfa_states bounds the RESULT; this bounds the WORK to reach it. Subset construction is
     * superlinear in its input, so an expansion an order of magnitude larger costs two orders of magnitude
     * more time -- a text-mode `\w+` expands into thousands of byte instructions and turns a
     * sub-millisecond build into a fraction of a second, which under a sanitized fuzzing build is a
     * timeout rather than a slow test.
     *
     * The cap sits above every shape that builds in about a millisecond and below the ones that do not. A
     * pattern past it declines with a message naming the cause, which is what the caller needs to keep
     * that rule on the Pike VM.
     */
    inline constexpr std::size_t max_dfa_byte_program {512};

    /*!
     * \brief Flattens \p programs into one union NFA, auditing DFA-ability.
     *
     * \param[in] programs The compiled patterns, one per rule, in rule order.
     * \return The union NFA in one address space.
     * \throws real::dfa_error if any program holds an assertion other than a head text_start,
     *         a lookaround, a code-point class, or a possessive/atomic construct.
     */
    inline dfa_nfa dfa_flatten(std::span<const program_view> programs)
    {
      dfa_nfa nfa;
      nfa.rule_count = programs.size();
      for (std::size_t r = 0; r < programs.size(); ++r) {
        const program_view& prog {programs[r]};

        // The AUDIT runs over the pattern's own program, so every message names what the user wrote.
        // A `klass_cp` is not among the refusals: it is expanded below.
        for (const instr& in : prog.code) {
          if (in.op == opcode::assert_position && in.arg8 != static_cast<std::uint8_t>(assert_kind::text_start)) {
            // text_start (`\A`/`^`) is handled as a conditional ε in the closure (true at the cursor,
            // false after any byte — exactly its anchored meaning). Any other assertion ($, \b, \B,
            // multiline ^/$, …) cannot be a pure DFA.
            throw dfa_error("pattern has a zero-width assertion that no DFA can represent "
                            "(only \\A/^ is allowed)");
          }
          if (in.op == opcode::assert_lookaround) {
            throw dfa_error("pattern has a lookaround, which no DFA can represent");
          }
          if (in.op == opcode::byte_loop_possessive || in.op == opcode::klass_loop_possessive ||
              in.op == opcode::klass_cp_loop_possessive) {
            // A Tier 1 possessive loop's on-no-match transition is an in-place, same-position epsilon
            // splice resolved by the Pike VM's step() (see pike.hpp) -- not a pure byte transition a DFA
            // state machine can represent, and its primary_target is a capture-slot index, not a branch
            // pc (copying it unremapped would corrupt the DFA).
            throw dfa_error("pattern has a possessive quantifier or atomic group, which no DFA can represent");
          }
        }

        // A code-point class matches a whole code point, which is not a byte transition -- so it is
        // replaced by the deterministic UTF-8 trie that recognises the same set, the same way the lazy DFA
        // does it. This is `build_byte_program`, the expansion the byte automata already run on, targets
        // remapped and all, which is what lets text-mode `\w`/`\d`/`\s` and case-folded ASCII classes
        // build here at all.
        //
        // `keep_assertions`: the audit above already accepted only a head `\A`/`^`, and the closure below
        // resolves it. Stripping assertions here instead would silently drop that anchor.
        const byte_program bp {build_byte_program(prog, true, max_dfa_byte_program)};
        if (!bp.eligible) {
          throw dfa_error("pattern's byte expansion is too large for a DFA (a wide code-point class such "
                          "as text-mode \\w, or one repeated many times), or holds a construct the byte "
                          "automata decline");
        }

        const std::size_t base  {nfa.code.size()};
        const std::size_t cbase {nfa.classes.size()};
        nfa.entry.push_back(base);
        for (const char_class& cls : bp.classes) {
          nfa.classes.push_back(cls);
        }
        for (const instr& in : bp.code) {
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
          nfa.code.push_back(out);
          nfa.accept_rule.push_back(in.op == opcode::match ? static_cast<std::int64_t>(r) : -1);
        }
      }
      return nfa;
    }

    using dfa_set = std::vector<std::uint64_t>; //!< A set of NFA PCs as a bitset (one per DFA state during construction).

    /*!
     * \brief Set bit \p i in \p s. Indices past the set's size are ignored (it is sized to fit).
     * \param[in,out] s The PC bitset.
     * \param[in]     i The PC to mark present.
     */
    inline void dfa_set_bit(dfa_set&    s,
                            std::size_t i)
    {
      const std::size_t word {i >> 6U};
      if (word < s.size()) { // defensive bound, mirroring dfa_test_bit (the set is sized to fit)
        s[word] |= (std::uint64_t {1} << (i & 63U));
      }
    }

    /*!
     * \brief Whether bit \p i is set in \p s.
     * \param[in] s The PC bitset.
     * \param[in] i The PC to test.
     * \return True when present; false for any index beyond the set.
     */
    inline bool dfa_test_bit(const dfa_set& s,
                             std::size_t    i)
    {
      const std::size_t word {i >> 6U};
      return word < s.size() && ((s[word] >> (i & 63U)) & 1U) != 0; // beyond the set ⇒ absent
    }

    /*!
     * \brief The epsilon-closure of \p seeds (a PC list), as a canonical PC bitset.
     *        \p at_start follows a text_start assertion (true only at offset 0).
     * \param[in] nfa      The union NFA.
     * \param[in] seeds    PCs to close over.
     * \param[in] at_start Whether a head text_start assertion may be crossed.
     * \return The closure as a canonical bitset.
     */
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
          case opcode::assert_lookaround:
          case opcode::byte_loop_possessive:
          case opcode::klass_loop_possessive:
          case opcode::klass_cp_loop_possessive:
            break; // terminal / unreachable (dfa_flatten rejects lookaround, klass_cp & Tier 1 loops)
        }
      }
      return present;
    }

    /*!
     * \brief The move on the byte \p rep: ε-closure of the successors of every PC
     *        in \p set that consumes \p rep.
     * \param[in] nfa The union NFA.
     * \param[in] set The source state's PC set.
     * \param[in] rep The representative byte of the class being stepped over.
     * \return The successor state's PC set.
     */
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

    /*!
     * \brief The accepting rule of a state set: the SMALLEST rule index among its
     *        match PCs (the order tie-break), or -1 if none accept.
     * \param[in] nfa The union NFA.
     * \param[in] set The state's PC set.
     * \return The winning rule index, or -1 when the state does not accept.
     */
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

    /*!
     * \brief Word count for a which-matched bitset over \p rule_count rules.
     * \param[in] rule_count Rules the bitset must hold.
     * \return 64-bit words needed.
     */
    [[nodiscard]] inline std::size_t dfa_mask_words(std::size_t rule_count) noexcept
    {
      return (rule_count + 63U) / 64U;
    }

    /*!
     * \brief Bitset of ALL accepting rule indices in \p set (which-matched; word-packed).
     *        Empty vector when no rule accepts (or rule_count == 0).
     * \param[in] nfa The union NFA.
     * \param[in] set The state's PC set.
     * \return The word-packed mask, or an empty vector when nothing accepts.
     */
    inline std::vector<std::uint64_t> dfa_accept_mask_of(const dfa_nfa& nfa,
                                                         const dfa_set& set)
    {
      std::vector<std::uint64_t> mask(dfa_mask_words(nfa.rule_count), 0);
      for (std::size_t pc = 0; pc < nfa.code.size(); ++pc) {
        if (!dfa_test_bit(set, pc) || nfa.accept_rule[pc] < 0) {
          continue;
        }
        const auto r {static_cast<std::size_t>(nfa.accept_rule[pc])};
        if (r < nfa.rule_count) {
          mask[r >> 6U] |= (std::uint64_t {1} << (r & 63U));
        }
      }
      return mask;
    }

    /*!
     * \brief Smallest rule index set in \p mask, or -1 if empty (munch tag derivation).
     * \param[in] mask A word-packed which-matched bitset.
     * \return The lowest rule index present, or -1 for an empty mask.
     */
    inline std::int64_t dfa_mask_min_rule(const std::vector<std::uint64_t>& mask)
    {
      for (std::size_t w = 0; w < mask.size(); ++w) {
        if (mask[w] == 0) {
          continue;
        }
        // ctz of the lowest set bit
        std::uint64_t x {mask[w]};
        std::size_t   b {0};
        while ((x & 1U) == 0U) {
          x >>= 1U;
          ++b;
        }
        return static_cast<std::int64_t>((w << 6U) + b);
      }
      return -1;
    }

    /*!
     * \brief Computes byte-equivalence classes: two bytes are equivalent iff they satisfy the same
     *        consuming predicates (every klass test and every byte literal). Reduces the alphabet so the
     *        DFA is built over classes, not over 256 bytes.
     */
    struct dfa_byte_classes
    {
      std::array<std::uint8_t, 256> of    {};   //!< byte -> class index.
      std::array<std::uint8_t, 256> rep   {};   //!< class index -> one representative byte of it.
      std::size_t                   count {0};  //!< Distinct classes, i.e. the reduced alphabet's size.
    };

    /*!
     * \brief Partition 0..255 by the union NFA's consuming predicates.
     * \param[in] nfa The union NFA.
     * \return The byte-to-class map, its representatives and their count.
     */
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
                              return std::ranges::all_of(class_preds,
                                                         [&](const auto& pred) {
                                                           return pred.test(static_cast<std::uint8_t>(a))
                                                                  == pred.test(static_cast<std::uint8_t>(b));
                                                         })
                                     && std::ranges::all_of(literal_preds, [&](unsigned lit) {
                                                              return (a == lit) == (b == lit);
                                                            });
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

    /*! \brief The baked DFA tables produced by \ref dfa_build. */
    struct dfa_tables
    {
      std::array<std::uint8_t, 256> byte_class  {};     //!< byte -> class index; the row stride's key.
      std::size_t                   num_classes {0};    //!< Reduced alphabet size, i.e. one \ref trans row's width.
      std::vector<std::uint32_t>    trans;              //!< [state*num_classes + cls] -> next state (0 = dead).
      std::vector<std::uint32_t>    accept;             //!< accept[state] = rule index, or NO_RULE (munch).
      std::vector<std::uint64_t>    accept_mask;        //!< accept_mask[state * mask_words + w] — full which-matched bitset per state (word-packed).
      std::vector<std::uint8_t>     any_accept;         //!< any_accept[state] != 0 if mask has any bit (skip mask-OR).
      std::size_t                   mask_words {0};     //!< Words per state in accept_mask.
      std::uint32_t                 start      {0};     //!< The state a walk begins in.
      std::size_t                   num_states {0};     //!< States in the minimized machine, including the dead state 0.
      std::size_t                   rule_count {0};     //!< Rules the tables were built for.
      bool                          unanchored {false}; //!< Built for which-matched mid-stream restart.
      //! Set-level first-byte skip for \ref dfa::which_matched -- union of each rule's
      //! \c first_bytes. Disabled if any rule has \c first_bytes_valid == false (empty match /
      //! can start anywhere). Applied only when the walk is in \ref start (no partial in flight).
      bool                          skip_first_enabled  {false};
      char_class                    skip_first_bytes;         //!< Union of rule first-bytes (valid iff enabled).
      std::int16_t                  skip_single_first   {-1}; //!< Unique union member, else -1.
      std::array<char, 4>           skip_small_set      {};   //!< 2..4 union members for memchr-cascade.
      std::uint8_t                  skip_small_set_size {0};  //!< 0, or 2..4.
    };

    inline constexpr std::uint32_t dfa_no_rule {std::numeric_limits<std::uint32_t>::max()}; //!< \ref dfa_tables::accept's "this state does not accept" marker.

    /*!
     * \brief Subset construction over byte-classes, then Moore minimization.
     *
     * Initial partition keys on the **full accept mask** (which-matched), not only the
     * min-rule munch tag — two states with the same earliest rule but different accept
     * sets must not merge. \p unanchored unions mid-stream pattern starts into every
     * post-move set (self-restart) so a single scan can discover matches at any offset.
     *
     * \param[in] programs   The flattened NFA programs.
     * \param[in] state_cap  Maximum DFA states before \ref dfa_error.
     * \param[in] unanchored Mid-stream restart for which-matched (Stage-2); munch uses false.
     * \return The baked tables.
     * \throws real::dfa_error when construction exceeds \p state_cap, or when \ref dfa_flatten refuses a
     *         pattern.
     */
    inline dfa_tables dfa_build(std::span<const program_view> programs,
                                std::size_t                   state_cap  = max_dfa_states,
                                bool                          unanchored = false)
    {
      const dfa_nfa          nfa {dfa_flatten(programs)};
      const dfa_byte_classes bc  {dfa_compute_classes(nfa)};
      const std::size_t      nc  {bc.count};
      const std::size_t      mw  {dfa_mask_words(nfa.rule_count)};
      dfa_tables             out;
      out.byte_class  = bc.of;
      out.num_classes = nc;
      out.rule_count  = nfa.rule_count;
      out.mask_words  = mw;
      out.unanchored  = unanchored;

      std::vector<dfa_set>                    sets;     // sets[s] = NFA PCs of DFA state s
      std::vector<std::vector<std::uint64_t>> mask_pre; // full accept mask per pre-min state
      std::vector<std::uint32_t>              entry_seeds;
      for (const std::size_t e : nfa.entry) {
        entry_seeds.push_back(static_cast<std::uint32_t>(e));
      }
      const dfa_set restart_mid {dfa_closure(nfa, entry_seeds, false)}; // mid-stream pattern starts

      const auto complete {[&](dfa_set s) {
                             if (!unanchored) {
                               return s;
                             }
                             // Union mid-stream pattern entries so every position can start a match.
                             for (std::size_t w = 0; w < s.size() && w < restart_mid.size(); ++w) {
                               s[w] |= restart_mid[w];
                             }
                             return s;
                           }};

      const auto find_or_add {[&](dfa_set s) -> std::uint32_t {
                                for (std::size_t i = 0; i < sets.size(); ++i) {
                                  if (sets[i] == s) {
                                    return static_cast<std::uint32_t>(i);
                                  }
                                }
                                // False positive: live locals; analyzer mis-models the vector.
                                // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
                                auto mask {dfa_accept_mask_of(nfa, s)};
                                sets.push_back(std::move(s));
                                mask_pre.push_back(std::move(mask));
                                if (sets.size() > state_cap) {
                                  throw dfa_error("DFA state count exceeded max_dfa_states; "
                                                  "pattern is too complex for a DFA");
                                }
                                return static_cast<std::uint32_t>(sets.size() - 1);
                              }};

      const std::size_t words {(nfa.code.size() + 63U) / 64U};
      sets.emplace_back(words, 0); // state 0 = dead (empty set)
      mask_pre.push_back(std::vector<std::uint64_t>(mw, 0));
      // Offset 0: text_start holds. Unanchored still starts here (anchors see pos 0).
      out.start = find_or_add(dfa_closure(nfa, entry_seeds, true));

      std::vector<std::uint32_t> trans_pre; // [s*nc + c]
      // Indexed: find_or_add appends to `sets`. A range-for captures end() once
      // and never expands the states it just discovered (UAF under realloc;
      // silent one-state machine otherwise).
      // NOLINTNEXTLINE(modernize-loop-convert)
      for (std::size_t s = 0; s < sets.size(); ++s) {
        for (std::size_t c = 0; c < nc; ++c) {
          trans_pre.push_back(find_or_add(complete(dfa_move(nfa, sets[s], bc.rep[c]))));
        }
      }
      const std::size_t n_pre {sets.size()};

      // Moore: initial partition by FULL accept mask (not min-rule alone).
      std::vector<std::int64_t>               block(n_pre, 0);
      std::vector<std::vector<std::uint64_t>> mask_keys;
      for (std::size_t s = 0; s < n_pre; ++s) {
        std::int64_t id {-1};
        for (std::size_t i = 0; i < mask_keys.size(); ++i) {
          if (mask_keys[i] == mask_pre[s]) {
            id = static_cast<std::int64_t>(i);
            break;
          }
        }
        if (id < 0) {
          id = static_cast<std::int64_t>(mask_keys.size());
          mask_keys.push_back(mask_pre[s]);
        }
        block[s] = id;
      }
      std::size_t num_blocks {mask_keys.size()};
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
            if (sigs[i] == sig) {
              id = static_cast<std::int64_t>(i);
              break;
            }
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

      out.num_states = num_blocks;
      std::vector<std::int64_t> rep_of_block(num_blocks, -1);
      for (std::size_t s = 0; s < n_pre; ++s) {
        std::int64_t& slot {rep_of_block[static_cast<std::size_t>(block[s])]};
        if (slot < 0) {
          slot = static_cast<std::int64_t>(s);
        }
      }
      out.trans.reserve(num_blocks * nc);
      out.accept.reserve(num_blocks);
      out.accept_mask.assign(num_blocks * mw, 0);
      out.any_accept.assign(num_blocks, 0);
      for (std::size_t b = 0; b < num_blocks; ++b) {
        const std::size_t  rep   {static_cast<std::size_t>(rep_of_block[b])};
        const std::int64_t min_r {dfa_mask_min_rule(mask_pre[rep])};
        out.accept.push_back(min_r < 0 ? dfa_no_rule : static_cast<std::uint32_t>(min_r));
        bool any                 {false};
        for (std::size_t w = 0; w < mw; ++w) {
          out.accept_mask[(b * mw) + w] = mask_pre[rep][w];
          any                           = any || (mask_pre[rep][w] != 0);
        }
        out.any_accept[b] = any ? 1 : 0;
        for (std::size_t c = 0; c < nc; ++c) {
          out.trans.push_back(static_cast<std::uint32_t>(block[trans_pre[(rep * nc) + c]]));
        }
      }
      out.start = static_cast<std::uint32_t>(block[out.start]);

      // Set-level first-byte union for the which_matched skip (unanchored only).
      // Any rule without a sound first-byte set disables the skip (correctness).
      if (unanchored && !programs.empty()) {
        bool                  ok {true};
        char_class            uni;
        for (const program_view& prog : programs) {
          if (!prog.hints.first_bytes_valid) {
            ok = false;
            break;
          }
          uni.merge(prog.hints.first_bytes);
        }
        if (ok && !uni.empty()) {
          out.skip_first_enabled = true;
          out.skip_first_bytes   = uni;
          std::array<char, 4> members  {};
          std::uint8_t        count    {0};
          bool                overflow {false};
          for (unsigned b = 0; b < 256U; ++b) {
            if (!uni.test(static_cast<std::uint8_t>(b))) {
              continue;
            }
            if (count < 4U) {
              members[count] = static_cast<char>(b);
            }
            else {
              overflow = true;
            }
            ++count;
          }
          if (count == 1U) {
            out.skip_single_first = static_cast<std::int16_t>(static_cast<std::uint8_t>(members[0]));
          }
          else if (!overflow && count >= 2U && count <= 4U) {
            out.skip_small_set      = members;
            out.skip_small_set_size = count;
          }
        }
      }
      return out;
    }
  } // namespace detail

  /*!
   * \brief Build mode for \c real::dfa.
   *
   * \c munch — maximal-munch at the cursor (lexer; default, SciLex).
   * \c which_matched — unanchored multi-accept single-pass (Stage-2 RegexSet fused).
   */
  enum class dfa_mode : std::uint8_t
  {
    munch          = 0, //!< One winner at the start of the subject (existing contract).
    which_matched  = 1, //!< Mid-stream restart; full accept-mask per state for which-matched.
  };

  /*!
   * \brief A multi-rule DFA: maximal-munch (\c dfa_mode::munch) or which-matched
   *        unanchored scan (\c dfa_mode::which_matched).
   *
   * Built once (heap-allocated tables), then immutable and cheap to copy-share.
   * \ref match is the lexer munch. \ref which_matched is Stage-2 multi-accept
   * (only valid when built with \ref dfa_mode::which_matched).
   */
  class dfa
  {
  public:

    /*!
     * \brief Builds the DFA from compiled programs (the embedder path).
     * \param[in] programs The patterns' programs, in priority order (see \ref regex::raw_program).
     * \param[in] mode     Munch (default) or which-matched unanchored multi-accept.
     * \throws real::dfa_error if any program holds a non-head zero-width assertion, a
     *         lookaround, a code-point class, or a possessive/atomic construct.
     */
    explicit dfa(std::span<const detail::program_view> programs,
                 dfa_mode                              mode = dfa_mode::munch)
      : tables_(detail::dfa_build(programs, detail::max_dfa_states, mode == dfa_mode::which_matched))
    {}

    /*!
     * \brief Builds the DFA from regexes (a convenience over \ref regex::raw_program).
     * \param[in] patterns The patterns, in priority order; they must outlive this call.
     * \param[in] mode     Munch (default) or which-matched.
     * \throws real::dfa_error if any pattern holds a non-head zero-width assertion, a
     *         lookaround, a code-point class, or a possessive/atomic construct.
     */
    explicit dfa(std::span<const regex> patterns,
                 dfa_mode               mode = dfa_mode::munch)
      : dfa(views_of(patterns),
            mode)
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

    /*!
     * \brief Which patterns match the subject at least once (single-pass).
     *
     * Requires a build with \ref dfa_mode::which_matched. Returns a bitset of length
     * \ref rule_count in construction order. Early-exits when every pattern has hit.
     * Empty matches are excluded (only states that accepted after consuming a byte
     * contribute, via post-move accept masks).
     *
     * \param[in] text            Subject text.
     * \param[in] first_byte_skip When true (default), fast-forward over bytes that cannot
     *                            start any rule while the walk is in the start state (a pure
     *                            optimization). Pass false to disable for equivalence tests.
     * \return One bool per rule, in construction order, true where that pattern matched.
     */
    [[nodiscard]] std::vector<bool> which_matched(std::string_view text,
                                                  bool             first_byte_skip = true) const
    {
      std::vector<bool> hit(tables_.rule_count, false);
      if (tables_.rule_count == 0 || tables_.mask_words == 0) {
        return hit;
      }
      std::vector<std::uint64_t> acc(tables_.mask_words, 0);
      std::uint32_t              state   {tables_.start};
      const std::size_t          nc      {tables_.num_classes};
      const std::size_t          mw      {tables_.mask_words};
      std::size_t                pending {tables_.rule_count};
      const bool                 do_skip {first_byte_skip && tables_.skip_first_enabled};
      for (std::size_t i = 0; i < text.size();) {
        // At start (no partial in flight), jump to the next set-first-byte candidate.
        if (do_skip && state == tables_.start) {
          const auto b0 {static_cast<std::uint8_t>(text[i])};
          if (!tables_.skip_first_bytes.test(b0)) {
            std::size_t next {i};
            if (tables_.skip_single_first >= 0) {
              next = detail::find_byte(text, i, static_cast<char>(tables_.skip_single_first));
            }
            else if (tables_.skip_small_set_size >= 2U) {
              // Same adaptive probe→cascade as pike next_candidate (dense near-hit cheap).
              constexpr std::size_t probe      {32};
              const std::size_t     window_end {i + probe < text.size() ? i + probe : text.size()};
              std::size_t           p          {i};
              while (p < window_end &&
                     !tables_.skip_first_bytes.test(static_cast<std::uint8_t>(text[p]))) {
                ++p;
              }
              if (p < window_end) {
                next = p;
              }
              else if (window_end == text.size()) {
                next = npos;
              }
              else {
                next = detail::find_bytes_cascade(text, window_end, tables_.skip_small_set.data(),
                                                  tables_.skip_small_set_size);
              }
            }
            else {
              while (next < text.size() &&
                     !tables_.skip_first_bytes.test(static_cast<std::uint8_t>(text[next]))) {
                ++next;
              }
              if (next >= text.size()) {
                next = npos;
              }
            }
            if (next == npos || next >= text.size()) {
              break;
            }
            i = next;
            // state remains start; fall through and consume text[i].
          }
        }
        const auto        byte {static_cast<std::uint8_t>(text[i])};
        const std::size_t cls  {tables_.byte_class[byte]};
        state = tables_.trans[(static_cast<std::size_t>(state) * nc) + cls];
        ++i;
        // Most states accept nothing — skip the mask-OR on the common path.
        if (state >= tables_.any_accept.size() || tables_.any_accept[state] == 0) {
          continue;
        }
        const std::size_t base {static_cast<std::size_t>(state) * mw};
        for (std::size_t w = 0; w < mw; ++w) {
          const std::uint64_t m {tables_.accept_mask[base + w]};
          const std::uint64_t neu {m & ~acc[w]};
          if (neu == 0) {
            continue;
          }
          acc[w] |= m;
          // Count newly set bits for early-exit.
          std::uint64_t x {neu};
          while (x != 0) {
            x &= x - 1U;
            if (pending > 0) {
              --pending;
            }
          }
        }
        if (pending == 0) {
          break;
        }
      }
      for (std::size_t r = 0; r < tables_.rule_count; ++r) {
        hit[r] = ((acc[r >> 6U] >> (r & 63U)) & 1U) != 0;
      }
      return hit;
    }

    /*!
     * \brief True if set-level first-byte skip is armed for which_matched.
     * \return Whether every rule contributed a valid first-byte set at build time.
     */
    [[nodiscard]] bool has_first_byte_skip() const noexcept
    {
      return tables_.skip_first_enabled;
    }

    /*!
     * \brief True if this DFA was built with mid-stream restart (which-matched mode).
     * \return Whether the tables carry self-restart transitions.
     */
    [[nodiscard]] bool is_unanchored() const noexcept
    {
      return tables_.unanchored;
    }

    /*!
     * \brief The number of states in the minimized automaton (includes the dead state).
     * \return The state count.
     */
    [[nodiscard]] std::size_t state_count() const noexcept
    {
      return tables_.num_states;
    }

    /*!
     * \brief The number of patterns the DFA was built from.
     * \return The rule count, which is also the width of \ref which_matched's answer.
     */
    [[nodiscard]] std::size_t rule_count() const noexcept
    {
      return tables_.rule_count;
    }

    /*!
     * \brief The number of byte-equivalence classes (the reduced alphabet width).
     * \return The class count.
     */
    [[nodiscard]] std::size_t class_count() const noexcept
    {
      return tables_.num_classes;
    }

  private:

    /*!
     * \brief Materializes program views from \p patterns (helper for the regex ctor).
     * \param[in] patterns The compiled regexes, which must outlive the views.
     * \return One view per pattern, in the same order.
     */
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
