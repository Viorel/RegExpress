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

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "real/version.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "real/core/charclass.hpp"
#include "real/core/program.hpp"

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
   * \brief Reports \p klass as up to two contiguous byte ranges.
   *
   * `[lo0, hi0]` is always the first run found scanning byte 0..255; `[lo1, hi1]`
   * the second, if any (`lo1 > hi1` when there is none). Used to test whether a
   * class qualifies for the SIMD range-compare fast path in `run_fixed_shape`.
   *
   * \param[in]  klass The class to scan.
   * \param[out] lo0   Lower bound of the first run.
   * \param[out] hi0   Upper bound of the first run.
   * \param[out] lo1   Lower bound of the second run (unset -- 1 -- when none).
   * \param[out] hi1   Upper bound of the second run (unset -- 0 -- when none).
   * \return The number of contiguous runs found; the caller should treat any count
   *         outside `[1, 2]` (an empty class, or three or more runs) as ineligible.
   */
  constexpr int class_range_count(const char_class&  klass,
                                  std::uint8_t&      lo0,
                                  std::uint8_t&      hi0,
                                  std::uint8_t&      lo1,
                                  std::uint8_t&      hi1)
  {
    int count {0};
    int byte  {0};
    while (byte <= 255) {
      if (!klass.test(static_cast<std::uint8_t>(byte))) {
        ++byte;
        continue;
      }
      const int start {byte};
      while (byte <= 255 && klass.test(static_cast<std::uint8_t>(byte))) {
        ++byte;
      }
      const int end {byte - 1};
      ++count;
      if (count == 1) {
        lo0 = static_cast<std::uint8_t>(start);
        hi0 = static_cast<std::uint8_t>(end);
      }
      else if (count == 2) {
        lo1 = static_cast<std::uint8_t>(start);
        hi1 = static_cast<std::uint8_t>(end);
      }
      else {
        return count; // already ineligible (> 2 runs); no need to keep scanning
      }
    }
    return count;
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
    // "class+", optionally wrapped in exactly ONE capturing group: save 0, [group-start save,] klass,
    // split(back to the klass, exit), [group-end save,] save 1, match. Greedy only (the lazy variant
    // differs). An enveloping group ((\w+), ([a-z]+)) has span == the whole match by
    // construction, so the fast path mirrors the bounds into the group's slots -- no re-match.
    {
      std::size_t  p  {0};
      std::int16_t gs {-1};
      if (p < code.size() && code[p].op == opcode::save) {
        ++p;
        if (p < code.size() && code[p].op == opcode::save) {
          gs = static_cast<std::int16_t>(code[p].arg16);
          ++p;
        }
        if (p + 1 < code.size() && code[p].op == opcode::klass && code[p + 1].op == opcode::split &&
            code[p + 1].primary_target == static_cast<std::int32_t>(p) &&
            code[p + 1].secondary_target == static_cast<std::int32_t>(p + 2)) {
          const std::int32_t cls {code[p].arg16};
          std::size_t        q   {p + 2};
          std::int16_t       ge  {-1};
          bool               ok  {gs < 0};
          if (gs >= 0 && q < code.size() && code[q].op == opcode::save) {
            ge = static_cast<std::int16_t>(code[q].arg16);
            ++q;
            ok = true;
          }
          if (ok && q + 1 < code.size() && code[q].op == opcode::save && code[q + 1].op == opcode::match &&
              q + 2 == code.size()) {
            hints.greedy_class_loop  = cls;
            hints.greedy_group_start = gs;
            hints.greedy_group_end   = ge;
          }
        }
      }
    }

    // Code-point class (klass_cp + its three `klass` continuations), optional greedy `+`, optionally
    // wrapped in one capturing group: save 0, [group-start save,] klass_cp, klass, klass, klass,
    // [split back,] [group-end save,] save 1, match. A \w/\d/\s run scanned code point by code point.
    {
      std::size_t  p  {0};
      std::int16_t gs {-1};
      if (p < code.size() && code[p].op == opcode::save) {
        ++p;
        if (p < code.size() && code[p].op == opcode::save) {
          gs = static_cast<std::int16_t>(code[p].arg16);
          ++p;
        }
        if (p + 3 < code.size() && code[p].op == opcode::klass_cp && code[p + 1].op == opcode::klass &&
            code[p + 2].op == opcode::klass && code[p + 3].op == opcode::klass) {
          const std::int32_t cp_idx  {code[p].arg16};
          const std::size_t  loop_pc {p};
          std::size_t        q       {p + 4};
          bool               plus    {false};
          if (q < code.size() && code[q].op == opcode::split &&
              code[q].primary_target == static_cast<std::int32_t>(loop_pc) &&
              code[q].secondary_target == static_cast<std::int32_t>(q + 1)) {
            plus = true;
            ++q;
          }
          std::int16_t ge {-1};
          bool         ok {gs < 0};
          if (gs >= 0 && q < code.size() && code[q].op == opcode::save) {
            ge = static_cast<std::int16_t>(code[q].arg16);
            ++q;
            ok = true;
          }
          if (ok && q + 1 < code.size() && code[q].op == opcode::save && code[q + 1].op == opcode::match &&
              q + 2 == code.size()) {
            hints.greedy_cp_class      = cp_idx;
            hints.greedy_cp_class_plus = plus;
            hints.greedy_group_start   = gs;
            hints.greedy_group_end     = ge;
          }
        }
      }
    }

    // "fixed shape": a straight-line run of fixed-width byte/klass consuming ops, possibly interleaved
    // with capturing saves ((\d{4})-(\d{2})-(\d{2}), (a)(b)), with no branches or assertions. The
    // whole match is fixed width, so one walk verifies it; because every width is fixed, each save sits
    // at a compile-time-constant offset from the match start, so the fast path fills each group slot by
    // that offset (no re-match). Covers class{n} and mixed sequences; pure literals hit the exact-literal
    // path first. A klass_cp (Unicode shorthand, variable width), split/jump (alternation, {n,m}/+/*/?),
    // `.` or a negated class (byte-level branches), and lookarounds all break the run, so they never form
    // this shape -- ASCII / explicit-class fixed widths are what qualify.
    {
      std::size_t  i           {};
      std::int32_t width       {};
      std::int32_t open_groups {};  // capturing groups (slots >= 2) currently open, for the nesting guard
      bool         closed      {};  // saw the closing save (slot 1)
      bool         nested      {};  // a group opened inside another -- kept on the general VM (flat only)
      if (i < code.size() && code[i].op == opcode::save && code[i].arg16 == 0) {
        ++i; // opening save (slot 0)
        while (i < code.size()) {
          const opcode op {code[i].op};
          if (op == opcode::byte || op == opcode::klass) {
            ++width;
            ++i;
          }
          else if (op == opcode::save) {
            const std::int32_t slot {code[i].arg16};
            if (slot == 1) {
              closed = true;
            }
            else if (slot >= 2 && (slot % 2) == 0) { // an inner group's opening save
              if (open_groups > 0) {
                nested = true;
              }
              ++open_groups;
            }
            else if (slot >= 3) { // an inner group's closing save
              --open_groups;
            }
            ++i;
          }
          else {
            break; // any other op (split/jump/klass_cp/assert/lookaround) disqualifies the shape
          }
        }
        if (width >= 1 && closed && !nested && i + 1 == code.size() && code[i].op == opcode::match) {
          hints.fixed_shape = true;
        }
      }

      // SIMD verify eligibility: the run above qualifies for a vectorized scan+verify (pike.hpp
      // run_fixed_shape) only when it is also HOMOGENEOUS -- every byte/klass position accepts the
      // identical set, itself <= 2 contiguous ranges -- because the sound "skip to the first failing
      // lane" only holds when a mismatch at any position rules out every position (same required set
      // everywhere). Mixed shapes ((\d{4})-(\d{2})-(\d{2})) stay on the scalar walk.
      if (hints.fixed_shape) {
        bool          homogeneous {true};
        bool          have_first  {false};
        std::uint8_t  lo0         {};
        std::uint8_t  hi0         {};
        std::uint8_t  lo1         {1};
        std::uint8_t  hi1         {};
        std::uint32_t len         {};
        for (std::size_t pc {1}; pc < i; ++pc) {
          const opcode op {code[pc].op};
          if (op != opcode::byte && op != opcode::klass) {
            continue; // interleaved capturing save -- epsilon for this purpose
          }
          ++len;
          std::uint8_t plo0 {};
          std::uint8_t phi0 {};
          std::uint8_t plo1 {1};
          std::uint8_t phi1 {};
          if (op == opcode::byte) {
            plo0 = code[pc].arg8;
            phi0 = code[pc].arg8;
          }
          else {
            const int ranges {class_range_count(classes[code[pc].arg16], plo0, phi0, plo1, phi1)};
            if (ranges < 1 || ranges > 2) {
              homogeneous = false;
              break;
            }
          }
          if (!have_first) {
            lo0        = plo0;
            hi0        = phi0;
            lo1        = plo1;
            hi1        = phi1;
            have_first = true;
          }
          else if (plo0 != lo0 || phi0 != hi0 || plo1 != lo1 || phi1 != hi1) {
            homogeneous = false; // a position needs a different set -- not uniform, disqualified
            break;
          }
        }
        if (homogeneous && have_first && len >= 1 && len <= 16) {
          hints.fixed_shape_lo0      = lo0;
          hints.fixed_shape_hi0      = hi0;
          hints.fixed_shape_lo1      = lo1;
          hints.fixed_shape_hi1      = hi1;
          hints.fixed_shape_simd_len = static_cast<std::uint8_t>(len);
        }
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
   * \brief Approximate static frequency of a byte in mixed English + source text (occurrences per 10000;
   *        higher = more common). No text is ever scanned — this only ranks candidate prefilter bytes
   *        against one another. Punctuation like `-` `@` `.` is far rarer than any letter, digit or space,
   *        which is the whole point: a required rare byte makes a far more selective `memchr` target than a
   *        common first-byte class.
   */
  constexpr std::uint16_t byte_frequency(std::uint8_t b)
  {
    constexpr std::array<std::uint16_t, 26> lower {
      650, 150, 300, 350, 1000, 200, 180, 450, 550, 15, 80, 350, 250,
      550, 600, 170, 10, 450, 500, 700, 250, 100, 200, 15, 180, 8};
    if (b >= 'a' && b <= 'z') {
      return lower[b - 'a'];
    }
    if (b >= 'A' && b <= 'Z') {
      return static_cast<std::uint16_t>((lower[b - 'A'] / 6) + 3);
    }
    if (b >= '0' && b <= '9') {
      return 120;
    }
    switch (b) {
      case ' ':                          return 1500;
      case '.':                          return 120;
      case '\n': case ',':               return 100;
      case '/':                          return 60;
      case '(': case ')': case '_':      return 50;
      case '"': case '\'':               return 45;
      case '\t': case '=': case '-':     return 40;
      case ':':                          return 35;
      case '>': case '<':                return 30;
      case ';': case '*':                return 25;
      case '{': case '}':                return 22;
      case '[': case ']': case '+':      return 20;
      case '?': case '!': case '|':      return 15;
      case '%':                          return 12;
      case '&':                          return 10;
      case '#': case '\\': case '$':     return 8;
      case '~': case '@': case '^': case '`': return 3;
      default:                           return 1; // control and high bytes: assume very rare
    }
  }

  /*!
   * \brief Finds a *required* literal byte at a FIXED offset that is statically far rarer than the
   *        pattern's first-byte set, and records it (\ref pattern_hints::rare_byte / rare_offset) so the
   *        search can `memchr` that one byte instead of scanning a common first-byte class per byte.
   *
   * Walks the leading FIXED-WIDTH shape from the start: `save`/`assert_position` are crossed (no width),
   * `byte` and `klass` each advance the byte offset by exactly one, and any `byte` is a candidate. It stops
   * at the first variable-width or branching op — `klass_cp` (a code point is 1–4 bytes, so offsets past it
   * are not fixed), `split`, `jump`, `match`. The chosen byte must be below an absolute rarity threshold
   * and several times rarer than the first-byte set, or the existing first-byte scan already suffices. The
   * hint only filters candidate starts; the VM still verifies, so it is always sound.
   */
  constexpr void extract_rare_byte(std::span<const instr> code,
                                   pattern_hints&         hints)
  {
    if (hints.anchored_start || hints.prefix_size >= 2) {
      return; // anchored needs no scan; a literal prefix is already a stronger filter
    }
    std::size_t   pc         {0};
    std::size_t   offset     {0};
    std::int16_t  best_byte  {-1};
    std::uint8_t  best_off   {0};
    std::uint16_t best_freq  {0xFFFFU};
    while (pc < code.size() && offset <= 0xFFU) {
      const opcode op {code[pc].op};
      if (op == opcode::save || op == opcode::assert_position) {
        ++pc;
        continue;
      }
      if (op == opcode::byte) {
        const auto freq {byte_frequency(static_cast<std::uint8_t>(code[pc].arg8))};
        if (freq < best_freq) {
          best_freq = freq;
          best_byte = static_cast<std::int16_t>(static_cast<std::uint8_t>(code[pc].arg8));
          best_off  = static_cast<std::uint8_t>(offset);
        }
        ++offset;
        ++pc;
      }
      else if (op == opcode::klass) {
        ++offset; // a byte class consumes exactly one byte: the offset stays fixed
        ++pc;
      }
      else {
        break; // klass_cp (variable width) / split / jump / match: the offset is no longer fixed
      }
    }
    if (best_byte < 0) {
      return;
    }
    // Effective commonness of the current first-byte filter: a single byte's frequency, or the sum over a
    // class (a class is only as selective as the total traffic it stops on).
    std::uint32_t first_freq {0};
    if (hints.single_first >= 0) {
      first_freq = byte_frequency(static_cast<std::uint8_t>(hints.single_first));
    }
    else {
      for (int b = 0; b < 256; ++b) {
        if (hints.first_bytes.test(static_cast<std::uint8_t>(b))) {
          first_freq += byte_frequency(static_cast<std::uint8_t>(b));
        }
      }
    }
    if (best_freq < 100U && static_cast<std::uint32_t>(best_freq) * 4U < first_freq) {
      hints.rare_byte   = best_byte;
      hints.rare_offset = best_off;
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
    // the fast-path hints are cleared at the end so none can fire even partially. This is a local,
    // not a persisted hint: nothing past analyze_program ever reads it (the fast paths and the DFA
    // gate on the individual hints this clears / on the program itself).
    bool has_lookaround {false};
    for (const instr& in : code) {
      if (in.op == opcode::assert_lookaround) {
        has_lookaround = true;
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
    if (has_lookaround) {
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
      // Enumerate the set, stopping once it exceeds four. A single member drives find_byte (one memchr);
      // two-to-four members drive the memchr-cascade (small_set); five or more stay on the bitmap loop.
      std::array<char, 4> members {};
      int                 count   {0};
      for (unsigned byte = 0; byte < 256; ++byte) {
        if (hints.first_bytes.test(static_cast<std::uint8_t>(byte))) {
          if (count < 4) {
            members[static_cast<std::size_t>(count)] = static_cast<char>(byte);
          }
          ++count;
          if (count > 4) {
            break;
          }
        }
      }
      if (count == 1) {
        hints.single_first = static_cast<std::int16_t>(static_cast<unsigned char>(members[0]));
      }
      else if (count >= 2 && count <= 4) {
        hints.small_set      = members;
        hints.small_set_size = static_cast<std::uint8_t>(count);
      }
    }

    // OPT-C: for a whole-pattern `class+` run (run_class_loop — a byte-wise scan), record the STOP
    // bytes (the complement of the accepted set) when there are at most six of them, so the run can
    // advance by a memchr-cascade to the next stop instead of testing every byte. The stops are derived
    // from the class table and do NOT enter the compiled program, so byte-identity is unaffected.
    if (hints.greedy_class_loop >= 0) {
      const char_class&   accepted   {classes[static_cast<std::size_t>(hints.greedy_class_loop)]};
      std::array<char, 6> stops      {};
      int                 stop_count {0};
      for (unsigned byte = 0; byte < 256; ++byte) {
        if (!accepted.test(static_cast<std::uint8_t>(byte))) {
          if (stop_count < 6) {
            stops[static_cast<std::size_t>(stop_count)] = static_cast<char>(byte);
          }
          ++stop_count;
          if (stop_count > 6) {
            break;
          }
        }
      }
      if (stop_count >= 1 && stop_count <= 6) {
        hints.stop_set      = stops;
        hints.stop_set_size = static_cast<std::uint8_t>(stop_count);
      }
    }
    // OPT-C-1b: a whole-pattern code-point-class run (`.`/`[^x]` in text/ascii mode) accepts EVERY valid
    // code point >= 0x80 — run_codepoint_class validates the UTF-8 structure but not membership above
    // ASCII — so once its ASCII complement is small the run can be SWAR-accelerated soundly: memchr the
    // ASCII stops for an upper bound, high-bit-scan the ASCII stretches, and drop to code-point
    // validation only across a non-ASCII cluster (so malformed UTF-8 still stops the run, unchanged). The
    // stops here are only the ASCII bytes the class rejects.
    else if (hints.codepoint_class_ascii >= 0) {
      const char_class&   accepted   {classes[static_cast<std::size_t>(hints.codepoint_class_ascii)]};
      std::array<char, 6> stops      {};
      int                 stop_count {0};
      for (unsigned byte = 0; byte < 0x80; ++byte) {
        if (!accepted.test(static_cast<std::uint8_t>(byte))) {
          if (stop_count < 6) {
            stops[static_cast<std::size_t>(stop_count)] = static_cast<char>(byte);
          }
          ++stop_count;
          if (stop_count > 6) {
            break;
          }
        }
      }
      if (stop_count >= 1 && stop_count <= 6) {
        hints.stop_set      = stops;
        hints.stop_set_size = static_cast<std::uint8_t>(stop_count);
      }
    }

    // OPT: a required rare literal byte at a fixed offset (e.g. the `-` in `[0-9]{4}-[0-9]{2}-[0-9]{2}`)
    // gives a single-byte memchr target far more selective than the first-byte class. Computed last, so it
    // can compare against the finalized first-byte hints. Sound: it only filters candidate starts.
    extract_rare_byte(code, hints);
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
   * \brief Index of the first occurrence of \p literal in `text[pos..)`, or \ref real::npos.
   *
   * The substring search behind the inner-literal prefilter. A single byte delegates to \ref find_byte (one
   * `memchr`). For a multi-byte literal it scans for the lead byte with `memchr` (SIMD at run time) and
   * verifies the tail — a portable substring search that needs no `memmem` (absent on MSVC), and stays a
   * plain loop during constant evaluation.
   */
  constexpr std::size_t find_literal(std::string_view text,
                                     std::size_t      pos,
                                     std::string_view literal)
  {
    if (literal.empty()) {
      return pos <= text.size() ? pos : npos;
    }
    if (literal.size() == 1U) {
      return find_byte(text, pos, literal.front());
    }
    if (text.size() < literal.size()) {
      return npos;
    }
    const std::size_t last_start {text.size() - literal.size()};
    std::size_t       i          {pos};
    while (i <= last_start) {
      const std::size_t hit {find_byte(text, i, literal.front())};
      if (hit == npos || hit > last_start) {
        return npos;
      }
      if (text.compare(hit, literal.size(), literal) == 0) {
        return hit;
      }
      i = hit + 1U;
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

  /*!
   * \brief Index of the first byte in `text[pos..)` that belongs to a small (2..4) first-byte set.
   *
   * A cascade of `std::memchr` — one per set member — taking the minimum hit position. After each hit
   * the scan window is narrowed to `[pos, best)`, so later members only search the shorter prefix and a
   * near hit makes the remaining calls cheap. This beats the one-test-per-byte bitmap loop when the set
   * is small: `memchr` is vectorised in libc, so even four sparse scans cover ground far faster than a
   * scalar byte loop. During constant evaluation the plain member-wise scan runs instead (the home-made
   * path — the same shape the bitmap loop takes).
   *
   * \param[in] text The subject text.
   * \param[in] pos  Index to start scanning from.
   * \param[in] set  Pointer to the enumerated set members (first \p n valid).
   * \param[in] n    Number of valid members (2..4).
   * \return The least index at or after \p pos whose byte is in the set, else npos.
   */
  constexpr std::size_t find_bytes_cascade(std::string_view text,
                                           std::size_t      pos,
                                           const char*      set,
                                           std::uint8_t     n)
  {
    if (pos >= text.size()) {
      return npos;
    }
    if (!std::is_constant_evaluated()) {
      const char* const base   {text.data()};
      std::size_t       window {text.size() - pos}; // narrows to [pos, best) as hits are found
      std::size_t       best   {npos};
      for (std::uint8_t i = 0; i < n; ++i) {
        const void* hit {std::memchr(base + pos, set[i], window)};
        if (hit != nullptr) {
          const std::size_t idx {static_cast<std::size_t>(static_cast<const char*>(hit) - base)};
          if (idx < best) {
            best   = idx;
            window = idx - pos; // subsequent members need only search before the current best
          }
        }
      }
      return best;
    }
    for (std::size_t i = pos; i < text.size(); ++i) {
      for (std::uint8_t j = 0; j < n; ++j) {
        if (text[i] == set[j]) {
          return i;
        }
      }
    }
    return npos;
  }

  /*!
   * \brief Index of the first byte `>= 0x80` in `text[pos, end)`, or \p end if the range is pure ASCII.
   *
   * A SWAR scan for the high bit: load eight bytes at a time (a `memcpy` into a `std::uint64_t`, so it is
   * alignment- and aliasing-safe) and test `& 0x8080…`; a clear word skips eight ASCII bytes at once. On
   * a hit the eight bytes are re-checked scalarly (endianness-free, and only for the one straddling
   * word); the head/tail run scalar. During constant evaluation the plain scalar loop runs.
   *
   * \param[in] text The subject text.
   * \param[in] pos  Start of the range.
   * \param[in] end  Exclusive end of the range (`<= text.size()`).
   * \return The least index in `[pos, end)` whose byte is `>= 0x80`, else \p end.
   */
  constexpr std::size_t first_high_byte(std::string_view text,
                                        std::size_t      pos,
                                        std::size_t      end)
  {
    if (!std::is_constant_evaluated()) {
      const char* const base {text.data()};
      std::size_t       i    {pos};
      for (; i + 8 <= end; i += 8) {
        std::uint64_t word {};
        std::memcpy(&word, base + i, 8);
        if ((word & 0x8080808080808080ULL) != 0U) {
          for (std::size_t b = 0; b < 8; ++b) {
            if (static_cast<std::uint8_t>(base[i + b]) >= 0x80U) {
              return i + b;
            }
          }
        }
      }
      for (; i < end; ++i) {
        if (static_cast<std::uint8_t>(base[i]) >= 0x80U) {
          return i;
        }
      }
      return end;
    }
    for (std::size_t i = pos; i < end; ++i) {
      if (static_cast<std::uint8_t>(text[i]) >= 0x80U) {
        return i;
      }
    }
    return end;
  }
} // namespace real::detail

#endif // REAL_PREFILTER_HPP
