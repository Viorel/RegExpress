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
#include "real/unicode/unicode_props.hpp" // word_ranges — exact \w identity for Arc B-1

namespace real::detail {

  //! \brief Prefilter work counter for the O(n) vs O(n²) smoke test.
  //!        Always declared (clang-tidy / tests see the symbol). Billing is a no-op unless
  //!        \c REAL_TEST_INSTRUMENT is defined on the test binary — wheel/prod pay nothing.
  inline std::uint64_t& prefilter_work_units() noexcept
  {
    static std::uint64_t units {0};
    return units;
  }

  inline void prefilter_note_scan(std::size_t n) noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    prefilter_work_units() += static_cast<std::uint64_t>(n);
#else
    (void) n;
#endif
  }

  /*!
   * \brief True if \p kind is `\b` or `\B` (the only position asserts B1 wraps on fast paths).
   * \param[in] kind Assertion kind from `assert_position`.
   */
  [[nodiscard]] constexpr bool is_word_boundary_kind(assert_kind kind) noexcept
  {
    return kind == assert_kind::word_boundary || kind == assert_kind::not_word_boundary;
  }

  /*!
   * \brief Encodes \p kind as a wb_lead/wb_trail hint value (1 = `\b`, 2 = `\B`); 0 if not a word boundary.
   * \param[in] kind Assertion kind from `assert_position`.
   */
  [[nodiscard]] constexpr std::uint8_t wb_hint_of(assert_kind kind) noexcept
  {
    if (kind == assert_kind::word_boundary) {
      return 1;
    }
    if (kind == assert_kind::not_word_boundary) {
      return 2;
    }
    return 0;
  }

  /*!
   * \brief Peel an optional lead `\b`/`\B` at \p p (typically after `save 0`).
   *
   * If \p p is not an assert, leaves \p lead at 0 and returns true. If it is a word-boundary
   * assert, records the hint, advances \p p, returns true. If it is any other assert, returns
   * false (shape disqualified for wb-wrapping fast paths).
   *
   * \param[in]     code Instruction stream.
   * \param[in,out] p    Program counter (advanced past the lead assert when peeled).
   * \param[out]    lead Hint 0/1/2.
   * \return false if a non-wb lead assert blocks the shape.
   */
  [[nodiscard]] constexpr bool peel_optional_lead_wb(std::span<const instr> code,
                                                     std::size_t&           p,
                                                     std::uint8_t&          lead) noexcept
  {
    lead = 0;
    if (p >= code.size() || code[p].op != opcode::assert_position) {
      return true;
    }
    const auto k {static_cast<assert_kind>(code[p].arg8)};
    if (!is_word_boundary_kind(k)) {
      return false;
    }
    lead = wb_hint_of(k);
    ++p;
    return true;
  }

  /*!
   * \brief Peel an optional trail `\b`/`\B` at \p p (before closing `save 1` / exit).
   *
   * Same contract as \ref peel_optional_lead_wb for a trailing assert.
   *
   * \param[in]     code  Instruction stream.
   * \param[in,out] p     Program counter (advanced past the trail assert when peeled).
   * \param[out]    trail Hint 0/1/2.
   * \return false if a non-wb trail assert blocks the shape.
   */
  [[nodiscard]] constexpr bool peel_optional_trail_wb(std::span<const instr> code,
                                                      std::size_t&           p,
                                                      std::uint8_t&          trail) noexcept
  {
    trail = 0;
    if (p >= code.size() || code[p].op != opcode::assert_position) {
      return true;
    }
    const auto k {static_cast<assert_kind>(code[p].arg8)};
    if (!is_word_boundary_kind(k)) {
      return false;
    }
    trail = wb_hint_of(k);
    ++p;
    return true;
  }

  //! \brief detect_fast_shapes's outer envelope: `save 0`, optional lead `\b`/`\B`. No-ops safely
  //!        on a shape with no `\b`/`\B` support (e.g. a literal `byte` right after `save 0`).
  struct shape_lead
  {
    std::size_t  body_start {}; //!< pc where the shape-specific body begins.
    std::uint8_t wb_lead    {}; //!< 0/1/2, see \ref wb_hint_of.
    bool         ok         {}; //!< false: no leading `save 0`, or a non-wb lead assert disqualifies.
  };

  [[nodiscard]] constexpr shape_lead parse_shape_lead(std::span<const instr> code) noexcept
  {
    if (code.empty() || code[0].op != opcode::save || code[0].arg16 != 0) {
      return {};
    }
    std::size_t  p       {1};
    std::uint8_t wb_lead {0};
    if (!peel_optional_lead_wb(code, p, wb_lead)) {
      return {};
    }
    return {.body_start = p, .wb_lead = wb_lead, .ok = true};
  }

  //! \brief The \ref shape_lead counterpart: optional trail `\b`/`\B` at \p from, then exactly
  //!        `save 1`, `match` at the very end of \p code. \p from (the body's own end) is the
  //!        caller's to supply -- only its shape-specific body walk knows where that is.
  struct shape_close
  {
    std::uint8_t wb_trail {}; //!< 0/1/2, see \ref wb_hint_of.
    bool         ok       {}; //!< false: not exactly `save 1`, `match` at the end after peeling.
  };

  [[nodiscard]] constexpr shape_close parse_shape_close(std::span<const instr> code,
                                                        std::size_t            from) noexcept
  {
    std::uint8_t wb_trail {0};
    if (!peel_optional_trail_wb(code, from, wb_trail)) {
      return {};
    }
    if (from + 1 < code.size() && code[from].op == opcode::save && code[from].arg16 == 1 &&
        code[from + 1].op == opcode::match && from + 2 == code.size()) {
      return {.wb_trail = wb_trail, .ok = true};
    }
    return {};
  }

  //! \brief True if \p cls is exactly the ASCII word set `[0-9A-Za-z_]` (`\w` under bytes/`re.A`).
  [[nodiscard]] constexpr bool is_full_ascii_word_class(const char_class& cls) noexcept
  {
    for (unsigned b = 0; b < 128U; ++b) {
      if (cls.test(static_cast<std::uint8_t>(b)) != is_ascii_word_byte(static_cast<std::uint8_t>(b))) {
        return false;
      }
    }
    for (unsigned b = 128U; b < 256U; ++b) {
      if (cls.test(static_cast<std::uint8_t>(b))) {
        return false;
      }
    }
    return true;
  }

  //! \brief True if every member of \p cls is an ASCII word byte (subset of `\w` under bytes/`re.A`).
  [[nodiscard]] constexpr bool is_ascii_word_subset_class(const char_class& cls) noexcept
  {
    bool any {false};
    for (unsigned b = 0; b < 256U; ++b) {
      if (!cls.test(static_cast<std::uint8_t>(b))) {
        continue;
      }
      any = true;
      if (!is_ascii_word_byte(static_cast<std::uint8_t>(b))) {
        return false;
      }
    }
    return any;
  }

  /*!
   * \brief True if \p cc is exactly the canonical Unicode `\w` class (not a user superset).
   *
   * Compares the ASCII bitmap to \ref is_ascii_word_byte and the non-ASCII range slice to
   * \ref word_ranges (generated, same table the compiler uses for `\w`). A threshold on
   * \c range_count alone is unsound: `[\w😀]` has the `\w` ASCII half plus one extra range
   * and would pass `>= 200`, but `\b[\w😀]+\b` is NOT equivalent to `[\w😀]+`.
   *
   * \param[in] cc         The code-point class under test.
   * \param[in] all_ranges Program flat range buffer (\p cc indexes a slice of it).
   */
  [[nodiscard]] constexpr bool is_full_unicode_word_cp_class(const cp_class&             cc,
                                                             std::span<const code_range> all_ranges) noexcept
  {
    for (unsigned b = 0; b < 128U; ++b) {
      if (cc.ascii.test(static_cast<std::uint8_t>(b)) !=
          is_ascii_word_byte(static_cast<std::uint8_t>(b))) {
        return false;
      }
    }
    std::size_t word_hi {0};
    for (std::size_t i = 0; i < word_ranges_size; ++i) {
      if (word_ranges[i].lo >= 0x80U) {
        ++word_hi;
      }
    }
    if (static_cast<std::size_t>(cc.range_count) != word_hi) {
      return false;
    }
    if (static_cast<std::size_t>(cc.range_begin) + word_hi > all_ranges.size()) {
      return false;
    }
    std::size_t j {0};
    for (std::size_t i = 0; i < word_ranges_size; ++i) {
      if (word_ranges[i].lo < 0x80U) {
        continue;
      }
      const code_range& r {all_ranges[static_cast<std::size_t>(cc.range_begin) + j]};
      if (r.lo != word_ranges[i].lo || r.hi != word_ranges[i].hi) {
        return false;
      }
      ++j;
    }
    return true;
  }

  //! \brief Arc B-1: `\b` next to a full-`\w` MAXIMAL run is redundant (`\B` never is).
  //!        Only sound when the match is a greedy `+` run: a maximal run of `\w` can only ever
  //!        START where the character before it is non-word (or absent) -- that IS `\b` (or the
  //!        text edge), so checking it again is redundant. A SINGLE code point (no `+`) has no
  //!        such guarantee: `\b\w` may legally start mid-run (any word code point qualifies as a
  //!        candidate start), so dropping the boundary there is unsound, not just conservative.
  //!        The caller is responsible for only calling this when \p lead / \p trail came from a
  //!        provably maximal-run shape (see \ref resolve_class_wb_hints's \p maximal_run).
  [[nodiscard]] constexpr bool wb_redundant_for_full_word(std::uint8_t lead,
                                                          std::uint8_t trail) noexcept
  {
    if (lead == 2 || trail == 2) {
      return false;
    }
    return lead == 1 || trail == 1;
  }

  /*!
   * \brief B-1/B-2 policy for class / cp-class loops under optional `\b` wraps.
   *
   * Unarms on `\B` or on a non-word-subset class under `\b` (superset maximal-run is unsound).
   * Full word + `\b` drops boundaries (B-1) -- but ONLY for a maximal (`+`) run; see \ref
   * wb_redundant_for_full_word. Proper word subset keeps the wrap (B-2). Bare (no `\b`) arms with
   * zero wb hints.
   *
   * \param[in]  full_word   Exact `\w` class (ASCII or Unicode table identity).
   * \param[in]  word_sub    Non-empty subset of `\w`.
   * \param[in]  maximal_run Whether the class loop is a greedy `+` (a maximal run, so any valid
   *                         start already sits at a word/non-word transition) rather than a
   *                         single code point (which may start anywhere inside a word run, where
   *                         B-1's redundancy argument does not hold).
   * \param[in]  lead        Peeled lead hint.
   * \param[in]  trail       Peeled trail hint.
   * \param[out] out_lead    Hints to store (0 when dropped).
   * \param[out] out_trail   Hints to store (0 when dropped).
   * \return true if the fast path should arm.
   */
  [[nodiscard]] constexpr bool resolve_class_wb_hints(bool          full_word,
                                                      bool          word_sub,
                                                      bool          maximal_run,
                                                      std::uint8_t  lead,
                                                      std::uint8_t  trail,
                                                      std::uint8_t& out_lead,
                                                      std::uint8_t& out_trail) noexcept
  {
    if (lead == 2 || trail == 2) {
      return false;
    }
    const bool has_wb {lead != 0 || trail != 0};
    if (has_wb && !full_word && !word_sub) {
      return false;
    }
    if (has_wb && word_sub &&
        !(maximal_run && full_word && wb_redundant_for_full_word(lead, trail))) {
      out_lead  = lead;
      out_trail = trail;
    }
    else {
      out_lead  = 0;
      out_trail = 0;
    }
    return true;
  }

  //! \brief True if every code point in [\p lo, \p hi] is a Unicode word char (covered by \ref word_ranges).
  [[nodiscard]] constexpr bool word_ranges_cover_interval(char32_t lo,
                                                          char32_t hi) noexcept
  {
    if (lo > hi) {
      return false;
    }
    char32_t cur {lo};
    while (cur <= hi) {
      // Linear scan is fine: called only on the (small) user-class range list at analyze time.
      bool found {false};
      for (std::size_t i = 0; i < word_ranges_size; ++i) {
        const code_range& wr {word_ranges[i]};
        if (wr.hi < cur) {
          continue;
        }
        if (wr.lo > cur) {
          return false; // gap: cur is not a word char
        }
        // wr covers cur; advance past wr.hi
        if (wr.hi >= hi) {
          return true;
        }
        cur   = wr.hi + 1;
        found = true;
        break;
      }
      if (!found) {
        return false;
      }
    }
    return true;
  }

  /*!
   * \brief True if \p cc is a non-empty subset of Unicode `\w` (safe for maximal-run + `\b` wrap).
   *
   * Supersets like `[\w😀]` must NOT take B-2: a maximal class run can start on a non-word member
   * and skip over a later word-bounded sub-run.
   */
  [[nodiscard]] constexpr bool is_unicode_word_subset_cp_class(const cp_class&             cc,
                                                               std::span<const code_range> all_ranges) noexcept
  {
    for (unsigned b = 0; b < 128U; ++b) {
      if (cc.ascii.test(static_cast<std::uint8_t>(b)) &&
          !is_ascii_word_byte(static_cast<std::uint8_t>(b))) {
        return false;
      }
    }
    if (static_cast<std::size_t>(cc.range_begin) + cc.range_count > all_ranges.size()) {
      return false;
    }
    for (std::uint32_t i = 0; i < cc.range_count; ++i) {
      const code_range& r {all_ranges[static_cast<std::size_t>(cc.range_begin) + i]};
      if (!word_ranges_cover_interval(static_cast<char32_t>(r.lo), static_cast<char32_t>(r.hi))) {
        return false;
      }
    }
    // At least one member (ASCII or range) so `\w+`-shaped paths stay non-nullable.
    bool any {false};
    for (unsigned b = 0; b < 128U && !any; ++b) {
      any = cc.ascii.test(static_cast<std::uint8_t>(b));
    }
    return any || cc.range_count > 0;
  }

  //! \brief D1-perf (Étage A) safety check: true if the ASCII byte \p b could be a member of code-point
  //!        class \p cc — used only to test whether a single-byte delimiter (a "quoted"-shape prefix or
  //!        suffix) could hide inside a `klass_cp_loop_possessive` body, in which case the delimited
  //!        fast path must decline (see \ref pattern_hints::possessive_prefix). A non-ASCII \p b (>=
  //!        0x80) is conservatively treated as a member (unsafe, declines) — this shape's corpus is
  //!        single-byte ASCII delimiters (`"`, `;`, …), so a multi-byte delimiter simply stays general.
  [[nodiscard]] constexpr bool cp_class_may_contain_ascii_byte(const cp_class& cc,
                                                               std::uint8_t    b) noexcept
  {
    if (b >= 0x80U) {
      return true;
    }
    return cc.ascii.test(b);
  }

  /*!
   * \brief Alternation of straight-line byte/klass branches, optionally wrapped in `\b`/`\B`.
   *
   * Layout: `save 0`, optional lead word-boundary assert, split chain of branches, optional trail
   * word-boundary assert, `save 1`, `match`. Branch jumps target the first instruction after the
   * last branch (trail assert or save 1). Captures other than group 0, nested branches, empty
   * branches, and non-wb assertions all disqualify.
   *
   * \param[in]  code          The instruction stream.
   * \param[out] out_wb_lead   Optional; receives lead wb hint (0/1/2).
   * \param[out] out_wb_trail  Optional; receives trail wb hint (0/1/2).
   * \param[out] out_body_pc   Optional; receives first branch/split pc after lead wrap.
   * \return `true` if the program has that shape with at least two branches.
   */
  constexpr bool is_fixed_alternation(std::span<const instr> code,
                                      std::uint8_t*          out_wb_lead  = nullptr,
                                      std::uint8_t*          out_wb_trail = nullptr,
                                      std::uint8_t*          out_body_pc  = nullptr)
  {
    const std::size_t code_size {code.size()};
    if (code_size < 7 || code[0].op != opcode::save || code[code_size - 1].op != opcode::match ||
        code[code_size - 2].op != opcode::save || code[code_size - 2].arg16 != 1) {
      return false;
    }
    std::uint8_t wb_lead  {0};
    std::uint8_t wb_trail {0};
    std::size_t  body     {1};
    std::size_t  exit_pc  {code_size - 2}; // save 1
    // Optional trail \b/\B immediately before save 1 (branches jump to it).
    if (exit_pc >= 2 && code[exit_pc - 1].op == opcode::assert_position) {
      std::size_t t {exit_pc - 1};
      if (!peel_optional_trail_wb(code, t, wb_trail)) {
        return false;
      }
      exit_pc = exit_pc - 1;
    }
    // Optional lead \b/\B immediately after save 0.
    if (!peel_optional_lead_wb(code, body, wb_lead)) {
      return false;
    }
    if (body >= exit_pc) {
      return false;
    }
    std::size_t  pc       {body};
    std::int32_t branches {};
    while (true) {
      const bool   is_split     {code[pc].op == opcode::split};
      std::size_t  branch_end   {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};
      std::int32_t branch_width {};
      while (branch_end < exit_pc &&
             (code[branch_end].op == opcode::byte || code[branch_end].op == opcode::klass)) {
        ++branch_end;
        ++branch_width;
      }
      if (branch_width == 0) {
        return false;
      }
      ++branches;
      if (is_split) {
        // A non-final branch ends with `jump exit`; continue at the split's y.
        if (branch_end >= exit_pc || code[branch_end].op != opcode::jump ||
            code[branch_end].primary_target != static_cast<std::int32_t>(exit_pc)) {
          return false;
        }
        pc = static_cast<std::size_t>(code[pc].secondary_target);
        if (pc >= exit_pc) {
          return false;
        }
      }
      else {
        // The final branch falls straight through to the exit (trail assert or save 1).
        if (branch_end == exit_pc && branches >= 2) {
          if (out_wb_lead != nullptr) {
            *out_wb_lead = wb_lead;
          }
          if (out_wb_trail != nullptr) {
            *out_wb_trail = wb_trail;
          }
          if (out_body_pc != nullptr) {
            *out_body_pc = static_cast<std::uint8_t>(body);
          }
          return true;
        }
        return false;
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
      // Leading asserts were crossed above. Allow only word-boundary asserts after the last
      // prefix byte (B1: `\bLIT\b` / `LIT\b`); any other trailing/inter assert stays on the VM.
      bool         blocking_assert {};
      bool         seen_byte       {};
      std::uint8_t trail_wb        {};
      std::uint8_t lead_wb         {};
      // Lead \b/\B: first assert_position before any byte (after saves).
      for (std::size_t i = 0; i < code.size(); ++i) {
        if (code[i].op == opcode::byte) {
          break;
        }
        if (code[i].op == opcode::assert_position) {
          const auto k {static_cast<assert_kind>(code[i].arg8)};
          if (is_word_boundary_kind(k)) {
            lead_wb = wb_hint_of(k);
          }
          else {
            // Non-wb lead (e.g. ^) — exact_literal still ok via replay; no wb_lead hint.
            lead_wb = 0;
          }
          break; // only the first lead assert matters for the wrap hint
        }
      }
      for (std::size_t i = 0; i < code.size() && !blocking_assert; ++i) {
        if (code[i].op == opcode::byte) {
          seen_byte = true;
        }
        else if (seen_byte && code[i].op == opcode::assert_position) {
          const auto k {static_cast<assert_kind>(code[i].arg8)};
          if (is_word_boundary_kind(k) && trail_wb == 0) {
            trail_wb = wb_hint_of(k); // single trailing wb allowed
          }
          else {
            blocking_assert = true;   // inter-assert, second trail, or non-wb trail
          }
        }
        else if (seen_byte && code[i].op == opcode::match) {
          break;
        }
      }
      if (!blocking_assert) {
        std::size_t q {prefix_pc};
        while (q < code.size() &&
               (code[q].op == opcode::save || code[q].op == opcode::assert_position)) {
          ++q;
        }
        if (q < code.size() && code[q].op == opcode::match) {
          hints.exact_literal_len = hints.prefix_size;
          hints.wb_lead           = lead_wb;
          hints.wb_trail          = trail_wb;
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
        case opcode::byte_loop_possessive:
          // Reachable via pure epsilon traversal from pc 0 ONLY when zero repetitions are
          // valid here (any mandatory-minimum copies were unrolled as plain `byte` instructions
          // AHEAD of this opcode, which this walker would have stopped at first) -- so `secondary_target`
          // (the on-no-match exit) is always a live alternative to explore, unconditionally.
          hints.first_bytes.set(instruction.arg8);
          stack.push_back(instruction.secondary_target);
          break;
        case opcode::klass_loop_possessive:
          hints.first_bytes.merge(classes[instruction.arg16]);
          stack.push_back(instruction.secondary_target);
          break;
        case opcode::klass_cp_loop_possessive:
          {
            // Same sound superset as klass_cp above: the ASCII members plus every UTF-8 lead
            // byte a non-ASCII member could begin with. Self-contained (no continuation chain).
            const cp_class& cc {cp_classes[static_cast<std::size_t>(instruction.arg16)]};
            hints.first_bytes.merge(cc.ascii);
            hints.first_bytes.merge(utf8_lead2_set());
            hints.first_bytes.merge(utf8_lead3_set());
            hints.first_bytes.merge(utf8_lead4_set());
            stack.push_back(instruction.secondary_target);
            break;
          }
      }
    }
    hints.first_bytes_valid    = !empty_match_possible && !hints.first_bytes.empty();
    hints.empty_match_possible = empty_match_possible;
  }

  //! \brief IL-fusion cap (compiler.hpp, `pattern_hints::il_fused_eligible`): the largest total width
  //!        (prefix + literal + suffix) that takes the fused arithmetic verify instead of the
  //!        reverse/forward-DFA route. A generous bound for the emails/dates/keys the route targets,
  //!        not a hard architectural limit -- kept narrow deliberately (scope, predictability).
  inline constexpr std::int32_t il_fused_max_width {32};

  /*!
   * \brief Total consuming width (in bytes) of a straight-line byte/klass program: `save 0`, an
   *        interleaved byte/klass/save sequence with no nested capturing groups, `save 1`, `match` --
   *        the same shape `detect_fast_shapes`'s `fixed_shape` check recognizes, factored out so a
   *        SEPARATE complete program (e.g. the inner-literal prefix sub-program, compiled on its own
   *        AST) can be measured the same way without re-deriving the walk.
   *
   * \param[in] code A complete instruction stream (`save 0` ... `save 1`, `match`).
   * \return The number of `byte`/`klass` ops consumed, or -1 if \p code is not this shape.
   */
  constexpr std::int32_t fixed_run_width(std::span<const instr> code)
  {
    std::size_t  i           {};
    std::int32_t width       {};
    std::int32_t open_groups {};
    bool         closed      {};
    bool         nested      {};
    if (i >= code.size() || code[i].op != opcode::save || code[i].arg16 != 0) {
      return -1;
    }
    ++i;
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
        else if (slot >= 2 && (slot % 2) == 0) {
          if (open_groups > 0) {
            nested = true;
          }
          ++open_groups;
        }
        else if (slot >= 3) {
          --open_groups;
        }
        ++i;
      }
      else {
        break;
      }
    }
    if (width >= 1 && closed && !nested && i + 1 == code.size() && code[i].op == opcode::match) {
      return width;
    }
    return -1;
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
   *        an alternation of straight-line branches, and trailing-lookaround class+ (P3c).
   * \param[in]     code           The instruction stream.
   * \param[in]     classes        Interned character classes referenced by \p code.
   * \param[in]     cp_classes     Match-time code-point classes (for `\w`/`\d`/`\s` Arc B word-class tests).
   * \param[in]     cp_ranges      Flat range buffer the \p cp_classes slices index into.
   * \param[in]     cp_mark_ascii  ASCII sub-class index of an emitted codepoint-class block (-1 = none).
   * \param[in]     cp_mark_offset Program offset where that block starts (-1 = none).
   * \param[in]     cp_mark_end    Program offset right after that block ends (-1 = none) -- the
   *                               block's instruction count is not fixed, so this locates its end.
   * \param[in]     lookarounds    Bounded lookaround subs (for trailing-LA eligibility); may be empty.
   * \param[in,out] hints          Hint bag to fill (class-loop, fixed-shape, trailing-LA, …).
   */
  constexpr void detect_fast_shapes(std::span<const instr>          code,
                                    std::span<const char_class>     classes,
                                    std::span<const cp_class>       cp_classes,
                                    std::span<const code_range>     cp_ranges,
                                    std::int32_t                    cp_mark_ascii,
                                    std::int32_t                    cp_mark_offset,
                                    std::int32_t                    cp_mark_end,
                                    std::span<const lookaround_sub> lookarounds,
                                    pattern_hints&                  hints)
  {
    // "class+" shape: save 0, [optional \b/\B,] [group-start save,] klass, split(back, exit),
    // [group-end save,] [optional \b/\B,] save 1, match. Arc B via peel + resolve_class_wb_hints.
    // R3: the outer envelope (open/close) is \ref parse_shape_lead / \ref parse_shape_close.
    {
      const shape_lead lead {parse_shape_lead(code)};
      if (lead.ok) {
        std::size_t  p  {lead.body_start};
        std::int16_t gs {-1};
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
          const shape_close close {ok ? parse_shape_close(code, q) : shape_close {}};
          if (ok && close.ok && cls >= 0 && static_cast<std::size_t>(cls) < classes.size()) {
            const char_class& cc        {classes[static_cast<std::size_t>(cls)]};
            std::uint8_t      out_lead  {0};
            std::uint8_t      out_trail {0};
            // This shape structurally requires the split/loop matched above -- always a maximal
            // `+` run, never a single code point -- so B-1's redundancy argument always applies.
            if (resolve_class_wb_hints(is_full_ascii_word_class(cc), is_ascii_word_subset_class(cc),
                                       /*maximal_run=*/ true, lead.wb_lead, close.wb_trail, out_lead,
                                       out_trail)) {
              hints.greedy_class_loop  = cls;
              hints.greedy_group_start = gs;
              hints.greedy_group_end   = ge;
              hints.wb_lead            = out_lead;
              hints.wb_trail           = out_trail;
              // B-1 dropped a genuine leading \b (wb_lead was 1, out_lead came back 0): the
              // runner's search-mode fast path needs the start>0 window-edge guard -- see
              // pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
              hints.wb_lead_maximal_run = (lead.wb_lead == 1 && out_lead == 0);
            }
          }
        }
      }
    }

    // Trailing-lookaround class+: save 0, klass, split(back, exit), assert_lookaround, jump AFTER,
    // [sub-program … match], AFTER: save 1, match. Groupless only (no enveloping capture — a group's
    // save would sit between the split and the lookaround and disqualify this shape). The body's
    // greedy class+ is the same scan as the plain class-loop; the lookaround is applied as an
    // end-condition on candidate ends of each maximal run (see run_class_loop). Leading lookaround
    // (assert before the klass) does not match this layout and stays on the general VM.
    if (hints.greedy_class_loop < 0 && code.size() >= 7 && code[0].op == opcode::save && code[0].arg16 == 0
        && code[1].op == opcode::klass && code[2].op == opcode::split
        && code[2].primary_target == 1 && code[2].secondary_target == 3
        && code[3].op == opcode::assert_lookaround && code[4].op == opcode::jump) {
      const std::size_t after  {static_cast<std::size_t>(code[4].primary_target)};
      const std::size_t sub_id {code[3].arg16};
      // Jump must land on the closing save 1 / match and skip a non-empty sub region that ends in match.
      if (after >= 6 && after + 1 < code.size() && after + 2 == code.size()
          && code[after].op == opcode::save && code[after].arg16 == 1
          && code[after + 1].op == opcode::match
          && code[after - 1].op == opcode::match // sub-program terminator
          && sub_id < lookarounds.size()
          && lookarounds[sub_id].code_offset == 5
          && lookarounds[sub_id].code_length == static_cast<std::int32_t>(after - 5)
          && lookarounds[sub_id].direction == look_dir::ahead) {
        // Do NOT arm greedy_class_loop — that would force every pure class+ call site to also
        // branch on trailing_lookaround. Cold path reads trailing_la_class only.
        hints.trailing_lookaround = static_cast<std::int16_t>(sub_id);
        hints.trailing_la_class   = code[1].arg16;
        hints.greedy_group_start  = -1;
        hints.greedy_group_end    = -1;
      }
    }

    // Code-point class (klass_cp + three klass continuations), optional greedy `+`, optional `\b`/`\B`
    // wraps (Arc B), optional one capturing group. Unicode `\w+` / `\d+` / `\s+` via peel + resolve.
    // R3: the outer envelope (open/close) is \ref parse_shape_lead / \ref parse_shape_close.
    {
      const shape_lead lead {parse_shape_lead(code)};
      if (lead.ok) {
        std::size_t  p  {lead.body_start};
        std::int16_t gs {-1};
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
          const shape_close close {ok ? parse_shape_close(code, q) : shape_close {}};
          if (ok && close.ok && cp_idx >= 0 && static_cast<std::size_t>(cp_idx) < cp_classes.size()) {
            const bool has_wb {lead.wb_lead != 0 || close.wb_trail != 0};
            // Bare path: no Unicode table walk (keeps constexpr light for static_regex).
            if (!has_wb) {
              hints.greedy_cp_class      = cp_idx;
              hints.greedy_cp_class_plus = plus;
              hints.greedy_group_start   = gs;
              hints.greedy_group_end     = ge;
              hints.wb_lead              = 0;
              hints.wb_trail             = 0;
            }
            else {
              const cp_class& cc        {cp_classes[static_cast<std::size_t>(cp_idx)]};
              std::uint8_t    out_lead  {0};
              std::uint8_t    out_trail {0};
              // Unlike the ASCII class+ shape above, `plus` here is genuinely optional (this
              // recognizer accepts both `\b\w+` and bare `\b\w`) -- B-1's redundancy argument
              // only holds for the former, so it must gate on the ACTUAL shape, not assume it.
              if (resolve_class_wb_hints(is_full_unicode_word_cp_class(cc, cp_ranges),
                                         is_unicode_word_subset_cp_class(cc, cp_ranges), plus,
                                         lead.wb_lead, close.wb_trail, out_lead, out_trail)) {
                hints.greedy_cp_class      = cp_idx;
                hints.greedy_cp_class_plus = plus;
                hints.greedy_group_start   = gs;
                hints.greedy_group_end     = ge;
                hints.wb_lead              = out_lead;
                hints.wb_trail             = out_trail;
                hints.wb_lead_maximal_run  = (lead.wb_lead == 1 && out_lead == 0);
              }
            }
          }
        }
      }
    }

    // "fixed shape": a straight-line run of fixed-width byte/klass consuming ops, possibly interleaved
    // with capturing saves ((\d{4})-(\d{2})-(\d{2}), (a)(b)), with optional leading/trailing `\b`/`\B`
    // (B1). The whole match is fixed width, so one walk verifies it; because every width is fixed, each
    // save sits at a compile-time-constant offset from the match start, so the fast path fills each
    // group slot by that offset (no re-match). Covers class{n} and mixed sequences; pure literals hit
    // the exact-literal path first. A klass_cp (Unicode shorthand, variable width), split/jump
    // (alternation, {n,m}/+/*/?), `.` or a negated class (byte-level branches), non-wb assertions,
    // and lookarounds all break the run.
    // R3: only \ref parse_shape_lead applies here -- the close interleaves its trailing-wb peel
    // with the arbitrary-length body walk below, unlike \ref shape_close's immediate check.
    {
      std::int32_t       width       {};
      std::int32_t       open_groups {}; // capturing groups (slots >= 2) currently open, for the nesting guard
      bool               closed      {}; // saw the closing save (slot 1)
      bool               nested      {}; // a group opened inside another -- kept on the general VM (flat only)
      std::uint8_t       wb_trail    {};
      std::uint8_t       body_pc     {1};
      bool               saw_body    {}; // true once a consuming op has been seen (trail assert only after)
      const shape_lead   lead        {parse_shape_lead(code)};
      std::size_t        i           {lead.ok ? lead.body_start : code.size()};
      const std::uint8_t wb_lead     {lead.wb_lead};
      if (lead.ok) {
        if (wb_lead != 0) {
          body_pc = static_cast<std::uint8_t>(i);
        }
        while (i < code.size()) {
          const opcode op {code[i].op};
          if (op == opcode::byte || op == opcode::klass) {
            ++width;
            ++i;
            saw_body = true;
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
          else if (op == opcode::assert_position && saw_body && wb_trail == 0) {
            // Single trailing \b/\B immediately before save 1.
            if (!peel_optional_trail_wb(code, i, wb_trail)) {
              break; // non-wb trail assert
            }
          }
          else {
            break; // split/jump/klass_cp/lookaround/extra assert disqualify
          }
        }
        if (width >= 1 && closed && !nested && i + 1 == code.size() && code[i].op == opcode::match) {
          hints.fixed_shape = true;
          hints.wb_lead     = wb_lead;
          hints.wb_trail    = wb_trail;
          hints.body_pc     = body_pc;
        }
      }

      // SIMD verify eligibility: the run above qualifies for a vectorized scan+verify (pike.hpp
      // run_fixed_shape) only when it is also HOMOGENEOUS -- every byte/klass position accepts the
      // identical set, itself <= 2 contiguous ranges -- because the sound "skip to the first failing
      // lane" only holds when a mismatch at any position rules out every position (same required set
      // everywhere). Mixed shapes ((\d{4})-(\d{2})-(\d{2})) stay on the scalar walk. Lead/trail `\b`
      // are zero-width and do not affect homogeneity of the consuming run.
      if (hints.fixed_shape) {
        bool          homogeneous {true};
        bool          have_first  {false};
        std::uint8_t  lo0         {};
        std::uint8_t  hi0         {};
        std::uint8_t  lo1         {1};
        std::uint8_t  hi1         {};
        std::uint32_t len         {};
        for (std::size_t pc {static_cast<std::size_t>(hints.body_pc)}; pc < i; ++pc) {
          const opcode op {code[pc].op};
          if (op != opcode::byte && op != opcode::klass) {
            continue; // interleaved capturing save or trail assert -- epsilon for this purpose
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
    // greedy `+`. Layout: save 0, the codepoint-class block (offset 1..cp_mark_end),
    // then either save 1, match (bare) or split(loop, exit), save 1, match (the
    // `+`). No captures; `*` is excluded because its empty match rules out a
    // consuming fast path. The block's own instruction count is NOT fixed (it
    // grows with the number of canonical byte-range branches the compiler emits
    // for the lead bytes it has to narrow) -- cp_mark_end (set alongside
    // cp_mark_offset/cp_mark_ascii by emit_any_codepoint_class) locates its end,
    // so this recognizer needs no hardcoded block size.
    if (cp_mark_offset == 1 && cp_mark_end > cp_mark_offset &&
        static_cast<std::size_t>(cp_mark_end) < code.size() && code[0].op == opcode::save) {
      const auto end {static_cast<std::size_t>(cp_mark_end)};
      // The ASCII sub-class index comes from the marker the compiler set when it
      // emitted the block (emit_any_codepoint_class) — we no longer reverse-engineer
      // the block's bytecode shape here. The whole-program layout / `+`-loop checks
      // are program structure; the ASCII-only test is class content; neither depends
      // on the block's internal opcode layout.
      std::int32_t ascii {(cp_mark_ascii >= 0 && static_cast<std::size_t>(cp_mark_ascii) < classes.size())
                          ? cp_mark_ascii
                          : -1};
      // Content guard: the recorded ASCII sub-class must hold ASCII bytes only.
      // Provably unreachable today for `.` (its accepted ASCII set always keeps at
      // least most of [0x00,0x7F]) — `ast.hpp::parse_class_item` rejects any class
      // member >= 0x80 and `char_class::invert_ascii` leaves the high bytes (>= 0x80)
      // cleared, so the marked sub-class is always pure ASCII when it is the ASCII
      // branch at all. It stops being the ASCII branch only for a class that negates
      // every ASCII byte (e.g. `[^\x00-\x7F]`, "any non-ASCII", ascii bitmap empty) --
      // emit_class_codepoints then skips the ascii branch entirely and this reads a
      // byte-range class's index instead, which this content check correctly rejects
      // (a lead/continuation byte-range set has high bytes set). Kept deliberately:
      // unlike the bytecode-shape recognition this replaced, it is a *content* check
      // that stays robust to layout changes.
      if (ascii >= 0) {
        const char_class& ascii_class {classes[static_cast<std::size_t>(ascii)]};
        for (int byte {0x80}; byte <= 0xFF; ++byte) {
          if (ascii_class.test(static_cast<std::uint8_t>(byte))) {
            ascii = -1; // a high byte would mean a non-ASCII sub-class (see guard above)
            break;
          }
        }
      }
      const bool bare {code.size() == end + 2 && code[end].op == opcode::save &&
                       code[end + 1].op == opcode::match};
      const bool plus {code.size() == end + 3 && code[end].op == opcode::split &&
                       code[end].primary_target == 1 && code[end + 1].op == opcode::save &&
                       code[end + 2].op == opcode::match};
      if (ascii >= 0 && (bare || plus)) {
        hints.codepoint_class_ascii = ascii;
        hints.codepoint_class_plus  = plus;
      }
    }

    // Whole pattern is an alternation of straight-line branches (optional lead/trail `\b`/`\B`).
    {
      std::uint8_t wb_lead  {};
      std::uint8_t wb_trail {};
      std::uint8_t body_pc  {1};
      if (is_fixed_alternation(code, &wb_lead, &wb_trail, &body_pc)) {
        hints.fixed_alternation = true;
        // Only set wb_* here if exact_literal / fixed_shape did not already claim them
        // (a pure literal alternation is rare; prefer not clobbering an earlier path).
        if (!hints.fixed_shape && hints.exact_literal_len == 0) {
          hints.wb_lead  = wb_lead;
          hints.wb_trail = wb_trail;
          hints.body_pc  = body_pc;
        }
      }
    }

    // D1-perf (Étage A): possessive class+/cp-class+ loop -- UNBOUNDED only (X*+/X++, self-loop via
    // `jump` back to the loop opcode's own pc; see pattern_hints's doc comment for why a bounded count
    // is out of scope). Layout: save 0, [optional lead \b/\B], [optional ONE mandatory copy: klass |
    // klass_cp(+3-instr chain), the SAME class/cp-class as the loop, min=1 -- min>=2 stays general],
    // loop_pc: klass_loop_possessive | klass_cp_loop_possessive(+3-instr chain), jump(self), [optional
    // trail \b/\B], [optional literal SUFFIX: 0+ plain `byte` ops, e.g. the 'x' in \d++x], save 1, match.
    //
    // Capture: NOT a preceding `save` -- Tier 1's own design (program.hpp's opcode doc comment)
    // deliberately never emits one (a `save` before the test would fire speculatively and corrupt a
    // prior successful iteration's capture the moment a later attempt failed). The ONLY place a capture
    // slot is visible is `code[loop_pc].primary_target`, read directly off the loop opcode itself once
    // found -- there is nothing to "look for" ahead of it. (`([a-z])*+b`, the group AROUND the
    // quantifier, compiles this way and is exactly what this block targets; `([a-z]++)`, the group
    // wrapping an ALREADY-possessive class with no quantifier of its own, is an ordinary capturing group
    // compiled with plain ahead-of-time save/save instructions around a Tier-1 loop that itself claims
    // no capture -- a structurally different, uncaptured-at-the-opcode-level shape this block also
    // matches, just with gs resolving to -1: correct, not a bug, since the group's OWN save/save pair,
    // sitting outside [p, loop_pc), is simply invisible to (and irrelevant for) this recognizer.
    // R3: only \ref parse_shape_lead applies here -- the close interleaves its trailing-wb peel
    // with an optional literal SUFFIX before save1+match, unlike \ref shape_close's immediate check.
    {
      const shape_lead   lead    {parse_shape_lead(code)};
      const std::size_t  p       {lead.ok ? lead.body_start : code.size()};
      const std::uint8_t wb_lead {lead.wb_lead};
      if (lead.ok) {
        const std::size_t mandatory_start {p};
        std::size_t       loop_pc         {mandatory_start};
        bool              has_mandatory   {false};
        class_ref         mandatory_ref   {};
        if (loop_pc < code.size() && code[loop_pc].op == opcode::byte) {
          loop_pc       = mandatory_start + 1;
          has_mandatory = true;
          mandatory_ref = {.kind = class_kind::byte, .index = code[mandatory_start].arg8};
        }
        else if (loop_pc < code.size() && code[loop_pc].op == opcode::klass) {
          loop_pc       = mandatory_start + 1;
          has_mandatory = true;
          mandatory_ref = {.kind = class_kind::klass, .index = code[mandatory_start].arg16};
        }
        else if (loop_pc + 3 < code.size() && code[loop_pc].op == opcode::klass_cp &&
                 code[loop_pc + 1].op == opcode::klass && code[loop_pc + 2].op == opcode::klass &&
                 code[loop_pc + 3].op == opcode::klass) {
          loop_pc       = mandatory_start + 4;
          has_mandatory = true;
          mandatory_ref = {.kind = class_kind::klass_cp, .index = code[mandatory_start].arg16};
        }
        if (loop_pc < code.size() && (code[loop_pc].op == opcode::byte_loop_possessive ||
                                      code[loop_pc].op == opcode::klass_loop_possessive ||
                                      code[loop_pc].op == opcode::klass_cp_loop_possessive)) {
          class_kind loop_kind {class_kind::klass_cp};
          if (code[loop_pc].op == opcode::byte_loop_possessive) {
            loop_kind = class_kind::byte;
          }
          else if (code[loop_pc].op == opcode::klass_loop_possessive) {
            loop_kind = class_kind::klass;
          }
          const std::int32_t body_idx  {loop_kind == class_kind::byte ? code[loop_pc].arg8
                                                                      : code[loop_pc].arg16};
          const class_ref    loop_ref  {.kind = loop_kind, .index = static_cast<std::uint16_t>(body_idx)};
          const std::int32_t cap_slot  {code[loop_pc].primary_target};
          const std::int16_t gs        {cap_slot >= 0 ? static_cast<std::int16_t>(cap_slot) : std::int16_t {-1}};
          // A captured shape must have no mandatory copy (min == 0): a captured min>=1 has its OWN,
          // structurally different shape (a save/save-wrapped mandatory copy, unrolled per repetition)
          // this block does not attempt to recognize this train -- see the doc comment above.
          // Possessive-capture-fix: write_success now captures the loop's own LAST iteration (a
          // last_width policy per class_kind), not the whole match span -- the bug that originally
          // made this recognizer decline kind=byte captured outright is fixed at the driver level, so
          // byte captures exactly like klass/klass_cp now.
          const bool capture_ok {cap_slot < 0 || !has_mandatory};
          // Never assume: the mandatory copy (if any) must be literally the same atom the loop tests
          // -- class_ref's own operator== compares \ref class_kind first, so a byte/klass/klass_cp
          // mismatch (the exact shape of Bug D/E: `[abc].*+`'s mandatory `klass` colliding with the
          // loop's `klass_cp` on a shared numeric index) cannot silently compare equal.
          const bool         same_atom   {!has_mandatory || mandatory_ref == loop_ref};
          const std::size_t  exit_pc     {static_cast<std::size_t>(code[loop_pc].secondary_target)};
          const std::size_t  block_width {loop_kind == class_kind::klass_cp ? std::size_t {4}
                                                                             : std::size_t {1}};
          const std::size_t after_loop   {loop_pc + block_width};
          if (capture_ok && same_atom && after_loop < code.size() &&
              code[after_loop].op == opcode::jump &&
              code[after_loop].primary_target == static_cast<std::int32_t>(loop_pc) &&
              exit_pc == after_loop + 1) {
            std::size_t  q        {exit_pc};
            std::uint8_t wb_trail {0};
            if (!peel_optional_trail_wb(code, q, wb_trail)) {
              q = code.size(); // disqualify below
            }
            std::array<char, 8>  suffix      {};
            std::uint8_t         suffix_len  {0};
            while (q < code.size() && code[q].op == opcode::byte && suffix_len < suffix.size()) {
              suffix[suffix_len] = static_cast<char>(code[q].arg8);
              ++suffix_len;
              ++q;
            }
            // Non-empty-consumption guard: a min=0 (star) loop with no required suffix can match the
            // EMPTY string (0 repetitions, nothing after) -- exactly the case run()'s dispatch comment
            // warns fast paths must never reach ("Fast paths only fire for patterns that always
            // consume"), since this driver has no forbid_empty_until/iterator-advance contract. Mirrors
            // greedy's own class+ recognizer, which for the identical reason never arms on bare `[a-z]*`
            // (confirmed empirically: `[a-z]*` alone stays on general_full, only `[a-z]+` arms).
            const bool table_bound_ok {loop_kind == class_kind::byte ||
                                       (loop_kind == class_kind::klass_cp
                                          ? static_cast<std::size_t>(body_idx) < cp_classes.size()
                                          : static_cast<std::size_t>(body_idx) < classes.size())};
            if (q + 1 < code.size() && q + 2 == code.size() && code[q].op == opcode::save &&
                code[q].arg16 == 1 && code[q + 1].op == opcode::match && body_idx >= 0 &&
                (has_mandatory || suffix_len >= 1) && table_bound_ok) {
              const bool   has_wb    {wb_lead != 0 || wb_trail != 0};
              // R2: a literal byte has no "word class" to resolve B-1 eligibility against yet --
              // the wb-wrapped byte-possessive shape (`\ba++\b`) stays on the general VM, documented
              // rather than silently dropped; the bare/suffixed shape (`a++`, `a++x`) still arms
              // (arm starts true whenever there is no wb at all, regardless of kind).
              bool         arm       {!has_wb};
              std::uint8_t out_lead  {0};
              std::uint8_t out_trail {0};
              if (has_wb && loop_kind != class_kind::byte) {
                // Unbounded possessive: always a maximal run wherever it starts (no upper bound to cut
                // it short at different lengths for different starts), so B-1's redundancy argument --
                // "a maximal run can only legitimately start where the byte before it is non-word" --
                // holds unconditionally here, unlike a BOUNDED possessive count (see pattern_hints's own
                // doc comment on why those stay out of this fast path's scope entirely).
                if (loop_kind == class_kind::klass_cp) {
                  const cp_class& cc {cp_classes[static_cast<std::size_t>(body_idx)]};
                  arm = resolve_class_wb_hints(is_full_unicode_word_cp_class(cc, cp_ranges),
                                               is_unicode_word_subset_cp_class(cc, cp_ranges),
                                               /*maximal_run=*/ true, wb_lead, wb_trail, out_lead,
                                               out_trail);
                }
                else {
                  const char_class& cc {classes[static_cast<std::size_t>(body_idx)]};
                  arm = resolve_class_wb_hints(is_full_ascii_word_class(cc), is_ascii_word_subset_class(cc),
                                               /*maximal_run=*/ true, wb_lead, wb_trail, out_lead,
                                               out_trail);
                }
              }
              if (arm) {
                hints.possessive_class        = loop_ref;
                hints.possessive_group_start  = gs;
                hints.possessive_group_end    = gs < 0 ? std::int16_t {-1}
                                                        : static_cast<std::int16_t>(gs + 1);
                hints.possessive_suffix       = suffix;
                hints.possessive_suffix_size  = suffix_len;
                hints.possessive_min_nonzero  = has_mandatory;
                if (has_wb) {
                  hints.wb_lead             = out_lead;
                  hints.wb_trail            = out_trail;
                  hints.wb_lead_maximal_run = (wb_lead == 1 && out_lead == 0);
                }
              }
            }
          }
        }
      }
    }

    // D1-perf (Étage A): possessive delimited ("quoted") shape -- literal PREFIX (1+ bytes) + possessive
    // class+/cp-class+ loop (UNBOUNDED, min=0, uncaptured) + literal SUFFIX (1+ bytes). Eligibility
    // additionally requires the loop's class to EXCLUDE the prefix's AND the suffix's leading byte: without
    // it, a prefix occurrence could hide inside an already-scanned body run (an alphanumeric "id=" prefix
    // inside an `[a-z0-9]*+` body, say), and the delimited runner's skip-to-body-end retry (pike.hpp) would
    // either silently skip a valid leftmost match or, absent the skip, degrade to quadratic on adversarial
    // input -- see pattern_hints's own doc comment. Mutually exclusive with the shape above by construction
    // (that one never starts with a literal `byte`; this one always does) and only tried when it did not
    // already claim the pattern.
    if (!hints.possessive_class.armed() && code.size() >= 6 &&
        code[0].op == opcode::save && code[0].arg16 == 0 && code[1].op == opcode::byte) {
      std::size_t           p          {1};
      std::array<char, 8>   prefix     {};
      std::uint8_t          prefix_len {0};
      while (p < code.size() && code[p].op == opcode::byte && prefix_len < prefix.size()) {
        prefix[prefix_len] = static_cast<char>(code[p].arg8);
        ++prefix_len;
        ++p;
      }
      const std::size_t loop_pc {p};
      if (loop_pc < code.size() &&
          (code[loop_pc].op == opcode::klass_loop_possessive ||
           code[loop_pc].op == opcode::klass_cp_loop_possessive) &&
          code[loop_pc].primary_target < 0) {
        const bool         is_cp       {code[loop_pc].op == opcode::klass_cp_loop_possessive};
        const std::int32_t body_idx    {code[loop_pc].arg16};
        const std::size_t  exit_pc     {static_cast<std::size_t>(code[loop_pc].secondary_target)};
        const std::size_t  block_width {is_cp ? std::size_t {4} : std::size_t {1}};
        const std::size_t  after_loop  {loop_pc + block_width};
        if (after_loop < code.size() && code[after_loop].op == opcode::jump &&
            code[after_loop].primary_target == static_cast<std::int32_t>(loop_pc) &&
            exit_pc == after_loop + 1) {
          std::size_t           q          {exit_pc};
          std::array<char, 8>   suffix     {};
          std::uint8_t          suffix_len {0};
          while (q < code.size() && code[q].op == opcode::byte && suffix_len < suffix.size()) {
            suffix[suffix_len] = static_cast<char>(code[q].arg8);
            ++suffix_len;
            ++q;
          }
          if (prefix_len >= 1 && suffix_len >= 1 && q + 1 < code.size() && q + 2 == code.size() &&
              code[q].op == opcode::save && code[q].arg16 == 1 && code[q + 1].op == opcode::match &&
              body_idx >= 0 &&
              (is_cp ? static_cast<std::size_t>(body_idx) < cp_classes.size()
                     : static_cast<std::size_t>(body_idx) < classes.size())) {
            const auto excludes = [&](std::uint8_t b) {
                                    return is_cp
                                             ? !cp_class_may_contain_ascii_byte(
                                      cp_classes[static_cast<std::size_t>(body_idx)], b)
                                             : !classes[static_cast<std::size_t>(body_idx)].test(b);
                                  };
            if (excludes(static_cast<std::uint8_t>(prefix[0])) &&
                excludes(static_cast<std::uint8_t>(suffix[0]))) {
              hints.possessive_prefix         = prefix;
              hints.possessive_prefix_size    = prefix_len;
              hints.possessive_suffix         = suffix;
              hints.possessive_suffix_size    = suffix_len;
              hints.possessive_min_nonzero    = false; // the loop itself is min=0 in this shape; the PREFIX is the mandatory part
              const class_kind delimited_kind {is_cp ? class_kind::klass_cp : class_kind::klass};
              hints.possessive_class          = {.kind = delimited_kind, .index = static_cast<std::uint16_t>(body_idx)};
            }
          }
        }
      }
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
   * \param[in] cp_ranges      Flat range buffer the \p cp_classes slices index into.
   * \param[in] cp_mark_ascii  ASCII sub-class index of an emitted codepoint-class
   *                           block (-1 = none), as recorded by `emit_any_codepoint_class`.
   * \param[in] cp_mark_offset Program offset where that block starts (-1 = none); the
   *                           whole-pattern codepoint fast path requires it to be 1.
   * \param[in] cp_mark_end    Program offset right after that block ends (-1 = none) --
   *                           the block's own instruction count is not fixed, so this
   *                           locates its end instead of a hardcoded size.
   * \param[in] lookarounds    Bounded lookaround subs (trailing-LA class+ detection).
   * \return The \ref pattern_hints (anchoring, literal prefix, first-byte set,
   *         and the `class+` / exact-literal fast-path flags).
   */
  constexpr pattern_hints analyze_program(std::span<const instr>          code,
                                          std::span<const char_class>     classes,
                                          std::span<const cp_class>       cp_classes,
                                          std::span<const code_range>     cp_ranges,
                                          std::int32_t                    cp_mark_ascii,
                                          std::int32_t                    cp_mark_offset,
                                          std::int32_t                    cp_mark_end,
                                          std::span<const lookaround_sub> lookarounds = {})
  {
    pattern_hints hints;

    // A lookaround forces the general Pike VM: no DFA, no pure class-loop — EXCEPT the measured
    // trailing-LA class+ shape (P3c), which arms trailing_lookaround + trailing_la_class (not
    // greedy_class_loop) so the pure [a-z]+ gate stays a single compare.
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

    detect_fast_shapes(code, classes, cp_classes, cp_ranges, cp_mark_ascii, cp_mark_offset, cp_mark_end,
                       lookarounds, hints);

    // Clear every pure fast-path hint when a lookaround is present. Trailing-LA class+ already
    // left greedy_class_loop at −1 and only set trailing_* (so this wipe is a no-op for those).
    // The literal prefix / first-byte set below stay valid (and sound) filters either way.
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
#if defined(REAL_TEST_INSTRUMENT)
      // Bill remaining haystack once per call — O(n) path bills ~once; per-pos restart → O(n²) total.
      prefilter_note_scan(text.size() - pos);
#endif
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
    if (!std::is_constant_evaluated()) {
#if defined(REAL_TEST_INSTRUMENT)
      // Bill remaining haystack once per call. Correct O(n) literal miss → ~1× size;
      // per-position restart of find_prefix → sum(N..1) ≈ N²/2 (smoke margin 25×).
      prefilter_note_scan(text.size() - pos);
#endif
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
   * FIX P0 #2 (O(n^2)): for **2+ members**, the window is grown **exponentially** (galloping
   * search) from a modest initial probe, doubling each round, rather than handed the full
   * remaining haystack up front. A caller that invokes this once per rejected candidate
   * (`next_candidate`'s icase small-set route) used to pay one `memchr` per set member over
   * `text.size() - pos` on EVERY such call; a set with an asymmetrically rare or entirely absent
   * member (e.g. `(?i)cafe`'s `{c, C}` on an all-lowercase haystack) turned that into a full
   * remaining-text scan on every rejected candidate — O(n) candidates x O(n) scan = O(n^2), same
   * family as A2's unbounded-reach fix but one level upstream (the candidate SEARCH, not the
   * anchored walk once a candidate is found). The geometric series bounds one call's total
   * scanned bytes to at most ~2x the distance to the actual hit (or the remaining text, on a
   * true miss) — the standard galloping-search argument — independent of any one member's
   * frequency.
   *
   * FIX (mono/multi split): a **single**-member call (`run_cascade_stop`'s class-loop stop-byte
   * scan is the hot one — its `stop_set_size` is frequently 1) cannot exhibit the O(n^2) above by
   * construction: one `memchr` call's own cost already equals its progress (the distance to the
   * hit, or the whole range on a miss) — there is no second member whose scan could redundantly
   * re-cover ground the first one already paid for. Windowing a mono-member call therefore adds
   * pure overhead (per-round setup, the narrowing compare, the doubling arithmetic) for zero
   * safety benefit, measured as a real x86 regression on stop-set-shaped patterns (`[^\x01]+`-
   * style) once the general galloping fix landed. So `n == 1` takes the direct pre-fix path — one
   * unbounded `memchr` over `[pos, text.size())` — and every `n >= 2` call keeps the galloping
   * loop exactly as the P0 #2 fix landed it, untouched.
   *
   * \param[in] text The subject text.
   * \param[in] pos  Index to start scanning from.
   * \param[in] set  Pointer to the enumerated set members (first \p n valid).
   * \param[in] n    Number of valid members (1..6 — `run_cascade_stop`'s stop_set allows up to 6).
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
      const char* const base  {text.data()};
      const std::size_t total {text.size() - pos};
      if (n == 1) {
        // Mono-member: a single memchr IS the whole search, unwindowed -- see the mono/multi
        // split note above. Billing mirrors the multi-member loop below: distance-to-hit on a
        // hit, the full remaining range on a miss (npos).
        const void* hit {std::memchr(base + pos, set[0], total)};
        if (hit != nullptr) {
          const std::size_t idx {static_cast<std::size_t>(static_cast<const char*>(hit) - base)};
#if defined(REAL_TEST_INSTRUMENT)
          prefilter_note_scan(idx - pos);
#endif
          return idx;
        }
#if defined(REAL_TEST_INSTRUMENT)
        prefilter_note_scan(total);
#endif
        return npos;
      }
      // Initial probe width: the caller (next_candidate's small-set route) already tried a 32-byte bitmap
      // probe before falling here, so this starts just past it. Members are enumerated in ascending byte
      // value (see the hint builder above), so for an icase pair like {c, C} the UPPERCASE byte (0x43) is
      // always member 0 -- checked first, with the full round window, before the far commoner lowercase
      // byte even gets a chance to narrow it. That makes this seed a genuine trade-off, not a free
      // parameter: measured on M1 across four adversarial shapes (a stop-byte set at a ~93-byte period,
      // and (?i)<literal> sparse/no-match/dense), the stop-set win saturates by ~96 B (no further gain to
      // 512+) while the icase-sparse/no-match cost keeps climbing past it (roughly +2% at 128 B, +40% by
      // 1024 B) -- so 512-1024 (an earlier candidate) would have traded a bounded stop-set win for an
      // unbounded-looking icase cost. 128 captures the stop-set win in full at a ~2% icase cost.
      constexpr std::size_t seed   {128};
      std::size_t           window {total < seed ? total : seed};
      while (true) {
        std::size_t best {npos};
        std::size_t win  {window};
        for (std::uint8_t i = 0; i < n; ++i) {
          const void* hit {std::memchr(base + pos, set[i], win)};
          if (hit != nullptr) {
            const std::size_t idx {static_cast<std::size_t>(static_cast<const char*>(hit) - base)};
            if (idx < best) {
              best = idx;
              win  = best - pos; // subsequent members this round only search the shorter prefix
            }
          }
        }
#if defined(REAL_TEST_INSTRUMENT)
        prefilter_note_scan(win); // bill this round's actual scanned width, like find_prefix does
#endif
        if (best != npos) {
          return best;
        }
        if (window >= total) {
          return npos; // the full remaining haystack was covered across the rounds above; no member anywhere
        }
        window = (window > total - window) ? total : window * 2; // double, capped to the remaining text
      }
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
