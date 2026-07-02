/*!
 * \file prefilter.hpp
 * \brief Search acceleration: pattern analysis and candidate-finding.
 *
 * Extracts \ref real::detail::pattern_hints from a compiled program (required
 * literal prefix, start anchoring, possible-first-byte set, fast-path shapes)
 * and provides the primitives the engine uses to skip ahead when no thread is
 * alive. Uses `memchr` / the platform substring search at run time and plain
 * loops in constexpr. Hints never affect \e what matches — only how fast; an
 * equivalence test runs the engine with hints disabled to prove it.
 */
#ifndef REAL_PREFILTER_HPP
#define REAL_PREFILTER_HPP

#include "version.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "charclass.hpp"
#include "program.hpp"

namespace real::detail {

  /*!
   * \brief Tests whether the whole program is an alternation of straight-line
   *        branches (e.g. `the|fox|dog`).
   *
   * Layout: save 0, a chain of `split` nodes whose `primary_target` is a branch
   * of byte/klass ending in `jump` to the shared exit and whose `secondary_target`
   * is the next split, the last branch falling through to save 1, match. Captures, assertions, nested
   * branches and empty branches all disqualify it.
   *
   * \param[in] code The instruction stream.
   * \return `true` if the program has that shape with at least two branches.
   */
  constexpr bool is_fixed_alternation(std::span<const instr> code)
  {
    const std::size_t code_size {code.size()};
    if (code_size < 7 || code[0].op != opcode::save || code[code_size - 2].op != opcode::save ||
        code[code_size - 1].op != opcode::match) {
      return false;
    }
    const std::size_t exit     {code_size - 2};
    std::size_t       pc       {1};
    std::int32_t      branches {};
    while (true) {
      const bool   is_split     {code[pc].op == opcode::split};
      std::size_t  branch_end   {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};
      std::int32_t branch_width {};
      while (branch_end < exit && (code[branch_end].op == opcode::byte || code[branch_end].op == opcode::klass)) {
        ++branch_end;
        ++branch_width;
      }
      if (branch_width == 0) {
        return false;
      }
      ++branches;
      if (is_split) {
        // A non-final branch ends with `jump exit`; continue at the split's y.
        if (branch_end >= exit || code[branch_end].op != opcode::jump ||
            code[branch_end].primary_target != static_cast<std::int32_t>(exit)) {
          return false;
        }
        pc = static_cast<std::size_t>(code[pc].secondary_target);
        if (pc >= exit) {
          return false;
        }
      }
      else {
        // The final branch falls straight through to the exit (save 1).
        return branch_end == exit && branches >= 2;
      }
    }
  }

  /*!
   * \brief Records start anchoring: the first non-save instruction tells whether every
   *        match must begin at position 0 (`\A`/`^` non-multiline) or at a line start.
   */
  constexpr void extract_anchoring(std::span<const instr> code,
                                   pattern_hints&         hints)
  {
    std::size_t pc {};
    while (code[pc].op == opcode::save) {
      ++pc;
    }
    if (code[pc].op == opcode::assert_position) {
      const auto kind {static_cast<assert_kind>(code[pc].arg8)};
      hints.anchored_start = kind == assert_kind::text_start;
      hints.line_anchored  = kind == assert_kind::line_start;
    }
  }

  /*!
   * \brief Collects the required literal prefix and the exact-literal fast-path length.
   *
   * The prefix is the consecutive leading byte instructions (saves and assertions do not
   * consume, so they are crossed: every match still has to begin with the collected bytes;
   * hints only ever filter candidate positions, the engine verifies). The exact-literal hint
   * fires when those bytes ARE the whole match — no assertion appears after the first byte up
   * to `match` (only saves may be crossed). Trailing/inter assertions ($, \b after, …) are
   * post-filters that must go through the normal VM; leading assertions are fine.
   */
  constexpr void extract_prefix(std::span<const instr> code,
                                pattern_hints&         hints)
  {
    std::size_t prefix_pc {};
    while (hints.prefix_size < hints.prefix.size()) {
      if (code[prefix_pc].op == opcode::save || code[prefix_pc].op == opcode::assert_position) {
        ++prefix_pc;
        continue;
      }
      if (code[prefix_pc].op != opcode::byte) {
        break;
      }
      hints.prefix[hints.prefix_size] = static_cast<char>(code[prefix_pc].arg8);
      ++hints.prefix_size;
      ++prefix_pc;
    }

    if (hints.prefix_size > 0) {
      bool has_inter_or_trailing_assert {};
      bool seen_byte                    {};
      for (std::size_t i = 0; i < code.size() && !has_inter_or_trailing_assert; ++i) {
        if (code[i].op == opcode::byte) {
          seen_byte = true;
        }
        else if (seen_byte && code[i].op == opcode::assert_position) {
          has_inter_or_trailing_assert = true;
        }
        else if (seen_byte && code[i].op == opcode::match) {
          break;
        }
      }
      if (!has_inter_or_trailing_assert) {
        std::size_t q {prefix_pc};
        while (q < code.size() && code[q].op == opcode::save) {
          ++q;
        }
        if (q < code.size() && code[q].op == opcode::match) {
          hints.exact_literal_len = hints.prefix_size;
        }
      }
    }
  }

