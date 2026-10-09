/*!
 * \file prefilter.hpp
 * \brief Search acceleration: pattern analysis and candidate-finding.
 *
 * Extracts \ref real::detail::pattern_hints from a compiled program (literal prefix, start anchoring,
 * first-byte set, fast-path shapes) and provides the skip-ahead primitives used when no thread is alive:
 * `memchr` / the platform substring search at run time, plain loops in constexpr. Hints change only
 * speed, never \e what matches; an equivalence test runs the engine with hints disabled.
 */
#ifndef REAL_PREFILTER_HPP
#define REAL_PREFILTER_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"
#include "real/core/config.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "real/core/charclass.hpp"
#include "real/core/program.hpp"
#include "real/engine/simd.hpp"           // load_pair_mask — the two-byte literal prefilter
#include "real/unicode/unicode_props.hpp" // word_ranges — exact \w identity for the DROP rule

#include <array>
#include <atomic>

namespace real::detail {

  /*!
   * \brief Test counters, one per mechanism, for the tests that pin when it runs. Billed through `note()`
   *        only under \c REAL_TEST_INSTRUMENT (free in production); process-wide relaxed atomics.
   */
  enum class counter : std::uint8_t
  {
    prefilter_work_units,        //!< Bytes the prefilters scanned: the O(n) against O(n²) gates.
    vm_window_runs,              //!< Pike VM runs over a window the DFAs found.
    batch_fills,                 //!< Calls a batched walk made to its filler.
    inner_literal_bill_trips,    //!< Times the inner-literal route gave way on its bill.
    inner_literal_reverse_bytes, //!< Bytes the inner-literal route's reverse automaton read.
    inner_literal_confirm_bytes, //!< Bytes the inner-literal route's rejected forward confirms read.
    byte_program_builds,         //!< Byte programs built (each expands every Unicode class's UTF-8 trie).
    il_prefix_run_walks,         //!< Two-run group fills that walked the prefix run on past the literal.
    batch_handouts,              //!< Buffered spans a batched walk handed out one at a time, slots written.
    vm_reseeds,                  //!< Positions past its start where a search-mode VM run seeded a thread.
    onepass_anchored_walks,      //!< Anchored matches whose end and groups the one-pass table found.
    bounded_backtrack_runs,      //!< Windows the bounded backtracker filled rather than the VM.
    dfa_quits,                   //!< Searches or confirms the lazy DFAs handed to the VM because a scan quit.
    literal_pair_scans,          //!< Literal searches the two-byte block filter answered.
    alternation_avx2_blocks,     //!< Alternation blocks the fingerprint masked 32 starts at a time (AVX2).
    literal_avx2_scans,          //!< Literal filter searches that ran on 32-byte AVX2 blocks.
    fixed_shape_batches,         //!< Batches the fixed-shape filler produced.
    literal_rest_scans,          //!< Literal searches whose first stop failed and that went on out of line.
    alternation_pair_blocks,     //!< Alternation blocks the pair filter masked.
    alternation_nibble_blocks,   //!< Alternation blocks the nibble fingerprint masked, among the pair blocks.
    ac_completion_walks,         //!< Branch walks the Aho-Corasick gate's completion sample spent.
    alternation_variant_scans,   //!< Searches that took a program's variant fingerprint.
    alternation_wide_scans,      //!< Subjects an alternation wider than the small set scanned by the fingerprint.
    alternation_pair_candidates, //!< Candidates the alternation pair filter left to verify.
    ahead_table_rows,            //!< Rows the unbounded-lookahead tables were filled with.
    behind_walk_steps,           //!< Bytes the lookbehind walks stepped.
    dfa_span_batches,            //!< Batches the lazy-DFA span filler produced.
    dfa_leases_taken,            //!< DFA set leases taken.
    class_folds,                 //!< Case folds the compiler computed for a class (a fold-cache hit costs none).
    count_                       //!< The number of counters.
  };

  /*!
   * \brief The value of a test counter, to read or to reset.
   * \param[in] c The counter.
   * \return A reference to its process-wide relaxed atomic.
   */
  inline std::atomic<std::uint64_t>& tally(counter c) noexcept
  {
    static std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(counter::count_)> counts {};
    return counts[static_cast<std::size_t>(c)];
  }

