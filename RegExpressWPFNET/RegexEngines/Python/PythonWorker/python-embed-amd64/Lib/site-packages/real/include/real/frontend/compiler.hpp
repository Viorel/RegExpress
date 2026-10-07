/*!
 * \file compiler.hpp
 * \brief AST → NFA program, via Thompson construction.
 *
 * The program always has the shape `save 0, <body>, save 1, match`: slots 0/1 delimit group 0.
 * Multi-byte code points compile to UTF-8 byte classes joined by split/jump, so the engine steps one
 * byte at a time in lock-step (linear time). Branch targets are emitted as placeholders and patched
 * only through `patch_primary` / `patch_secondary`.
 */
#ifndef REAL_COMPILER_HPP
#define REAL_COMPILER_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "real/frontend/ast.hpp"
#include "real/core/charclass.hpp"
#include "real/core/config.hpp"
#include "real/engine/prefilter.hpp"
#include "real/frontend/inner_literal.hpp"
#include "real/core/program.hpp"
#include "real/unicode/unicode_fold.hpp"
#include "real/automata/utf8_ranges.hpp"

namespace real::detail {

  /*!
   * \brief Whether \p ranges is exactly the whole non-ASCII space `[U+0080, U+10FFFF]` — the
   *        "any non-ASCII code point" shape emitted by \ref compiler::emit_any_codepoint_class.
   * \param[in] ranges The class's non-ASCII ranges.
   * \return Whether they are exactly the one range covering all of non-ASCII.
   */
  constexpr bool is_any_non_ascii(const std::vector<code_range>& ranges)
  {
    return ranges.size() == 1 && ranges[0].lo == 0x80U && ranges[0].hi == 0x10FFFFU;
  }

  /*!
   * \brief Whether \p ranges is what every consumer of a \ref cp_class requires: each range
   *        non-empty, the sequence strictly ascending and disjoint.
   *
   * Both match-time paths rely on the order (`fill_cp_page_row` stops at the first range past the page,
   * `cp_class_matches` binary-searches above it): an unsorted list loses members silently. Not
   * minimality: a touching pair costs a comparison, never an answer.
   *
   * \param[in] ranges The class's non-ASCII ranges, in the order they would be interned.
   * \return Whether they are ordered as the matchers require.
   */
  [[nodiscard]] constexpr bool cp_ranges_are_normalised(const std::vector<code_range>& ranges)
  {
    for (std::size_t i = 0; i < ranges.size(); ++i) {
      if (ranges[i].lo > ranges[i].hi) {
        return false; // an inverted or empty range names no code point
      }
      if (i > 0 && ranges[i - 1].hi >= ranges[i].lo) {
        return false; // out of order or overlapping
      }
    }
    return true;
  }

  /*!
   * \brief Expands a character class to its Unicode simple case-fold closure (text-mode `icase`).
   *
   * The fold acts on the whole class, across the ASCII boundary both ways, before negation: each
   * ASCII member contributes its partners (`k`↦Kelvin becomes a range), and each fold entry inside a
   * class range contributes its partners (`[U+0080-U+10FFFF]` pulls `k`/`K` into the bitmap). The
   * ASCII-letter literal fold takes this same route.
   *
   * \param[in] in The class as written.
   * \return Its case-fold closure: the folded ASCII bitmap plus the coalesced non-ASCII ranges.
   */
  constexpr class_def unicode_casefold(const class_def& in)
  {
    class_def               out;
    out.ascii = in.ascii;
    std::vector<code_range> ranges {in.ranges}; // seed with the input's non-ASCII ranges
    const auto              add_partner {[&out, &ranges](std::uint32_t p) {
                                           if (p < 0x80U) {
                                             out.ascii.set(static_cast<std::uint8_t>(p)); // ASCII partner -> bitmap
                                           }
                                           else {
                                             ranges.push_back({.lo = p, .hi = p});        // non-ASCII partner (coalesced below)
                                           }
                                         }};
    for (std::uint32_t cp = 0; cp < 0x80U; ++cp) {
      if (in.ascii.test(static_cast<std::uint8_t>(cp))) {
        const std::size_t idx {find_fold_index(cp)};
        if (idx != unicode_fold_table_size) {
          const fold_entry& entry {unicode_fold_table[idx]};
          for (std::uint8_t i = 0; i < entry.count; ++i) {
            add_partner(entry.partner[i]);
          }
        }
      }
    }
    // Walk the sorted fold table per range (seek, then forward), never the whole table per class:
    // O(table x ranges) made one icase `\w` cost six figures of comparisons, times each `{k}` copy.
    // Overlapping ranges may revisit an entry; the duplicate {p, p} is merged below.
    for (const code_range& r : in.ranges) {
      for (std::size_t i {find_fold_lower_bound(r.lo)}; i < unicode_fold_table_size; ++i) {
        const fold_entry& entry {unicode_fold_table[i]};
        if (entry.cp > r.hi) {
          break;
        }
        for (std::uint8_t k = 0; k < entry.count; ++k) {
          add_partner(entry.partner[k]);
        }
      }
    }
    // The fold adds many degenerate {cp, cp} ranges; coalescing also yields the order consumers need.
    out.ranges = coalesce_ranges(std::move(ranges));
    return out;
  }