  /*!
   * \brief Computes the possible first-byte set by a DFS over the epsilon closure of pc 0.
   *
   * Assertions are crossed conservatively (they constrain positions, not bytes; a lookaround
   * yields a sound SUPERSET so ⑤ never wrongly rejects a valid start). If `match` is reachable
   * without consuming, an empty match is possible and no byte-based skipping is sound.
   */
  constexpr void compute_first_bytes(std::span<const instr>      code,
                                     std::span<const char_class> classes,
                                     std::span<const cp_class>   cp_classes,
                                     pattern_hints&              hints)
  {
    std::vector<unsigned char> visited(code.size(), 0); // unsigned char, not vector<bool> (constexpr, faster)
    std::vector<std::int32_t>  stack;
    stack.push_back(0);
    bool empty_match_possible {};
    while (!stack.empty()) {
      const std::int32_t current_pc {stack.back()};
      stack.pop_back();
      if (visited[static_cast<std::size_t>(current_pc)] != 0) {
        continue;
      }
      visited[static_cast<std::size_t>(current_pc)] = 1;
      const instr& instruction {code[static_cast<std::size_t>(current_pc)]};
      switch (instruction.op) {
        case opcode::save:
        case opcode::assert_position:
        case opcode::assert_lookaround:
          stack.push_back(current_pc + 1);
          break;
        case opcode::jump:
          stack.push_back(instruction.primary_target);
          break;
        case opcode::split:
          stack.push_back(instruction.primary_target);
          stack.push_back(instruction.secondary_target);
          break;
        case opcode::byte:
          hints.first_bytes.set(instruction.arg8);
          break;
        case opcode::klass:
          hints.first_bytes.merge(classes[instruction.arg16]);
          break;
        case opcode::klass_cp: {
            // A code-point predicate: its effective ASCII members (a `\W`-style complement is already
            // materialised into the bitmap) plus every UTF-8 lead byte a non-ASCII member could begin
            // with -- a sound superset of the possible first bytes.
            const cp_class& cc {cp_classes[static_cast<std::size_t>(instruction.arg16)]};
            hints.first_bytes.merge(cc.ascii);
            hints.first_bytes.merge(utf8_lead2_set());
            hints.first_bytes.merge(utf8_lead3_set());
            hints.first_bytes.merge(utf8_lead4_set());
            break;
          }
        case opcode::match:
          empty_match_possible = true;
          break;
      }
    }
    hints.first_bytes_valid    = !empty_match_possible && !hints.first_bytes.empty();
    hints.empty_match_possible = empty_match_possible;
  }

