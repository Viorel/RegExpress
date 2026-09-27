/*!
 * \file dfa.hpp
 * \brief `real::dfa` — a maximal-munch DFA over a set of patterns (opt-in).
 *
 * A lexer matches many rules at every position; running each rule's Pike VM in
 * turn is linear but re-scans the input once per candidate rule. `real::dfa`
 * fuses a set of patterns into one deterministic automaton that recognizes the
 * winning rule in a single left-to-right pass (longest match; ties to the earliest
 * rule), reached far faster when many rules share leading bytes. It is built from
 * the patterns' compiled programs, runs at run time (the tables are heap-allocated
 * once and then immutable), and is the accelerated rule-dispatch path SciLex opts
 * into.
 *
 * \note NOT the internal `real::detail::lazy_dfa` (`automata/lazy_dfa.hpp`): that one is a private,
 *       *priority-preserving* forward DFA that finds a single pattern's match boundary for the Pike route.
 *       This `real::dfa` is a public, capture-free *maximal-munch* recognizer over a whole rule set.
 *
 * Scope: a pattern is DFA-able iff its program holds no zero-width assertion other
 * than a leading `\A`/`^` (a no-op under anchored scanning), no lookaround, no
 * possessive quantifier or atomic group, and a byte expansion small enough to build.
 * Anything else throws \ref real::dfa_error rather than silently mis-recognizing —
 * the caller keeps such rules on the Pike VM.
 *
 * **DFA-able is not the same as faithful.** A DFA recognizes a pattern's *language*
 * and takes the LONGEST match, while `regex::match()` takes the match its priority
 * order prefers. The two agree for many patterns and disagree for others, and the
 * difference is not visible in the syntax: `a|ab` on `"ab"` matches 1 byte and the
 * DFA takes 2; so does the greedy, longer-branch-first `(?:ab|a)(?:bc)?` on
 * `"abc"`, while the lazy `x*?y` agrees on every input. So a caller that needs the
 * DFA to reproduce a per-rule `match()` munch asks \ref real::dfa_faithful, which
 * DECIDES the question for each pattern and names an input that separates the two
 * when they differ. Include this header explicitly; `real.hpp` does not.
 */
#ifndef REAL_DFA_HPP
#define REAL_DFA_HPP

#include "real/version.hpp"

#include <algorithm>
#include <bit>
#include <array>
#include <cstddef>
#include <ranges>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "real/core/config.hpp"
#include "real/real.hpp"

namespace real {