  /*!
   * \brief Bills \p n to a test counter. A no-op unless the test binary defines \c REAL_TEST_INSTRUMENT.
   * \param[in] c The counter.
   * \param[in] n What to add.
   */
  constexpr void note([[maybe_unused]] counter       c,
                      [[maybe_unused]] std::uint64_t n = 1) noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    if (!std::is_constant_evaluated()) { // a compile-time regex runs these paths too, and bills nothing
      tally(c).fetch_add(n, std::memory_order_relaxed);
    }
#endif
  }

  /*!
   * \brief Test seam: keep the alternation fingerprint on 16-byte blocks where the CPU has AVX2, so a differential
   *        can compare both widths in one binary. Not for production use.
   * \return Reference to the process-wide seam flag.
   */
  inline bool& alternation_avx2_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief Test seam: keep the literal filter on 16-byte blocks where the CPU has AVX2, so a differential can
   *        compare both widths in one binary. Not for production use.
   * \return Reference to the process-wide seam flag.
   */
  inline bool& literal_avx2_disabled()
  {
    static bool disabled {false};
    return disabled;
  }

  /*!
   * \brief True if \p kind is `\b` or `\B` (the only position asserts a fast path wraps).
   * \param[in] kind Assertion kind from `assert_position`.
   * \return Whether it is a word-boundary assertion.
   */
  [[nodiscard]] constexpr bool is_word_boundary_kind(assert_kind kind) noexcept
  {
    return kind == assert_kind::word_boundary || kind == assert_kind::not_word_boundary;
  }

  /*!
   * \brief Encodes \p kind as a wb_lead/wb_trail hint value (1 = `\b`, 2 = `\B`); 0 if not a word boundary.
   * \param[in] kind Assertion kind from `assert_position`.
   * \return 1 for `\b`, 2 for `\B`, 0 for anything else.
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
   * \brief Peels an optional `\b`/`\B` assertion at \p p, lead or trail alike.
   * \param[in]     code Instruction stream.
   * \param[in,out] p    Program counter, advanced past a peeled assert.
   * \param[out]    hint 0 when nothing was peeled, else 1/2 (see \ref wb_hint_of).
   * \return false only when another assertion sits at \p p (the shape is disqualified).
   */
  [[nodiscard]] constexpr bool peel_optional_wb(std::span<const instr> code,
                                                std::size_t&           p,
                                                std::uint8_t&          hint) noexcept
  {
    hint = 0;
    if (p >= code.size() || code[p].op != opcode::assert_position) {
      return true;
    }
    const auto k {static_cast<assert_kind>(code[p].arg8)};
    if (!is_word_boundary_kind(k)) {
      return false;
    }
    hint = wb_hint_of(k);
    ++p;
    return true;
  }

  /*!
   * \brief A fixed shape's lead: `save 0`, an optional `\A`/`^`, an optional `\b`/`\B`.
   */
  struct shape_lead
  {
    std::size_t  body_start     {}; //!< pc where the shape-specific body begins.
    std::uint8_t wb_lead        {}; //!< 0/1/2, see \ref wb_hint_of.
    //! \brief A leading `\A`/`^` (not multiline `^`) was peeled: the shape may only match at 0.
    //!
    //! A recognizer that does not honour it MUST refuse the shape: a forward scan would return matches
    //! the program forbids.
    bool         anchored_start {};
    bool         ok             {}; //!< false: no leading `save 0`, or a non-wb lead assert disqualifies.
  };

  /*!
   * \brief Peels a fixed shape's `save 0` and its optional lead `\b`/`\B`.
   * \param[in] code The program's instruction stream.
   * \return Where the body begins and which lead wrap was found; \c ok false when the shape disqualifies.
   */
  [[nodiscard]] constexpr shape_lead parse_shape_lead(std::span<const instr> code) noexcept
  {
    if (code.empty() || code[0].op != opcode::save || code[0].arg16 != 0) {
      return {};
    }
    std::size_t p {1};
    // `\A`/`^` is peeled and reported, not rejected: `^X` searched is `X` in prefix mode, which reaches
    // the class loop (81x faster on 100 KB). Multiline `^` stays disqualifying.
    bool anchored {false};
    if (p < code.size() && code[p].op == opcode::assert_position
        && static_cast<assert_kind>(code[p].arg8) == assert_kind::text_start) {
      anchored = true;
      ++p;
    }
    std::uint8_t wb_lead {0};
    if (!peel_optional_wb(code, p, wb_lead)) {
      return {};
    }
    return {.body_start = p, .wb_lead = wb_lead, .anchored_start = anchored, .ok = true};
  }

  /*!
   * \brief The \ref shape_lead counterpart: optional trail `\b`/`\B`, optional `\Z`/`$`, then exactly
   *        `save 1`, `match` ending the program.
   */
  struct shape_close
  {
    std::uint8_t wb_trail   {}; //!< 0/1/2, see \ref wb_hint_of.
    //! \brief A trailing end anchor was peeled: 0 none, 1 `\Z`, 2 `$` (end, or before ONE final
    //!        newline, which is why `^X$` is not `fullmatch(X)`).
    //!
    //! A recognizer that does not honour it MUST refuse the shape.
    std::uint8_t end_anchor {};
    bool         ok         {}; //!< false: not exactly `save 1`, `match` at the end after peeling.
  };

  /*!
   * \brief Peels a fixed shape's optional trail `\b`/`\B`, then its `save 1` and `match`.
   * \param[in] code The program's instruction stream.
   * \param[in] from Program counter just past the body.
   * \return Which trail wrap was found; \c ok false when the tail is not exactly `save 1`, `match`.
   */
  [[nodiscard]] constexpr shape_close parse_shape_close(std::span<const instr> code,
                                                        std::size_t            from) noexcept
  {
    // Not peel_optional_wb: it refuses non-wb asserts, so the end anchor could never be peeled. The
    // compiler emits `\b` before `$` (`X+\b$`); any other order fails the `save 1`, `match` test.
    std::uint8_t wb_trail {0};
    if (from < code.size() && code[from].op == opcode::assert_position
        && is_word_boundary_kind(static_cast<assert_kind>(code[from].arg8))) {
      wb_trail = wb_hint_of(static_cast<assert_kind>(code[from].arg8));
      ++from;
    }
    // Multiline `$` is not peeled and disqualifies.
    std::uint8_t end_anchor {0};
    if (from < code.size() && code[from].op == opcode::assert_position) {
      const auto kind {static_cast<assert_kind>(code[from].arg8)};
      if (kind == assert_kind::text_end) {
        end_anchor = 1;
        ++from;
      }
      else if (kind == assert_kind::text_end_or_final_newline) {
        end_anchor = 2;
        ++from;
      }
    }
    if (from + 1 < code.size() && code[from].op == opcode::save && code[from].arg16 == 1 &&
        code[from + 1].op == opcode::match && from + 2 == code.size()) {
      return {.wb_trail = wb_trail, .end_anchor = end_anchor, .ok = true};
    }
    return {};
  }

  /*!
   * \brief True if \p cls is exactly the ASCII word set `[0-9A-Za-z_]` (`\w` under bytes/`re.A`).
   * \param[in] cls The byte class under test.
   * \return Whether it is exactly `\w`, neither a subset nor a superset.
   */
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

  /*!
   * \brief True if every member of \p cls is an ASCII word byte (subset of `\w` under bytes/`re.A`).
   * \param[in] cls The byte class under test.
   * \return Whether it is non-empty and every member is an ASCII word byte.
   */
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
   * Range identity, not a \c range_count threshold: `[\w😀]` would pass one, yet `\b[\w😀]+\b` is not `[\w😀]+`.
   *
   * \param[in] cc         The code-point class under test.
   * \param[in] all_ranges Program flat range buffer (\p cc indexes a slice of it).
   * \return Whether \p cc is exactly Unicode `\w`.
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
    for (const code_range& wr : word_ranges) {
      if (wr.lo >= 0x80U) {
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
    for (const code_range& wr : word_ranges) {
      if (wr.lo < 0x80U) {
        continue;
      }
      const code_range& r {all_ranges[static_cast<std::size_t>(cc.range_begin) + j]};
      if (r.lo != wr.lo || r.hi != wr.hi) {
        return false;
      }
      ++j;
    }
    return true;
  }

  /*!
   * \brief The DROP rule: `\b` next to a full-`\w` maximal run is redundant (`\B` never is).
   *
   * Sound only for a maximal (`+`) run, which starts and ends at `\b` anyway; a single atom may start
   * mid-run. Call only for a maximal-run shape.
   *
   * \param[in] lead  The lead wrap hint (0/1/2, see \ref wb_hint_of).
   * \param[in] trail The trail wrap hint, same encoding.
   * \return True when at least one side is `\b` and neither is `\B`, so both may be dropped.
   */
  [[nodiscard]] constexpr bool wb_redundant_for_full_word(std::uint8_t lead,
                                                          std::uint8_t trail) noexcept
  {
    if (lead == 2 || trail == 2) {
      return false;
    }
    return lead == 1 || trail == 1;
  }

  /*!
   * \brief DROP / WRAP policy for class / cp-class loops under optional `\b`/`\B` wraps.
   *
   * - Full word + `\b` on a maximal (`+`) run: drop the boundaries (\ref wb_redundant_for_full_word).
   * - Proper word subset + `\b`, or full word + `\b` on a single atom: keep the wrap (the WRAP rule).
   * - `\B` on a maximal run: unarm. The runner skips whole class runs on a failed lead check, but `\B`
   *   starts mid-run (`\B\w+` on "hello" is "ello").
   * - `\B` on a single atom (`\B\w`): keep the wrap; a failed check advances one atom.
   * - Non-word-subset class under any wb: unarm (a superset maximal run is unsound).
   * - Bare (no wb): arm with zero wb hints.
   *
   * \param[in]  full_word   Exact `\w` class (ASCII or Unicode table identity).
   * \param[in]  word_sub    Non-empty subset of `\w`.
   * \param[in]  maximal_run Whether the class loop is a greedy `+` (starts sit at word transitions)
   *                         rather than a single code point (which may start mid-run).
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
    if ((lead == 2 || trail == 2) && maximal_run) {
      return false;
    }
    const bool has_wb {lead != 0 || trail != 0};
    if (has_wb && !full_word && !word_sub) {
      return false;
    }
    if (has_wb && (word_sub || full_word) &&
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

  /*!
   * \brief True if every code point in [\p lo, \p hi] is a Unicode word char (\ref word_ranges),
   *        resuming the scan at \p cursor and leaving it past the last range consulted.
   *
   * \p cursor is a hint, not a precondition: an out-of-order interval rewinds it. Restarting at 0 per
   * interval cost `\w` 771^2 steps, 95 % of compiling `\b\w+\b`.
   *
   * \param[in]     lo     First code point of the interval.
   * \param[in]     hi     Last code point of the interval (inclusive).
   * \param[in,out] cursor Index to resume from, advanced in place.
   * \return `true` if every code point in the interval is a Unicode word character.
   */
  [[nodiscard]] constexpr bool word_ranges_cover_interval_from(char32_t     lo,
                                                               char32_t     hi,
                                                               std::size_t& cursor) noexcept
  {
    if (lo > hi) {
      return false;
    }
    if (cursor > word_ranges_size || (cursor > 0 && word_ranges[cursor - 1].hi >= lo)) {
      cursor = 0; // the hint has overshot this interval — the sorted walk restarts
    }
    char32_t cur {lo};
    while (cur <= hi) {
      while (cursor < word_ranges_size && word_ranges[cursor].hi < cur) {
        ++cursor;
      }
      if (cursor >= word_ranges_size || word_ranges[cursor].lo > cur) {
        return false; // ran out, or a gap: cur is not a word char
      }
      if (word_ranges[cursor].hi >= hi) {
        return true;  // this range finishes the interval
      }
      cur = static_cast<char32_t>(word_ranges[cursor].hi) + 1; // the next range must be adjacent
      ++cursor;
    }
    return true;
  }

  /*!
   * \brief True if every code point in [\p lo, \p hi] is a Unicode word char (covered by \ref
   *        word_ranges). Standalone form of \ref word_ranges_cover_interval_from.
   * \param[in] lo First code point of the interval.
   * \param[in] hi Last code point of the interval, inclusive.
   * \return Whether every code point in it is a Unicode word character.
   */
  [[nodiscard]] constexpr bool word_ranges_cover_interval(char32_t lo,
                                                          char32_t hi) noexcept
  {
    std::size_t cursor {0};
    return word_ranges_cover_interval_from(lo, hi, cursor);
  }

  /*!
   * \brief True if \p cc is a non-empty subset of Unicode `\w` (safe for maximal-run + `\b` wrap).
   *
   * A superset like `[\w😀]` must not take the WRAP rule: a maximal run starting on a non-word member
   * skips a later word-bounded sub-run.
   *
   * \param[in] cc         The code-point class under test.
   * \param[in] all_ranges Program flat range buffer (\p cc indexes a slice of it).
   * \return Whether \p cc is non-empty and wholly inside Unicode `\w`.
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
    // One cursor for the class: both range lists are sorted, so this is one merge.
    std::size_t cursor {0};
    for (std::uint32_t i = 0; i < cc.range_count; ++i) {
      const code_range& r {all_ranges[static_cast<std::size_t>(cc.range_begin) + i]};
      if (!word_ranges_cover_interval_from(static_cast<char32_t>(r.lo), static_cast<char32_t>(r.hi), cursor)) {
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

  /*!
   * \brief Whether byte \p b could be a member of \p cc; a delimiter that could hide in a possessive
   *        code-point loop makes the delimited fast path decline (\ref pattern_hints::possessive_prefix).
   * \param[in] cc The loop body's code-point class.
   * \param[in] b  The candidate delimiter byte.
   * \return True when \p b could be a member, and for any \p b >= 0x80.
   */
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
   * \param[in]  code            The instruction stream.
   * \param[out] out_wb_lead     Optional; receives lead wb hint (0/1/2).
   * \param[out] out_wb_trail    Optional; receives trail wb hint (0/1/2).
   * \param[out] out_body_pc     Optional; receives first branch/split pc after lead wrap.
   * \param[out] out_branch_count Optional; receives the branch count, so a caller picks a strategy
   *             (e.g. Aho-Corasick) without re-walking the split chain.
   * \return `true` if the program has that shape with at least two branches.
   */
  constexpr bool is_fixed_alternation(std::span<const instr> code,
                                      std::uint8_t*          out_wb_lead      = nullptr,
                                      std::uint8_t*          out_wb_trail     = nullptr,
                                      std::uint8_t*          out_body_pc      = nullptr,
                                      std::int32_t*          out_branch_count = nullptr)
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
      if (!peel_optional_wb(code, t, wb_trail)) {
        return false;
      }
      exit_pc = exit_pc - 1;
    }
    if (!peel_optional_wb(code, body, wb_lead)) {
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
          if (out_branch_count != nullptr) {
            *out_branch_count = branches;
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
   *
   * \param[in]     code  The program's instruction stream.
   * \param[in,out] hints Hints to record the anchoring in.
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
      if (kind == assert_kind::line_start) {
        hints.line_anchored = 1U;
      }
      else if (kind == assert_kind::line_start_cr) {
        hints.line_anchored = 2U; // an ECMAScript line also starts after `\r`
      }
      else {
        hints.line_anchored = 0U;
      }
    }
  }

  /*!
   * \brief Collects the required literal prefix and the exact-literal fast-path length.
   *
   * The prefix is the leading byte instructions; saves and assertions consume nothing and are crossed
   * (hints only filter candidates, the engine verifies). The exact-literal hint fires when those bytes
   * are the whole match: past the first byte only saves and one trailing `\b`/`\B` may precede `match`
   * (recorded as wb hints); any other trailing or inner assertion stays on the VM.
   *
   * \param[in]     code  The program's instruction stream.
   * \param[in,out] hints Hints to record the prefix and the exact-literal length in.
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
      bool         blocking_assert {};
      bool         seen_byte       {};
      std::uint8_t trail_wb        {};
      std::uint8_t lead_wb         {};
      // Lead \b/\B: first assert_position before any byte (after saves).
      for (const instr& in : code) {
        if (in.op == opcode::byte) {
          break;
        }
        if (in.op == opcode::assert_position) {
          const auto k {static_cast<assert_kind>(in.arg8)};
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
   * Assertions and lookarounds are crossed (they constrain positions, not bytes), so the set is a sound
   * superset. If `match` is reachable without consuming, an empty match is possible and no byte-based
   * skipping is sound.
   *
   * \param[in]     code       The program's instruction stream.
   * \param[in]     classes    Its byte classes.
   * \param[in]     cp_classes Its code-point classes.
   * \param[in,out] hints      Hints to record the first-byte set in.
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
            // Its ASCII members (a `\W`-style complement is already in the bitmap) plus every UTF-8
            // lead byte: a sound superset.
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
          // Reached by epsilon from pc 0 only when zero repetitions are valid (mandatory copies are
          // unrolled as plain `byte` ahead of it), so the exit is always a live alternative.
          hints.first_bytes.set(instruction.arg8);
          stack.push_back(instruction.secondary_target);
          break;
        case opcode::klass_loop_possessive:
          hints.first_bytes.merge(classes[instruction.arg16]);
          stack.push_back(instruction.secondary_target);
          break;
        case opcode::klass_cp_loop_possessive:
          {
            // Same superset as klass_cp above.
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

  /*!
   * \brief Reports \p klass as up to two contiguous byte ranges.
   *
   * `[lo0, hi0]` is the first run in byte order, `[lo1, hi1]` the second; with no second run they are
   * left untouched, so the caller presets `lo1 > hi1`. Decides the SIMD range-compare eligibility.
   *
   * \param[in]  klass The class to scan.
   * \param[out] lo0   Lower bound of the first run.
   * \param[out] hi0   Upper bound of the first run.
   * \param[out] lo1   Lower bound of the second run (untouched when none).
   * \param[out] hi1   Upper bound of the second run (untouched when none).
   * \return The number of runs found (scanning stops at 3); outside `[1, 2]` is ineligible.
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
   *        an alternation of straight-line branches, and trailing-lookaround class+.
   * \param[in]     code           The instruction stream.
   * \param[in]     classes        Interned character classes referenced by \p code.
   * \param[in]     cp_classes     Match-time code-point classes (for `\w`/`\d`/`\s` word-class tests).
   * \param[in]     cp_ranges      Flat range buffer the \p cp_classes slices index into.
   * \param[in]     cp_mark_ascii  ASCII sub-class index of an emitted codepoint-class block (-1 = none).
   * \param[in]     cp_mark_offset Program offset where that block starts (-1 = none).
   * \param[in]     cp_mark_end    Program offset right after that block (-1 = none); its length varies.
   * \param[in]     lookarounds    Bounded lookaround subs (for trailing-LA eligibility); may be empty.
   * \param[in,out] hints          Hint bag to fill (class-loop, fixed-shape, trailing-LA, …).
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only, never on a search path
#endif
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
    // "class+" shape: save 0, [\b/\B,] [group save,] klass{k}, split(back, exit), [group save,] [\b/\B,]
    // save 1, match. klass{k} (k copies of the same class, then the self-loop) is `X{k,}`: k-1 mandatory
    // copies, the k-th doubling as the loop body (emit_repeat); `\w\w\w+` compiles to the same.
    {
      const shape_lead lead {parse_shape_lead(code)};
      if (lead.ok) {
        std::size_t  p  {lead.body_start};
        std::int16_t gs {-1};
        if (p < code.size() && code[p].op == opcode::save) {
          gs = static_cast<std::int16_t>(code[p].arg16);
          ++p;
        }
        if (p < code.size() && code[p].op == opcode::klass) {
          const std::int32_t cls {code[p].arg16};
          std::size_t        k   {1};
          while (p + k < code.size() && code[p + k].op == opcode::klass && code[p + k].arg16 == cls) {
            ++k;
          }
          const std::size_t last {p + k - 1};
          if (last + 1 < code.size() && code[last + 1].op == opcode::split &&
              code[last + 1].primary_target == static_cast<std::int32_t>(last) &&
              code[last + 1].secondary_target == static_cast<std::int32_t>(last + 2)) {
            std::size_t  q  {last + 2};
            std::int16_t ge {-1};
            bool         ok {gs < 0};
            if (gs >= 0 && q < code.size() && code[q].op == opcode::save) {
              ge = static_cast<std::int16_t>(code[q].arg16);
              ++q;
              ok = true;
            }
            const shape_close close {ok ? parse_shape_close(code, q) : shape_close {}};
            // A `\b`/`\B` wrap with an end anchor is refused: the WRAP branch never sees the peeled limit
            // (`[a-z]+\b$` and `\b(?>\w)$` diverged).
            if (ok && close.ok
                && (close.end_anchor == 0 || (lead.wb_lead == 0 && close.wb_trail == 0))
                && cls >= 0 && static_cast<std::size_t>(cls) < classes.size()
                && k <= 65535) {
              const char_class& cc        {classes[static_cast<std::size_t>(cls)]};
              std::uint8_t      out_lead  {0};
              std::uint8_t      out_trail {0};
              // The self-loop makes this always a maximal run, so the DROP rule applies for any k.
              if (resolve_class_wb_hints(is_full_ascii_word_class(cc), is_ascii_word_subset_class(cc),
                                         /*maximal_run=*/ true, lead.wb_lead, close.wb_trail, out_lead,
                                         out_trail)) {
                hints.greedy_class_loop     = cls;
                hints.greedy_class_loop_end = close.end_anchor;
                hints.greedy_class_loop_min = static_cast<std::uint16_t>(k);
                hints.greedy_group_start    = gs;
                hints.greedy_group_end      = ge;
                hints.wb_lead               = out_lead;
                hints.wb_trail              = out_trail;
                // The DROP rule removed a real leading `\b`: search mode needs the start > 0 window-edge
                // guard (pattern_hints::wb_lead_maximal_run).
                hints.wb_lead_maximal_run = (lead.wb_lead == 1 && out_lead == 0);
              }
            }
          }
        }
      }
    }

    // Trailing-lookaround class+: save 0, BODY, split(back, exit), assert_lookaround, jump AFTER,
    // [sub-program … match], AFTER: save 1, match. BODY is one klass, or a klass_cp and its 3-slot
    // continuation chain. Groupless only (a group's save would sit between the split and the lookaround).
    // The lookaround is an end condition on each maximal run's candidate ends (run_class_loop); a leading
    // lookaround stays on the general VM.
    if (hints.greedy_class_loop < 0 && code.size() >= 2 && code[0].op == opcode::save && code[0].arg16 == 0
        && (code[1].op == opcode::klass || code[1].op == opcode::klass_cp)) {
      const bool        cp    {code[1].op == opcode::klass_cp};
      const std::size_t split {cp ? std::size_t {5} : std::size_t {2}};
      if (code.size() >= split + 5 && code[split].op == opcode::split && code[split].primary_target == 1
          && code[split].secondary_target == static_cast<std::int32_t>(split + 1)
          && code[split + 1].op == opcode::assert_lookaround && code[split + 2].op == opcode::jump) {
        const std::size_t after  {static_cast<std::size_t>(code[split + 2].primary_target)};
        const std::size_t sub_id {code[split + 1].arg16};
        // Jump must land on the closing save 1 / match and skip a non-empty sub region that ends in match.
        if (after >= split + 4 && after + 2 == code.size()
            && code[after].op == opcode::save && code[after].arg16 == 1
            && code[after + 1].op == opcode::match
            && code[after - 1].op == opcode::match // sub-program terminator
            && sub_id < lookarounds.size()
            && lookarounds[sub_id].code_offset == static_cast<std::int32_t>(split + 3)
            && lookarounds[sub_id].code_length == static_cast<std::int32_t>(after - (split + 3))
            && lookarounds[sub_id].direction == look_dir::ahead) {
          // Not greedy_class_loop: every pure class+ site would then branch on trailing_lookaround.
          hints.trailing_lookaround = static_cast<std::int16_t>(sub_id);
          hints.trailing_la_class   = code[1].arg16;
          hints.trailing_la_cp      = cp;
          hints.greedy_group_start  = -1;
          hints.greedy_group_end    = -1;
        }
      }
    }

    // Code-point class (klass_cp + three klass continuations){k}, optional greedy `+` (a self-loop of the
    // LAST block), optional `\b`/`\B` wraps, optional one capturing group: Unicode `\w+`, `\d+`, `\w{k,}`.
    // Content-based interning makes repeated blocks identical; verified below anyway, so an emitter
    // change declines the shape rather than misrecognizing it.
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
          const std::int32_t  cp_idx {code[p].arg16};
          const std::int32_t  cont0  {code[p + 1].arg16};
          const std::int32_t  cont1  {code[p + 2].arg16};
          const std::int32_t  cont2  {code[p + 3].arg16};
          std::size_t         k      {1};
          while (p + (k * 4) + 3 < code.size()) {
            const std::size_t bp {p + (k * 4)};
            if (code[bp].op != opcode::klass_cp || code[bp].arg16 != cp_idx ||
                code[bp + 1].op != opcode::klass || code[bp + 1].arg16 != cont0 ||
                code[bp + 2].op != opcode::klass || code[bp + 2].arg16 != cont1 ||
                code[bp + 3].op != opcode::klass || code[bp + 3].arg16 != cont2) {
              break;
            }
            ++k;
          }
          const std::size_t  loop_pc {p + ((k - 1) * 4)}; // the LAST block's own klass_cp position
          std::size_t        q       {loop_pc + 4};
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
          // `X`, `X+` and `X{k,}` leave the max at 0 (unbounded). `X{k}` (no self-loop) is bounded at k
          // and the route must stop there: `\w{8}` over a nine-letter word matches eight.
          const std::uint16_t cp_max {plus || k == 1 ? std::uint16_t {0} : static_cast<std::uint16_t>(k)};
          // A counted run declines a word boundary: run_cp_class_loop's retry loops skip a failed
          // candidate's whole run, sound only for a maximal run (`\w{4}\b` over "abcdefghi" matches at 5,
          // the skip to 4 loses it). Lifting this needs both loops to step by one code point.
          const bool counted_wb {cp_max != 0 && (lead.wb_lead != 0 || close.wb_trail != 0)};
          // A counted run declines an end anchor for the same reason: `\w{2}\Z` over "xab" matches at 1,
          // but the skip only tries multiples of the width. `+` and `{k,}` keep the route (cp_max 0).
          const bool counted_end {cp_max != 0 && close.end_anchor != 0};
          // Wrap + end anchor: refused, as in the class+ shape above.
          if (ok && close.ok
              && (close.end_anchor == 0 || (lead.wb_lead == 0 && close.wb_trail == 0))
              && !counted_wb && !counted_end && cp_idx >= 0
              && static_cast<std::size_t>(cp_idx) < cp_classes.size() && k <= 65535) {
            const bool has_wb {lead.wb_lead != 0 || close.wb_trail != 0};
            // Bare path: no Unicode table walk (keeps constexpr light for static_regex).
            if (!has_wb) {
              hints.greedy_cp_class      = cp_idx;
              hints.greedy_cp_class_end  = close.end_anchor;
              hints.greedy_cp_class_plus = plus;
              hints.greedy_cp_class_min  = static_cast<std::uint16_t>(k);
              hints.greedy_cp_class_max  = cp_max;
              hints.greedy_group_start   = gs;
              hints.greedy_group_end     = ge;
              hints.wb_lead              = 0;
              hints.wb_trail             = 0;
            }
            else {
              const cp_class& cc        {cp_classes[static_cast<std::size_t>(cp_idx)]};
              std::uint8_t    out_lead  {0};
              std::uint8_t    out_trail {0};
              // `plus` is optional here (`\b\w+` and `\b\w`); the DROP rule holds only with it.
              if (resolve_class_wb_hints(is_full_unicode_word_cp_class(cc, cp_ranges),
                                         is_unicode_word_subset_cp_class(cc, cp_ranges), plus,
                                         lead.wb_lead, close.wb_trail, out_lead, out_trail)) {
                hints.greedy_cp_class      = cp_idx;
                hints.greedy_cp_class_plus = plus;
                hints.greedy_cp_class_min  = static_cast<std::uint16_t>(k);
                hints.greedy_cp_class_max  = cp_max;
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

    // "fixed shape": a straight run of fixed-width byte/klass ops, possibly interleaved with capturing
    // saves (`(\d{4})-(\d{2})`), with optional `\b`/`\B` wraps. Each save sits at a constant offset from
    // the match start, so the fast path fills group slots by offset. klass_cp (variable width), split/jump,
    // `.` or a negated class (byte-level branches), non-wb assertions and lookarounds break the run; pure
    // literals take exact_literal first. The close is peeled inside the body walk, not up front.
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
      std::uint8_t       end_anchor  {};
      const std::uint8_t wb_lead     {lead.wb_lead};
      if (lead.ok) {
        body_pc = static_cast<std::uint8_t>(i);
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
          else if (op == opcode::assert_position && saw_body && wb_trail == 0 && end_anchor == 0) {
            const shape_close close {parse_shape_close(code, i)};
            if (!close.ok) { break; }
            if (close.end_anchor != 0 && !lead.anchored_start) { break; }
            wb_trail   = close.wb_trail;
            end_anchor = close.end_anchor;
            i          = code.size() - 2;
          }
          else {
            break; // split/jump/klass_cp/lookaround/extra assert disqualify
          }
        }
        if (width >= 1 && closed && !nested && i + 1 == code.size() && code[i].op == opcode::match) {
          hints.fixed_shape   = true;
          hints.fs_end_anchor = end_anchor;
          hints.wb_lead       = wb_lead;
          hints.wb_trail      = wb_trail;
          hints.body_pc       = body_pc;
          // A bare single byte class (`[a-z]`), the batchable sub-case: exactly `save 0`, `klass`, `save 1`,
          // `match`, excluding capture and `\b` wraps, anchors and a literal byte (see
          // pattern_hints::single_class). Four instructions also imply slot_count 2.
          if (code.size() == 4 && wb_lead == 0 && wb_trail == 0 && body_pc == 1
              && code[1].op == opcode::klass) {
            hints.single_class = code[1].arg16;
          }
        }
      }

      // The fused SIMD scan+verify (run_fixed_shape) needs a HOMOGENEOUS run, every position accepting
      // the same <= 2-range set: "skip to the first failing lane" is sound only when a mismatch anywhere
      // rules out every position. A mixed run gets the two-position pair filter (fs_pair_width).
      if (hints.fixed_shape) {
        std::array<std::uint8_t, 16> plo0s       {};
        std::array<std::uint8_t, 16> phi0s       {};
        std::array<std::uint8_t, 16> plo1s       {};
        std::array<std::uint8_t, 16> phi1s       {};
        bool                         all_small   {true}; // every position resolved to 1..2 ranges
        std::uint32_t                len         {};
        for (std::size_t pc {static_cast<std::size_t>(hints.body_pc)}; pc < i; ++pc) {
          const opcode op {code[pc].op};
          if (op != opcode::byte && op != opcode::klass) {
            continue;          // interleaved capturing save or trail assert -- epsilon for this purpose
          }
          if (len >= 16) {
            all_small = false; // wider than one vector block; neither path applies
            break;
          }
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
              all_small = false;
              break;
            }
          }
          plo0s[len] = plo0;
          phi0s[len] = phi0;
          plo1s[len] = plo1;
          phi1s[len] = phi1;
          ++len;
        }
        // A peeled anchor refuses both SIMD paths: they read neither anchor, so they would return a
        // candidate it forbids. An anchored shape has one candidate; fixed_shape honours both anchors.
        if (all_small && len >= 1 && len <= 16 && !lead.anchored_start && end_anchor == 0) {
          bool homogeneous {true};
          for (std::uint32_t k {1}; k < len; ++k) {
            if (plo0s[k] != plo0s[0] || phi0s[k] != phi0s[0]
                || plo1s[k] != plo1s[0] || phi1s[k] != phi1s[0]) {
              homogeneous = false;
              break;
            }
          }
          if (homogeneous) {
            hints.fixed_shape_lo0      = plo0s[0];
            hints.fixed_shape_hi0      = phi0s[0];
            hints.fixed_shape_lo1      = plo1s[0];
            hints.fixed_shape_hi1      = phi1s[0];
            hints.fixed_shape_simd_len = static_cast<std::uint8_t>(len);
          }
          else if (len >= 2) {
            // The two most selective positions (smallest sets), ties toward the widest gap: distant bytes
            // of real text correlate less. O(len^2), len <= 16, at compile time.
            const auto card = [&](std::uint32_t k) {
                                std::uint32_t c {static_cast<std::uint32_t>(phi0s[k] - plo0s[k]) + 1U};
                                if (plo1s[k] <= phi1s[k]) {
                                  c += static_cast<std::uint32_t>(phi1s[k] - plo1s[k]) + 1U;
                                }
                                return c;
                              };
            std::uint32_t best_a {0};
            std::uint32_t best_b {1};
            std::uint32_t best_c {card(0) + card(1)};
            std::uint32_t best_d {1};
            for (std::uint32_t a {0}; a < len; ++a) {
              for (std::uint32_t b {a + 1}; b < len; ++b) {
                const std::uint32_t c {card(a) + card(b)};
                const std::uint32_t d {b - a};
                if (c < best_c || (c == best_c && d > best_d)) {
                  best_a = a;
                  best_b = b;
                  best_c = c;
                  best_d = d;
                }
              }
            }
            hints.fs_pair_width = static_cast<std::uint8_t>(len);
            hints.fs_pair_off_a = static_cast<std::uint8_t>(best_a);
            hints.fs_pair_off_b = static_cast<std::uint8_t>(best_b);
            hints.fs_pair_a_lo0 = plo0s[best_a];
            hints.fs_pair_a_hi0 = phi0s[best_a];
            hints.fs_pair_a_lo1 = plo1s[best_a];
            hints.fs_pair_a_hi1 = phi1s[best_a];
            hints.fs_pair_b_lo0 = plo0s[best_b];
            hints.fs_pair_b_hi0 = phi0s[best_b];
            hints.fs_pair_b_lo1 = plo1s[best_b];
            hints.fs_pair_b_hi1 = phi1s[best_b];
          }
        }
      }
    }

    // Whole pattern is one code-point class (`.`/negated), optionally `+`: save 0, the class block
    // (1..cp_mark_end), then save 1, match or split(loop, exit), save 1, match. No captures; `*` is
    // excluded (its empty match). The block's length varies with the lead-byte branches emitted, so
    // cp_mark_end (set by emit_any_codepoint_class) locates its end.
    if (cp_mark_offset == 1 && cp_mark_end > cp_mark_offset &&
        static_cast<std::size_t>(cp_mark_end) < code.size() && code[0].op == opcode::save) {
      const auto end     {static_cast<std::size_t>(cp_mark_end)};
      // The ASCII sub-class comes from the compiler's marker; the block's bytecode is never reverse-engineered.
      std::int32_t ascii {(cp_mark_ascii >= 0 && static_cast<std::size_t>(cp_mark_ascii) < classes.size())
                          ? cp_mark_ascii
                          : -1};
      // Content guard: the marked sub-class must hold ASCII bytes only. A class negating every ASCII byte
      // (`[^\x00-\x7F]`) emits no ASCII branch, so the marker names a byte-range class, rejected here.
      if (ascii >= 0) {
        const char_class& ascii_class {classes[static_cast<std::size_t>(ascii)]};
        for (int byte {0x80}; byte <= 0xFF; ++byte) {
          if (ascii_class.test(static_cast<std::uint8_t>(byte))) {
            ascii = -1;
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
      std::uint8_t  wb_lead      {};
      std::uint8_t  wb_trail     {};
      std::uint8_t  body_pc      {1};
      std::int32_t  branch_count {};
      if (is_fixed_alternation(code, &wb_lead, &wb_trail, &body_pc, &branch_count)) {
        hints.fixed_alternation        = true;
        // Saturated: a wrapped count would read as a handful of branches to the readers that pick a
        // per-branch route under a small count.
        hints.alternation_branch_count = static_cast<std::uint16_t>(
          std::min<std::int32_t>(branch_count, std::numeric_limits<std::uint16_t>::max()));
        // wb_* only when exact_literal / fixed_shape did not claim them.
        if (!hints.fixed_shape && hints.exact_literal_len == 0) {
          hints.wb_lead  = wb_lead;
          hints.wb_trail = wb_trail;
          hints.body_pc  = body_pc;
        }
      }
    }

    // Possessive loop, UNBOUNDED only (X*+, X++: a `jump` back to the loop opcode; pattern_hints says why
    // a bounded count is out of scope). Layout: save 0, [lead \b/\B], [ONE mandatory copy of the loop's
    // atom: byte | klass | klass_cp+3; min >= 2 stays general], loop_pc: byte/klass/klass_cp
    // _loop_possessive, jump(self), [trail \b/\B], [literal suffix bytes, the `x` of `\d++x`], save 1, match.
    //
    // The capture slot is the loop opcode's `primary_target` (`([a-z])*+b`): no `save` precedes the loop,
    // since a speculative save would corrupt the last successful iteration's capture.
    {
      const shape_lead   lead    {parse_shape_lead(code)};
      const std::size_t  p       {lead.ok && !lead.anchored_start ? lead.body_start : code.size()};
      const std::uint8_t wb_lead {lead.wb_lead};
      if (lead.ok && !lead.anchored_start) {
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
          // A captured min >= 1 compiles to a different shape (a save-wrapped copy per repetition).
          const bool capture_ok        {cap_slot < 0 || !has_mandatory};
          // The mandatory copy must be the loop's own atom. class_ref's operator== compares the kind first,
          // so `[abc].*+`'s klass and the loop's klass_cp sharing an index never compare equal.
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
            if (!peel_optional_wb(code, q, wb_trail)) {
              q = code.size(); // disqualify below
            }
            std::array<char, 8>  suffix      {};
            std::uint8_t         suffix_len  {0};
            while (q < code.size() && code[q].op == opcode::byte && suffix_len < suffix.size()) {
              suffix[suffix_len] = static_cast<char>(code[q].arg8);
              ++suffix_len;
              ++q;
            }
            // Fast paths must always consume (this driver has no empty-match advance contract), so a star
            // loop needs a suffix: `has_mandatory || suffix_len >= 1` below.
            const bool table_bound_ok {loop_kind == class_kind::byte ||
                                       (loop_kind == class_kind::klass_cp
                                          ? static_cast<std::size_t>(body_idx) < cp_classes.size()
                                          : static_cast<std::size_t>(body_idx) < classes.size())};
            if (q + 1 < code.size() && q + 2 == code.size() && code[q].op == opcode::save &&
                code[q].arg16 == 1 && code[q + 1].op == opcode::match && body_idx >= 0 &&
                (has_mandatory || suffix_len >= 1) && table_bound_ok) {
              const bool   has_wb    {wb_lead != 0 || wb_trail != 0};
              // A byte loop has no word class for the DROP rule: `\ba++\b` stays general, `a++x` arms.
              bool         arm       {!has_wb};
              std::uint8_t out_lead  {0};
              std::uint8_t out_trail {0};
              if (has_wb && loop_kind != class_kind::byte) {
                // An unbounded possessive run is maximal wherever it starts, so the DROP rule holds.
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
              // A bare unbounded possessive class loop (`[a-z]++`) is the greedy language, so it takes the
              // batched greedy route, decided here as a hint: a class-index parameter in the runtime fillers
              // cost the Unicode class rows 5.7-9.1 % (inlining budget). Bare only: a suffix means the match
              // is not the run, a capture lives in possessive_group_start (unread by the greedy path), and
              // `X*+` with no mandatory copy can match empty.
              const bool redirect {arm && suffix_len == 0 && gs < 0 && has_mandatory && !has_wb
                                   && (loop_ref.kind == class_kind::klass
                                       || loop_ref.kind == class_kind::klass_cp)};
              if (redirect && loop_ref.kind == class_kind::klass) {
                hints.greedy_class_loop     = loop_ref.index;
                hints.greedy_class_loop_min = 1;
                hints.greedy_class_loop_end = 0;
              }
              else if (redirect) {
                // The code-point twin: greedy_cp_class is batched too (it is what `\w+` takes).
                hints.greedy_cp_class      = loop_ref.index;
                hints.greedy_cp_class_plus = true; // a possessive loop is unbounded by construction
                hints.greedy_cp_class_min  = 1;
                hints.greedy_cp_class_end  = 0;
                hints.greedy_cp_class_max  = 0;
              }
              else if (arm) {
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

    // Possessive delimited ("quoted") shape: literal PREFIX (1+ bytes), unbounded uncaptured min-0
    // class/cp-class possessive loop, literal SUFFIX (1+ bytes). The loop's class must exclude the
    // prefix's and the suffix's first byte: a prefix hidden in a scanned body run (`id=` in `[a-z0-9]*+`)
    // makes the runner's skip-to-body-end retry miss the leftmost match, or go quadratic without the skip.
    // Tried only when the shape above did not arm.
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
              hints.possessive_min_nonzero    = false; // the loop is min 0; the prefix is the mandatory part
              const class_kind delimited_kind {is_cp ? class_kind::klass_cp : class_kind::klass};
              hints.possessive_class          = {.kind = delimited_kind, .index = static_cast<std::uint16_t>(body_idx)};
            }
          }
        }
      }
    }
  }

  /*!
   * \brief Approximate static frequency of a byte in mixed English and source text, per 10000, used only
   *        to rank candidate prefilter bytes: a rare required byte (`-`, `@`) is a far more selective
   *        `memchr` target than a common first-byte class.
   *
   * \param[in] b The byte to rank.
   * \return Its approximate frequency, in occurrences per 10000.
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
      default:                           break;
    }
    // UTF-8 bytes are not rare: ranked at 1, `café` memchr'd the 0xC3 that leads every Latin-1 letter,
    // several times the scan. What counts is selectivity (a lead stands for 64+ characters): these values
    // keep the Latin-1 leads and every continuation byte above the rarity threshold, while the rarer leads
    // stay eligible for scripts made of nothing else (CJK).
    if (b >= 0x80U && b <= 0xBFU) {
      return 200;             // continuation: as common as whatever leads it, and shared by every script
    }
    switch (b) {
      case 0xC3U: return 250; // the Latin-1 letters: ubiquitous in Western European text
      case 0xC2U: return 120; // Latin-1 punctuation/symbols (nbsp, guillemets, degree)
      case 0xE2U: return 60;  // general punctuation: dashes, curly quotes, arrows
      default:    break;
    }
    if (b >= 0xC4U && b <= 0xDFU) {
      return 40; // Latin Extended, Greek, Cyrillic, Hebrew, Arabic leads
    }
    if (b >= 0xE0U && b <= 0xEFU) {
      return 30; // 3-byte leads: CJK, Indic, Hangul
    }
    if (b >= 0xF0U && b <= 0xF4U) {
      return 10; // 4-byte leads: astral planes and emoji
    }
    return 1; // control bytes and the two never-valid UTF-8 bytes: genuinely rare
  }

  /*!
   * \brief Offset of the rarest byte of \p literal by \ref byte_frequency (the first of equals).
   * \param[in] literal The needle (non-empty).
   * \return The offset.
   */
  constexpr std::uint8_t literal_rarest_offset(std::string_view literal)
  {
    std::size_t best {0};
    for (std::size_t k = 1; k < literal.size() && k <= 0xFFU; ++k) {
      if (byte_frequency(static_cast<std::uint8_t>(literal[k])) < byte_frequency(static_cast<std::uint8_t>(literal[best]))) {
        best = k;
      }
    }
    return static_cast<std::uint8_t>(best);
  }

  /*!
   * \brief Records a *required* literal byte at a FIXED offset far rarer than the first-byte set
   *        (\ref pattern_hints::rare_byte / rare_offset), so the search can `memchr` that one byte.
   *
   * Walks the leading fixed-width run and stops at the first variable-width or branching op (`klass_cp`,
   * `split`, `jump`, `match`). The byte must clear an absolute rarity threshold and be several times rarer
   * than the first-byte set. Sound: the hint only filters candidate starts.
   *
   * \param[in]     code  The program's instruction stream.
   * \param[in,out] hints Hints to record the rare byte and its offset in.
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
        break; // klass_cp (variable width) / split / jump / match: the offset is not fixed
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
   * \brief Arms the rare-discriminant prefilter for shapes like `https?://…`:
   *        fixed prefix (`http`) + optional mono-byte (`s?`) + fixed mid with a rare disc (`://`).
   *
   * Unlike \ref extract_rare_byte, the disc need not sit at a fixed offset: the search memchr's it and
   * back-verifies the optional shape. Sound: it only filters candidates.
   *
   * \param[in]     code  The program's instruction stream.
   * \param[in,out] hints Hints to record the discriminant and its surrounding shape in.
   */
  constexpr void extract_rare_discriminant(std::span<const instr> code,
                                           pattern_hints&         hints)
  {
    if (hints.anchored_start) {
      return;
    }
    // Leading saves, then fixed `byte` ops, optionally interrupted by a mono-byte `?` split.
    std::size_t pc {0};
    while (pc < code.size() && code[pc].op == opcode::save) {
      ++pc;
    }
    std::array<char, 16> fixed     {};
    std::uint8_t         fixed_len {};
    std::int16_t         opt       {-1};
    std::uint8_t         opt_at    {}; // index in `fixed` where the optional sits (prefix ends there)
    bool                 saw_opt   {};
    while (pc < code.size() && fixed_len < fixed.size()) {
      if (code[pc].op == opcode::byte) {
        fixed[fixed_len++] = static_cast<char>(code[pc].arg8);
        ++pc;
        continue;
      }
      // Optional mono-byte once: split → byte X → join (the `s?` shape).
      if (!saw_opt && code[pc].op == opcode::split) {
        const std::int32_t pri {code[pc].primary_target};
        const std::int32_t sec {code[pc].secondary_target};
        if (pri == static_cast<std::int32_t>(pc + 1) && sec == static_cast<std::int32_t>(pc + 2) &&
            pc + 1 < code.size() && code[pc + 1].op == opcode::byte) {
          opt     = static_cast<std::int16_t>(static_cast<std::uint8_t>(code[pc + 1].arg8));
          opt_at  = fixed_len; // prefix = fixed[0..opt_at)
          saw_opt = true;
          pc      = static_cast<std::size_t>(sec);
          continue;
        }
      }
      break;  // variable-width / complex branch
    }
    if (fixed_len < 2) {
      return; // need at least disc + something (or disc mid-run)
    }
    // The disc is the rarest byte past the optional site (among `://` in `https?://`), else of the whole run.
    const std::uint8_t search_from {saw_opt ? opt_at : static_cast<std::uint8_t>(0)};
    if (search_from >= fixed_len) {
      return;
    }
    std::int16_t  best_byte {-1};
    std::uint8_t  best_idx  {};
    std::uint16_t best_freq {0xFFFFU};
    for (std::uint8_t i {search_from}; i < fixed_len; ++i) {
      const auto freq {byte_frequency(static_cast<std::uint8_t>(fixed[i]))};
      if (freq < best_freq) {
        best_freq = freq;
        best_byte = static_cast<std::int16_t>(static_cast<std::uint8_t>(fixed[i]));
        best_idx  = i;
      }
    }
    if (best_byte < 0 || best_idx < search_from) {
      return;
    }
    std::uint8_t prefix_len {};
    if (saw_opt) {
      // The disc must follow the optional immediately (`:` after `s?`).
      if (best_idx != opt_at) {
        return;
      }
      prefix_len = opt_at;
      if (prefix_len == 0 || prefix_len > 8) {
        return;
      }
    }
    else {
      prefix_len = best_idx;
      if (prefix_len > 8) {
        return;
      }
    }
    const std::uint8_t after_len {static_cast<std::uint8_t>(fixed_len - best_idx - 1U)};
    if (after_len > 4) {
      return;
    }
    // Rarity gate vs the first-byte filter (same spirit as extract_rare_byte).
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
    if (best_freq >= 100U || static_cast<std::uint32_t>(best_freq) * 4U >= first_freq) {
      return;
    }
    hints.rare_disc            = best_byte;
    hints.rare_disc_prefix_len = prefix_len;
    for (std::uint8_t k {0}; k < prefix_len; ++k) {
      hints.rare_disc_prefix[k] = fixed[k];
    }
    hints.rare_disc_opt       = saw_opt ? opt : static_cast<std::int16_t>(-1);
    hints.rare_disc_after_len = after_len;
    for (std::uint8_t j {0}; j < after_len; ++j) {
      hints.rare_disc_after[j] = fixed[static_cast<std::size_t>(best_idx) + 1U + j];
    }
  }

  /*!
   * \brief The structural half of \ref pattern_hints::capture_free_walk, that `save 0` is the program's
   *        first instruction.
   *
   * The walk keeps group 0's start in one scalar shared by a whole epsilon closure, correct only while
   * `save 0` cannot be skipped: behind a split, a bypassing branch would inherit its sibling's start (a
   * wrong answer). The other half (no `save` past slot 1) is the caller's: one that reads no captures
   * (\ref real::basic_regex::count_matches) may set the flag on this condition alone.
   *
   * \param[in] code The instruction stream.
   * \return True when the capture-free walk's single-scalar start is sound for \p code.
   */
  [[nodiscard]] constexpr bool capture_free_walk_structural(std::span<const instr> code) noexcept
  {
    return !code.empty() && code[0].op == opcode::save && code[0].arg16 == 0U;
  }

  /*!
   * \brief Walks a compiled program once to derive its search hints.
   * \param[in] code           The instruction stream.
   * \param[in] classes        The interned character classes referenced by \p code.
   * \param[in] cp_classes     The match-time code-point classes referenced by `klass_cp`.
   * \param[in] cp_ranges      Flat range buffer the \p cp_classes slices index into.
   * \param[in] cp_mark_ascii  ASCII sub-class index of an emitted codepoint-class block (-1 = none).
   * \param[in] cp_mark_offset Program offset where that block starts (-1 = none).
   * \param[in] cp_mark_end    Program offset right after that block (-1 = none); its length varies.
   * \param[in] lookarounds    Bounded lookaround subs (trailing-LA class+ detection).
   * \return The \ref pattern_hints.
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

    // Derived by scan, not from the group count. Past the structural half, every `save` must write slot 1,
    // which the walk ignores (the end is `pos` at the match). Lookaround sub-programs emit no saves, so a
    // bounded lookaround does not disqualify.
    hints.capture_free_walk = capture_free_walk_structural(code);
    for (std::size_t i = 1; hints.capture_free_walk && i < code.size(); ++i) {
      if (code[i].op == opcode::save && code[i].arg16 != 1U) {
        hints.capture_free_walk = false;
      }
    }

    // A lookaround forces the general Pike VM, except the trailing-LA class+ shape, which arms
    // trailing_lookaround (not greedy_class_loop) so the pure [a-z]+ gate stays a single compare.
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

    // Trailing-LA class+ set only trailing_*, which this wipe keeps; the prefix and first-byte set stay
    // sound filters.
    if (has_lookaround) {
      hints.greedy_class_loop     = -1;
      hints.exact_literal_len     = 0;
      hints.fixed_shape           = false;
      hints.fs_pair_width         = 0;    // paired with fixed_shape: never read without it
      hints.single_class          = -1;   // likewise paired with fixed_shape
      hints.codepoint_class_ascii = -1;
      hints.fixed_alternation     = false;
    }

    if (hints.prefix_size > 0) {
      hints.single_first = static_cast<unsigned char>(hints.prefix[0]);
    }
    else if (hints.first_bytes_valid) {
      // One member drives find_byte (memchr); two to eight the small_set SIMD scan, whose cap must
      // match run_alternation's small_set_size <= 8 gate; nine or more stay on the bitmap loop.
      std::array<char, 8> members {};
      int                 count   {0};
      for (unsigned byte = 0; byte < 256; ++byte) {
        if (hints.first_bytes.test(static_cast<std::uint8_t>(byte))) {
          if (count < 8) {
            members[static_cast<std::size_t>(count)] = static_cast<char>(byte);
          }
          ++count;
          if (count > 8) {
            break;
          }
        }
      }
      if (count == 1) {
        hints.single_first = static_cast<std::int16_t>(static_cast<unsigned char>(members[0]));
      }
      else if (count >= 2 && count <= 8) {
        for (auto k = static_cast<std::size_t>(count); k < members.size(); ++k) {
          members[k] = members[0]; // the block scans compare eight lanes: a spare one repeats a member
        }
        hints.small_set      = members;
        hints.small_set_size = static_cast<std::uint8_t>(count);
      }
    }

    // A whole-pattern class run's stop set: the bytes below `limit` the class rejects, when one to six, so
    // the run memchr-cascades to the next stop. Outside the program: byte identity is unaffected.
    const auto record_stops {[&hints](const char_class& accepted, unsigned limit) {
                               std::array<char, 6> stops      {};
                               int                 stop_count {0};
                               for (unsigned byte = 0; byte < limit && stop_count <= 6; ++byte) {
                                 if (!accepted.test(static_cast<std::uint8_t>(byte))) {
                                   if (stop_count < 6) {
                                     stops[static_cast<std::size_t>(stop_count)] = static_cast<char>(byte);
                                   }
                                   ++stop_count;
                                 }
                               }
                               if (stop_count >= 1 && stop_count <= 6) {
                                 hints.stop_set      = stops;
                                 hints.stop_set_size = static_cast<std::uint8_t>(stop_count);
                               }
                             }};
    if (hints.greedy_class_loop >= 0) {
      record_stops(classes[static_cast<std::size_t>(hints.greedy_class_loop)], 256U);
    }
    // A whole-pattern code-point class (`.`/`[^x]`) accepts every valid code point >= 0x80
    // (run_codepoint_class checks structure, not membership), so only its ASCII rejects are stops;
    // malformed UTF-8 still stops the run through code-point validation.
    else if (hints.codepoint_class_ascii >= 0) {
      record_stops(classes[static_cast<std::size_t>(hints.codepoint_class_ascii)], 0x80U);
    }

    // Last, so it compares against the final first-byte hints (the `-` of `[0-9]{4}-[0-9]{2}`).
    extract_rare_byte(code, hints);
    if (hints.prefix_size >= 2) {
      hints.prefix_rare = literal_rarest_offset(std::string_view {hints.prefix.data(), hints.prefix_size});
    }
    // Rare discriminant past an optional mono-byte (URL `https?://`): memchr the disc, back-verify
    // prefix+opt+after. Preferable to a weak literal prefix (`http`) when the disc is rarer.
    extract_rare_discriminant(code, hints);

    // A single-byte memchr target (rare_byte, single_first; both exist only past the shape walk) vetoes
    // the fixed-shape pair filter. Semantic, not an ISA gate: where memchr is wider than the filter's
    // 128-bit block the pair path loses to it, but an icase literal (`(?i)cafe`) has no byte to memchr
    // and the pair filter wins on both ISAs.
    if (hints.rare_byte >= 0 || hints.single_first >= 0) {
      hints.fs_pair_width = 0;
    }

    // Last: it reads the anchoring, the prefix (after the lookaround wipe) and rare_disc. See
    // pattern_hints::literal_one_search.
    if (hints.exact_literal_len >= 2 && hints.prefix_size == hints.exact_literal_len
        && !hints.anchored_start && hints.line_anchored == 0U && hints.rare_disc < 0) {
      bool no_assert {true};
      for (const instr& instruction : code) {
        if (instruction.op == opcode::assert_position) {
          no_assert = false;
          break;
        }
        if (instruction.op == opcode::match) {
          break;
        }
      }
      hints.literal_one_search = no_assert;
    }
    return hints;
  }

#if defined(__ARM_NEON)
  /*!
   * \brief The single-byte scan behind \ref find_byte on NEON: 64 bytes per round, rejected by one test on
   *        the OR of four compares (\ref any_byte64).
   *
   * Twice the platform `memchr` on arm64 (64 KB, no hit: 625 against 1250 ns). Lanes are taken in block
   * order, so the first hit is the leftmost. x86-64 keeps `memchr`, whose vectors are wider.
   * \param[in] text The subject text.
   * \param[in] pos  Index to start scanning from (below `text.size()`).
   * \param[in] byte The byte to find.
   * \return The index of the first occurrence at or after \p pos, else \ref real::npos.
   */
  inline std::size_t simd_byte_scan(std::string_view text,
                                    std::size_t      pos,
                                    std::uint8_t     byte)
  {
    const char* const     base   {text.data()};
    std::size_t           p      {pos};
    constexpr std::size_t unroll {4};
    while (p + (unroll * 16) <= text.size()) {
      std::array<std::uint8_t, unroll * 16> blk {};
      std::memcpy(blk.data(), base + p, unroll * 16); // MISRA-clean byte loads (no type-pun)
      if (any_byte64(blk.data(), byte)) {
        for (std::size_t u = 0; u < unroll; ++u) {
          const mask_t mask {load_byte_mask(blk.data() + (u * 16), byte)};
          if (!empty(mask)) {
            return p + (u * 16) + first_lane(mask);
          }
        }
      }
      p += unroll * 16;
    }
    for (; p < text.size(); ++p) { // tail: fewer than 64 bytes left
      if (static_cast<std::uint8_t>(base[p]) == byte) {
        return p;
      }
    }
    return npos;
  }

#endif

  /*!
   * \brief Index of \p byte in `text[pos..)`, or \ref real::npos.
   *
   * `memchr` at run time (NEON: `simd_byte_scan`), a plain loop in constant evaluation.
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
      // Bill remaining haystack once per call — O(n) path bills ~once; per-pos restart → O(n²) total.
      note(counter::prefilter_work_units, text.size() - pos);
#if defined(__ARM_NEON)
      return simd_byte_scan(text, pos, static_cast<std::uint8_t>(byte));
#else
      const void* hit {std::memchr(text.data() + pos, byte, text.size() - pos)};
      return hit == nullptr
             ? npos
             : static_cast<std::size_t>(static_cast<const char*>(hit) - text.data());
#endif
    }
    for (std::size_t i = pos; i < text.size(); ++i) {
      if (text[i] == byte) {
        return i;
      }
    }
    return npos;
  }

  /*!
   * \brief Consecutive disc hits that fail back-verify before the density gate trips.
   *        Dense `:` filler (e.g. `a:b:c:d…`) makes memchr+verify lose to a selective `http` prefix.
   */
  inline constexpr std::uint32_t rare_disc_fail_abandon {32};

  /*!
   * \brief Next candidate start for the rare-discriminant prefilter, or \ref real::npos.
   *
   * Scans for the disc with \ref find_byte, then back-verifies `[prefix][opt?][disc][after]`. After
   * \ref rare_disc_fail_abandon failed hits it sets \p density_abandon and returns npos; the caller then
   * takes the prefix path for the rest of the subject.
   *
   * \param[in]  text            Subject.
   * \param[in]  pos             Lower bound on the returned start.
   * \param[in]  hints           The armed hints, read for the discriminant and its shape.
   * \param[out] density_abandon When non-null, set if the disc proved too dense to be worth scanning.
   * \return The verified candidate start, or \ref real::npos when there is none.
   */
  constexpr std::size_t find_rare_disc_candidate(std::string_view     text,
                                                 std::size_t          pos,
                                                 const pattern_hints& hints,
                                                 bool*                density_abandon = nullptr)
  {
    if (density_abandon != nullptr) {
      *density_abandon = false;
    }
    if (hints.rare_disc < 0) {
      return npos;
    }
    const auto        disc     {static_cast<char>(hints.rare_disc)};
    const std::size_t pref     {hints.rare_disc_prefix_len};
    const bool        has_opt  {hints.rare_disc_opt >= 0};
    const auto        opt_ch   {static_cast<char>(hints.rare_disc_opt)};
    const std::size_t after    {hints.rare_disc_after_len};
    // The shortest back-span is the prefix alone (no optional byte).
    const std::size_t min_back {pref};
    std::size_t       scan     {pos + min_back < text.size() ? pos + min_back : text.size()};
    std::uint32_t     fails    {};
    while (scan < text.size()) {
      const std::size_t i {find_byte(text, scan, disc)};
      if (i == npos) {
        return npos;
      }
      if (i + 1U + after > text.size()) {
        return npos;
      }
      bool after_ok {true};
      for (std::size_t j {0}; j < after; ++j) {
        if (text[i + 1U + j] != hints.rare_disc_after[j]) {
          after_ok = false;
          break;
        }
      }
      if (!after_ok) {
        ++fails;
        if (fails >= rare_disc_fail_abandon) {
          if (density_abandon != nullptr) {
            *density_abandon = true;
          }
          return npos;
        }
        scan = i + 1U;
        continue;
      }
      // Prefer longer prefix when both fit (https before http).
      std::size_t start {npos};
      if (has_opt && i >= pref + 1U) {
        const std::size_t s {i - pref - 1U};
        if (s >= pos && text[s + pref] == opt_ch) {
          bool ok {true};
          for (std::size_t k {0}; k < pref; ++k) {
            if (text[s + k] != hints.rare_disc_prefix[k]) {
              ok = false;
              break;
            }
          }
          if (ok) {
            start = s;
          }
        }
      }
      if (start == npos && i >= pref) {
        const std::size_t s {i - pref};
        if (s >= pos) {
          bool ok {true};
          for (std::size_t k {0}; k < pref; ++k) {
            if (text[s + k] != hints.rare_disc_prefix[k]) {
              ok = false;
              break;
            }
          }
          if (ok) {
            start = s;
          }
        }
      }
      if (start != npos) {
        return start;
      }
      ++fails;
      if (fails >= rare_disc_fail_abandon) {
        if (density_abandon != nullptr) {
          *density_abandon = true;
        }
        return npos;
      }
      scan = i + 1U;
    }
    return npos;
  }

#if defined(__ARM_NEON) || defined(__SSE2__)
  /*!
   * \brief The two-byte block filter behind \ref find_literal_adaptive, taken once a needle's rarest byte
   *        proved common in the subject.
   *
   * One vector compare tests 16 candidate starts at two needle offsets (first and last byte), so a block
   * with no survivor costs two loads and an AND; one probe would be what `memchr` already gives. Written
   * once against simd.hpp's \ref mask_t interface, with no ISA branch of its own.
   *
   * A one-byte scan wins while its byte is rare (x86-64, `example.com` over 500 KB of logs: 0.014 against
   * 0.044 ms) and loses once it is common (`the`: 0.25 against 0.051), hence the hand-off. Linear: each
   * block verifies at most 16 candidates. The caller bills the work counter once per call, so this must
   * not be entered per candidate.
   *
   * \param[in] text    The subject text.
   * \param[in] pos     Index to start searching from.
   * \param[in] literal The needle (>= 2 bytes; a single byte belongs on \ref find_byte's one memchr).
   * \return The index of the first occurrence at or after \p pos, else \ref real::npos.
   */
  inline std::size_t simd_literal_scan(std::string_view text,
                                       std::size_t      pos,
                                       std::string_view literal)
  {
#if defined(__AVX2__) || (defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__)))
#  if defined(__AVX2__)
    if (!literal_avx2_disabled()) {
#  else
    if (!literal_avx2_disabled() && cpu_has_avx2()) {
#  endif
      note(counter::literal_avx2_scans);
      return avx2_literal_scan(text, pos, literal); // twice the block, where the CPU has it
    }
#endif
    const std::size_t len {literal.size()};
    if (text.size() < len) {
      return npos;
    }
    const std::size_t last  {text.size() - len}; // last index a match could start at
    const auto        lead  {static_cast<std::uint8_t>(literal.front())};
    const auto        trail {static_cast<std::uint8_t>(literal[len - 1])};
    const std::size_t delta {len - 1};           // trail's offset from a candidate start
    const char* const base  {text.data()};
    std::size_t       p     {pos};
    // Four blocks (64 candidates) per round, rejected by ONE test on their OR (any_pair64): at one block a
    // round this filter ran ~13 % slower than `find` on a pure miss, at four ~2.5x faster than memchr.
    // Masks are taken in block then lane order: the first verified hit is the leftmost, as callers require.
    constexpr std::size_t unroll {4};
    while (p + (unroll * 16) <= last + 1) {
      std::array<std::uint8_t, unroll * 16> blk_lead  {};
      std::array<std::uint8_t, unroll * 16> blk_trail {};
      std::memcpy(blk_lead.data(), base + p, unroll * 16); // MISRA-clean byte loads (no type-pun)
      std::memcpy(blk_trail.data(), base + p + delta, unroll * 16);
      if (any_pair64(blk_lead.data(), lead, blk_trail.data(), trail)) {
        for (std::size_t u = 0; u < unroll; ++u) {
          mask_t mask {load_pair_mask(blk_lead.data() + (u * 16), lead, blk_trail.data() + (u * 16), trail)};
          while (!empty(mask)) {
            const std::size_t cand {p + (u * 16) + first_lane(mask)};
            if (std::memcmp(base + cand, literal.data(), len) == 0) {
              return cand;
            }
            mask = clear_first(mask);
          }
        }
      }
      p += unroll * 16;
    }
    // A block covers candidates [p, p + 16); the furthest one reads its trail byte at p + 15 + delta,
    // which the `p + 16 <= last + 1` guard keeps inside the text (p + 15 <= last).
    while (p + 16 <= last + 1) {
      std::array<std::uint8_t, 16> blk_lead  {};
      std::array<std::uint8_t, 16> blk_trail {};
      std::memcpy(blk_lead.data(), base + p, 16);          // MISRA-clean byte loads (no pointer type-pun)
      std::memcpy(blk_trail.data(), base + p + delta, 16);
      mask_t mask {load_pair_mask(blk_lead.data(), lead, blk_trail.data(), trail)};
      while (!empty(mask)) {
        const std::size_t cand {p + first_lane(mask)};
        if (std::memcmp(base + cand, literal.data(), len) == 0) {
          return cand;
        }
        mask = clear_first(mask); // this window can still hold a later candidate — no reload
      }
      p += 16;
    }
    while (p <= last) { // tail: fewer than 16 candidate starts left
      if (std::memcmp(base + p, literal.data(), len) == 0) {
        return p;
      }
      ++p;
    }
    return npos;
  }

#endif // __ARM_NEON || __SSE2__

  /*!
   * \brief What the adaptive literal search learned about one subject: whether the needle's rarest byte
   *        is common there.
   *
   * Kept per subject (reset when it changes), so later searches take the pair filter at once. A fresh
   * value per call is correct too; it only re-learns.
   */
  struct literal_density
  {
    std::uint32_t cands  {};     //!< Distinct stops the rarest-byte scan made on this subject.
    std::size_t   origin {npos}; //!< Offset of the first of them.
    std::size_t   last   {npos}; //!< Offset of the furthest of them: a search that starts behind it counts no stop twice.
    std::size_t   rare   {npos}; //!< Offset scanned instead of the hints' one, once this subject showed it rarer.
    bool          dense  {};     //!< Sticky: the pair filter takes this subject from here on.
  };

  /*!
   * \brief The two literal densities of one subject, held in a `std::optional` built at the first literal
   *        search, so constructing a search state (every search does) writes no densities.
   */
  struct literal_memo
  {
    literal_density prefix {}; //!< What the subject showed of the prefix's rarest byte.
    literal_density inner  {}; //!< The same for the inner literal.
  };

  inline constexpr std::uint32_t literal_dense_min_cands {8}; //!< Stops the rarest-byte scan makes before its density is judged: fewer say nothing.

  /*!
   * \brief Mean bytes between stops below which the rarest byte counts as common: under it, a stop costs
   *        more than the pair filter spends crossing that many bytes.
   *
   * Per ISA, since the one-byte scan's width differs. Measured break-even over 1 MB with no match: x86-64
   * `memchr` near 70 bytes, the arm64 128-bit loop near 190.
   */
#if defined(__ARM_NEON)
  inline constexpr std::size_t literal_dense_gap {192};
#else
  inline constexpr std::size_t literal_dense_gap {64};
#endif

  /*!
   * \brief Writes the adaptive search's local density back (\ref find_literal_adaptive_rest keeps it in
   *        locals while it scans).
   * \param[out] density The subject's density.
   * \param[in]  cands   Distinct stops counted.
   * \param[in]  origin  Offset of the first.
   * \param[in]  next    First offset not yet counted (0: none counted).
   */
  constexpr void store_literal_density(literal_density& density,
                                       std::uint32_t    cands,
                                       std::size_t      origin,
                                       std::size_t      next) noexcept
  {
    density.cands  = cands;
    density.origin = origin;
    density.last   = next == 0U ? npos : next - 1U;
  }

  /*!
   * \brief The body of \ref find_literal_adaptive past its first stop: the pair filter for a dense subject,
   *        else the rarest-byte scan that counts its stops and judges their density.
   *
   * Out of line: next_candidate runs per candidate on routes that never reach a literal; inlined there it
   * cost `\d{4}-\d{2}-\d{2}` 7 % on arm64.
   * \param[in]     text    The subject text.
   * \param[in]     pos     Index to start from (a stop the caller saw fail, or where a dense search starts).
   * \param[in]     literal The needle (>= 2 bytes).
   * \param[in]     rare    Offset of its rarest byte by the hints.
   * \param[in,out] density What this subject has shown so far.
   * \return The index of the first occurrence at or after \p pos, else \ref real::npos.
   */
  REAL_NOINLINE
  inline std::size_t find_literal_adaptive_rest(std::string_view text,
                                                std::size_t      pos,
                                                std::string_view literal,
                                                std::size_t      rare,
                                                literal_density& density)
  {
    note(counter::literal_rest_scans);
    const std::size_t len {literal.size()};
#if defined(__ARM_NEON) || defined(__SSE2__)
    if (density.dense) {
      note(counter::literal_pair_scans);
      return simd_literal_scan(text, pos, literal);
    }
#endif
    const std::size_t last {text.size() - len}; // last index a match could start at
    if (density.rare != npos) {
      rare = density.rare;
    }
    char              byte   {literal[rare]};
    const char* const base   {text.data()};
    std::size_t       p      {pos};
    // The density in locals: the scan calls out on every stop, and fields behind a reference would be
    // reloaded after each call. Written back on every way out.
    std::uint32_t     cands  {density.cands};
    std::size_t       origin {density.origin};
    std::size_t       next   {density.last == npos ? 0U : density.last + 1U}; // first offset not yet counted
    while (p <= last) {
      // The rarest byte of a candidate starting at or before `last` sits at or before `last + rare`.
#if defined(__ARM_NEON)
      const std::size_t hit   {simd_byte_scan(text.substr(0, last + rare + 1), p + rare, static_cast<std::uint8_t>(byte))};
#else
      const void* const found {std::memchr(base + p + rare, byte, last - p + 1)};
      const std::size_t hit   {found == nullptr ? npos : static_cast<std::size_t>(static_cast<const char*>(found) - base)};
#endif
      if (hit == npos) {
        store_literal_density(density, cands, origin, next);
        return npos;
      }
      const std::size_t cand {hit - rare};
      if (std::memcmp(base + cand, literal.data(), len) == 0) {
        store_literal_density(density, cands, origin, next);
        return cand;
      }
      p = cand + 1;
      // Searches on one subject may start behind one another (a batch fills ahead, a per-match search
      // resumes behind it): a stop counts once, or re-counted stops would read as density.
      if (cand < next) {
        continue;
      }
      origin = cands == 0U ? cand : origin;
      next   = cand + 1;
      ++cands;
      if (cands >= literal_dense_min_cands && cand - origin < cands * literal_dense_gap) {
        // The static rank chose a byte this subject uses often. Before giving up on a single byte, count
        // each needle byte over the stretch these stops crossed: one rare there is scanned instead, once
        // per subject; none rare enough, and the pair filter takes over.
        if (density.rare == npos) {
          const std::string_view         seen       {text.substr(origin, cand + len - origin)};
          std::size_t                    best       {rare};
          std::size_t                    best_count {cands};
          std::array<std::uint32_t, 256> counts     {}; // one pass over the stretch, whatever the needle's length
          for (const char c : seen) {
            ++counts[static_cast<std::uint8_t>(c)];
          }
          for (std::size_t k = 0; k < len; ++k) {
            const std::size_t count {counts[static_cast<std::uint8_t>(literal[k])]};
            if (count < best_count) {
              best       = k;
              best_count = count;
            }
          }
          if (best != rare && best_count * literal_dense_gap < seen.size()) {
            density.rare = best; // the byte this subject is scanned by from here
            cands        = 0;
            rare         = best;
            byte         = literal[best];
            continue;
          }
          density.rare = rare; // judged, and none rarer: not judged again on this subject
        }
#if defined(__ARM_NEON) || defined(__SSE2__)
        store_literal_density(density, cands, origin, next);
        density.dense = true;
        note(counter::literal_pair_scans);
        return simd_literal_scan(text, cand + 1, literal);
#endif
        // No pair filter on this target: the single-byte scan goes on.
      }
    }
    store_literal_density(density, cands, origin, next);
    return npos;
  }

  /*!
   * \brief Index of the first occurrence of \p literal in `text[pos..)`, or \ref real::npos, by its rarest
   *        byte while that byte is rare in the subject and by the two-byte block filter once it is not.
   *
   * Once \ref literal_dense_min_cands stops sit under \ref literal_dense_gap bytes apart on average,
   * \p density turns dense and the pair filter resumes at the last stop's start plus one, so an overlapping
   * occurrence is still found. The route depends on the subject's bytes only. Linear: a position is a
   * candidate at most once. Billed once per call to the test work counter.
   *
   * \param[in]     text    The subject text.
   * \param[in]     pos     Index to start searching from.
   * \param[in]     literal The needle (>= 2 bytes).
   * \param[in]     rare    Offset of its rarest byte (\ref literal_rarest_offset), below `literal.size()`.
   * \param[in,out] density What this subject has shown so far.
   * \return The index of the first occurrence at or after \p pos, else \ref real::npos.
   */
  inline std::size_t find_literal_adaptive(std::string_view text,
                                           std::size_t      pos,
                                           std::string_view literal,
                                           std::size_t      rare,
                                           literal_density& density)
  {
    const std::size_t len {literal.size()};
    if (len > text.size() || pos > text.size() - len) {
      return npos;
    }
    note(counter::prefilter_work_units, text.size() - pos);
    // Inline: a dense subject's pair filter, and a sparse one's first stop (most searches end on it where
    // matches are dense). The rest is out of line.
#if defined(__ARM_NEON) || defined(__SSE2__)
    if (density.dense) {
      note(counter::literal_pair_scans);
      return simd_literal_scan(text, pos, literal);
    }
#endif
    {
      const std::size_t k     {density.rare != npos ? density.rare : rare};
      const std::size_t last  {text.size() - len};
#if defined(__ARM_NEON)
      const std::size_t hit   {simd_byte_scan(text.substr(0, last + k + 1), pos + k, static_cast<std::uint8_t>(literal[k]))};
#else
      const void* const found {std::memchr(text.data() + pos + k, literal[k], last - pos + 1)};
      const std::size_t hit   {found == nullptr ? npos : static_cast<std::size_t>(static_cast<const char*>(found) - text.data())};
#endif
      if (hit == npos) {
        return npos;
      }
      if (std::memcmp(text.data() + hit - k, literal.data(), len) == 0) {
        return hit - k;
      }
      pos = hit - k; // the stop failed: the rest finds it again and counts it
    }
    return find_literal_adaptive_rest(text, pos, literal, rare, density);
  }

  /*!
   * \brief Two probe bytes per branch of a literal alternation: the branch's first byte and one byte further
   *        in it, for the pair filter an alternation's block scan turns to once the first bytes prove common.
   *
   * Built from the program once per regex (not kept in the hints, which every search copies). `count` is 0 when
   * a branch starts with neither a byte nor a class, or the alternation has more branches than the arrays; a
   * nonzero `count` carries the pairs, the fingerprint, or both (\ref pairs, \ref nibbles).
   */
  struct alternation_pairs
  {
    std::uint8_t                                count       {}; //!< Branches planned; 0 means no plan.
    std::uint8_t                                max_d       {}; //!< Largest probe offset: a block reads up to this far past its 16 starts.
    std::array<std::uint8_t, 16>                lead        {}; //!< Each branch's first byte (valid with \ref pairs).
    std::array<std::uint8_t, 16>                probe       {}; //!< Each branch's second probe byte.
    std::array<std::uint8_t, 16>                delta       {}; //!< Its offset in the branch (at most 15; 0 probes the first byte twice).
    std::array<byte_splat, 16>                  lead_splat  {}; //!< \ref lead, each in all 16 lanes: a block loads rather than broadcasts it.
    std::array<byte_splat, 16>                  probe_splat {}; //!< \ref probe, likewise.
    bool                                        nibbles     {}; //!< \ref nibble_lo and \ref nibble_hi are valid: every branch is at least two bytes wide.
    bool                                        pairs       {}; //!< \ref lead, \ref probe and \ref delta are valid: every branch starts with a byte.
    std::array<std::array<std::uint8_t, 16>, 3> nibble_lo   {}; //!< Per fingerprint byte, low nibble to the bits of the buckets (branch index mod 8) it admits.
    std::array<std::array<std::uint8_t, 16>, 3> nibble_hi   {}; //!< The same for the high nibble.
  };

  /*!
   * \brief Whether an alternation's first bytes are dense in one subject, decided once from a sample of it.
   */
  struct alternation_density
  {
    bool decided {}; //!< The sample has run on this subject.
    bool dense   {}; //!< Its first bytes stop often enough that the pair filter takes the subject.
  };


  /*!
   * \brief Whether the nibble fingerprint can run here: AArch64 always, x86 when the build enables SSSE3 or, with
   *        gcc or clang, when the running CPU has it. A plan built elsewhere never claims one: the fingerprint's
   *        reach is shorter than the pairs', and a scan that bounded its blocks by it while masking by the pairs
   *        would read past the subject.
   * \return True when a plan may carry the fingerprint.
   */
  inline bool alternation_nibbles_supported()
  {
#if defined(__aarch64__) || defined(__SSSE3__)
    return true;
#elif defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__))
    return cpu_has_ssse3();
#else
    return false;
#endif
  }

  //! \brief Fewest branches for which the fingerprint replaces the pairs. Two pairs are two compares a block, a
  //!        fingerprint six table lookups: on x86 (SSSE3) `cat|dog` measured +13 % by the fingerprint and three
  //!        branches break even; AArch64's lookups are cheap enough that two branches already gain.
#if defined(__aarch64__)
  inline constexpr std::size_t alternation_nibbles_min_branches {2};
#else
  inline constexpr std::size_t alternation_nibbles_min_branches {3};
#endif

  inline constexpr std::size_t alternation_sample_min        {4096}; //!< Shorter rests are scanned by the first bytes, unsampled (at least the sample and its reach).
  inline constexpr std::size_t alternation_sample_bytes      {512};  //!< Bytes sampled for the first bytes' density.
  inline constexpr std::size_t alternation_wide_min_branches {2};    //!< Fewest branches the wide route considers: branches that open on classes outgrow the small set with a few.
  inline constexpr std::size_t alternation_wide_max_branches {16};   //!< Most branches the fingerprint's plan holds.
  inline constexpr std::size_t alternation_wide_false_budget {256};  //!< Past this many false candidates times branches in a sample, the automaton scans cheaper than the fingerprint and its verifier.

  //! \brief Mean bytes between first-byte hits in the sample below which the pair filter takes over (both
  //!        ISAs: where the first-byte scan and the pair filter crossed on 500 KB of log lines).
  inline constexpr std::size_t alternation_dense_gap {32};

  //! \brief The same threshold for a plan masking by the nibble fingerprint, whose cost per block is fixed: it
  //!        crossed the first-byte loop at 256-512 bytes between false stops (arm64, x86-64 AVX2, 3 to 10
  //!        branches); at 128 its worst ratio was 0.77 of the loop's time.
  inline constexpr std::size_t alternation_dense_gap_nibbles {128};

#if defined(__ARM_NEON) || defined(__SSE2__)
  /*!
   * \brief Mask of the 16 starts at \p at where some branch's two probe bytes both sit.
   *
   * Every branch's pair is a necessary condition of that branch, so a start no pair marks matches no branch;
   * the caller verifies the marked ones in branch order, as it does the first-byte candidates.
   * \param[in] at   The first of the 16 starts; `at + 15 + plan.max_d` must be inside the subject.
   * \param[in] plan The branches' probes.
   * \return The mask.
   */
  inline mask_t alternation_pair_mask(const char*              at,
                                      const alternation_pairs& plan)
  {
    return load_pairs_mask(at, plan.lead_splat.data(), plan.probe_splat.data(), plan.delta.data(), plan.count);
  }

  /*!
   * \brief The sample's block filter: the nibble fingerprint of each branch's first three bytes where
   *        \p nibbles (a cost per block that does not grow with the branches), else the byte pairs.
   *
   * Both mark a superset of the starts where some branch matches, and neither says which: the caller verifies
   * every marked start in branch order, so priority does not depend on how branches share a bucket.
   * \param[in] at      The first of the 16 starts; `plan.max_d` bytes past its 16 are read (two for the
   *                    fingerprint).
   * \param[in] plan    The branches' probes and fingerprint.
   * \param[in] nibbles Whether to take the fingerprint (`plan.nibbles`, and the seam not set).
   * \return The mask.
   */
  inline mask_t alternation_filter_mask(const char*              at,
                                        const alternation_pairs& plan,
                                        bool                     nibbles)
  {
#if defined(__aarch64__) || defined(__SSSE3__) || (defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__)))
    if (nibbles) {
      return load_nibble3_mask(at, plan.nibble_lo, plan.nibble_hi); // a call where it is built for SSSE3 alone
    }
#else
    static_cast<void>(nibbles);
#endif
    return alternation_pair_mask(at, plan);
  }

  /*!
   * \brief The first-byte mask of 16 bytes against \p cnt members, with the fewest unrolled compares that cover
   *        them: four, six or eight slots, the unused ones repeating a member.
   * \param[in] buf16 16 already-loaded bytes.
   * \param[in] mem   The members, padded to eight by repeating one.
   * \param[in] cnt   How many are distinct (1..8).
   * \return The mask.
   */
  inline mask_t load_members_padded_mask(const std::uint8_t * buf16,
                                         const std::uint8_t * mem,
                                         std::size_t          cnt)
  {
    if (cnt <= 4) {
      return load_members4_mask(buf16, mem);
    }
    if (cnt <= 6) {
      return load_members6_mask(buf16, mem);
    }
    return load_members8_mask(buf16, mem);
  }

  /*!
   * \brief What a sample of a subject shows an alternation's two filters stopping on.
   */
  struct alternation_sample
  {
    std::size_t first_bytes {}; //!< Starts the first-byte mask marks.
    std::size_t pairs       {}; //!< Starts the pair mask marks: the matches, and the false stops the pairs keep.
  };

  /*!
   * \brief Counts, over \ref alternation_sample_bytes bytes at \p at, the starts each filter would stop on.
   * \param[in] at      The sample's start; \ref alternation_sample_bytes + `plan.max_d` bytes (two for the
   *                    fingerprint) must follow it.
   * \param[in] mem     The first bytes, padded to eight by repeating one (the members compares read all of them).
   * \param[in] cnt     How many of \p mem are distinct members.
   * \param[in] plan    The branches' probe pairs.
   * \param[in] nibbles Whether the block filter is the fingerprint (\ref alternation_filter_mask).
   * \return Both counts.
   */
  inline alternation_sample alternation_sample_hits(const char*              at,
                                                    const std::uint8_t *     mem,
                                                    std::size_t              cnt,
                                                    const alternation_pairs& plan,
                                                    bool                     nibbles)
  {
    alternation_sample sample {};
    for (std::size_t off = 0; off < alternation_sample_bytes; off += 16) {
      std::array<std::uint8_t, 16> buf {};
      std::memcpy(buf.data(), at + off, 16); // MISRA-clean byte load (no pointer type-pun)
      for (mask_t mask {load_members_padded_mask(buf.data(), mem, cnt)}; !empty(mask); mask = clear_first(mask)) {
        ++sample.first_bytes;
      }
      for (mask_t mask {alternation_filter_mask(at + off, plan, nibbles)}; !empty(mask); mask = clear_first(mask)) {
        ++sample.pairs;
      }
    }
    return sample;
  }

#endif

  /*!
   * \brief Index of the first occurrence of \p literal in `text[pos..)`, or \ref real::npos.
   *
   * One byte is \ref find_byte; a longer literal takes \ref find_literal_adaptive with a density that lasts
   * this call (the engine's routes keep one per subject). No `memmem` (absent on MSVC).
   *
   * \param[in] text    The subject text.
   * \param[in] pos     Index to start searching from.
   * \param[in] literal The literal to locate.
   * \return The index of the first occurrence at or after \p pos, else npos.
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
    if (!std::is_constant_evaluated()) {
      literal_density density {};
      return find_literal_adaptive(text, pos, literal, literal_rarest_offset(literal), density);
    }
    const std::size_t last_start {text.size() - literal.size()}; // the oracle of the adaptive search
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
   * \brief First position >= \p pos where \p prefix occurs in \p text, or npos; the dispatch of
   *        \ref find_literal.
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
      // Billed once per call inside: an O(n) miss bills ~1x the size, a per-position restart ~N^2/2.
      if (prefix.size() >= 2U) {
        literal_density density {};
        return find_literal_adaptive(text, pos, prefix, literal_rarest_offset(prefix), density);
      }
      return find_byte(text, pos, prefix.front());
    }
    const auto off {text.substr(pos).find(prefix)};
    if (off == std::string_view::npos) {
      return npos;
    }
    return pos + off;
  }

  /*!
   * \brief Whether a consumer should ARM a filter on \ref find_members (one ISA only, measured).
   *
   * One pass over N members' union competes with N platform `memchr` calls: it wins where memchr is no
   * wider than this 128-bit loop and loses by a comparable factor where it is twice as wide, a structural
   * gap no tuning closes (a wider `mask_t` leg would). \ref find_members stays compiled everywhere; this
   * gates only the mechanisms built on it.
   */
#if defined(__ARM_NEON)
  inline constexpr bool have_members_scan {true};
#else
  inline constexpr bool have_members_scan {false};
#endif

  /*!
   * \brief Least index at or after \p pos whose byte is one of \p n members, in ONE pass.
   *
   * Unlike \ref find_bytes_cascade (one `memchr` per member: a six-byte union over 8 KB cost what the six
   * searches it replaced did), this tests all \p n members against sixteen bytes at a time.
   *
   * \param[in] text    The subject.
   * \param[in] pos     Index to start scanning from.
   * \param[in] mem     The member bytes, already in the mask load's layout (first \p n valid).
   * \param[in] n       How many members, 1..8.
   * \return The least index at or after \p pos whose byte is a member, else \ref npos.
   */
  inline std::size_t find_members(std::string_view                   text,
                                  std::size_t                        pos,
                                  const std::array<std::uint8_t, 8>& mem,
                                  std::uint8_t                       n)
  {
    if (pos >= text.size() || n == 0U) {
      return npos;
    }
    // The members arrive in the mask load's layout: a per-call copy into it was most of a +16 % on a
    // 55 ns early-hit baseline.
    const std::size_t cnt {n <= 8U ? static_cast<std::size_t>(n) : std::size_t {8}};
    const std::size_t sz  {text.size()};
    std::size_t       at  {pos};
#if defined(__ARM_NEON) || defined(__SSE2__)
    for (; at + 16 <= sz; at += 16) {
      std::array<std::uint8_t, 16> buf {};
      std::memcpy(buf.data(), text.data() + at, 16); // MISRA-clean byte load (no pointer type-pun)
      const mask_t mask                {load_members_mask(buf.data(), mem.data(), cnt)};
      if (!empty(mask)) {
        return at + first_lane(mask);
      }
    }
#endif
    for (; at < sz; ++at) { // scalar tail, and the whole loop on a build with no vector ISA
      const std::uint8_t byte {static_cast<std::uint8_t>(text[at])};
      for (std::size_t i = 0; i < cnt; ++i) {
        if (byte == mem[i]) {
          return at;
        }
      }
    }
    return npos;
  }

  /*!
   * \brief Where an ECMAScript line ends: the index of the first `\n` or `\r` in `text[pos..)`, or \ref real::npos.
   * \param[in] text The subject.
   * \param[in] pos  Index to start scanning from.
   * \return The least index at or after \p pos holding either byte, else npos.
   */
  constexpr std::size_t find_line_end_cr(std::string_view text,
                                         std::size_t      pos)
  {
    if (!std::is_constant_evaluated()) {
      constexpr std::array<std::uint8_t, 8> ends {'\n', '\r'};
      return find_members(text, pos, ends, 2U);
    }
    for (std::size_t i {pos}; i < text.size(); ++i) {
      if (text[i] == '\n' || text[i] == '\r') {
        return i;
      }
    }
    return npos;
  }

  /*!
   * \brief Index of the first byte in `text[pos..)` that belongs to a small first-byte set.
   *
   * A cascade of `std::memchr`, one per member, keeping the least hit and narrowing the window to
   * `[pos, best)` for the later members. Constant evaluation runs the plain member-wise scan.
   *
   * With two or more members the window gallops (doubling from a seed) rather than spanning the rest:
   * a caller running this per rejected candidate (next_candidate's icase small-set route) would otherwise
   * rescan the rest for a rare or absent member (`(?i)cafe`'s `C` in lowercase text), O(n^2). Galloping
   * bounds a call to ~2x the distance to the hit. One member cannot go quadratic (a memchr costs its
   * progress), and windowing it regressed x86 stop sets (`[^\x01]+`), so it takes one unbounded `memchr`.
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
        // Billed like the loop below: the distance to the hit, or the whole rest on a miss.
        const void* hit {std::memchr(base + pos, set[0], total)};
        if (hit != nullptr) {
          const std::size_t idx {static_cast<std::size_t>(static_cast<const char*>(hit) - base)};
          note(counter::prefilter_work_units, idx - pos);
          return idx;
        }
        note(counter::prefilter_work_units, total);
        return npos;
      }
      // The seed starts past next_candidate's 32-byte bitmap probe. Members ascend by byte value, so an icase
      // pair's rare uppercase byte is searched first, with the full window. Measured: the stop-set gain
      // saturates near 128 while the icase sparse/no-match cost keeps climbing (a few % at 1024).
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
        note(counter::prefilter_work_units, win); // this round's scanned width
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
   * SWAR: eight bytes per `memcpy`'d word (alignment- and aliasing-safe); the hit word is rechecked byte by
   * byte, which keeps it endianness-free.
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