  /*!
   * \brief Detects the whole-pattern fast-path shapes and sets their hint flags: `class+`,
   *        fixed-shape straight runs, a single codepoint class (`.`/negated, optional `+`),
   *        and an alternation of straight-line branches.
   */
  constexpr void detect_fast_shapes(std::span<const instr>      code,
                                    std::span<const char_class> classes,
                                    std::int32_t                cp_mark_ascii,
                                    std::int32_t                cp_mark_offset,
                                    pattern_hints&              hints)
  {
    // "class+" shape: save 0, klass, split(back to the klass, exit),
    // save 1, match -- greedy only (the lazy variant has different
    // semantics) and no capture groups.
    if (code.size() == 5 && code[0].op == opcode::save && code[1].op == opcode::klass &&
        code[2].op == opcode::split && code[2].primary_target == 1 && code[2].secondary_target == 3 &&
        code[3].op == opcode::save && code[4].op == opcode::match) {
      hints.greedy_class_loop = code[1].arg16;
    }

    // Code-point class, optional greedy `+`: save 0, klass_cp, cont, cont, cont, [split back], save 1,
    // match -- a Unicode shorthand (\w/\d/\s) run, scanned code point by code point without threads.
    // The three `klass` continuations are the klass_cp skip chain; a `+` adds a split looping to the
    // klass_cp. Greedy only, no captures.
    if (code.size() >= 7 && code[0].op == opcode::save && code[1].op == opcode::klass_cp &&
        code[2].op == opcode::klass && code[3].op == opcode::klass && code[4].op == opcode::klass) {
      if (code.size() == 7 && code[5].op == opcode::save && code[6].op == opcode::match) {
        hints.greedy_cp_class      = code[1].arg16;
        hints.greedy_cp_class_plus = false;
      }
      else if (code.size() == 8 && code[5].op == opcode::split && code[5].primary_target == 1 &&
               code[5].secondary_target == 6 && code[6].op == opcode::save && code[7].op == opcode::match) {
        hints.greedy_cp_class      = code[1].arg16;
        hints.greedy_cp_class_plus = true;
      }
    }

    // "fixed shape": a straight-line run of byte/klass with no branches or
    // assertions and no captures (exactly one leading and one trailing save).
    // The whole match is fixed width, so one walk verifies it. Covers class{n}
    // and mixed sequences such as \d{4}-\d{2}-\d{2}; pure literals are caught by
    // the exact-literal path first. Negated classes and `.` expand to byte-level
    // branches, so they never form this shape.
    {
      std::size_t  i          {};
      std::int32_t lead_saves {};
      while (i < code.size() && code[i].op == opcode::save) {
        ++lead_saves;
        ++i;
      }
      std::int32_t width {};
      while (i < code.size() && (code[i].op == opcode::byte || code[i].op == opcode::klass)) {
        ++width;
        ++i;
      }
      std::int32_t trail_saves {};
      while (i < code.size() && code[i].op == opcode::save) {
        ++trail_saves;
        ++i;
      }
      if (lead_saves == 1 && trail_saves == 1 && width >= 1 && i + 1 == code.size() &&
          code[i].op == opcode::match) {
        hints.fixed_shape = true;
      }
    }

    // Whole pattern is a single codepoint class (`.`/negated class), optionally a
    // greedy `+`. Layout: save 0, the 16-instruction codepoint block (at 1..16),
    // then either save 1, match (bare, 19 instructions) or split(loop, exit),
    // save 1, match (the `+`, 20 instructions). No captures; `*` is excluded
    // because its empty match rules out a consuming fast path.
    if ((code.size() == 19 || code.size() == 20) && code[0].op == opcode::save) {
      // The ASCII sub-class index comes from the marker the compiler set when it
      // emitted the block (emit_codepoint_class) — we no longer reverse-engineer the
      // 16-instruction bytecode shape here. The whole-program size / `+`-loop checks
      // are program structure; the ASCII-only test is class content; neither depends
      // on the block's internal opcode layout.
      std::int32_t ascii {(cp_mark_ascii >= 0 && cp_mark_offset == 1
                           && static_cast<std::size_t>(cp_mark_ascii) < classes.size())
                          ? cp_mark_ascii
                          : -1};
      // Content guard: the recorded ASCII sub-class must hold ASCII bytes only.
      // Provably unreachable today — `ast.hpp::parse_class_item` rejects any class
      // member >= 0x80 and `char_class::invert_ascii` leaves the high bytes (>= 0x80)
      // cleared (non-ASCII codepoints are matched via the UTF-8 multi-byte branches),
      // so the marked sub-class is always pure ASCII. Kept deliberately: unlike the
      // bytecode-shape recognition this replaced, it is a *content* check that stays
      // robust to layout changes and becomes load-bearing again if a Unicode
      // codepoint-class mode is ever added.
      if (ascii >= 0) {
        const char_class& ascii_class {classes[static_cast<std::size_t>(ascii)]};
        for (int byte {0x80}; byte <= 0xFF; ++byte) {
          if (ascii_class.test(static_cast<std::uint8_t>(byte))) {
            ascii = -1; // a high byte would mean a non-ASCII sub-class (see guard above)
            break;
          }
        }
      }
      const bool bare {code.size() == 19 && code[17].op == opcode::save &&
                       code[18].op == opcode::match};
      const bool plus {code.size() == 20 && code[17].op == opcode::split && code[17].primary_target == 1 &&
                       code[18].op == opcode::save && code[19].op == opcode::match};
      if (ascii >= 0 && (bare || plus)) {
        hints.codepoint_class_ascii = ascii;
        hints.codepoint_class_plus  = plus;
      }
    }

    // Whole pattern is an alternation of straight-line branches.
    if (is_fixed_alternation(code)) {
      hints.fixed_alternation = true;
    }
  }