  /*!
   * \brief Thrown when a pattern cannot be represented as a DFA.
   *
   * Five causes: a zero-width assertion other than a leading `\A`/`^` (`$`,
   * `\b`, `\B`, multiline anchors), a lookaround, a possessive quantifier /
   * atomic group, a code-point class whose UTF-8 expansion is too large (text-mode
   * `\w`, or a class repeated many times — narrower classes such as `\d`,
   * `\p{Greek}` or `[àé]` build), or an automaton past the state cap (65 536 states).
   * `real::dfa` never falls back silently — a violated contract is an error the
   * caller handles (e.g. by keeping that rule on the Pike VM).
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
     *         a lookaround, a possessive/atomic construct, or a code-point class too wide to expand.
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
      for (std::size_t w = 0; w < set.size(); ++w) {
        for (std::uint64_t bits {set[w]}; bits != 0U; bits &= bits - 1U) { // set PCs only: the set is sparse
          const std::size_t pc      {(w << 6U) + static_cast<std::size_t>(std::countr_zero(bits))};
          const dfa_instr&  in      {nfa.code[pc]};
          const bool        consume {(in.op == opcode::byte && in.arg8 == rep)
                                     || (in.op == opcode::klass && nfa.classes[in.klass].test(rep))};
          if (consume) {
            seeds.push_back(static_cast<std::uint32_t>(pc + 1));
          }
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
     * \brief Every class's move from one state in one pass over the state's PCs: \ref dfa_move for each
     *        class of \p bc, without rescanning the set once per class.
     * \param[in] nfa           The union NFA.
     * \param[in] set           The source state's PC set.
     * \param[in] bc            The byte classes (a byte PC consumes exactly its own byte's class).
     * \param[in] klass_members For each NFA class, the byte-class indices it contains.
     * \param[in,out] closures  Closures already computed during this construction, by seed list:
     *            classes and states that consume through the same PCs share one closure.
     * \return The successor PC set per class, indexed by class.
     */
    inline std::vector<dfa_set> dfa_move_all(const dfa_nfa&                                 nfa,
                                             const dfa_set&                                 set,
                                             const dfa_byte_classes&                        bc,
                                             const std::vector<std::vector<std::uint8_t>>&  klass_members,
                                             std::map<std::vector<std::uint32_t>, dfa_set>& closures)
    {
      std::vector<std::vector<std::uint32_t>> seeds(bc.count);
      for (std::size_t w = 0; w < set.size(); ++w) {
        for (std::uint64_t bits {set[w]}; bits != 0U; bits &= bits - 1U) {
          const std::size_t pc {(w << 6U) + static_cast<std::size_t>(std::countr_zero(bits))};
          const dfa_instr&  in {nfa.code[pc]};
          if (in.op == opcode::byte) {
            seeds[bc.of[in.arg8]].push_back(static_cast<std::uint32_t>(pc + 1));
          }
          else if (in.op == opcode::klass) {
            for (const std::uint8_t c : klass_members[in.klass]) {
              seeds[c].push_back(static_cast<std::uint32_t>(pc + 1));
            }
          }
        }
      }
      std::vector<dfa_set> out;
      out.reserve(bc.count);
      for (auto& per_class : seeds) {
        auto found {closures.find(per_class)};
        if (found == closures.end()) {
          dfa_set closed {dfa_closure(nfa, per_class, false)}; // post-consumption: text_start is false here
          found = closures.emplace(std::move(per_class), std::move(closed)).first;
        }
        out.push_back(found->second);
      }
      return out;
    }

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