  /*!
   * \brief True if the AST subtree rooted at \p idx can match the empty string. `empty`, `anchor`
   *        and `lookaround` are zero-width, so exactly nullable; `byte`/`klass`/`any` never are.
   * \param[in] tree The AST.
   * \param[in] idx  Root of the subtree; a negative index reads as nullable (an absent body).
   * \return Whether the subtree can match the empty string.
   */
  constexpr bool node_nullable(const ast&   tree,
                               std::int32_t idx)
  {
    if (idx < 0) {
      return true;
    }
    const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
    switch (n.kind) {
      case node_kind::empty:
      case node_kind::anchor:
      case node_kind::lookaround:
        return true;
      case node_kind::byte:
      case node_kind::klass:
      case node_kind::any:
        return false;
      case node_kind::concat:
        for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
          if (!node_nullable(tree, c)) {
            return false;
          }
        }
        return true;
      case node_kind::alternation:
        for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
          if (node_nullable(tree, c)) {
            return true;
          }
        }
        return false;
      case node_kind::group:
        return node_nullable(tree, n.child);
      case node_kind::repeat:
        return n.min == 0 || node_nullable(tree, n.child);
    }
    return true;
  }

  /*!
   * \brief True if the AST subtree rooted at \p idx contains, at any depth, a capturing group
   *        (`group >= 0`) whose body is nullable (\ref node_nullable).
   * \param[in] tree The AST.
   * \param[in] idx  Root of the subtree.
   * \return Whether a capturing group with a nullable body sits anywhere underneath.
   */
  constexpr bool subtree_has_nullable_capturing_group(const ast&   tree,
                                                      std::int32_t idx)
  {
    if (idx < 0) {
      return false;
    }
    const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
    switch (n.kind) {
      case node_kind::group:
        if (n.group >= 0 && node_nullable(tree, n.child)) {
          return true;
        }
        return subtree_has_nullable_capturing_group(tree, n.child);
      case node_kind::concat:
      case node_kind::alternation:
        for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
          if (subtree_has_nullable_capturing_group(tree, c)) {
            return true;
          }
        }
        return false;
      case node_kind::repeat:
      case node_kind::lookaround:
        return subtree_has_nullable_capturing_group(tree, n.child);
      case node_kind::empty:
      case node_kind::byte:
      case node_kind::klass:
      case node_kind::any:
      case node_kind::anchor:
        return false;
    }
    return false;
  }

  /*!
   * \brief True if a capturing group with a nullable body sits anywhere under a quantifier (`?`
   *        included): the source of \ref pattern_hints::nullable_captured_repeat. A safe
   *        over-approximation: it flags the shape (`(\b|x)+` counts), not a proven divergent capture.
   * \param[in] tree The AST.
   * \param[in] idx  Root to walk from.
   * \return Whether some quantifier in the tree has a nullable capturing group under it.
   */
  constexpr bool ast_has_nullable_captured_repeat(const ast&   tree,
                                                  std::int32_t idx)
  {
    if (idx < 0) {
      return false;
    }
    const ast_node& n {tree.nodes[static_cast<std::size_t>(idx)]};
    switch (n.kind) {
      case node_kind::repeat:
        if (subtree_has_nullable_capturing_group(tree, n.child)) {
          return true;
        }
        return ast_has_nullable_captured_repeat(tree, n.child);
      case node_kind::group:
      case node_kind::lookaround:
        return ast_has_nullable_captured_repeat(tree, n.child);
      case node_kind::concat:
      case node_kind::alternation:
        for (std::int32_t c = n.child; c >= 0; c = tree.nodes[static_cast<std::size_t>(c)].next) {
          if (ast_has_nullable_captured_repeat(tree, c)) {
            return true;
          }
        }
        return false;
      case node_kind::empty:
      case node_kind::byte:
      case node_kind::klass:
      case node_kind::any:
      case node_kind::anchor:
        return false;
    }
    return false;
  }

  /*!
   * \brief Compiles an \ref ast into a \ref dynamic_program (NFA bytecode).
   */
  class compiler
  {
  public:

    /*!
     * \brief Binds the compiler to a parsed pattern and its flags.
     * \param[in] tree The AST to compile (borrowed, must outlive the compiler).
     * \param[in] compile_flags The effective compilation flags.
     */
    constexpr compiler(const ast& tree,
                       flags      compile_flags)
      : tree_(tree),
        flags_(compile_flags)
    {}

  private:

    static constexpr std::size_t fold_cache_ways {4}; //!< Ways of the \ref effective_class fold cache; a miss only folds again.

    /*!
     * \brief Cache tag per way: the (class index, fold mode, negated) key, or -1 for empty.
     *
     * `mutable`: the emit path reaches \ref effective_class through const members. Written only
     * outside constant evaluation: MSVC's constant evaluator rejects an indeterminate subobject here,
     * and a `static_regex` gains nothing from the cache (its budget is the fold's step count).
     */
    mutable std::array<std::int32_t, fold_cache_ways> fold_key_ {-1, -1, -1, -1};

    mutable std::array<class_def, fold_cache_ways> fold_val_    {}; //!< Finished (folded, coalesced, negated) class per way.

  public:

    /*!
     * \brief True when the full inner literal starts at \p pc as consecutive `byte` ops.
     *
     * A byte equal to the literal's first byte is not the literal: in `(?:a){2}ax` the prefix's `a`
     * would anchor the hint at the wrong distance, a silent false negative wherever the route runs
     * (`static_regex` at any size, dynamic past the size floor; short dynamic subjects never reach it).
     *
     * \param[in] prog The program being built.
     * \param[in] pc   Index of the candidate first `byte` op.
     * \return Whether the whole literal, all \ref pattern_hints::inner_literal_len bytes of it, is here.
     */
    static constexpr bool inner_literal_starts_at(const dynamic_program& prog,
                                                  std::size_t            pc)
    {
      const std::size_t len {prog.hints.inner_literal_len};
      if (len == 0 || pc + len > prog.code.size()) {
        return false;
      }
      for (std::size_t k = 0; k < len; ++k) {
        if (prog.code[pc + k].op != opcode::byte
            || prog.code[pc + k].arg8 != prog.hints.inner_literal[k]) {
          return false;
        }
      }
      return true;
    }

    /*!
     * \brief Whether a match can open on a UTF-8 continuation byte (`10xxxxxx`).
     * \param[in] first_bytes The pattern's possible first bytes.
     * \return True when one of them is a continuation byte.
     */
    static constexpr bool opens_on_continuation(const char_class& first_bytes)
    {
      for (unsigned b {0x80U}; b <= 0xBFU; ++b) {
        if (first_bytes.test(static_cast<std::uint8_t>(b))) {
          return true;
        }
      }
      return false;
    }

    /*!
     * \brief Emits the full NFA program for the bound AST.
     * \return The compiled \ref dynamic_program (code, classes, names, hints).
     * \throws real::regex_error if the program exceeds \ref max_program_size.
     */
    constexpr dynamic_program compile()
    {
      dynamic_program prog;
      prog.slot_count = static_cast<std::uint16_t>(2 * (tree_.group_count + 1));
      prog.names      = tree_.names;
      emit(prog, {.op = opcode::save, .arg16 = 0});
      emit_node(prog, tree_.root);
      emit(prog, {.op   = opcode::save, .arg16 = 1});
      emit(prog, {.op   = opcode::match});
      prog.byte_mode    = has_flag(flags_, flags::bytes);
      prog.unicode_word = !has_flag(flags_, flags::bytes) && !has_flag(flags_, flags::ascii);
      prog.hints        = analyze_program(prog.code, prog.classes, prog.cp_classes, prog.cp_ranges,
                                          prog.codepoint_mark_ascii, prog.codepoint_mark_offset,
                                          prog.codepoint_mark_end, prog.lookarounds);
      // capture_free_walk also needs slot_count == 2: analyze_program sees only the code, and a possessive
      // with a `\b` wrap (`\b(\w)*+`) writes its group from the fast path with no `save`. Must follow the
      // assignment above, which overwrites `prog.hints` wholesale.
      if (prog.slot_count != 2U) {
        prog.hints.capture_free_walk = false;
      }
      // A lookaround is evaluated at whatever position a thread reaches it, and the backtracker reaches
      // positions out of order -- which a lookbehind walk pays for by restarting. The VM keeps those.
      prog.hints.bounded_backtrack = static_cast<std::uint8_t>(prog.lookarounds.empty()
                                                               && prog.slot_count <= bounded_backtrack_max_slots);
      // The required inner literal and its prefix boundary go to the hints only, never into the code.
      const inner_literal il {extract_inner_literal(tree_)};
      prog.hints.inner_literal             = il.bytes;
      prog.hints.inner_literal_len         = il.len;
      if (il.len >= 2) {
        std::array<char, 16> chars {};
        for (std::size_t k = 0; k < il.len; ++k) {
          chars[k] = static_cast<char>(il.bytes[k]);
        }
        prog.hints.inner_literal_rare = literal_rarest_offset(std::string_view {chars.data(), il.len});
      }
      prog.hints.inner_literal_prefix      = il.prefix_child_count;
      // Peel-lead skip for the reverse prefix (build_prefix_ast), non-zero only when the IL route is live;
      // confirm_at still runs the full program, lead/trail `\b`/`\B` included.
      prog.hints.inner_literal_prefix_skip =
        (il.len > 0 && il.prefix_child_count >= 1 && il.prefix_skip > 0)
          ? static_cast<std::uint8_t>(il.prefix_skip)
          : std::uint8_t {0};
      // il_rev_class: the program opens with `save… <class atom> split(back, out) save… byte(literal)`.
      // Read off the code, not the AST, because the answer is a class index. A fixed count (`\d{4}-…`)
      // emits no `split` and keeps the general path.
      if (il.len > 0 && il.prefix_child_count == 1) {
        std::size_t pc {0};
        while (pc < prog.code.size() && prog.code[pc].op == opcode::save) {
          ++pc; // leading whole-match save, plus a capture group's open save
        }
        const std::size_t atom  {pc};
        std::size_t       after {pc};
        bool              is_cp {false};
        if (atom < prog.code.size() && prog.code[atom].op == opcode::klass) {
          after = atom + 1;
        }
        else if (atom < prog.code.size() && prog.code[atom].op == opcode::klass_cp) {
          after = atom + 4; // klass_cp is a four-slot construct (see run_cp_class_loop's `pc += 3`)
          is_cp = true;
        }
        if (after > atom && after < prog.code.size() && prog.code[after].op == opcode::split
            && prog.code[after].primary_target == static_cast<std::int32_t>(atom)
            && prog.code[after].secondary_target == static_cast<std::int32_t>(after) + 1) {
          std::size_t lit {after + 1};
          while (lit < prog.code.size() && prog.code[lit].op == opcode::save) {
            ++lit; // the group's closing save, and the next group's opening one
          }
          if (inner_literal_starts_at(prog, lit)) {
            prog.hints.il_rev_class = static_cast<std::int32_t>(prog.code[atom].arg16);
            prog.hints.il_rev_is_cp = is_cp;

            // il_fwd_class: the whole pattern is `class+ <literal> class+` (then only saves and `match`).
            // Every op is checked, so any assertion or trailing structure leaves the hint clear.
            std::size_t tail {lit};
            while (tail < prog.code.size() && prog.code[tail].op == opcode::byte) {
              ++tail; // a multi-byte literal is several `byte` ops
            }
            while (tail < prog.code.size() && prog.code[tail].op == opcode::save) {
              ++tail;
            }
            const std::size_t suffix  {tail};
            std::size_t       past    {tail};
            bool              tail_cp {false};
            if (suffix < prog.code.size() && prog.code[suffix].op == opcode::klass) {
              past = suffix + 1;
            }
            else if (suffix < prog.code.size() && prog.code[suffix].op == opcode::klass_cp) {
              past    = suffix + 4;
              tail_cp = true;
            }
            if (past > suffix && past < prog.code.size() && prog.code[past].op == opcode::split
                && prog.code[past].primary_target == static_cast<std::int32_t>(suffix)
                && prog.code[past].secondary_target == static_cast<std::int32_t>(past) + 1) {
              std::size_t end {past + 1};
              while (end < prog.code.size() && prog.code[end].op == opcode::save) {
                ++end;
              }
              // The runs place the literal where the prefix run stops, which is the greedy answer only if
              // the literal cannot occur inside that run (`[abx]+ba[ab]+` over "aaaxbabaaxbab" wants the
              // last `ba`). When it can, the span still holds if the suffix run covers every prefix and
              // literal byte, but groups must split at the last occurrence (il_fwd_last: `(\w+)_(\w+)`
              // over "a_b_c" is "a_b" and "c").
              const auto class_bytes {[&](std::size_t at, bool cp) {
                                        std::array<bool, 256> bytes {};
                                        for (unsigned b {0}; b < 256U; ++b) {
                                          bytes[b] = cp ? (b >= 0x80U || prog.cp_classes[prog.code[at].arg16].ascii.test(static_cast<std::uint8_t>(b)))
                                : prog.classes[prog.code[at].arg16].test(static_cast<std::uint8_t>(b));
                                        }
                                        return bytes;
                                      }};
              const std::array<bool, 256> run_bytes   {class_bytes(atom, is_cp)};
              const std::array<bool, 256> tail_bytes  {class_bytes(suffix, tail_cp)};
              bool                        tail_covers {true};
              for (unsigned b {0}; b < 256U && tail_covers; ++b) {
                tail_covers = !run_bytes[b] || tail_bytes[b];
              }
              for (std::size_t i {0}; i < il.len && tail_covers; ++i) {
                tail_covers = tail_bytes[il.bytes[i]];
              }
              if (end + 1 == prog.code.size() && prog.code[end].op == opcode::match
                  && (!inner_literal_detail::can_occur_in_prefix(il, run_bytes) || tail_covers)) {
                prog.hints.il_fwd_class = static_cast<std::int32_t>(prog.code[suffix].arg16);
                prog.hints.il_fwd_is_cp = tail_cp;
                prog.hints.il_fwd_last  = inner_literal_detail::can_occur_in_prefix(il, run_bytes);
                // One class on both sides: a literal of members takes the prefix run on to the suffix's end, and
                // one holding a non-member cannot recur ahead of the candidate inside the suffix run.
                prog.hints.il_fwd_run_to_end = prog.hints.il_fwd_last && is_cp == tail_cp
                                               && prog.code[atom].arg16 == prog.code[suffix].arg16;
              }
            }
          }
        }
      }
      // il_cp_shape_eligible: saves plus a fixed sequence of code-point atoms and literal bytes, no split,
      // jump or assertion. It covers the `klass_cp` shapes the byte-width `fixed_shape` route cannot.
      if (il.len > 0 && il.prefix_child_count >= 1) {
        std::size_t pc          {0};
        std::size_t cps         {0};
        bool        ok          {true};
        bool        hit_literal {false};
        while (pc < prog.code.size() && ok) {
          const opcode op {prog.code[pc].op};
          if (op == opcode::save) {
            ++pc;
          }
          else if (op == opcode::klass_cp) {
            pc  += 4; // the four-slot construct (see run_cp_class_loop's `pc += 3`)
            cps += hit_literal ? 0U : 1U;
          }
          else if (op == opcode::klass) {
            ++pc;
            cps += hit_literal ? 0U : 1U;
          }
          else if (op == opcode::byte) {
            if (!hit_literal) {
              // The whole literal, not a byte equal to its first (see inner_literal_starts_at).
              ok          = inner_literal_starts_at(prog, pc);
              hit_literal = true;
            }
            ++pc;
          }
          else if (op == opcode::match) {
            break;
          }
          else {
            ok = false; // a split, jump, assertion or lookaround: not a fixed sequence
          }
        }
        if (ok && hit_literal && cps > 0 && cps <= 255 && pc < prog.code.size()
            && prog.code[pc].op == opcode::match) {
          prog.hints.il_cp_shape_eligible = true;
          prog.hints.il_cp_prefix_cps     = static_cast<std::uint8_t>(cps);
        }
      }
      // Read off the AST (the program loses the group-under-quantifier structure). The compat layer
      // routes replace/iterate to std for such patterns: their nullable loop captures the last consuming
      // iteration, where an ECMAScript backtracker adds an empty one.
      prog.hints.nullable_captured_repeat = ast_has_nullable_captured_repeat(tree_, tree_.root);
      // In text mode a match never starts on a UTF-8 continuation byte (pike_vm::seed_viable). A pattern
      // that can open on one is left to the VM and the DFAs, which hold that rule; the literal, alternation
      // and loop routes do not. Last, so it clears every route hint set above; the compat-layer facts stay.
      // Under allow_raw_byte a `\C` lead may start inside a code point, as in RE2, and keeps its routes.
      prog.hints.raw_byte_starts = has_flag(flags_, flags::allow_raw_byte);
      if (!prog.byte_mode && !prog.hints.raw_byte_starts && prog.hints.first_bytes_valid
          && opens_on_continuation(prog.hints.first_bytes)) {
        pattern_hints facts {};
        facts.empty_match_possible     = prog.hints.empty_match_possible;
        facts.nullable_captured_repeat = prog.hints.nullable_captured_repeat;
        prog.hints                     = facts;
      }
      if (prog.code.size() > max_program_size) {
        throw regex_error(std::string {program_too_large}, 0);
      }
      return prog;
    }

  private:

    const ast& tree_;                //!< The AST being compiled.
    flags      flags_ {flags::none}; //!< Effective compilation flags.

    // --- low-level emission helpers -------------------------------------

    /*!
     * \brief Returns the index of the next instruction.
     * \param[in] prog The program.
     * \return The index of the next instruction.
     */
    static constexpr std::int32_t here(const dynamic_program& prog)
    {
      return static_cast<std::int32_t>(prog.code.size());
    }

    /*!
     * \brief Appends one instruction, enforcing the program-size cap.
     *
     * The check lives here so it fires during a large unroll, before the vector reaches the bad size:
     * the defense against nested bounded quantifiers expanding to hundreds of millions of
     * instructions. Exceeding the cap fails compilation of a `static_regex`, or throws at run time.
     *
     * \param[in,out] prog        The program being built.
     * \param[in]     instruction The instruction to append.
     * \throws real::regex_error when \ref max_program_size would be exceeded.
     */
    static constexpr void emit(dynamic_program& prog,
                               instr            instruction)
    {
      if (prog.code.size() >= max_program_size) {
        throw regex_error(std::string {program_too_large}, 0);
      }
      prog.code.push_back(instruction);
    }

    /*!
     * \brief Emits a `split` with placeholder targets.
     * \param[in,out] prog The program being built.
     * \return Its instruction index, for the caller to patch.
     */
    static constexpr std::int32_t emit_split(dynamic_program& prog)
    {
      emit(prog, {.op = opcode::split, .primary_target = -1, .secondary_target = -1});
      return here(prog) - 1;
    }

    /*!
     * \brief Emits a `jump` with a placeholder target.
     * \param[in,out] prog The program being built.
     * \return Its instruction index, for the caller to patch.
     */
    static constexpr std::int32_t emit_jump(dynamic_program& prog)
    {
      emit(prog, {.op = opcode::jump, .primary_target = -1});
      return here(prog) - 1;
    }

    /*!
     * \brief Sets the primary branch target of the instruction at \p pc.
     * \param[in,out] prog   The program being built.
     * \param[in]     pc     Index of the split/jump to patch.
     * \param[in]     target Instruction index to branch to.
     */
    static constexpr void patch_primary(dynamic_program& prog,
                                        std::int32_t     pc,
                                        std::int32_t     target)
    {
      prog.code[static_cast<std::size_t>(pc)].primary_target = target;
    }

    /*!
     * \brief Sets the secondary branch target of the split at \p pc.
     * \param[in,out] prog   The program being built.
     * \param[in]     pc     Index of the split to patch.
     * \param[in]     target Instruction index to branch to.
     */
    static constexpr void patch_secondary(dynamic_program& prog,
                                          std::int32_t     pc,
                                          std::int32_t     target)
    {
      prog.code[static_cast<std::size_t>(pc)].secondary_target = target;
    }

    /*!
     * \brief Interns \p klass into `prog.classes` (deduplicating), returning its index.
     *
     * Shared by \ref emit_klass and `klass_loop_possessive`. Identical bitmaps share one slot.
     *
     * \param[in,out] prog  The program being built.
     * \param[in]     klass The class bitmap to intern.
     * \return Its index in `prog.classes`.
     * \throws real::regex_error if more than 65536 distinct classes are needed.
     */
    static constexpr std::uint16_t intern_class(dynamic_program&  prog,
                                                const char_class& klass)
    {
      std::size_t index {prog.classes.size()};
      for (std::size_t i = 0; i < prog.classes.size(); ++i) {
        if (prog.classes[i] == klass) {
          index = i;
          break;
        }
      }
      if (index == prog.classes.size()) {
        if (index > 0xFFFF) {
          throw regex_error("too many character classes", 0);
        }
        prog.classes.push_back(klass);
      }
      return static_cast<std::uint16_t>(index);
    }

    /*!
     * \brief Emits a `klass` instruction, interning \p klass through \ref intern_class.
     *
     * \param[in,out] prog  The program being built.
     * \param[in]     klass The class bitmap to match.
     * \throws real::regex_error if more than 65536 distinct classes are needed.
     */
    static constexpr void emit_klass(dynamic_program&  prog,
                                     const char_class& klass)
    {
      emit(prog, {.op = opcode::klass, .arg16 = intern_class(prog, klass)});
    }

    /*!
     * \brief Interns \p cd into `prog.cp_classes`/`prog.cp_ranges` (deduplicating), returning its index.
     *
     * Shared by \ref emit_klass_cp and `klass_cp_loop_possessive`, for any effective class.
     *
     * \param[in,out] prog The program being built.
     * \param[in]     cd   The effective code-point class (ASCII bitmap + non-ASCII ranges).
     * \return Its index in `prog.cp_classes`.
     */
    // Public so a test can intern an unordered list (no pattern produces one) and see the gate fire;
    // nothing here differs between release and test builds.

  public:

    static constexpr std::uint16_t intern_cp_class(dynamic_program& prog,
                                                   const class_def& cd)
    {
      // The one gate every code-point class passes: a producer that skips coalesce_ranges fails loudly. A
      // throw, not an assert, so it fires in release and stops an unordered `static_regex`. New classes
      // only: checking every call added 36% to `(?i:\w{256})\w{256}` against the compile-scaling bound.
      std::size_t index {prog.cp_classes.size()};
      for (std::size_t i = 0; i < prog.cp_classes.size(); ++i) {
        const cp_class& existing {prog.cp_classes[i]};
        if (!(existing.ascii == cd.ascii) || existing.range_count != cd.ranges.size()) {
          continue;
        }
        bool same {true};
        for (std::uint32_t k = 0; k < existing.range_count; ++k) {
          const code_range& a {prog.cp_ranges[existing.range_begin + k]};
          if (a.lo != cd.ranges[k].lo || a.hi != cd.ranges[k].hi) {
            // Same bitmap and count, different ranges: never merge. No current emitter reaches this arm.
            same = false;
            break;
          }
        }
        if (same) {
          index = i;
          break;
        }
      }
      if (index == prog.cp_classes.size()) {
        if (!cp_ranges_are_normalised(cd.ranges)) {
          throw regex_error("code-point class ranges are not normalised", 0);
        }
        if (index > 0xFFFF) {
          throw regex_error("too many code-point classes", 0);
        }
        const auto begin {static_cast<std::uint32_t>(prog.cp_ranges.size())};
        for (const code_range& r : cd.ranges) {
          prog.cp_ranges.push_back(r);
        }
        const auto n           {static_cast<std::uint32_t>(cd.ranges.size())};
        // Fingerprinted once here; the match-time cp_hi cache reads it and never re-hashes.
        const std::uint64_t fp {fingerprint_cp_class_content(
                                  cd.ascii, n == 0 ? nullptr : &prog.cp_ranges[begin], n)};
        prog.cp_classes.push_back({.ascii       = cd.ascii,
                                   .range_begin = begin,
                                   .range_count = n,
                                   .fingerprint = fp});
      }
      return static_cast<std::uint16_t>(index);
    }

  private:

    /*!
     * \brief Emits a code-point class in text mode: `klass_cp`, then three `klass utf8_cont` slots.
     *        `klass_cp` decodes one code point and, on membership, enters the chain at a computed skip.
     *        \p cd is already effective (fold and negation applied), so membership is a positive test.
     *
     * \param[in,out] prog The program being built.
     * \param[in]     cd   The effective code-point class (ASCII bitmap + non-ASCII ranges).
     */
    static constexpr void emit_klass_cp(dynamic_program& prog,
                                        const class_def& cd)
    {
      emit(prog, {.op = opcode::klass_cp, .arg16 = intern_cp_class(prog, cd)});
      emit_klass(prog, utf8_cont_set()); // three continuation slots; klass_cp's skip picks the entry
      emit_klass(prog, utf8_cont_set());
      emit_klass(prog, utf8_cont_set());
    }

    static constexpr std::size_t fixed_width_class_max_members {8}; //!< Most members a \ref try_emit_fixed_width_class candidate may have.

    /*!
     * \brief Emits a small non-ASCII class as fixed-width bytes when every member encodes to the same
     *        length and they differ in exactly one byte position. Returns false otherwise.
     *
     * An icase accented letter (`é`/`É` = `C3 A9`/`C3 89`) becomes `byte C3` plus a byte class: fixed
     * width, so the prefilter's fixed-offset walk and the literal routes still apply. Deliberately
     * narrow: a class needing mixed lengths (`(?i)[a-z]` gains 2- and 3-byte members) would need an
     * alternation, slower than `klass_cp`. One length and one varying position never emit a branch.
     *
     * \param[in,out] prog The program being built.
     * \param[in]     eff  The effective class (ASCII bitmap + non-ASCII ranges).
     * \param[in]     probe_only When `true`, answers whether the shape matches and emits nothing
     *                 (\ref emit_unbounded_body asks, as this form is wrong under an unbounded quantifier).
     * \return `true` if the class was emitted here (or matches, when probing); `false` if the caller
     *         must emit it otherwise.
     */
    static constexpr bool try_emit_fixed_width_class(dynamic_program& prog,
                                                     const class_def& eff,
                                                     bool             probe_only = false)
    {
      if (!eff.ascii.empty()) {
        return false; // a mixed ASCII/non-ASCII class is variable width by construction
      }
      std::uint32_t cps[fixed_width_class_max_members] {};
      std::size_t   n                                  {0};
      for (const code_range& r : eff.ranges) {
        for (std::uint32_t c = r.lo; c <= r.hi; ++c) {
          if (n == fixed_width_class_max_members) {
            return false; // too many members to be a fold pair or a small hand-written class
          }
          cps[n++] = c;
          if (c == 0xFFFFFFFFU) {
            break; // unreachable for a valid class; guards the ++c overflow
          }
        }
      }
      if (n == 0) {
        return false;
      }
      std::uint8_t enc[fixed_width_class_max_members][4] {};
      std::size_t  len                                   {0};
      for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t      b[4] {};
        const std::size_t l    {encode_utf8_bytes(cps[i], b)};
        if (i == 0) {
          len = l;
        }
        else if (l != len) {
          return false; // mixed lengths: only an alternation could express this, and that loses
        }
        for (std::size_t k = 0; k < 4; ++k) {
          enc[i][k] = b[k];
        }
      }
      std::size_t varying {len}; // len == "no varying position seen yet"
      for (std::size_t k = 0; k < len; ++k) {
        bool same {true};
        for (std::size_t i = 1; i < n; ++i) {
          if (enc[i][k] != enc[0][k]) {
            same = false;
            break;
          }
        }
        if (same) {
          continue;
        }
        if (varying != len) {
          return false; // two positions differ: a byte-wise form would need a branch
        }
        varying = k;
      }
      if (probe_only) {
        return true; // the shape matches; the caller wants only the answer
      }
      for (std::size_t k = 0; k < len; ++k) {
        if (k == varying) {
          char_class cc;
          for (std::size_t i = 0; i < n; ++i) {
            cc.set(enc[i][k]);
          }
          emit_klass(prog, cc);
        }
        else {
          emit(prog, {.op = opcode::byte, .arg8 = enc[0][k]});
        }
      }
      return true;
    }

    // --- UTF-8 byte expansion --------------------------------------------

    /*!
     * \brief Emits "one codepoint matching \p ascii, or any non-ASCII codepoint".
     *
     * The non-ASCII part goes through \ref emit_class_codepoints, whose canonical splitting narrows
     * the first continuation byte after `0xE0`, `0xED`, `0xF0` and `0xF4`, so overlong and surrogate
     * encodings never read as a code point. Flat lead/continuation classes would accept them.
     *
     * \param[in,out] prog  The program being built.
     * \param[in]     ascii The accepted ASCII bytes (non-ASCII is always included).
     */
    constexpr void emit_any_codepoint_class(dynamic_program&  prog,
                                            const char_class& ascii) const
    {
      const std::int32_t block_start {here(prog)}; // start offset, recorded as the marker below
      emit_class_codepoints(prog, ascii, {{.lo = 0x80U, .hi = 0x10FFFFU}});
      const std::int32_t block_end   {here(prog)};

      // The marker spares analyze_program from reverse-engineering this block. The ASCII sub-class
      // sits at code[block_start + 1] whenever `ascii` is non-empty (always for `.`); for
      // `[^\x00-\x7F]` this reads a byte-range class, which the prefilter's ASCII-only guard rejects.
      prog.codepoint_mark_offset = block_start;
      prog.codepoint_mark_end    = block_end;
      prog.codepoint_mark_ascii  = static_cast<std::int32_t>(prog.code[static_cast<std::size_t>(block_start) + 1].arg16);
    }

    /*!
     * \brief Emits an alternation of byte-range sequences as split/jump; each branch is a chain of
     *        `klass` steps and the leftmost matching branch wins.
     *
     * \param[in,out] prog     The program being built.
     * \param[in]     branches One byte-range chain per branch, tried in order.
     */
    constexpr void emit_byte_sequences(dynamic_program&                            prog,
                                       const std::vector<std::vector<char_class>>& branches) const
    {
      std::vector<std::int32_t> jumps;
      for (std::size_t b = 0; b + 1 < branches.size(); ++b) {
        const std::int32_t split {emit_split(prog)};
        patch_primary(prog, split, here(prog));
        for (const char_class& step : branches[b]) {
          emit_klass(prog, step);
        }
        jumps.push_back(emit_jump(prog));
        patch_secondary(prog, split, here(prog));
      }
      for (const char_class& step : branches.back()) {
        emit_klass(prog, step);
      }
      const std::int32_t end {here(prog)};
      for (const std::int32_t jump : jumps) {
        patch_primary(prog, jump, end);
      }
    }

    /*!
     * \brief Emits a code-point class: the ASCII bitmap (if any) OR the canonical UTF-8 byte
     *        sequences of each code-point range.
     *
     * \param[in,out] prog   The program being built.
     * \param[in]     ascii  The class's ASCII bitmap; skipped when empty.
     * \param[in]     ranges Its non-ASCII code-point ranges.
     */
    constexpr void emit_class_codepoints(dynamic_program&               prog,
                                         const char_class&              ascii,
                                         const std::vector<code_range>& ranges) const
    {
      std::vector<std::vector<char_class>> branches;
      if (!ascii.empty()) {
        branches.push_back({ascii});
      }
      for (const code_range& range : ranges) {
        for (const utf8_byte_seq& seq : utf8_range_sequences(range.lo, range.hi)) {
          std::vector<char_class> branch;
          for (std::size_t i = 0; i < seq.length; ++i) {
            char_class step;
            step.set_range(seq.parts[i].lo, seq.parts[i].hi);
            branch.push_back(step);
          }
          branches.push_back(branch);
        }
      }
      if (branches.empty()) {
        // An impossible class (the negation of every code point): an empty bitmap never matches.
        emit_klass(prog, char_class {});
        return;
      }
      emit_byte_sequences(prog, branches);
    }

    /*!
     * \brief The class a `node_kind::klass` node effectively accepts, after negation, icase folding
     *        and the bytes/code-point split. The one source for both \ref emit_node and
     *        \ref l_max_bytes, so the emission and its measured width cannot disagree.
     *
     * \param[in] node The `node_kind::klass` node.
     * \return The set it accepts, after negation, folding and the bytes/code-point split.
     */
    [[nodiscard]] constexpr class_def effective_class(const ast_node& node) const
    {
      // icase and ascii come from the node's own scope; bytes is not scopable and stays global.
      const flags node_flags {static_cast<flags>(node.effective_flags)};
      const auto  klass_idx  {static_cast<std::size_t>(node.klass)};

      // Fold mode: 0 none, 1 ASCII-only, 2 full Unicode.
      std::size_t mode {0};
      if (has_flag(node_flags, flags::icase)) {
        mode = (has_flag(flags_, flags::bytes) || has_flag(node_flags, flags::ascii)) ? 1U : 2U;
      }

      // A bounded repeat emits its one class node per repetition (`\w{500}` folds 500 times), so the
      // fold is cached: four direct-mapped ways in a fixed array. Keep it unallocated: a vector sized by
      // the class table cost patterns that fold once up to 19 %. Keyed by (class, mode, negated): a
      // scoped `(?i:...)` can fold one class two ways.
      if (mode != 0 && !std::is_constant_evaluated()) {
        // Cache the finished class, negation included: caching only the fold leaves finish_class's
        // coalesce_ranges sort per repetition (icase `[a-z]` cost 24x its plain marginal).
        const auto        key {static_cast<std::int32_t>((klass_idx * 6U) + (mode * 2U) + (node.negated ? 1U : 0U))};
        const std::size_t way {static_cast<std::size_t>(key) % fold_cache_ways};
        if (fold_key_[way] == key) {
          return fold_val_[way];
        }
        class_def folded {tree_.classes[klass_idx]};
        if (mode == 1) {
          fold_ascii_case(folded.ascii);     // bytes / ASCII mode (re.A): ASCII-only fold, no Unicode partners
        }
        else {
          folded = unicode_casefold(folded); // text: full Unicode fold of the whole class, both directions
        }
        fold_key_[way] = key;
        fold_val_[way] = finish_class(node, std::move(folded));
        return fold_val_[way];
      }
      class_def folded {tree_.classes[klass_idx]};
      if (mode == 1) {
        fold_ascii_case(folded.ascii);
      }
      else if (mode == 2) {
        folded = unicode_casefold(folded);
      }
      return finish_class(node, std::move(folded));
    }

    /*!
     * \brief Applies negation (and its mode-dependent complement) to an already-folded class.
     * \param[in] node   The class node being emitted.
     * \param[in] folded Its class after any case fold.
     * \return The class as the node means it.
     */
    [[nodiscard]] constexpr class_def finish_class(const ast_node& node,
                                                   class_def       folded) const
    {
      // Fold before negation (Python order): icase [^k] is the complement of {k, K, Kelvin}.
      if (!node.negated) {
        // Members arrive in parse order (`[\dЩ]`), and the matchers need them sorted and merged. The
        // negated path is normalised by complement_code_ranges.
        folded.ranges = coalesce_ranges(std::move(folded.ranges));
        return folded;
      }
      if (has_flag(flags_, flags::bytes)) {
        folded.ascii.invert(); // raw bytes: plain 256-bit complement, no code-point ranges
        return {.ascii = folded.ascii, .ranges = {}};
      }
      folded.ascii.invert_ascii();
      return {.ascii = folded.ascii, .ranges = complement_code_ranges(folded.ranges)};
    }

    // --- node emission ----------------------------------------------------

    /*!
     * \brief Emits the bytecode for the AST node at \p index (recursively).
     * \param[in,out] prog         The program being built.
     * \param[in]     index        Index of the node in \ref ast::nodes.
     * \param[in]     capture_free When true, capturing groups emit no `save` ops — used
     *                             inside a lookaround sub-program, whose captures do not
     *                             participate in the overall match.
     */
    constexpr void emit_node(dynamic_program& prog,
                             std::int32_t     index,
                             bool             capture_free = false) const
    {
      const ast_node& node {tree_.nodes[static_cast<std::size_t>(index)]};
      switch (node.kind) {
        case node_kind::empty:
          break;
        case node_kind::byte:
          // Never case-folded: under icase the parser turns a cased literal into a singleton class, so a
          // `byte` node is an escape or a non-cased literal (the `\xHH` provenance split, divergences.dox).
          emit(prog, {.op = opcode::byte, .arg8 = node.byte});
          break;
        case node_kind::klass:
          {
            // A Unicode shorthand (\w/\d/\s) in text mode: a match-time code-point predicate. The stored
            // class is already effective, so it carries no negation flag ([^\W] == \w).
            if (tree_.classes[static_cast<std::size_t>(node.klass)].codepoint_predicate) {
              emit_klass_cp(prog, effective_class(node));
              break;
            }
            const class_def eff {effective_class(node)};
            if (has_flag(flags_, flags::bytes) || eff.ranges.empty()) {
              // Bytes mode, or no non-ASCII member: one bitmap (empty = never-match).
              emit_klass(prog, eff.ascii);
              break;
            }
            if (is_any_non_ascii(eff.ranges)) {
              // `.`-family / `[^x]`: the shape the prefilter's codepoint_class_ascii route recognises.
              emit_any_codepoint_class(prog, eff.ascii);
              break;
            }
            // A bitmap plus a few non-ASCII members (icase `[a-z]` gains the long s and Kelvin) stays a
            // `klass_cp`: as a byte-level alternation `(?i)[a-z]+` loses the class-loop route and falls to
            // the lazy DFA, several times slower. `klass_cp` also keeps one-pass eligibility
            // (build_utf8_trie) and `real::dfa` (dfa_flatten). Same-length members differing in one byte
            // (`(?i)é`) go fixed-width instead (try_emit_fixed_width_class).
            emit_effective_class(prog, eff);
            break;
          }
        case node_kind::any:
          if (node.raw_byte) {
            // \C (RE2's raw-byte escape): any byte, '\n' included regardless of dotall.
            char_class all;
            all.set_range(0x00, 0xFF);
            emit_klass(prog, all);
            break;
          }
          {
            // dotall comes from the node's own scope; bytes and ecma are not scopable and stay global.
            const flags node_flags {static_cast<flags>(node.effective_flags)};
            char_class  head;
            head.set_range(0x00, 0x7F);
            if (has_flag(flags_, flags::bytes)) {
              head.set_range(0x80, 0xFF); // any raw byte
            }
            if (!has_flag(node_flags, flags::dotall)) {
              char_class newline;
              newline.set('\n');
              if (has_flag(flags_, flags::ecma)) {
                newline.set('\r'); // ECMAScript `.` excludes \n AND \r (byte-level; U+2028/2029 are multi-byte)
              }
              head.bits[0] &= ~newline.bits[0];
            }
            if (has_flag(flags_, flags::bytes)) {
              emit_klass(prog, head);
            }
            else {
              emit_any_codepoint_class(prog, head);
            }
            break;
          }
        case node_kind::anchor:
          {
            // ^/$ follow the node's own multiline. arg16 is a flip bit: 1 when the node's word-ness
            // differs from the program default (a scoped (?a:...) / (?-a:...) island).
            const flags node_flags   {static_cast<flags>(node.effective_flags)};
            const bool  prog_unicode {!has_flag(flags_, flags::bytes) && !has_flag(flags_, flags::ascii)};
            const bool  node_unicode {!has_flag(flags_, flags::bytes) && !has_flag(node_flags, flags::ascii)};
            emit(prog, {.op    = opcode::assert_position,
                        .arg8  = static_cast<std::uint8_t>(assert_kind_for(node.anchor, node_flags)),
                        .arg16 = node_unicode != prog_unicode ? std::uint16_t {1} : std::uint16_t {0}});
          }
          break;
        case node_kind::concat:
          for (std::int32_t child = node.child; child != -1;
               child              = tree_.nodes[static_cast<std::size_t>(child)].next) {
            emit_node(prog, child, capture_free);
          }
          break;
        case node_kind::repeat:
          emit_repeat(prog, node, capture_free);
          break;
        case node_kind::alternation:
          emit_alternation(prog, node, capture_free);
          break;
        case node_kind::group:
          if (node.possessive) {
            // Atomic group `(?>...)`: not capturing itself, but captures in its body stay live.
            emit_atomic_group(prog, node, capture_free);
          }
          else if (node.group >= 0 && !capture_free) {
            emit(prog, {.op = opcode::save, .arg16 = static_cast<std::uint16_t>(2 * node.group)});
            emit_node(prog, node.child, capture_free);
            emit(prog, {.op = opcode::save, .arg16 = static_cast<std::uint16_t>((2 * node.group) + 1)});
          }
          else {
            // capture-free (a lookaround sub-pattern) or non-capturing group.
            emit_node(prog, node.child, capture_free);
          }
          break;
        case node_kind::lookaround:
          emit_lookaround(prog, node, capture_free);
          break;
      }
    }

    /*!
     * \brief Maps an AST \ref anchor_kind to the runtime \ref assert_kind.
     *
     * `^` and `$` depend on multiline (from the anchor's scope) and the global ecma flag; everything
     * else maps one-to-one.
     *
     * \param[in] anchor     The AST anchor kind.
     * \param[in] node_flags The flags in force at this anchor's scope.
     * \return The assertion the engine should evaluate.
     */
    [[nodiscard]] constexpr assert_kind assert_kind_for(anchor_kind anchor,
                                                        flags       node_flags) const
    {
      const bool  multiline {has_flag(node_flags, flags::multiline)};
      assert_kind result    {};
      switch (anchor) {
        case anchor_kind::caret:
          if (multiline) {
            result = has_flag(flags_, flags::ecma) ? assert_kind::line_start_cr : assert_kind::line_start;
          }
          else {
            result = assert_kind::text_start;
          }
          break;
        case anchor_kind::dollar:
          // Python: end or before a final `\n`; ecma or dollar_endonly: the very end only.
          if (multiline) {
            result = has_flag(flags_, flags::ecma) ? assert_kind::line_end_cr : assert_kind::line_end;
          }
          else if (has_flag(flags_, flags::ecma) || has_flag(flags_, flags::dollar_endonly)) {
            result = assert_kind::text_end;
          }
          else {
            result = assert_kind::text_end_or_final_newline;
          }
          break;
        case anchor_kind::text_start:
          result = assert_kind::text_start;
          break;
        case anchor_kind::text_end:
          result = assert_kind::text_end;
          break;
        case anchor_kind::word_boundary:
          result = assert_kind::word_boundary;
          break;
        case anchor_kind::not_word_boundary:
          result = assert_kind::not_word_boundary;
          break;
        case anchor_kind::word_start:
          result = assert_kind::word_start;
          break;
        case anchor_kind::word_end:
          result = assert_kind::word_end;
          break;
      }
      return result;
    }

    /*!
     * \brief The class an alternation of single ATOMS is, when it is one.
     *
     * `(?:é|à|è)` is `[éàè]`, which the shape recognisers see and a split chain hides. Exact: every
     * branch consumes one atom and none captures, so leftmost-first preference is unobservable. Both
     * \ref emit_alternation and \ref emit_unbounded_body ask here; they differ only in how they emit.
     *
     * \param[in]  node The alternation node.
     * \param[out] out  The fused class, valid only when this returns `true`.
     * \return `true` if every branch is one atom and there are at least two of them.
     */
    [[nodiscard]] constexpr bool fuse_single_atom_alternation(const ast_node& node,
                                                              class_def&      out) const
    {
      std::int32_t scan {node.child};
      std::size_t  n    {0};
      while (scan != -1) {
        const ast_node& b {tree_.nodes[static_cast<std::size_t>(scan)]};
        if (b.kind == node_kind::byte) {
          out.ascii.set(b.byte);
        }
        else if (const std::uint32_t cp {single_codepoint_atom(tree_, scan)};
                 cp != not_a_single_codepoint && !has_flag(flags_, flags::bytes)) {
          // UTF-8 bytes spelling one character; the parser and emit_unbounded_body ask the same predicate.
          out.ranges.push_back({.lo = cp, .hi = cp});
        }
        else {
          return false; // a class, a sequence, a group, or an EMPTY branch (`a|` matches empty)
        }
        ++n;
        scan = b.next;
      }
      if (n < 2) {
        return false; // one branch is not an alternation to fuse
      }
      // Branch order is the author's spelling; the matchers need sorted, disjoint ranges
      // (cp_ranges_are_normalised: unsorted, `\U0001F968|é` matches neither branch). Same call as
      // finish_class, so `(?:é|à|è)` and `[éàè]` become one program.
      out.ranges = coalesce_ranges(std::move(out.ranges));
      return true;
    }

    /*!
     * \brief Emits an already-materialised class the one way this compiler emits classes.
     *
     * `(?:é|à|è)` and `[éàè]` must become the same program, so fusion emits through here. Do not
     * special-case a standalone non-ASCII class: arm64 prefers `klass_cp` and x86 the fixed-width form
     * by comparable margins, so no choice wins on both. After a literal both prefer fixed width.
     *
     * \param[in,out] prog The program being built.
     * \param[in]     eff  The effective class (ASCII bitmap + non-ASCII ranges).
     */
    constexpr void emit_effective_class(dynamic_program& prog,
                                        const class_def& eff) const
    {
      if (has_flag(flags_, flags::bytes) || eff.ranges.empty()) {
        emit_klass(prog, eff.ascii);
        return;
      }
      if (is_any_non_ascii(eff.ranges)) {
        emit_any_codepoint_class(prog, eff.ascii);
        return;
      }
      if (try_emit_fixed_width_class(prog, eff)) {
        return;
      }
      emit_klass_cp(prog, eff);
    }

    /*!
     * \brief Emits an alternation: branches chained with leftmost-preferred splits.
     *
     * Every branch but the last jumps to a shared exit, patched once at the end.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     node         The \ref node_kind::alternation node.
     * \param[in]     capture_free Propagated to each branch (see \ref emit_node).
     */
    constexpr void emit_alternation(dynamic_program& prog,
                                    const ast_node&  node,
                                    bool             capture_free) const
    {
      // An alternation of single atoms is a class (fuse_single_atom_alternation), emitted as one.
      {
        class_def fused;
        if (fuse_single_atom_alternation(node, fused)) {
          emit_effective_class(prog, fused);
          return;
        }
      }
      std::vector<std::int32_t> jumps;
      std::int32_t              branch {node.child};
      while (branch != -1) {
        const std::int32_t after {tree_.nodes[static_cast<std::size_t>(branch)].next};
        if (after != -1) {
          const std::int32_t s {emit_split(prog)};
          patch_primary(prog, s, here(prog));
          emit_node(prog, branch, capture_free);
          jumps.push_back(emit_jump(prog));
          patch_secondary(prog, s, here(prog));
        }
        else {
          emit_node(prog, branch, capture_free); // last branch: falls through
        }
        branch = after;
      }
      const std::int32_t end {here(prog)};
      for (const std::int32_t j : jumps) {
        patch_primary(prog, j, end);
      }
    }

    /*!
     * \brief Emits an UNBOUNDED quantifier's body, promoting a bare literal byte to a one-member
     *        byte class so the shape routes can see it.
     *
     * The class-loop recognizer matches `klass` only, so a bare `a+` would miss every fast route that
     * `[a]+` takes (over an order of magnitude). Only for `max == -1`: a bounded `a{3}` keeps its bytes,
     * a literal run the literal routes read.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     child        The quantifier's body node.
     * \param[in]     capture_free Propagated to \ref emit_node.
     */
    constexpr void emit_unbounded_body(dynamic_program& prog,
                                       std::int32_t     child,
                                       bool             capture_free) const
    {
      // Peel non-capturing, non-atomic groups, which would hide the atom from the promotions below.
      // Scoped flags survive: every node carries its own `effective_flags`. `(?>...)` is semantic.
      std::int32_t atom {child};
      while (atom >= 0) {
        const ast_node& w {tree_.nodes[static_cast<std::size_t>(atom)]};
        if (w.kind != node_kind::group || w.group >= 0 || w.possessive) {
          break;
        }
        atom = w.child;
      }
      if (atom < 0) {
        emit_node(prog, child, capture_free);
        return;
      }
      const ast_node& c {tree_.nodes[static_cast<std::size_t>(atom)]};
      if (c.kind == node_kind::byte && c.next < 0) {
        char_class one;
        one.set(c.byte);
        emit_klass(prog, one);
        return;
      }
      // A literal code point (`é` is a concat of its UTF-8 bytes) is `[é]`: a one-member code-point
      // class takes the cp-class route, where a repeated byte sequence takes the lazy DFA per match.
      // The strict decode refuses a concat of more than one code point (`(?:ab)+`, `(?:éé)+`).
      if (c.next < 0 && !has_flag(flags_, flags::bytes)) {
        const std::uint32_t cp {single_codepoint_atom(tree_, atom)};
        if (cp != not_a_single_codepoint) {
          class_def one;
          one.ranges.push_back({.lo = cp, .hi = cp});
          emit_klass_cp(prog, one);
          return;
        }
      }
      // A fused single-atom alternation goes `klass_cp` here too: the bare-class path would give the
      // fixed-width form, which has no route under a loop (several times slower).
      if (c.kind == node_kind::alternation && c.next < 0) {
        class_def fused;
        if (fuse_single_atom_alternation(c, fused) && !fused.ranges.empty()) {
          emit_klass_cp(prog, fused);
          return;
        }
      }
      // A class try_emit_fixed_width_class would take must not take it under an unbounded quantifier:
      // `é+` as a repeated two-byte sequence has no route and dispatches `lazy_dfa_anchored` once per
      // match (route counters), while one `klass_cp` keeps the cp-class route.
      if (c.kind == node_kind::klass && c.next < 0
          && !has_flag(flags_, flags::bytes)
          && !tree_.classes[static_cast<std::size_t>(c.klass)].codepoint_predicate) {
        const class_def eff {effective_class(c)};
        if (!eff.ranges.empty() && !is_any_non_ascii(eff.ranges)
            && try_emit_fixed_width_class(prog, eff, /*probe_only=*/ true)) {
          emit_klass_cp(prog, eff);
          return;
        }
      }
      emit_node(prog, child, capture_free);
    }

    /*!
     * \brief Emits a quantifier (Thompson construction).
     *
     * Greedy prefers `split.primary_target` (enter the body); lazy swaps the branches. Counted forms
     * unroll: `min` mandatory copies, then a loop (`max == -1`) or optional copies sharing one exit.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     node         The \ref node_kind::repeat node.
     * \param[in]     capture_free Propagated to the body copies (see \ref emit_node).
     */
    constexpr void emit_repeat(dynamic_program& prog,
                               const ast_node&  node,
                               bool             capture_free) const
    {
      if (node.possessive) {
        emit_possessive_repeat(prog, node.child, node.min, node.max, capture_free);
        return;
      }
      for (std::int32_t i = 0; i < node.min; ++i) {
        if (node.max == -1 && i == node.min - 1) {
          // The last mandatory copy doubles as the loop body: `e+` emits it once.
          const std::int32_t body {here(prog)};
          emit_unbounded_body(prog, node.child, capture_free);
          const std::int32_t s    {emit_split(prog)};
          patch_primary(prog, s, node.lazy ? here(prog) : body);
          patch_secondary(prog, s, node.lazy ? body : here(prog));
          return;
        }
        // `{k,}`'s copies match the loop body's shape: the recognizer reads the minimum off consecutive
        // identical `klass` ops.
        if (node.max == -1) {
          emit_unbounded_body(prog, node.child, capture_free);
        }
        else {
          emit_node(prog, node.child, capture_free);
        }
      }
      if (node.max == -1) {                                  // min == 0: a star loop
        const std::int32_t s {emit_split(prog)};
        patch_primary(prog, s, node.lazy ? -1 : here(prog)); // body side set below
        emit_unbounded_body(prog, node.child, capture_free);
        const std::int32_t j {emit_jump(prog)};
        patch_primary(prog, j, s);
        if (node.lazy) {
          patch_primary(prog, s, here(prog));
          patch_secondary(prog, s, s + 1);
        }
        else {
          patch_secondary(prog, s, here(prog));
        }
        return;
      }
      // Optional copies: each split can bail out to the common exit.
      std::vector<std::int32_t> exits;
      for (std::int32_t i = node.min; i < node.max; ++i) {
        exits.push_back(emit_split(prog));
        emit_node(prog, node.child, capture_free);
      }
      const std::int32_t end {here(prog)};
      for (const std::int32_t s : exits) {
        patch_primary(prog, s, node.lazy ? end : s + 1);
        patch_secondary(prog, s, node.lazy ? s + 1 : end);
      }
    }

    /*!
     * \brief Emits a bounded lookaround: an `assert_lookaround` whose sub-program is a
     *        capture-free region the main flow jumps over.
     *
     * Layout: `assert_lookaround sub_id; jump AFTER; [sub-program] match; AFTER: …`; only the sub-VM
     * enters the region, at `code_offset`. The sub-pattern must be bounded (L_max in bytes ≤
     * \ref max_lookaround_length): the linear-time guarantee.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     node         The \ref node_kind::lookaround node.
     * \param[in]     capture_free True only when already inside a lookaround (rejected).
     * \throws real::regex_error on an unbounded or over-long sub-pattern, or nesting.
     */
    constexpr void emit_lookaround(dynamic_program& prog,
                                   const ast_node&  node,
                                   bool             capture_free) const
    {
      if (capture_free) {
        // Unreachable (the parser rejects nesting first); a nested lookaround would break linear time.
        throw regex_error("nested lookaround is not supported", 0, error_kind::unsupported);
      }
      const std::int32_t lmax {l_max_bytes(node.child)};
      static_assert(max_lookaround_length == 255, "the two messages below name the bound");
      if (lmax < 0 && node.direction == look_dir::behind) {
        // Name the rewrite (`.*` -> `.{0,N}`). The ceiling is in bytes and a UTF-8 `.` takes up to 4,
        // so the example stays well under it.
        throw regex_error("unbounded lookbehind is not supported (bound the repetition, e.g. "
                          ".* -> .{0,32}; the sub-pattern must match at most 255 bytes)",
                          0, error_kind::unsupported);
      }
      if (lmax > max_lookaround_length) {
        // Say the unit: bytes, two to four per non-ASCII character.
        throw regex_error("lookaround sub-pattern too long (it may match at most 255 bytes; a non-ASCII "
                          "character takes up to 4)",
                          0, error_kind::unsupported);
      }
      const std::size_t sub_id {prog.lookarounds.size()};
      if (sub_id > 0xFFFF) {
        // arg16 holds the index; truncation would retarget the assertion at another sub-pattern.
        // Reachable under max_program_size: an empty lookaround costs three instructions.
        throw regex_error("too many lookarounds", 0);
      }
      prog.lookarounds.push_back({});                  // placeholder, filled once the region is emitted
      emit(prog, {.op = opcode::assert_lookaround, .arg16 = static_cast<std::uint16_t>(sub_id)});
      const std::int32_t skip       {emit_jump(prog)}; // main flow jumps over the sub-region
      const std::int32_t sub_offset {here(prog)};
      emit_node(prog, node.child, /*capture_free=*/ true);
      emit(prog, {.op = opcode::match});               // sub-program terminator
      patch_primary(prog, skip, here(prog));
      prog.lookarounds[sub_id] = {.code_offset = sub_offset,
                                  .code_length = here(prog) - sub_offset,
                                  .l_max       = lmax,
                                  .direction   = node.direction,
                                  .negative    = node.negated};
    }

    /*!
     * \brief Upper bound, in bytes, on what the sub-AST at \p index can consume; -1 if
     *        unbounded (a `*`, `+` or `{n,}` repeat) or if it nests a lookaround.
     *
     * `.` counts 4 bytes outside bytes mode; a class counts its widest encoding; a literal byte, 1.
     *
     * \param[in] index Index of the sub-AST node.
     * \return The byte upper bound, or -1 when not statically bounded.
     */
    [[nodiscard]] constexpr std::int32_t l_max_bytes(std::int32_t index) const
    {
      const ast_node& node {tree_.nodes[static_cast<std::size_t>(index)]};
      switch (node.kind) {
        case node_kind::empty:
        case node_kind::anchor:
          return 0;
        case node_kind::byte:
          return 1;
        case node_kind::klass:
          {
            // Widest encoding of the same effective class emit_node compiles: 1 for an ASCII member,
            // 2/3/4 by a range's top code point.
            if (has_flag(flags_, flags::bytes)) {
              return 1;
            }
            const class_def eff   {effective_class(node)};
            std::int32_t    width {eff.ascii.empty() ? 0 : 1};
            for (const code_range& r : eff.ranges) {
              std::int32_t w {2};
              if (r.hi >= 0x10000U) {
                w = 4;
              }
              else if (r.hi >= 0x800U) {
                w = 3;
              }
              if (w > width) {
                width = w;
              }
            }
            // An impossible class counts 0, so `a|<impossible>{300}` stays width 1; its emitted
            // never-match still fails the branch.
            return width;
          }
        case node_kind::any:
          return has_flag(flags_, flags::bytes) ? 1 : 4;
        case node_kind::concat:
          {
            std::int32_t total {0};
            for (std::int32_t child = node.child; child != -1;
                 child              = tree_.nodes[static_cast<std::size_t>(child)].next) {
              const std::int32_t c {l_max_bytes(child)};
              if (c < 0) {
                return -1;
              }
              total += c;
            }
            return total;
          }
        case node_kind::alternation:
          {
            std::int32_t widest {0};
            for (std::int32_t branch = node.child; branch != -1;
                 branch              = tree_.nodes[static_cast<std::size_t>(branch)].next) {
              const std::int32_t c {l_max_bytes(branch)};
              if (c < 0) {
                return -1;
              }
              if (c > widest) {
                widest = c;
              }
            }
            return widest;
          }
        case node_kind::repeat:
          {
            if (node.max == -1) {
              return -1; // *, +, {n,} are not statically bounded
            }
            const std::int32_t body {l_max_bytes(node.child)};
            if (body < 0) {
              return -1;
            }
            if (body > 0 && node.max > max_lookaround_length / body) {
              // Bounded but over the cap (`\w{64}` = 256 B): saturate, so the error reads "too long",
              // not "unbounded".
              return max_lookaround_length + 1;
            }
            return node.max * body;
          }
        case node_kind::group:
          return l_max_bytes(node.child);
        case node_kind::lookaround:
          // Unreachable (the parser rejects a nested lookaround); read as unbounded.
          return -1;
      }
      return -1;
    }

    // --- atomic groups / possessive quantifiers (Tier 1: a bare atom, or one in a capturing group) ---

    /*!
     * \brief Is \p index a bare, unwrapped single atom (a literal byte, a character class, or
     *        `.`)?
     *
     * \param[in] index Index of the sub-AST node.
     * \return `true` if \p index is `byte`, `klass`, or `any`.
     */
    [[nodiscard]] constexpr bool is_single_atom(std::int32_t index) const
    {
      const node_kind k {tree_.nodes[static_cast<std::size_t>(index)].kind};
      return k == node_kind::byte || k == node_kind::klass || k == node_kind::any;
    }

    /*!
     * \brief Tier 1 eligibility: is \p index a bare single atom, or an ordinary (non-atomic)
     *        capturing group wrapping exactly one (`X*+`, `(a)*+`, `(?>X*)`, …)?
     *
     * Such a loop fails within one opcode dispatch (see \ref emit_possessive_repeat).
     *
     * \param[in] index Index of the sub-AST node.
     * \return `true` if \p index is Tier 1 eligible.
     */
    [[nodiscard]] constexpr bool is_tier1_body(std::int32_t index) const
    {
      if (is_single_atom(index)) {
        return true;
      }
      const ast_node& node {tree_.nodes[static_cast<std::size_t>(index)]};
      return node.kind == node_kind::group && !node.possessive && node.group >= 0 &&
             is_single_atom(node.child);
    }

    /*!
     * \brief The bare atom Tier 1 should test: \p index itself, or its single captured child
     *        when \p index is a capturing-group wrapper. \ref is_tier1_body must hold.
     * \param[in] index The loop body's node index.
     * \return The bare atom's node index.
     */
    [[nodiscard]] constexpr std::int32_t tier1_atom(std::int32_t index) const
    {
      const ast_node& node {tree_.nodes[static_cast<std::size_t>(index)]};
      return node.kind == node_kind::group ? node.child : index;
    }

    /*!
     * \brief The capture group number Tier 1 should wrap the loop in, or -1 for none.
     *        \ref is_tier1_body must hold.
     * \param[in] index The loop body's node index.
     * \return The enveloping group's number, or -1 when the body is not wrapped in one.
     */
    [[nodiscard]] constexpr std::int32_t tier1_capture_group(std::int32_t index) const
    {
      const ast_node& node {tree_.nodes[static_cast<std::size_t>(index)]};
      return node.kind == node_kind::group ? node.group : -1;
    }

    /*!
     * \brief Whether compiling the sub-AST at \p index emits no `split` reachable from the outer flow.
     *
     * Asked only for a one-shot atomic group (\ref emit_atomic_group), which then has nothing to give
     * back. Mirrors the emitted shape: an alternation always splits; a non-possessive repeat splits
     * unless `min == max`; an atomic group (deterministic or rejected) and a lookaround (its own
     * sub-region) are opaque to the outer flow.
     *
     * \param[in] index Index of the sub-AST node.
     * \return `true` if compiling \p index introduces no `split` reachable from the outer flow.
     */
    [[nodiscard]] constexpr bool is_deterministic(std::int32_t index) const
    {
      const ast_node& node {tree_.nodes[static_cast<std::size_t>(index)]};
      switch (node.kind) {
        case node_kind::empty:
        case node_kind::anchor:
        case node_kind::byte:
        case node_kind::klass:
        case node_kind::any:
        case node_kind::lookaround:
          return true;
        case node_kind::concat:
          for (std::int32_t child = node.child; child != -1;
               child              = tree_.nodes[static_cast<std::size_t>(child)].next) {
            if (!is_deterministic(child)) {
              return false;
            }
          }
          return true;
        case node_kind::group:
          return node.possessive || is_deterministic(node.child);
        case node_kind::repeat:
          if (node.possessive) {
            return is_deterministic(node.child);
          }
          return node.max != -1 && node.min == node.max && is_deterministic(node.child);
        case node_kind::alternation:
          return false;
      }
      return false;
    }

    /*!
     * \brief Emits a Tier 1 atom test (`byte_loop_possessive` / `klass_loop_possessive` /
     *        `klass_cp_loop_possessive`): `secondary_target` is a placeholder for the no-match exit,
     *        `primary_target` the capture start slot (-1 for none; the end slot is start + 1).
     *
     * The opcode writes both capture slots itself, on a match only: a leading `save` would fire on the
     * failing attempt a possessive loop always makes last and tear the last good capture. The cp form
     * keeps the three-slot continuation chain of \ref emit_klass_cp.
     *
     * \param[in,out] prog               The program being built.
     * \param[in]     atom               Index of the single-atom AST node (`byte`/`klass`/`any`).
     * \param[in]     capture_start_slot The capture group's start slot, or -1 for none.
     * \return The emitted TEST instruction's index (its `secondary_target` needs patching).
     */
    constexpr std::int32_t emit_tier1_atom_test(dynamic_program& prog,
                                                std::int32_t     atom,
                                                std::int32_t     capture_start_slot) const
    {
      const ast_node&    node {tree_.nodes[static_cast<std::size_t>(atom)]};
      const std::int32_t pc   {here(prog)};
      if (node.kind == node_kind::byte) {
        emit(prog, {.op             = opcode::byte_loop_possessive, .arg8 = node.byte,
                    .primary_target = capture_start_slot, .secondary_target = -1});
        return pc;
      }
      if (node.kind == node_kind::any) {
        if (node.raw_byte) {
          // \C: any byte, newline included, as in emit_node.
          char_class all;
          all.set_range(0x00, 0xFF);
          emit(prog, {.op             = opcode::klass_loop_possessive, .arg16 = intern_class(prog, all),
                      .primary_target = capture_start_slot, .secondary_target = -1});
          return pc;
        }
        const flags node_flags {static_cast<flags>(node.effective_flags)};
        char_class  head;
        head.set_range(0x00, 0x7F);
        if (has_flag(flags_, flags::bytes)) {
          head.set_range(0x80, 0xFF);
        }
        if (!has_flag(node_flags, flags::dotall)) {
          char_class newline;
          newline.set('\n');
          if (has_flag(flags_, flags::ecma)) {
            newline.set('\r');
          }
          head.bits[0] &= ~newline.bits[0];
        }
        if (has_flag(flags_, flags::bytes)) {
          emit(prog, {.op             = opcode::klass_loop_possessive, .arg16 = intern_class(prog, head),
                      .primary_target = capture_start_slot, .secondary_target = -1});
          return pc;
        }
        const class_def cd {.ascii = head, .ranges = {{.lo = 0x80U, .hi = 0x10FFFFU}}};
        emit(prog, {.op             = opcode::klass_cp_loop_possessive, .arg16 = intern_cp_class(prog, cd),
                    .primary_target = capture_start_slot, .secondary_target = -1});
        emit_klass(prog, utf8_cont_set());
        emit_klass(prog, utf8_cont_set());
        emit_klass(prog, utf8_cont_set());
        return pc;
      }
      // node_kind::klass
      if (tree_.classes[static_cast<std::size_t>(node.klass)].codepoint_predicate) {
        emit(prog, {.op             = opcode::klass_cp_loop_possessive,
                    .arg16          = intern_cp_class(prog, effective_class(node)),
                    .primary_target = capture_start_slot, .secondary_target = -1});
        emit_klass(prog, utf8_cont_set());
        emit_klass(prog, utf8_cont_set());
        emit_klass(prog, utf8_cont_set());
        return pc;
      }
      const class_def eff {effective_class(node)};
      if (has_flag(flags_, flags::bytes) || eff.ranges.empty()) {
        emit(prog, {.op             = opcode::klass_loop_possessive, .arg16 = intern_class(prog, eff.ascii),
                    .primary_target = capture_start_slot, .secondary_target = -1});
        return pc;
      }
      emit(prog, {.op             = opcode::klass_cp_loop_possessive, .arg16 = intern_cp_class(prog, eff),
                  .primary_target = capture_start_slot, .secondary_target = -1});
      emit_klass(prog, utf8_cont_set());
      emit_klass(prog, utf8_cont_set());
      emit_klass(prog, utf8_cont_set());
      return pc;
    }

    /*!
     * \brief Emits a Tier 1 possessive loop over a single atom, optionally wrapped in one
     *        capturing group.
     *
     * Mandatory copies (\p min) are ordinary emission: a failure there kills the thread. The optional
     * tail is a self-loop (`max == -1`) or unrolled copies, each one \ref emit_tier1_atom_test whose
     * no-match exit is patched to the shared exit.
     *
     * \param[in,out] prog          The program being built.
     * \param[in]     atom          Index of the single-atom body (`byte`, `klass`, or `any`).
     * \param[in]     min           Minimum repetition count.
     * \param[in]     max           Maximum repetition count (-1 = unbounded).
     * \param[in]     capture_group Capture group number to wrap the loop in, or -1 for none.
     * \param[in]     capture_free  Whether captures are suppressed here (inside a lookaround).
     */
    constexpr void emit_tier1_loop(dynamic_program& prog,
                                   std::int32_t     atom,
                                   std::int32_t     min,
                                   std::int32_t     max,
                                   std::int32_t     capture_group,
                                   bool             capture_free) const
    {
      const bool         captured   {capture_group >= 0 && !capture_free};
      const std::int32_t start_slot {captured ? 2 * capture_group : -1};

      for (std::int32_t i = 0; i < min; ++i) {
        if (captured) {
          emit(prog, {.op = opcode::save, .arg16 = static_cast<std::uint16_t>(start_slot)});
        }
        emit_node(prog, atom, capture_free);
        if (captured) {
          emit(prog, {.op = opcode::save, .arg16 = static_cast<std::uint16_t>(start_slot + 1)});
        }
      }

      if (max == min) {
        return; // exact count: nothing left to give back to begin with
      }

      std::vector<std::int32_t> exits; // secondary_target sites to patch to the shared exit

      if (max == -1) {
        const std::int32_t tail_start {here(prog)};
        exits.push_back(emit_tier1_atom_test(prog, atom, start_slot));
        const std::int32_t back       {emit_jump(prog)};
        patch_primary(prog, back, tail_start);
      }
      else {
        const std::int32_t optional_count {max - min};
        for (std::int32_t i = 0; i < optional_count; ++i) {
          exits.push_back(emit_tier1_atom_test(prog, atom, start_slot));
        }
      }

      const std::int32_t exit {here(prog)};
      for (const std::int32_t pc : exits) {
        patch_secondary(prog, pc, exit);
      }
    }

    /*!
     * \brief Dispatches a possessive quantifier body to Tier 1 or a clean rejection; shared by
     *        \ref emit_repeat and \ref emit_atomic_group.
     *
     * A compound body (`(?:ab)*+`) is out of scope: the thread list keeps one position per round, and a
     * thread that fails inside a compound body dies without reaching an exit, so the loop could not
     * offer its exit only once the body has definitively failed. A single atom fails within its own
     * dispatch: the opcode is its own fail-redirect.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     body         Index of the quantified body.
     * \param[in]     min          Minimum repetition count.
     * \param[in]     max          Maximum repetition count (-1 = unbounded).
     * \param[in]     capture_free Whether captures are suppressed here (inside a lookaround).
     * \throws real::regex_error when \p body is not Tier 1 eligible, or \p capture_free is true:
     *         the lookaround sub-VM's dispatchers know only `byte`/`klass`/`klass_cp`, and would read
     *         a `klass_cp_loop_possessive`'s index in the wrong class table.
     */
    constexpr void emit_possessive_repeat(dynamic_program& prog,
                                          std::int32_t     body,
                                          std::int32_t     min,
                                          std::int32_t     max,
                                          bool             capture_free) const
    {
      if (capture_free) {
        throw regex_error("possessive/atomic quantifiers inside a lookaround are not supported yet", 0,
                          error_kind::unsupported);
      }
      if (!is_tier1_body(body)) {
        throw regex_error("possessive/atomic over a compound body is not supported yet", 0,
                          error_kind::unsupported);
      }
      emit_tier1_loop(prog, tier1_atom(body), min, max, tier1_capture_group(body), capture_free);
    }

    /*!
     * \brief Emits an atomic group `(?>...)`.
     *
     * 1. A child `repeat` over a Tier 1 body (`(?>X*)`, `(?>(a)+)`) becomes possessive whatever its own
     *    flag, tested before any width check so `(?>[^"]*)` compiles; same restrictions as
     *    \ref emit_possessive_repeat.
     * 2. Else a deterministic body (\ref is_deterministic, `(?>ab)`) never gives back, so ordinary
     *    emission is exact, even inside a lookaround.
     * 3. Else (`(?>ab|a)`) inline emission would let a give-back reach the inner `split`: rejected.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     node         The \ref node_kind::group node (`possessive == true`).
     * \param[in]     capture_free Propagated to the body.
     */
    constexpr void emit_atomic_group(dynamic_program& prog,
                                     const ast_node&  node,
                                     bool             capture_free) const
    {
      const ast_node& child {tree_.nodes[static_cast<std::size_t>(node.child)]};
      if (child.kind == node_kind::repeat && is_tier1_body(child.child)) {
        if (capture_free) {
          throw regex_error("possessive/atomic quantifiers inside a lookaround are not supported yet", 0,
                            error_kind::unsupported);
        }
        emit_tier1_loop(prog, tier1_atom(child.child), child.min, child.max,
                        tier1_capture_group(child.child), capture_free);
        return;
      }
      if (is_deterministic(node.child)) {
        emit_node(prog, node.child, capture_free); // vacuously atomic: no repeat, nothing to give back
        return;
      }
      throw regex_error("possessive/atomic over a compound body is not supported yet", 0,
                        error_kind::unsupported);
    }
  };

  /*!
   * \brief Compiles \p tree to an NFA program (convenience over \ref compiler).
   * \param[in] tree The parsed AST.
   * \param[in] compile_flags The effective compilation flags.
   * \return The compiled \ref dynamic_program.
   * \throws real::regex_error if the program exceeds \ref max_program_size.
   */
  constexpr dynamic_program compile(const ast& tree,
                                    flags      compile_flags)
  {
    dynamic_program prog {compiler(tree, compile_flags).compile()};
    // The inner-literal prefix sub-program, for the reverse start-finder. Dynamic only: a second compile
    // would pass a static_regex's constexpr budget.
    if (!std::is_constant_evaluated() && prog.hints.inner_literal_prefix >= 1) {
      const dynamic_program pp {
        compiler(build_prefix_ast(tree, prog.hints.inner_literal_prefix,
                                  prog.hints.inner_literal_prefix_skip),
                 compile_flags)
        .compile()};
      prog.prefix_code       = pp.code;
      prog.prefix_classes    = pp.classes;
      prog.prefix_cp_classes = pp.cp_classes;
      prog.prefix_cp_ranges  = pp.cp_ranges;
    }
    return prog;
  }
} // namespace real::detail

#endif // REAL_COMPILER_HPP
