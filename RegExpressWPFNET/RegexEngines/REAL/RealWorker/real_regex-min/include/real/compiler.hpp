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

#include "version.hpp"

#include <cstdint>
#include <vector>

#include "ast.hpp"
#include "charclass.hpp"
#include "config.hpp"
#include "prefilter.hpp"
#include "program.hpp"

namespace real::detail {

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
      emit(prog, {.op = opcode::save, .arg16 = 1});
      emit(prog, {.op = opcode::match});
      prog.byte_mode  = has_flag(flags_, flags::bytes);
      prog.hints      = analyze_program(prog.code, prog.classes, prog.codepoint_mark_ascii, prog.codepoint_mark_offset);
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
          {
            const auto byte_value {node.byte};
            const bool letter = (byte_value >= 'A' && byte_value <= 'Z') ||
                                (byte_value >= 'a' && byte_value <= 'z');
            if (has_flag(flags_, flags::icase) && letter) {
              char_class both;
              both.set(byte_value);
              fold_ascii_case(both);
              emit_klass(prog, both);
            }
            else {
              emit(prog, {.op = opcode::byte, .arg8 = byte_value});
            }
            break;
          }
        case node_kind::klass:
          {
            char_class written {tree_.classes[static_cast<std::size_t>(node.klass)]};
            if (has_flag(flags_, flags::icase)) {
              fold_ascii_case(written); // before negation, like Python
            }
            if (!node.negated) {
              emit_klass(prog, written);
              break;
            }
            if (has_flag(flags_, flags::bytes)) {
              written.invert(); // raw bytes: plain 256-bit complement
              emit_klass(prog, written);
              break;
            }
            written.invert_ascii();
            emit_codepoint_class(prog, written);
            break;
          }
        case node_kind::any:
          {
            char_class head;
            head.set_range(0x00, 0x7F);
            if (has_flag(flags_, flags::bytes)) {
              head.set_range(0x80, 0xFF); // any raw byte
            }
            if (!has_flag(flags_, flags::dotall)) {
              char_class newline;
              newline.set('\n');
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
          emit(prog, {.op = opcode::assert_position, .arg8 = static_cast<std::uint8_t>(assert_kind_for(node.anchor))});
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
     * \param[in] anchor The AST anchor kind.
     * \return The assertion the engine should evaluate.
     */
    [[nodiscard]] constexpr assert_kind assert_kind_for(anchor_kind anchor) const
    {
      const bool  multiline {has_flag(flags_, flags::multiline)};
      assert_kind result    {};
      switch (anchor) {
        case anchor_kind::caret:
          result = multiline ? assert_kind::line_start : assert_kind::text_start;
          break;
        case anchor_kind::dollar:
          result = multiline ? assert_kind::line_end : assert_kind::text_end_or_final_newline;
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
          return (node.negated && !has_flag(flags_, flags::bytes)) ? 4 : 1;
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
    return compiler(tree, compile_flags).compile();
  }
} // namespace real::detail

#endif // REAL_COMPILER_HPP