      // Known sets by content, so a lookup is a logarithmic search rather than a comparison with every
      // state.
      std::map<dfa_set, std::uint32_t> index;
      const auto                       find_or_add {[&](dfa_set s) -> std::uint32_t {
                                                      if (const auto known {index.find(s)}; known != index.end()) {
                                                        return known->second;
                                                      }
                                                      index.emplace(s, static_cast<std::uint32_t>(index.size())); // index holds one entry per state
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
      index.emplace(sets.back(), 0U);
      mask_pre.push_back(std::vector<std::uint64_t>(mw, 0));
      // Offset 0: text_start holds. Unanchored still starts here (anchors see pos 0).
      out.start = find_or_add(dfa_closure(nfa, entry_seeds, true));

      std::vector<std::uint32_t> trans_pre; // [s*nc + c]
      // Which byte classes each NFA class holds, and the closures computed so far (by seed list).
      std::vector<std::vector<std::uint8_t>> klass_members(nfa.classes.size());
      for (std::size_t k = 0; k < nfa.classes.size(); ++k) {
        for (std::size_t c = 0; c < nc; ++c) {
          if (nfa.classes[k].test(bc.rep[c])) {
            klass_members[k].push_back(static_cast<std::uint8_t>(c));
          }
        }
      }
      std::map<std::vector<std::uint32_t>, dfa_set> closures;
      // Indexed: find_or_add appends to `sets`. A range-for captures end() once
      // and never expands the states it just discovered (UAF under realloc;
      // silent one-state machine otherwise).
      // NOLINTNEXTLINE(modernize-loop-convert)
      for (std::size_t s = 0; s < sets.size(); ++s) {
        std::vector<dfa_set> moves {dfa_move_all(nfa, sets[s], bc, klass_members, closures)};
        for (std::size_t c = 0; c < nc; ++c) {
          trans_pre.push_back(find_or_add(complete(std::move(moves[c]))));
        }
      }
      const std::size_t n_pre {sets.size()};

      // Moore: initial partition by FULL accept mask (not min-rule alone).
      // Blocks are numbered in first-seen order, looked up through an ordered index: a comparison with
      // every block seen so far made each round quadratic in the state count.
      std::vector<std::int64_t>                             block(n_pre, 0);
      std::map<std::vector<std::uint64_t>, std::int64_t>    mask_ids;
      for (std::size_t s = 0; s < n_pre; ++s) {
        block[s] = mask_ids.try_emplace(mask_pre[s], static_cast<std::int64_t>(mask_ids.size())).first->second;
      }
      std::size_t num_blocks {mask_ids.size()};
      for (bool changed = true; changed;) {
        changed = false;
        std::map<std::vector<std::int64_t>, std::int64_t> sig_ids;
        std::vector<std::int64_t>                         new_block(n_pre, 0);
        for (std::size_t s = 0; s < n_pre; ++s) {
          std::vector<std::int64_t> sig;
          sig.reserve(nc + 1);
          sig.push_back(block[s]);
          for (std::size_t c = 0; c < nc; ++c) {
            sig.push_back(block[trans_pre[(s * nc) + c]]);
          }
          new_block[s] = sig_ids.try_emplace(std::move(sig), static_cast<std::int64_t>(sig_ids.size())).first->second;
        }
        if (sig_ids.size() != num_blocks) {
          changed    = true;
          num_blocks = sig_ids.size();
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

    /*!
     * \brief The priority-ordered epsilon closure of \p seed, as the Pike walk builds it: consuming pcs are
     *        appended to \p out in priority order, and reaching a `match` stops the walk, because a thread
     *        list is cut below its first accepting thread.
     *
     * This replays `pike_vm::add_thread` over the flattened byte program, and it must stay the SAME walk:
     * DFS order (primary before secondary), a pc dropped once \p seen in this generation, and the
     * empty-iteration routing -- a jump back to a loop head already seen exits the loop through the head's
     * secondary, at this thread's own priority. Without that routing `(?:c??)*` on `"c"` reads as faithful
     * while the engine answers the empty match. `dfa_fidelity_agrees_with_the_engine` in
     * `tests/automata/test_dfa_fidelity.cpp` holds the two walks together.
     *
     * \param[in]     nfa      The union NFA (one rule).
     * \param[in]     seed     The pc to close over.
     * \param[in]     at_start Whether a head text_start assertion may be crossed.
     * \param[in,out] seen     Per-generation visited marks, shared by every seed of one step.
     * \param[in,out] out      Receives the consuming pcs, in priority order.
     * \return True when the walk reached a `match`.
     */
    inline bool dfa_priority_closure(const dfa_nfa&              nfa,
                                     std::uint32_t               seed,
                                     bool                        at_start,
                                     std::vector<std::uint8_t>&  seen,
                                     std::vector<std::uint32_t>& out)
    {
      std::vector<std::uint32_t> stack {seed};
      while (!stack.empty()) {
        const std::uint32_t pc {stack.back()};
        stack.pop_back();
        if (seen[pc] != 0U) {
          continue;
        }
        seen[pc] = 1U;
        const dfa_instr& in {nfa.code[pc]};
        switch (in.op) {
          case opcode::split:
            stack.push_back(static_cast<std::uint32_t>(in.secondary));
            stack.push_back(static_cast<std::uint32_t>(in.primary));
            break;
          case opcode::jump:
            {
              auto head {static_cast<std::size_t>(in.primary)};
              for (int hops = 0; hops < max_loop_hops && seen[head] != 0U && nfa.code[head].op == opcode::jump;
                   ++hops) {
                head = static_cast<std::size_t>(nfa.code[head].primary);
              }
              const bool exits {seen[head] != 0U && nfa.code[head].op == opcode::split};
              stack.push_back(static_cast<std::uint32_t>(exits ? nfa.code[head].secondary : in.primary));
            }
            break;
          case opcode::save: stack.push_back(pc + 1); break;
          case opcode::assert_position:
            if (at_start) { // dfa_flatten admits only a head text_start: ε at offset 0, dead after a byte
              stack.push_back(pc + 1);
            }
            break;
          case opcode::byte:
          case opcode::klass: out.push_back(pc); break;
          case opcode::match: return true;
          case opcode::klass_cp:
          case opcode::assert_lookaround:
          case opcode::byte_loop_possessive:
          case opcode::klass_loop_possessive:
          case opcode::klass_cp_loop_possessive:
            break; // absent from a flattened byte program (dfa_flatten expands or refuses them)
        }
      }
      return false;
    }

    /*!
     * \brief The per-pattern answer of \ref real::dfa_faithful, before the public wrapping.
     */
    struct dfa_fidelity_raw
    {
      std::uint8_t outcome {0}; //!< 0 faithful, 1 divergent, 2 undecided.
      std::string  witness;     //!< Divergent only: an input separating `match()` from the longest match.
    };

    /*!
     * \brief Decides whether \p prog's priority match equals its longest match on every input.
     *
     * The search walks the PRODUCT of two simulations over the same byte program: the priority one (the
     * ordered thread list above the current best, as \ref dfa_priority_closure builds it, and whether a
     * best exists) and the set one (every live pc, as the DFA sees it). A reachable product state whose set
     * accepts at a byte where the priority list does not is a divergence, and the path to it is an input
     * on which `match()` stops short of the longest match. Conversely, every divergence has such a state:
     * the priority best only moves forward, so at the longest match's end the set accepts and the list
     * did not. The search is therefore exact, and bounded only by \p budget.
     *
     * \param[in] prog   The pattern's program.
     * \param[in] budget Product states explored before the answer is "undecided".
     * \return The outcome, with a witness when divergent.
     * \throws real::dfa_error for a pattern that is not DFA-able.
     */
    inline dfa_fidelity_raw dfa_decide_fidelity(const program_view& prog,
                                                std::size_t         budget)
    {
      const std::array<program_view, 1> one   {prog};
      const dfa_nfa                     nfa   {dfa_flatten(one)};
      const dfa_byte_classes            bc    {dfa_compute_classes(nfa)};
      const auto                        entry {static_cast<std::uint32_t>(nfa.entry[0])};
      struct product_state
      {
        std::vector<std::uint32_t> live; //!< Priority threads above the current best, in order.
        bool                       best; //!< A priority match has been recorded.
        dfa_set                    all;  //!< Every live pc, as a set.
      };
      using product_key = std::tuple<std::vector<std::uint32_t>, bool, dfa_set>;
      std::vector<product_state>                        states;
      std::vector<std::pair<std::size_t, std::uint8_t>> parent; // (state, byte) that first reached it
      std::map<product_key, std::size_t>                index;

      std::vector<std::uint8_t>  seen(nfa.code.size(), 0U);
      std::vector<std::uint32_t> live0;
      const bool                 best0 {dfa_priority_closure(nfa, entry, true, seen, live0)};
      states.push_back({.live = live0, .best = best0, .all = dfa_closure(nfa, {entry}, true)});
      parent.emplace_back(std::size_t {0}, std::uint8_t {0});
      index.emplace(product_key {states[0].live, states[0].best, states[0].all}, std::size_t {0});

      // NOLINTNEXTLINE(modernize-loop-convert) -- indexed: the loop appends the states it discovers.
      for (std::size_t s = 0; s < states.size(); ++s) {
        for (std::size_t c = 0; c < bc.count; ++c) {
          const std::uint8_t rep {bc.rep[c]};
          dfa_set            all {dfa_move(nfa, states[s].all, rep)};
          if (std::ranges::all_of(all, [](std::uint64_t word) { return word == 0U; })) {
            continue; // dead: no thread survives, nothing can accept again
          }
          std::ranges::fill(seen, std::uint8_t {0});
          std::vector<std::uint32_t> live;
          bool                       hit {false};
          for (const std::uint32_t pc : states[s].live) {
            const dfa_instr& in       {nfa.code[pc]};
            const bool       consumes {(in.op == opcode::byte && in.arg8 == rep)
                                       || (in.op == opcode::klass && nfa.classes[in.klass].test(rep))};
            if (consumes && dfa_priority_closure(nfa, pc + 1, false, seen, live)) {
              hit = true;
              break; // everything below the accepting thread is cut
            }
          }
          if (!hit && dfa_accept_of(nfa, all) >= 0) {
            std::string witness(1, static_cast<char>(rep));
            for (std::size_t at = s; at != 0; at = parent[at].first) {
              witness.insert(witness.begin(), static_cast<char>(parent[at].second));
            }
            return {.outcome = 1, .witness = std::move(witness)};
          }
          const bool  best {states[s].best || hit};
          product_key key  {live, best, all};
          if (index.contains(key)) {
            continue;
          }
          if (states.size() >= budget) {
            return {.outcome = 2, .witness = {}};
          }
          index.emplace(std::move(key), states.size());
          states.push_back({.live = std::move(live), .best = best, .all = std::move(all)});
          parent.emplace_back(s, rep);
        }
      }
      return {.outcome = 0, .witness = {}};
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

  class dfa;

  namespace detail {
    /*!
     * \brief Throws the std::invalid_argument a misused dfa_munch_memo raises, out of line so the throw
     *        does not weigh on the per-token match that checks for it.
     * \param[in] what The message.
     */
    [[noreturn]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((cold, noinline))
#endif
    inline void dfa_memo_misuse(const char* what)
    {
      throw std::invalid_argument(what);
    }
  } // namespace detail

  /*!
   * \brief What successive munches over ONE subject have learnt, so that tokenizing the whole subject
   *        costs O(states × length) instead of O(length²) (Reps, "Maximal-munch tokenization in linear
   *        time", 1998).
   *
   * A munch from an offset walks the DFA until it dies or the subject ends, then answers the last
   * accepting position. Every (state, position) the walk visited AFTER that position leads to no
   * accept -- that is why the walk went on without answering -- so a later munch reaching the same
   * pair can stop there with the answer it already holds. The next munch starts at or after the
   * previous answer's end, so without this a rule like `a*b` beside `a` rescans the rest of `aaa…`
   * from every position.
   *
   * Bound to one \ref dfa and one subject: pass it to \ref dfa::match(std::string_view, std::size_t,
   * dfa_munch_memo&) const with the same pair every time. Memory is one bit per remembered state per
   * position of the subject, allocated for a state the first time it is remembered, and nothing else: a
   * walk replays the stretch after its last accept to mark it rather than holding it.
   */
  class dfa_munch_memo
  {
  public:

    /*!
     * \brief The longest stretch after a walk's last accept that is left unmarked. A later walk reaching
     *        one of its pairs dies within that many steps on its own, so the bound stays linear
     *        (O(length x (states + short_stretch))). The first longer stretch also arms the memo: until
     *        then no walk consults it, so an ordinary tokenization pays nothing for it.
     */
    static constexpr std::size_t short_stretch {32};

    /*!
     * \brief An empty memo for one subject.
     * \param[in] subject_size The subject's length in bytes.
     */
    explicit dfa_munch_memo(std::size_t subject_size)
      : size_(subject_size)
    {}

    /*!
     * \brief DFA transitions taken by every munch so far -- the work the bound is stated in.
     * \return The count, summed over every call that used this memo.
     */
    [[nodiscard]] std::size_t transitions() const noexcept
    {
      return transitions_;
    }

    /*!
     * \brief Whether a walk over this subject has run more than \ref short_stretch steps past its last
     *        accept, so later walks consult the memo; until then it holds nothing and costs nothing.
     * \return True once armed.
     */
    [[nodiscard]] bool armed() const noexcept
    {
      return !marked_.empty();
    }

  private:

    friend class dfa;

    std::size_t                                        size_;                  //!< The subject's length.
    std::vector<std::vector<bool>>                     dead_after_;            //!< [state][position]: no accept follows.
    std::vector<std::uint8_t>                          marked_;                //!< [state]: dead_after_[state] holds a mark.
    std::size_t                                        transitions_ {0};       //!< See transitions().
    const void        *                                owner_       {nullptr}; //!< The dfa's tables this memo describes.
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
     * \throws real::dfa_error for a pattern that is not DFA-able (see \ref dfa_error).
     */
    explicit dfa(std::span<const detail::program_view> programs,
                 dfa_mode                              mode = dfa_mode::munch)
      : tables_(detail::dfa_build(programs, detail::max_dfa_states, mode == dfa_mode::which_matched))
    {}

    /*!
     * \brief Builds the DFA from regexes (a convenience over \ref regex::raw_program).
     * \param[in] patterns The patterns, in priority order; they must outlive this call.
     * \param[in] mode     Munch (default) or which-matched.
     * \throws real::dfa_error for a pattern that is not DFA-able (see \ref dfa_error).
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
     * \brief \ref match(std::string_view) const at \p offset of \p subject, remembering in \p memo what the
     *        walk proved so that successive calls over the same subject cost linear time in total.
     *
     * Answers exactly what `match(subject.substr(offset))` answers.
     *
     * \param[in]     subject The whole subject; every call with \p memo must pass the same one.
     * \param[in]     offset  Where this munch starts (at most `subject.size()`).
     * \param[in,out] memo    The subject's memo (see \ref dfa_munch_memo).
     * \return The winning rule index and byte length, or `std::nullopt` if nothing non-empty matches.
     * \throws std::invalid_argument If \p memo was made for a subject of another length, was used with
     *         another DFA, or \p offset lies beyond \p subject.
     */
    [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((always_inline))
#endif
    std::optional<dfa_match> match(std::string_view subject,
                                   std::size_t      offset,
                                   dfa_munch_memo&  memo) const
    {
      if (memo.size_ != subject.size() || offset > subject.size()) [[unlikely]] {
        detail::dfa_memo_misuse("real::dfa::match: the memo belongs to another subject, or the offset is past it");
      }
      if (memo.owner_ != tables_.trans.data()) [[unlikely]] {
        if (memo.owner_ != nullptr) {
          detail::dfa_memo_misuse("real::dfa::match: the memo belongs to another DFA");
        }
        memo.owner_ = tables_.trans.data();
      }
      if (!memo.marked_.empty()) [[unlikely]] {
        return match_armed(subject, offset, memo);
      }
      // Unarmed: the plain walk, plus one comparison at the end. Only a walk that ran more than
      // short_stretch steps past its last accept arms the memo, so an ordinary tokenization pays nothing.
      std::uint32_t state     {tables_.start};
      std::uint32_t best_rule {detail::dfa_no_rule};
      std::size_t   best_end  {offset};
      std::size_t   i         {offset};
      while (i < subject.size()) {
        state = tables_.trans[(static_cast<std::size_t>(state) * tables_.num_classes)
                              + tables_.byte_class[static_cast<std::uint8_t>(subject[i])]];
        if (state == 0U) { // dead state
          break;
        }
        ++i;
        const std::uint32_t rule {tables_.accept[state]};
        if (rule != detail::dfa_no_rule) {
          best_rule = rule;
          best_end  = i;
        }
      }
      if (i - best_end > dfa_munch_memo::short_stretch) [[unlikely]] {
        mark_dead_stretch(subject, state_at(subject, offset, best_end), best_end, i, memo);
      }
      memo.transitions_ += i - offset;
      if (best_rule == detail::dfa_no_rule) {
        return std::nullopt;
      }
      return dfa_match {.rule_index = best_rule, .length = best_end - offset};
    }

    /*!
     * \brief Which patterns match the subject at least once (single-pass).
     *
     * Requires a build with \ref dfa_mode::which_matched. Returns a bitset of length
     * \ref rule_count in construction order. Early-exits when every pattern has hit.
     * A rule that accepts the EMPTY string is credited: the start state's accept mask is folded in
     * before the walk, so a nullable member answers true on an empty subject as this type's N-walk
     * oracle does. Only that one position is added — every other accept still comes from a
     * post-move mask.
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

      //! Fold one state's accept mask into the accumulator, counting newly-set bits for early exit.
      const auto absorb = [&](std::uint32_t st) {
                            if (st >= tables_.any_accept.size() || tables_.any_accept[st] == 0) {
                              return;
                            }
                            const std::size_t base {static_cast<std::size_t>(st) * mw};
                            for (std::size_t w = 0; w < mw; ++w) {
                              const std::uint64_t m   {tables_.accept_mask[base + w]};
                              const std::uint64_t neu {m & ~acc[w]};
                              if (neu == 0) {
                                continue;
                              }
                              acc[w] |= m;
                              std::uint64_t x {neu};
                              while (x != 0) {
                                x &= x - 1U;
                                if (pending > 0) {
                                  --pending;
                                }
                              }
                            }
                          };

      // A rule that accepts the EMPTY string has already accepted before any byte is read, and the
      // loop below only folds POST-MOVE masks -- so on an empty subject it never ran at all and
      // every nullable rule answered false, against this type's own N-walk oracle. The start
      // state's own mask is that answer and the only place it lives.
      //
      // On a non-empty subject this is not a second crediting: `acc` starts at zero, and the loop's
      // own fold is idempotent (`m & ~acc[w]`), so a rule already credited here contributes no new
      // bit when the walk re-enters an accepting state.
      absorb(state);
      for (std::size_t i = 0; i < text.size() && pending != 0;) {
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
        absorb(state); // most states accept nothing; `absorb` returns at once for those
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
     * \brief The state a walk from the start state at \p from reaches at \p to (a replay, only on the
     *        rare path that arms a memo).
     * \param[in] subject The text.
     * \param[in] from    Where the walk begins.
     * \param[in] to      Where it has read up to.
     * \return The state after reading `subject[from, to)`.
     */
    [[nodiscard]] std::uint32_t state_at(std::string_view subject,
                                         std::size_t      from,
                                         std::size_t      to) const noexcept
    {
      std::uint32_t state {tables_.start};
      for (std::size_t at = from; at < to; ++at) {
        state = tables_.trans[(static_cast<std::size_t>(state) * tables_.num_classes)
                              + tables_.byte_class[static_cast<std::uint8_t>(subject[at])]];
      }
      return state;
    }

    /*!
     * \brief \ref match once \p memo is armed: the same walk, stopping at a pair an earlier walk proved
     *        dead, and marking a long dead stretch of its own. Out of line: only a subject that armed its
     *        memo -- one with a long dead stretch -- reaches it.
     * \param[in]     subject The memo's subject.
     * \param[in]     offset  Where this munch starts.
     * \param[in,out] memo    The subject's memo, armed.
     * \return The winning rule index and byte length, or `std::nullopt`.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    std::optional<dfa_match> match_armed(std::string_view subject,
                                         std::size_t      offset,
                                         dfa_munch_memo&  memo) const
    {
      std::uint32_t state        {tables_.start};
      std::uint32_t best_rule    {detail::dfa_no_rule};
      std::size_t   best_end     {offset};
      std::uint32_t resume_state {state}; // the state at best_end: where the dead stretch begins
      std::size_t   i            {offset};
      while (i < subject.size()) {
        state = tables_.trans[(static_cast<std::size_t>(state) * tables_.num_classes)
                              + tables_.byte_class[static_cast<std::uint8_t>(subject[i])]];
        if (state == 0U) { // dead state
          break;
        }
        ++i;
        const std::uint32_t rule {tables_.accept[state]};
        if (rule != detail::dfa_no_rule) {
          best_rule    = rule;
          best_end     = i;
          resume_state = state;
          continue;
        }
        if (memo.marked_[state] != 0U && memo.dead_after_[state][i]) {
          break; // an earlier walk proved no accept follows this pair
        }
      }
      if (i - best_end > dfa_munch_memo::short_stretch) {
        mark_dead_stretch(subject, resume_state, best_end, i, memo);
      }
      memo.transitions_ += i - offset;
      if (best_rule == detail::dfa_no_rule) {
        return std::nullopt;
      }
      return dfa_match {.rule_index = best_rule, .length = best_end - offset};
    }

    /*!
     * \brief Replays the walk from \p state at \p from to \p to, marking every pair it passes as dead in
     *        \p memo (arming it on first use). Out of line: only a long dead stretch reaches it, and
     *        keeping it out of \ref match keeps the per-token walk as small as the plain one.
     * \param[in]     subject The memo's subject.
     * \param[in]     state   The state the walk stood on at \p from (its last accept, or the start).
     * \param[in]     from    Where the dead stretch begins.
     * \param[in]     to      Where the walk stopped.
     * \param[in,out] memo    The subject's memo.
     */
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void mark_dead_stretch(std::string_view subject,
                           std::uint32_t    state,
                           std::size_t      from,
                           std::size_t      to,
                           dfa_munch_memo&  memo) const
    {
      if (memo.marked_.empty()) {
        memo.dead_after_.resize(tables_.num_states);
        memo.marked_.assign(tables_.num_states, 0U);
      }
      for (std::size_t at = from; at < to; ++at) {
        state = tables_.trans[(static_cast<std::size_t>(state) * tables_.num_classes)
                              + tables_.byte_class[static_cast<std::uint8_t>(subject[at])]];
        std::vector<bool>& known {memo.dead_after_[state]};
        if (memo.marked_[state] == 0U) {
          known.resize(memo.size_ + 1, false);
          memo.marked_[state] = 1U;
        }
        known[at + 1] = true;
      }
      memo.transitions_ += to - from;
    }

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

  /*!
   * \brief The default search budget of \ref real::dfa_faithful, in product states: the same cap a
   *        \ref dfa's own construction runs under.
   */
  inline constexpr std::size_t dfa_default_state_budget {65536};

  // Spelled as a literal so the published signature names no internal; bound to the construction cap here
  // so the two cannot drift apart.
  static_assert(dfa_default_state_budget == detail::max_dfa_states,
                "dfa_faithful's default budget must be the cap real::dfa's construction runs under");

  /*!
   * \brief The three answers \ref real::dfa_faithful can give.
   */
  enum class dfa_fidelity_outcome : std::uint8_t
  {
    faithful  = 0, //!< `match()` equals the longest match on every input: the DFA reproduces it.
    divergent = 1, //!< Some input separates them; \ref dfa_fidelity::witness is one.
    undecided = 2, //!< The search hit its state budget. Treat as not faithful; never as faithful.
  };

  /*!
   * \brief The answer of \ref real::dfa_faithful.
   */
  struct dfa_fidelity
  {
    dfa_fidelity_outcome outcome    {dfa_fidelity_outcome::faithful}; //!< The decision.
    std::size_t          rule_index {0};                              //!< The first pattern not proven faithful (0 for one pattern).
    std::string          witness;                                     //!< Divergent only: an input on which `match()` is shorter than
                                                                      //!< the longest match. Bytes, not necessarily valid UTF-8.
  };

  /*!
   * \brief Decides whether \p pattern's `match()` is always its longest match -- whether a \ref dfa
   *        built from it reproduces `pattern.match()` on every input.
   *
   * The answer is EXACT for the pattern, never a sample: `faithful` holds for every input, and
   * `divergent` comes with an input that proves it. Only the search's size is bounded; past
   * \p state_budget the answer is `undecided`, which a caller must treat as not faithful.
   *
   * \param[in] pattern      The pattern to decide.
   * \param[in] state_budget Product states the search may explore.
   * \return The decision, with a witness when divergent.
   * \throws real::dfa_error for a pattern that is not DFA-able (see \ref dfa_error).
   */
  [[nodiscard]] inline dfa_fidelity dfa_faithful(const regex& pattern,
                                                 std::size_t  state_budget = dfa_default_state_budget)
  {
    detail::dfa_fidelity_raw raw {detail::dfa_decide_fidelity(pattern.raw_program(), state_budget)};
    return {.outcome = static_cast<dfa_fidelity_outcome>(raw.outcome), .rule_index = 0,
            .witness = std::move(raw.witness)};
  }

  /*!
   * \brief Decides \ref real::dfa_faithful for each of \p patterns, in order, and answers for the first one not
   *        proven faithful (its index in \ref dfa_fidelity::rule_index).
   *
   * `faithful` means every pattern is, which is SUFFICIENT for a \ref dfa over the set to reproduce the
   * per-rule `match()` munch (longest wins, earliest on a tie, empty never wins): with every rule's
   * `match()` equal to its longest match, both reach the same length and the same earliest rule. It is
   * not NECESSARY -- a divergent rule that a higher-priority rule always outlasts changes no answer, and
   * this still refuses the set. That refusal is correct, only cautious; an acceptance is never wrong.
   *
   * \param[in] patterns     The patterns, in priority order.
   * \param[in] state_budget Product states each pattern's search may explore.
   * \return The first non-faithful pattern's decision, or `faithful`.
   * \throws real::dfa_error for a pattern that is not DFA-able (see \ref dfa_error).
   */
  [[nodiscard]] inline dfa_fidelity dfa_faithful(std::span<const regex> patterns,
                                                 std::size_t            state_budget = dfa_default_state_budget)
  {
    for (std::size_t i = 0; i < patterns.size(); ++i) {
      dfa_fidelity one {dfa_faithful(patterns[i], state_budget)};
      if (one.outcome != dfa_fidelity_outcome::faithful) {
        one.rule_index = i;
        return one;
      }
    }
    return {};
  }
} // namespace real

#endif // REAL_DFA_HPP
