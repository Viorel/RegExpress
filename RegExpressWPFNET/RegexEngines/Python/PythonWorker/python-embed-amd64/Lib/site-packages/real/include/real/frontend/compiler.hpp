/*!
 * \file compiler.hpp
 * \brief AST → NFA program, via Thompson construction.
 *
 * The emitted program always has the shape `save 0, <body>, save 1,
 * match`, so slots 0/1 delimit group 0 (the whole match).
 *
 * Multi-codepoint semantics are compiled down to byte-level alternatives
 * (RE2-style): `.` and negated classes expand to UTF-8 lead/continuation
 * byte classes joined by split/jump, so the engine itself only ever steps one
 * byte at a time, in lock-step — which preserves linear time.
 *
 * Branch targets are emitted as placeholders and patched only through the
 * `patch_primary` / `patch_secondary` helpers, never by rewriting emitted instructions
 * wholesale.
 */
#ifndef REAL_COMPILER_HPP
#define REAL_COMPILER_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "real/version.hpp"

#include <algorithm>
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

  // The code-point-range → UTF-8 byte-range algorithm (utf8_byte_seq, utf8_range_sequences,
  // encode_utf8_bytes) now lives in utf8_ranges.hpp, shared with the lazy DFA's klass_cp expansion.

  //! \brief Whether \p ranges is exactly the whole non-ASCII space `[U+0080, U+10FFFF]` — the
  //!        "any non-ASCII code point" shape emitted by the compact \ref compiler::emit_codepoint_class.
  constexpr bool is_any_non_ascii(const std::vector<code_range>& ranges)
  {
    return ranges.size() == 1 && ranges[0].lo == 0x80U && ranges[0].hi == 0x10FFFFU;
  }

  /*!
   * \brief Expands a character class to its Unicode simple case-fold closure (text-mode `icase`).
   *
   * The fold acts on the WHOLE class, cross-boundary in both directions, before
   * negation:
   *   - **Bitmap (iterate-members-lookup):** each ASCII member (< 0x80) contributes its fold partners
   *     (ASCII partners re-enter the bitmap; non-ASCII partners like `k`↦Kelvin become code-point
   *     ranges). This is also the path the ASCII-letter literal fold takes, so there is one route.
   *   - **Ranges (intersect-entries):** every fold entry whose code point falls inside a class range
   *     contributes its partners (so a range attracts its ASCII partners, e.g. `[K…]`↦`k`, and
   *     `[U+0080-U+10FFFF]` attracts `k`/`K` back into the bitmap).
   *
   * Idempotent on ASCII-only orbits (`[a]`↦`{a, A}`, no non-ASCII contamination). Partners that are
   * already present are harmlessly re-added (the compiler tolerates redundant ranges).
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
    for (std::size_t i = 0; i < unicode_fold_table_size; ++i) {
      const fold_entry& entry {unicode_fold_table[i]};
      if (std::ranges::any_of(in.ranges,
                              [&entry](const code_range& r) { return entry.cp >= r.lo && entry.cp <= r.hi; })) {
        for (std::uint8_t k = 0; k < entry.count; ++k) {
          add_partner(entry.partner[k]);
        }
      }
    }
    // Coalesce: the fold adds many degenerate {cp, cp} ranges (a class's own members' partners, and
    // partners of members just outside the class); merging overlapping/adjacent ranges collapses that
    // fragmentation without changing the accepted set — pure size optimisation.
    out.ranges = coalesce_ranges(std::move(ranges));
    return out;
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
      prog.hints        = analyze_program(prog.code, prog.classes, prog.cp_classes, prog.codepoint_mark_ascii,
                                          prog.codepoint_mark_offset, prog.lookarounds);
      // The required inner literal + its prefix boundary (a single AST walk). Recorded for the inner-literal
      // search path; not yet routed on. Kept off the program code, so byte-identity is untouched.
      const inner_literal il {extract_inner_literal(tree_)};
      prog.hints.inner_literal        = il.bytes;
      prog.hints.inner_literal_len    = il.len;
      prog.hints.inner_literal_prefix = il.prefix_child_count;
      if (prog.code.size() > max_program_size) {
        throw regex_error("program too large", 0);
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
     * The check lives inside `emit` so it fires \e during a large unroll loop,
     * before the vector grows to the full bad size — this is the central
     * defense (\ref max_program_size) against the DoS where tiny nested bounded
     * quantifiers expand to hundreds of millions of instructions. It is
     * constexpr-friendly: exceeding the cap fails compilation for a
     * `static_regex`, or throws at run time.
     *
     * \param[in,out] prog        The program being built.
     * \param[in]     instruction The instruction to append.
     * \throws real::regex_error when \ref max_program_size would be exceeded.
     */
    static constexpr void emit(dynamic_program& prog,
                               instr            instruction)
    {
      if (prog.code.size() >= max_program_size) {
        throw regex_error("program too large", 0);
      }
      prog.code.push_back(instruction);
    }

    /*!
     * \brief Emits a `split` with placeholder targets.
     * \return Its instruction index.
     */
    static constexpr std::int32_t emit_split(dynamic_program& prog)
    {
      emit(prog, {.op = opcode::split, .primary_target = -1, .secondary_target = -1});
      return here(prog) - 1;
    }

    /*!
     * \brief Emits a `jump` with a placeholder target.
     * \return Its instruction index.
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
     * \brief Emits a `klass` instruction, interning \p klass.
     *
     * Identical bitmaps share one slot, so the UTF-8 continuation class is
     * stored once however often it is emitted.
     *
     * \param[in,out] prog The program being built.
     * \param[in]     klass   The class bitmap to match.
     * \throws real::regex_error if more than 65536 distinct classes are needed.
     */
    static constexpr void emit_klass(dynamic_program&  prog,
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
      emit(prog, {.op = opcode::klass, .arg16 = static_cast<std::uint16_t>(index)});
    }

    /*!
     * \brief Emits a match-time code-point predicate for a Unicode shorthand (`\w \d \s` and their
     *        negations) in text mode: a `klass_cp` over the interned code-point class, followed by a
     *        three-instruction continuation chain (`klass utf8_cont` ×3). At match time `klass_cp`
     *        decodes one code point and, on membership, enters the chain at a computed skip so the
     *        remaining continuation bytes are walked one per step — see pike.hpp. The class is the
     *        already-effective set (the fold and any external negation were materialised by
     *        \ref effective_class), so membership is a plain positive test.
     *
     * \param[in,out] prog The program being built.
     * \param[in]     cd   The effective code-point class (ASCII bitmap + non-ASCII ranges).
     */
    static constexpr void emit_klass_cp(dynamic_program& prog,
                                        const class_def& cd)
    {
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
            // Two code-point classes with the SAME ASCII bitmap and range COUNT but different ranges:
            // the interner must not merge them. In practice the shorthand classes (\w/\d/\s and their
            // complements) have distinct bitmaps and counts, so this range mismatch is a defensive
            // arm of the dedup, not hit by the current emitters (hence uncovered by the runtime report).
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
        if (index > 0xFFFF) {
          throw regex_error("too many code-point classes", 0);
        }
        const auto begin {static_cast<std::uint32_t>(prog.cp_ranges.size())};
        for (const code_range& r : cd.ranges) {
          prog.cp_ranges.push_back(r);
        }
        prog.cp_classes.push_back({.ascii       = cd.ascii,
                                   .range_begin = begin,
                                   .range_count = static_cast<std::uint32_t>(cd.ranges.size())});
      }
      emit(prog, {.op = opcode::klass_cp, .arg16 = static_cast<std::uint16_t>(index)});
      emit_klass(prog, utf8_cont_set()); // three continuation slots; klass_cp's skip picks the entry
      emit_klass(prog, utf8_cont_set());
      emit_klass(prog, utf8_cont_set());
    }

    // --- UTF-8 byte expansion --------------------------------------------

    /*!
     * \brief Emits "one codepoint matching \p ascii, or any non-ASCII codepoint".
     *
     * Expands to the byte-level alternation
     * `ascii | lead2 cont | lead3 cont cont | lead4 cont cont cont`, so
     * the engine steps one byte at a time while still consuming whole codepoints.
     * The UTF-8 byte sets come from charclass.hpp, shared with the prefilter that
     * recognizes this exact shape.
     *
     * \param[in,out] prog  The program being built.
     * \param[in]     ascii The accepted ASCII bytes (the non-ASCII branches are
     *                      always included).
     */
    constexpr void emit_codepoint_class(dynamic_program&  prog,
                                        const char_class& ascii) const
    {
      const std::int32_t block_start {here(prog)}; // start offset, recorded as the marker below
      const char_class   cont        {utf8_cont_set()};
      const char_class   lead2       {utf8_lead2_set()};
      const char_class   lead3       {utf8_lead3_set()};
      const char_class   lead4       {utf8_lead4_set()};

      const std::int32_t s1          {emit_split(prog)};
      patch_primary(prog, s1, here(prog));
      emit_klass(prog, ascii);
      const std::int32_t j1 {emit_jump(prog)};

      patch_secondary(prog, s1, here(prog));
      const std::int32_t s2 {emit_split(prog)};
      patch_primary(prog, s2, here(prog));
      emit_klass(prog, lead2);
      emit_klass(prog, cont);
      const std::int32_t j2 {emit_jump(prog)};

      patch_secondary(prog, s2, here(prog));
      const std::int32_t s3 {emit_split(prog)};
      patch_primary(prog, s3, here(prog));
      emit_klass(prog, lead3);
      emit_klass(prog, cont);
      emit_klass(prog, cont);
      const std::int32_t j3 {emit_jump(prog)};

      patch_secondary(prog, s3, here(prog));
      emit_klass(prog, lead4);
      emit_klass(prog, cont);
      emit_klass(prog, cont);
      emit_klass(prog, cont);

      const std::int32_t end {here(prog)};
      patch_primary(prog, j1, end);
      patch_primary(prog, j2, end);
      patch_primary(prog, j3, end);

      // Record the marker (offset + ASCII sub-class index) so analyze_program reads
      // it instead of reverse-engineering this 16-instruction block's bytecode shape.
      prog.codepoint_mark_offset = block_start;
      prog.codepoint_mark_ascii  = static_cast<std::int32_t>(prog.code[static_cast<std::size_t>(block_start) + 1].arg16);
    }

    /*!
     * \brief Emits an alternation of byte-range sequences (`branches`) as split/jump — the general
     *        form of \ref emit_codepoint_class. Each branch is a chain of `klass` steps; the leftmost
     *        matching branch wins. Used for a character class carrying specific code-point ranges.
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
     * \brief Emits a code-point class: the ASCII bitmap (one byte, if any) OR the canonical UTF-8
     *        byte sequences of each code-point range. This is the specific-code-point generalization
     *        of \ref emit_codepoint_class (whose `[U+0080, U+10FFFF]` "any non-ASCII" is one case).
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
        // An impossible class (e.g. the negation of the whole code-point space): match nothing. An
        // empty bitmap rejects every byte, so the thread dies — a never-match, not a crash.
        emit_klass(prog, char_class {});
        return;
      }
      emit_byte_sequences(prog, branches);
    }

    /*!
     * \brief The class a `node_kind::klass` node effectively accepts, after negation, icase folding
     *        and the bytes/code-point split. This is the ONE source of truth consumed by both
     *        \ref emit_node and \ref l_max_bytes, so what is emitted and its measured width can never
     *        disagree. Positive: as written. Negated: the ASCII complement plus, in code-point mode,
     *        the code-point complement over `[U+0080, U+10FFFF]` minus surrogates.
     */
    [[nodiscard]] constexpr class_def effective_class(const ast_node& node) const
    {
      // icase and ascii are read from the node's own scope (stamped by the parser from the flag-scope
      // stack), so a scoped (?i:...) / (?a:...) folds and picks tables per scope. bytes is not scopable
      // and stays global.
      const flags node_flags {static_cast<flags>(node.effective_flags)};
      class_def   folded     {tree_.classes[static_cast<std::size_t>(node.klass)]};
      if (has_flag(node_flags, flags::icase)) {
        if (has_flag(flags_, flags::bytes) || has_flag(node_flags, flags::ascii)) {
          fold_ascii_case(folded.ascii);     // bytes / ASCII mode (re.A): ASCII-only fold, no Unicode partners
        }
        else {
          folded = unicode_casefold(folded); // text: full Unicode fold of the whole class, both directions
        }
      }
      // The fold is applied BEFORE negation (Python order): [^k] under icase is the complement of
      // {k, K, Kelvin}, so it rejects Kelvin.
      if (!node.negated) {
        // Members accumulate in PARSE order — a predicate's range table (`\d`/`\w`/`\p{…}`), then any literal
        // or range that follows it — but `klass_cp` binary-searches the ranges at match time, so they must be
        // sorted and merged. Without this, a non-ASCII member after a predicate (`[\dЩ]`, `[\w\W]`) landed out
        // of order and was silently missed. The negated path already coalesces via complement_code_ranges.
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
          // A `byte` node is a raw byte with byte provenance (a `\xHH` / octal escape, or a non-cased
          // literal), never case-folded: under icase a cased literal was promoted to a foldable
          // singleton class at the parser, so it never reaches here. This preserves the deliberate
          // `\xHH` provenance split (see emit_literal_codepoint / divergences.dox).
          emit(prog, {.op = opcode::byte, .arg8 = node.byte});
          break;
        case node_kind::klass:
          {
            // A text-mode class with a Unicode-shorthand contribution (\w/\d/\s, bare or in a class):
            // a match-time code-point predicate, not the byte-NFA. effective_class materialises the
            // fold and the external negation, so the stored cp_class needs no negation flag -- this is
            // also what gives [^\W] == \w, [^\D] == \d, [^\S] == \s.
            if (tree_.classes[static_cast<std::size_t>(node.klass)].codepoint_predicate) {
              emit_klass_cp(prog, effective_class(node));
              break;
            }
            const class_def eff {effective_class(node)};
            if (has_flag(flags_, flags::bytes) || eff.ranges.empty()) {
              // Bytes mode is a single 256-bit bitmap; a code-point class with no non-ASCII members is
              // just its ASCII bitmap. An empty bitmap here (impossible class) is a never-match.
              emit_klass(prog, eff.ascii);
              break;
            }
            if (is_any_non_ascii(eff.ranges)) {
              // "ASCII bitmap OR any non-ASCII code point" (`.`-family, `[^x]`): the compact
              // emit_codepoint_class shape the prefilter/DFA fast path recognizes.
              emit_codepoint_class(prog, eff.ascii);
              break;
            }
            emit_class_codepoints(prog, eff.ascii, eff.ranges);
            break;
          }
        case node_kind::any:
          {
            // dotall is read from this node's own scope (a scoped (?s:.) matches \n inside the island
            // only); bytes and ecma are not scopable and stay global. A non-scoped node carries the
            // global dotall, so its emitted class is byte-identical to before.
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
              emit_codepoint_class(prog, head);
            }
            break;
          }
        case node_kind::anchor:
          {
            // The assert_kind for ^/$ follows this node's own multiline (a scoped (?m:...)); \b \B \< \>
            // additionally carry a per-instruction FLIP bit: 1 when this node's word-ness differs from
            // the program default (a scoped (?a:...) / (?-a:...) island), else 0. A non-scoped pattern
            // is all-0 here and maps to the same assert_kinds, so its program is byte-identical.
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
          if (node.group >= 0 && !capture_free) {
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
     * `^` and `$` depend on the multiline flag; everything else maps
     * one-to-one.
     *
     * \param[in] anchor     The AST anchor kind.
     * \param[in] node_flags The flag set in force at this anchor's scope; its `multiline` selects the
     *                       line-relative vs absolute form of `^`/`$` (a scoped `(?m:...)`).
     * \return The assertion the engine should evaluate.
     */
    [[nodiscard]] constexpr assert_kind assert_kind_for(anchor_kind anchor,
                                                        flags       node_flags) const
    {
      // multiline is read from the anchor node's own scope (a scoped (?m:^...$) is line-relative inside
      // the island only); ecma is not scopable and stays global. A non-scoped node carries the global
      // multiline, so `^`/`$` map to the same assert_kind as before — byte-identical.
      const bool  multiline {has_flag(node_flags, flags::multiline)};
      assert_kind result    {};
      switch (anchor) {
        case anchor_kind::caret:
          result = multiline ? assert_kind::line_start : assert_kind::text_start;
          break;
        case anchor_kind::dollar:
          // Default (Python): `$` matches at end OR just before a final `\n`. With the ecma OR dollar_endonly
          // flag, `$` (no multiline) matches only at the very end — ECMAScript / Rust (`\z`) semantics.
          result = multiline
                   ? assert_kind::line_end
                   : (has_flag(flags_, flags::ecma) || has_flag(flags_, flags::dollar_endonly)
                        ? assert_kind::text_end
                        : assert_kind::text_end_or_final_newline);
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
     * \brief Emits a quantifier (Thompson construction).
     *
     * Greedy prefers `split.primary_target` (enter the body); lazy swaps the branches.
     * Counted forms unroll: `min` mandatory copies, then either a loop
     * (`max == -1`) or optional copies sharing one exit.
     *
     * \param[in,out] prog         The program being built.
     * \param[in]     node         The \ref node_kind::repeat node.
     * \param[in]     capture_free Propagated to the body copies (see \ref emit_node).
     */
    constexpr void emit_repeat(dynamic_program& prog,
                               const ast_node&  node,
                               bool             capture_free) const
    {
      for (std::int32_t i = 0; i < node.min; ++i) {
        if (node.max == -1 && i == node.min - 1) {
          // Last mandatory copy doubles as the loop body: e+ patterns
          // emit the body exactly once.
          const std::int32_t body {here(prog)};
          emit_node(prog, node.child, capture_free);
          const std::int32_t s    {emit_split(prog)};
          patch_primary(prog, s, node.lazy ? here(prog) : body);
          patch_secondary(prog, s, node.lazy ? body : here(prog));
          return;
        }
        emit_node(prog, node.child, capture_free);
      }
      if (node.max == -1) {                                  // min == 0: a star loop
        const std::int32_t s {emit_split(prog)};
        patch_primary(prog, s, node.lazy ? -1 : here(prog)); // body side set below
        emit_node(prog, node.child, capture_free);
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
     * Layout: `assert_lookaround sub_id; jump AFTER; [sub-program] match; AFTER: …`. The
     * main VM only steps the `assert_lookaround` (epsilon) and the skip-`jump`; the sub
     * region is entered solely by the sub-VM at `code_offset`. The sub-pattern must be
     * bounded (L_max in bytes ≤ \ref max_lookaround_length) — the linear-time guarantee.
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
        // intentionally uncovered: fail-loud net for a parser-guaranteed invariant. The
        // parser rejects nested lookarounds first, so capture_free is never true here; a
        // nested lookaround reaching the compiler would break the linear-time guarantee,
        // so a throw beats a silent miscompile if that parser guard ever regresses.
        throw regex_error("nested lookaround is not supported", 0);
      }
      const std::int32_t lmax {l_max_bytes(node.child)};
      if (lmax < 0) {
        throw regex_error("unbounded lookaround is not supported (use a fixed repeat count)", 0);
      }
      if (lmax > max_lookaround_length) {
        throw regex_error("lookaround sub-pattern too long", 0);
      }
      const std::size_t sub_id {prog.lookarounds.size()};
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
     * Codepoint-consuming shapes (`.`, a negated class outside bytes mode) count as one
     * codepoint = up to 4 bytes (A1); a literal byte or an ASCII class is one byte.
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
            // Widest UTF-8 encoding the class can match, from the SAME effective (post-negation) class
            // that emit_node compiles — so width and emission never disagree. Bytes mode: one byte.
            // Otherwise 1 for any ASCII member, plus the widest code-point range (2/3/4 by top code
            // point); an impossible class matches nothing, reported as 1 (harmless — it never matches).
            if (has_flag(flags_, flags::bytes)) {
              return 1;
            }
            const class_def eff   {effective_class(node)};
            std::int32_t    width {eff.ascii.empty() ? 0 : 1};
            for (const code_range& r : eff.ranges) {
              const std::int32_t w {r.hi < 0x800U ? 2 : (r.hi < 0x10000U ? 3 : 4)};
              if (w > width) {
                width = w;
              }
            }
            // An impossible (never-match) class contributes 0: it consumes nothing, so a dead branch
            // in a bounded lookaround (the negation of the whole code-point space, repeated) does not
            // inflate the width -- the alternation `a | <impossible>{300}` stays width 1.
            // Its emitted never-match still makes the branch fail — a width of 0 is not an empty match.
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
              return -1; // the product would exceed the cap (nested {n}{m}...) -> reject; no int32 overflow
            }
            return node.max * body;
          }
        case node_kind::group:
          return l_max_bytes(node.child);
        case node_kind::lookaround:
          // intentionally uncovered: -Wswitch exhaustiveness arm; the parser rejects nested
          // lookarounds first, so l_max_bytes never recurses into one. Treated as unbounded.
          return -1;
      }
      return -1;
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
    // IL: compile the inner-literal prefix sub-program (the part before the literal) for the reverse
    // start-finder. Dynamic-only — a static_regex compiles in a constant-evaluated context and keeps the core
    // search, sidestepping the constexpr budget of a second compile (the "dynamic-only if it would blow the
    // budget" choice, taken up front). The prefix program is a subset, so this does not recurse into itself.
    if (!std::is_constant_evaluated() && prog.hints.inner_literal_prefix >= 1) {
      const dynamic_program pp {
        compiler(build_prefix_ast(tree, prog.hints.inner_literal_prefix), compile_flags).compile()};
      prog.prefix_code       = pp.code;
      prog.prefix_classes    = pp.classes;
      prog.prefix_cp_classes = pp.cp_classes;
      prog.prefix_cp_ranges  = pp.cp_ranges;
      // IL-fusion: when the WHOLE pattern is a plain fixed-width byte/klass sequence (fixed_shape --
      // no branches/klass_cp/assertions anywhere, prefix and suffix both included), the memmem hit's
      // match start is pure arithmetic and the whole span verifies in one match_byte_klass_run pass
      // (run_inner_literal, pike.hpp) -- no reverse DFA, no forward DFA, no one-pass extraction. The
      // prefix sub-program (pp, just compiled) gives the prefix's own width directly; fixed_shape
      // already guarantees prog.code as a whole (prefix + literal + suffix) is this same shape, so a
      // second walk of it is only needed for the total-width cap, not to re-prove eligibility.
      if (prog.hints.fixed_shape) {
        const std::int32_t prefix_w {fixed_run_width(pp.code)};
        const std::int32_t total_w  {fixed_run_width(prog.code)};
        if (prefix_w >= 0 && total_w >= 0 && total_w <= il_fused_max_width) {
          prog.hints.il_fused_eligible     = true;
          prog.hints.il_fused_prefix_width = static_cast<std::uint8_t>(prefix_w);
        }
      }
    }
    return prog;
  }
} // namespace real::detail

#endif // REAL_COMPILER_HPP