  /*!
   * \brief Walks a compiled program once to derive its search hints.
   * \param[in] code           The instruction stream.
   * \param[in] classes        The interned character classes referenced by \p code.
   * \param[in] cp_classes     The match-time code-point classes referenced by `klass_cp`.
   * \param[in] cp_mark_ascii  ASCII sub-class index of an emitted codepoint-class
   *                           block (-1 = none), as recorded by `emit_codepoint_class`.
   * \param[in] cp_mark_offset Program offset where that block starts (-1 = none); the
   *                           whole-pattern codepoint fast path requires it to be 1.
   * \return The \ref pattern_hints (anchoring, literal prefix, first-byte set,
   *         and the `class+` / exact-literal fast-path flags).
   */
  constexpr pattern_hints analyze_program(std::span<const instr>      code,
                                          std::span<const char_class> classes,
                                          std::span<const cp_class>   cp_classes,
                                          std::int32_t                cp_mark_ascii,
                                          std::int32_t                cp_mark_offset)
  {
    pattern_hints hints;

    // A lookaround forces the general Pike VM: no DFA, no fast path. Detected up front;
    // the fast-path hints are cleared at the end so none can fire even partially.
    for (const instr& in : code) {
      if (in.op == opcode::assert_lookaround) {
        hints.has_lookaround = true;
        break;
      }
    }

    extract_anchoring(code, hints);

    extract_prefix(code, hints);

    compute_first_bytes(code, classes, cp_classes, hints);

    detect_fast_shapes(code, classes, cp_mark_ascii, cp_mark_offset, hints);

    // A lookaround program never takes a fast path or the DFA: the general VM must run so
    // the sub-VM can evaluate the assertion. Clear every fast-path hint (belt-and-suspenders
    // — the structural detectors above already miss these shapes). The literal prefix /
    // first-byte set below stay valid (and sound) filters.
    if (hints.has_lookaround) {
      hints.greedy_class_loop     = -1;
      hints.exact_literal_len     = 0;
      hints.fixed_shape           = false;
      hints.codepoint_class_ascii = -1;
      hints.fixed_alternation     = false;
    }

    if (hints.prefix_size > 0) {
      hints.single_first = static_cast<unsigned char>(hints.prefix[0]);
    }
    else if (hints.first_bytes_valid) {
      int found {-1};
      for (unsigned byte = 0; byte < 256 && found != -2; ++byte) {
        if (hints.first_bytes.test(static_cast<std::uint8_t>(byte))) {
          found = found == -1 ? static_cast<int>(byte) : -2;
        }
      }
      hints.single_first = found >= 0 ? static_cast<std::int16_t>(found) : std::int16_t {-1};
    }
    return hints;
  }

  /*!
   * \brief Index of \p byte in `text[pos..)`, or \ref real::npos.
   *
   * Uses `memchr` at run time and a plain loop during constant evaluation.
   *
   * \param[in] text The subject text.
   * \param[in] pos  Index to start scanning from.
   * \param[in] byte The byte to find.
   * \return The index of the first occurrence at or after \p pos, else npos.
   */
  constexpr std::size_t find_byte(std::string_view text,
                                  std::size_t      pos,
                                  char             byte)
  {
    if (pos >= text.size()) {
      return npos;
    }
    if (!std::is_constant_evaluated()) {
      const void* hit {std::memchr(text.data() + pos, byte, text.size() - pos)};
      return hit == nullptr
             ? npos
             : static_cast<std::size_t>(static_cast<const char*>(hit) - text.data());
    }
    for (std::size_t i = pos; i < text.size(); ++i) {
      if (text[i] == byte) {
        return i;
      }
    }
    return npos;
  }

  /*!
   * \brief First position >= \p pos where \p prefix occurs in \p text, or npos.
   *
   * A thin wrapper over the platform's substring search, which is correct and
   * well tuned for the short prefixes (<= 16 bytes) the analyzer extracts.
   *
   * \param[in] text   The subject text.
   * \param[in] pos    Index to start searching from.
   * \param[in] prefix The literal to locate (empty matches at \p pos).
   * \return The index of the first occurrence at or after \p pos, else npos.
   */
  constexpr std::size_t find_prefix(std::string_view text,
                                    std::size_t      pos,
                                    std::string_view prefix)
  {
    if (prefix.empty()) {
      return pos;
    }
    if (pos >= text.size()) {
      return npos;
    }
    const auto off {text.substr(pos).find(prefix)};
    if (off == std::string_view::npos) {
      return npos;
    }
    return pos + off;
  }
} // namespace real::detail

#endif // REAL_PREFILTER_HPP
