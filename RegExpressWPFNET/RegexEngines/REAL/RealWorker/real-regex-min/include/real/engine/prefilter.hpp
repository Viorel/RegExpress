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
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

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
   * \brief Prefilter work counter for the O(n) vs O(n²) smoke test.
   *        Always declared (clang-tidy / tests see the symbol). Billing is a no-op unless
   *        \c REAL_TEST_INSTRUMENT is defined on the test binary — wheel/prod pay nothing. Relaxed atomic,
   *        as the other counters: threads searching at once bill it concurrently.
   * \return A reference to the process-wide counter.
   */
  inline std::atomic<std::uint64_t>& prefilter_work_units() noexcept
  {
    static std::atomic<std::uint64_t> units {0};
    return units;
  }

  /*!
   * \brief Bill \p n scanned bytes to \ref prefilter_work_units. A no-op unless the test binary
   *        defines \c REAL_TEST_INSTRUMENT.
   * \param[in] n Bytes the caller just scanned.
   */
  inline void prefilter_note_scan(std::size_t n) noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    prefilter_work_units().fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
#else
    (void) n;
#endif
  }

  /*!
   * \brief Pike VM runs over a window the DFAs found, counted for the tests that pin which windows need no VM.
   *        Relaxed atomic: threads searching at once bill it concurrently.
   * \return A reference to the process-wide counter.
   */
  inline std::atomic<std::uint64_t>& vm_window_runs() noexcept
  {
    static std::atomic<std::uint64_t> runs {0};
    return runs;
  }

  /*!
   * \brief Bill one Pike VM run over a DFA window to \ref vm_window_runs. A no-op unless the test binary
   *        defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_vm_window() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    vm_window_runs().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Calls a batched walk made to its filler, counted for the tests that pin how often a walk scans.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& batch_fills() noexcept
  {
    static std::atomic<std::uint64_t> fills {0};
    return fills;
  }

  /*!
   * \brief Bill one filler call to \ref batch_fills. A no-op unless the test binary defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_batch_fill() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    batch_fills().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Bounded-backtracker runs, counted for the tests that pin which windows it fills rather than the VM.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& bounded_backtrack_runs() noexcept
  {
    static std::atomic<std::uint64_t> runs {0};
    return runs;
  }

  /*!
   * \brief Bill one bounded-backtracker run to \ref bounded_backtrack_runs. A no-op unless the test binary
   *        defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_bounded_backtrack() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    bounded_backtrack_runs().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Searches or confirms the lazy DFAs handed to the VM because a scan quit (a Unicode word boundary
   *        next to a non-ASCII byte, or a thrashing cache), counted for the tests that pin where no scan quits.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& dfa_quits() noexcept
  {
    static std::atomic<std::uint64_t> quits {0};
    return quits;
  }

  /*!
   * \brief Bill one quit scan to \ref dfa_quits. A no-op unless the test binary defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_dfa_quit() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    dfa_quits().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Literal searches the two-byte block filter answered (a dense subject), counted for the tests that
   *        pin when the adaptive literal search hands over.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& literal_pair_scans() noexcept
  {
    static std::atomic<std::uint64_t> scans {0};
    return scans;
  }

  /*!
   * \brief Bill one pair-filter search to \ref literal_pair_scans. A no-op unless the test binary defines
   *        \c REAL_TEST_INSTRUMENT.
   */
  inline void note_literal_pair_scan() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    literal_pair_scans().fetch_add(1, std::memory_order_relaxed);
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
   * \brief Alternation blocks the fingerprint masked 32 starts at a time (AVX2), counted for the tests that pin
   *        where the wider scan runs.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& alternation_avx2_blocks() noexcept
  {
    static std::atomic<std::uint64_t> blocks {0};
    return blocks;
  }

  /*!
   * \brief Literal filter searches that ran on 32-byte AVX2 blocks, counted for the tests that pin where the
   *        wider scan is taken.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& literal_avx2_scans() noexcept
  {
    static std::atomic<std::uint64_t> scans {0};
    return scans;
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
   * \brief Batches the fixed-shape filler produced, counted for the tests that pin which walks it serves.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& fixed_shape_batches() noexcept
  {
    static std::atomic<std::uint64_t> batches {0};
    return batches;
  }

  /*!
   * \brief Literal searches whose first stop failed and that went on out of line, counted for the tests that
   *        pin that a byte the subject showed rare is the one scanned first.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& literal_rest_scans() noexcept
  {
    static std::atomic<std::uint64_t> scans {0};
    return scans;
  }

  /*!
   * \brief Bill one out-of-line literal search to \ref literal_rest_scans. A no-op unless the test binary
   *        defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_literal_rest_scan() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    literal_rest_scans().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Alternation blocks the pair filter masked (a dense subject), counted for the tests that pin when an
   *        alternation's scan turns to it.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& alternation_pair_blocks() noexcept
  {
    static std::atomic<std::uint64_t> blocks {0};
    return blocks;
  }

  /*!
   * \brief Bill one pair-filtered alternation block to \ref alternation_pair_blocks. A no-op unless the test
   *        binary defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_alternation_pair_block() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    alternation_pair_blocks().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Alternation blocks the nibble fingerprint masked (among \ref alternation_pair_blocks), counted for the
   *        tests that pin when the fingerprint rather than the pairs does it.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& alternation_nibble_blocks() noexcept
  {
    static std::atomic<std::uint64_t> blocks {0};
    return blocks;
  }

  /*!
   * \brief Bill one fingerprint-masked block to \ref alternation_nibble_blocks. A no-op unless the test binary
   *        defines \c REAL_TEST_INSTRUMENT.
   * \param[in] nibbles Whether the block was masked by the fingerprint.
   */
  inline void note_alternation_nibble_block(bool nibbles) noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    if (nibbles) {
      alternation_nibble_blocks().fetch_add(1, std::memory_order_relaxed);
    }
#else
    static_cast<void>(nibbles);
#endif
  }

  /*!
   * \brief Branch walks the Aho-Corasick gate's completion sample spent, counted for the tests that pin its
   *        budget.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& ac_completion_walks() noexcept
  {
    static std::atomic<std::uint64_t> walks {0};
    return walks;
  }

  /*!
   * \brief Searches a program that is not a fixed alternation took its variants' fingerprint for, counted for
   *        the tests that pin when it does.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& alternation_variant_scans() noexcept
  {
    static std::atomic<std::uint64_t> scans {0};
    return scans;
  }

  /*!
   * \brief Subjects an alternation wider than the small set scanned by the fingerprint, counted for the tests
   *        that pin when it takes one.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& alternation_wide_scans() noexcept
  {
    static std::atomic<std::uint64_t> scans {0};
    return scans;
  }

  /*!
   * \brief Candidates the alternation pair filter left to verify, counted for the tests that pin that its second
   *        probe does filter.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& alternation_pair_candidates() noexcept
  {
    static std::atomic<std::uint64_t> candidates {0};
    return candidates;
  }

  /*!
   * \brief Bill one pair-filter candidate to \ref alternation_pair_candidates. A no-op unless the test binary
   *        defines \c REAL_TEST_INSTRUMENT.
   */
  inline void note_alternation_pair_candidate() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    alternation_pair_candidates().fetch_add(1, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Rows the unbounded-lookahead tables were filled with: one per position of each subject a table was
   *        built for, counted for the test that pins one pass per subject rather than one per position.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& ahead_table_rows() noexcept
  {
    static std::atomic<std::uint64_t> rows {0};
    return rows;
  }

  /*!
   * \brief Bill \p rows table rows to \ref ahead_table_rows. A no-op unless the test binary defines
   *        \c REAL_TEST_INSTRUMENT.
   * \param[in] rows The rows one fill wrote.
   */
  inline void note_ahead_table_rows([[maybe_unused]] std::size_t rows) noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    ahead_table_rows().fetch_add(rows, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Bytes the lookbehind walks stepped, counted for the test that pins one step per byte and search
   *        whatever the lookbehind's bound, rather than one window per start.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& behind_walk_steps() noexcept
  {
    static std::atomic<std::uint64_t> steps {0};
    return steps;
  }

  /*!
   * \brief Bill \p steps walk steps to \ref behind_walk_steps. A no-op unless the test binary defines
   *        \c REAL_TEST_INSTRUMENT.
   * \param[in] steps The bytes one query stepped.
   */
  inline void note_behind_walk_steps([[maybe_unused]] std::size_t steps) noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    behind_walk_steps().fetch_add(steps, std::memory_order_relaxed);
#endif
  }

  /*!
   * \brief Batches the lazy-DFA span filler produced, counted for the tests that pin which walks it serves.
   * \return A reference to the process-wide counter (relaxed atomic, as \ref vm_window_runs).
   */
  inline std::atomic<std::uint64_t>& dfa_span_batches() noexcept
  {
    static std::atomic<std::uint64_t> batches {0};
    return batches;
  }

  /*!
   * \brief Bill one lazy-DFA span batch to \ref dfa_span_batches. A no-op unless the test binary defines
   *        \c REAL_TEST_INSTRUMENT.
   */
  inline void note_dfa_span_batch() noexcept
  {
#if defined(REAL_TEST_INSTRUMENT)
    dfa_span_batches().fetch_add(1, std::memory_order_relaxed);
#endif
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
   * \brief Peel an optional `\b`/`\B` assertion at \p p.
   *
   * If \p p is not an assert, leaves \p hint at 0 and returns true. If it is a word-boundary assert,
   * records the hint, advances \p p, returns true. If it is any other assert, returns false (shape
   * disqualified for wb-wrapping fast paths).
   *
   * **Lead and trail are the same operation.** This was two functions, `peel_optional_lead_wb` and
   * `peel_optional_trail_wb`, byte-identical but for the out-parameter's name — nothing here looks at
   * WHERE \p p points, so the position is entirely the caller's. Their doc comments already said "same
   * contract as", and the risk was not the duplicate lines but a fix landing on one of two copies that
   * both feed load-bearing hint decisions.
   *
   * \param[in]     code Instruction stream.
   * \param[in,out] p    Program counter (advanced past the assert when peeled).
   * \param[out]    hint Hint 0/1/2 — see \ref wb_hint_of.
   * \return false if a non-wb assert blocks the shape.
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
   * \brief detect_fast_shapes's outer envelope: `save 0`, optional lead `\b`/`\B`. No-ops safely on a
   *        shape with no `\b`/`\B` support (e.g. a literal `byte` right after `save 0`).
   */
  struct shape_lead
  {
    std::size_t  body_start     {}; //!< pc where the shape-specific body begins.
    std::uint8_t wb_lead        {}; //!< 0/1/2, see \ref wb_hint_of.
    //! \brief A leading `\A`/`^` (NOT multiline `^`) was peeled — the shape may only match at 0.
    //!
    //! Reported rather than rejected, but every recognizer that does not itself honour the anchor
    //! MUST refuse the shape when this is set -- arming a fast path that scans forward for a pattern
    //! pinned to position 0 would return matches the program forbids. Only the class-loop recognizer
    //! accepts it today, and the route it arms reads \ref pattern_hints::anchored_start.
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
    // A leading `\A`/`^` is PEELED and reported, not rejected. It pins the match to position 0, which
    // is a mode rather than a shape: `^X` in search mode and `X` in prefix mode are the same question,
    // and measured on 100 KB the second is 81x faster because only the second reaches the class loop.
    // Multiline `^` (line_start) is a different assertion and stays disqualifying.
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
   * \brief The \ref shape_lead counterpart: optional trail `\b`/`\B` at \p from, then exactly
   *        `save 1`, `match` at the very end of \p code. \p from (the body's own end) is the caller's
   *        to supply -- only its shape-specific body walk knows where that is.
   */
  struct shape_close
  {
    std::uint8_t wb_trail   {}; //!< 0/1/2, see \ref wb_hint_of.
    //! \brief A trailing end anchor was peeled — 0 none, 1 `\Z` (strict end), 2 `$` (end, or just
    //!        before ONE final newline -- Python semantics, and the reason `^X$` is NOT `fullmatch(X)`).
    //!
    //! Same contract as the lead anchor above — reported, not rejected, and every recognizer
    //! that does not itself honour it MUST refuse the shape.
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
    // Peeled inline rather than through peel_optional_wb, which REFUSES any assertion that is
    // not a word boundary -- so calling it first made the end-anchor peel below unreachable. The
    // compiler emits the trail in this order (`X+\b$` -> klass, split, assert(\b), assert($)), and
    // anything else that lands here simply fails the `save 1`/`match` check at the end.
    std::uint8_t wb_trail {0};
    if (from < code.size() && code[from].op == opcode::assert_position
        && is_word_boundary_kind(static_cast<assert_kind>(code[from].arg8))) {
      wb_trail = wb_hint_of(static_cast<assert_kind>(code[from].arg8));
      ++from;
    }
    // A trailing `\Z`/`$` pins where the match ENDS, which is a limit rather than a shape. Peeled and
    // reported here for the same reason the lead anchor is: it lets the shape routes keep the pattern
    // and honour the limit themselves. Multiline `$` (line_end) is a different assertion, is not
    // peeled, and therefore still disqualifies the shape.
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
   * Compares the ASCII bitmap to \ref is_ascii_word_byte and the non-ASCII range slice to
   * \ref word_ranges (generated, same table the compiler uses for `\w`). A threshold on
   * \c range_count alone is unsound: `[\w😀]` has the `\w` ASCII half plus one extra range
   * and would pass `>= 200`, but `\b[\w😀]+\b` is NOT equivalent to `[\w😀]+`.
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
   * \brief The DROP rule: `\b` next to a full-`\w` MAXIMAL run is redundant (`\B` never is).
   *        Only sound when the match is a greedy `+` run: a maximal run of `\w` can only ever
   *        START where the character before it is non-word (or absent) -- that IS `\b` (or the
   *        text edge), so checking it again is redundant. A SINGLE code point (no `+`) has no
   *        such guarantee: `\b\w` may legally start mid-run (any word code point qualifies as a
   *        candidate start), so dropping the boundary there is unsound, not just conservative.
   *        The caller is responsible for only calling this when \p lead / \p trail came from a
   *        provably maximal-run shape (see \ref resolve_class_wb_hints's \p maximal_run).
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
   * - Full word + `\b` on a maximal (`+`) run: drop the boundaries (the DROP rule); see \ref
   *   wb_redundant_for_full_word.
   * - Proper word subset + `\b`, or full word + `\b` on a single atom: keep the wrap (the WRAP rule).
   * - `\B` on a **maximal** run: **unarm** — the runner skips whole class runs on a failed lead
   *   check, but `\B` legitimately starts *mid-run* (`\B\w+` on "hello" → "ello"). That skip is
   *   unsound; stay on the general VM (nothing here invents a mid-run scanner).
   * - `\B` on a **single** atom (`\B\w`, `\B\d`, …): keep the wrap — each candidate is one
   *   code point, so a failed lead check advances one atom and mid-run hits are found.
   * - Non-word-subset class under any wb: unarm (superset maximal-run is unsound).
   * - Bare (no wb): arm with zero wb hints.
   *
   * \param[in]  full_word   Exact `\w` class (ASCII or Unicode table identity).
   * \param[in]  word_sub    Non-empty subset of `\w`.
   * \param[in]  maximal_run Whether the class loop is a greedy `+` (a maximal run, so any valid
   *                         start already sits at a word/non-word transition) rather than a
   *                         single code point (which may start anywhere inside a word run, where
   *                         the DROP rule's redundancy argument does not hold).
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
    // `\B` + maximal-run scanner is unsound (mid-run starts); unarm. Single-atom `\B` is fine.
    if ((lead == 2 || trail == 2) && maximal_run) {
      return false;
    }
    const bool has_wb {lead != 0 || trail != 0};
    if (has_wb && !full_word && !word_sub) {
      return false;
    }
    // Keep the wrap for subset/`\B`/single-atom `\b`; the DROP rule applies only to maximal full-word + `\b`.
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
   * \ref word_ranges is sorted and disjoint, so a caller testing intervals in ascending order never
   * needs to look at a range it has already passed — that is what \p cursor carries. Without it the
   * scan restarts at index 0 on every step, and an interval spanning the whole word set then costs
   * O(word_ranges_size^2): for `\w` (whose class IS the word set) that is 771^2 steps, measured at
   * 1.79M instructions and 95 % of the cost of compiling `\b\w+\b`. With the cursor the whole subset
   * test is one merge of two sorted lists.
   *
   * \p cursor is a hint, never a precondition: if a range before it could still cover \p lo — a
   * caller passing intervals out of order — the scan rewinds. So the answer never depends on the
   * order the caller happens to use, only the speed does.
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
   * Supersets like `[\w😀]` must NOT take the WRAP rule: a maximal class run can start on a non-word member
   * and skip over a later word-bounded sub-run.
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
    // One cursor for the whole class: a cp_class's ranges are sorted and disjoint (the same property
    // pike_vm::cp_page_table relies on to stop early), so this is a single merge of two sorted lists
    // rather than one full-word-set scan per class range. Out-of-order ranges would only cost the
    // rewind inside the callee, never a wrong answer.
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
   * \brief safety check: true if the ASCII byte \p b could be a member of code-point
   *        class \p cc — used only to test whether a single-byte delimiter (a "quoted"-shape prefix or
   *        suffix) could hide inside a `klass_cp_loop_possessive` body, in which case the delimited
   *        fast path must decline (see \ref pattern_hints::possessive_prefix). A non-ASCII \p b (>=
   *        0x80) is conservatively treated as a member (unsafe, declines) — this shape's corpus is
   *        single-byte ASCII delimiters (`"`, `;`, …), so a multi-byte delimiter simply stays general.
   * \param[in] cc The loop body's code-point class.
   * \param[in] b  The candidate delimiter byte.
   * \return True when \p b could be a member — including for any \p b >= 0x80, conservatively.
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
   * \param[out] out_branch_count Optional; receives the branch count (already tracked internally
   *             to enforce the ">= 2 branches" rule below) -- lets a caller pick a runtime STRATEGY
   *             (e.g. Aho-Corasick past a literal-count threshold) without re-walking the split
   *             chain a second time. Does not change eligibility: still requires >= 2 branches.
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
    // Optional lead \b/\B immediately after save 0.
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
   * The prefix is the consecutive leading byte instructions (saves and assertions do not
   * consume, so they are crossed: every match still has to begin with the collected bytes;
   * hints only ever filter candidate positions, the engine verifies). The exact-literal hint
   * fires when those bytes ARE the whole match — no assertion appears after the first byte up
   * to `match` (only saves may be crossed). Trailing/inter assertions ($, \b after, …) are
   * post-filters that must go through the normal VM; leading assertions are fine.
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
      // Leading asserts were crossed above. Allow only word-boundary asserts after the last
      // prefix byte (a kept wrap: `\bLIT\b` / `LIT\b`); any other trailing/inter assert stays on the VM.
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
   * Assertions are crossed conservatively (they constrain positions, not bytes; a lookaround
   * yields a sound SUPERSET so ⑤ never wrongly rejects a valid start). If `match` is reachable
   * without consuming, an empty match is possible and no byte-based skipping is sound.
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
   *        an alternation of straight-line branches, and trailing-lookaround class+.
   * \param[in]     code           The instruction stream.
   * \param[in]     classes        Interned character classes referenced by \p code.
   * \param[in]     cp_classes     Match-time code-point classes (for `\w`/`\d`/`\s` word-class tests).
   * \param[in]     cp_ranges      Flat range buffer the \p cp_classes slices index into.
   * \param[in]     cp_mark_ascii  ASCII sub-class index of an emitted codepoint-class block (-1 = none).
   * \param[in]     cp_mark_offset Program offset where that block starts (-1 = none).
   * \param[in]     cp_mark_end    Program offset right after that block ends (-1 = none) -- the
   *                               block's instruction count is not fixed, so this locates its end.
   * \param[in]     lookarounds    Bounded lookaround subs (for trailing-LA eligibility); may be empty.
   * \param[in,out] hints          Hint bag to fill (class-loop, fixed-shape, trailing-LA, …).
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold)) // build-time only: see the note in prefilter.hpp's detect_fast_shapes
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
    // "class+" shape: save 0, [optional \b/\B,] [group-start save,] klass{k}, split(back, exit),
    // [group-end save,] [optional \b/\B,] save 1, match. word-boundary handling via peel + resolve_class_wb_hints.
    // R3: the outer envelope (open/close) is \ref parse_shape_lead / \ref parse_shape_close.
    // `klass{k}` (k >= 1 consecutive copies of the SAME class) generalizes the
    // original single-`klass` shape -- `X{k,}` desugars to k-1 mandatory copies then a k-th copy
    // that doubles as the loop body (compiler.hpp's emit_repeat), so k identical `klass` ops
    // followed by a self-loop split is the bytecode signature of `X{k,}` (k==1 is the original
    // bare `X+`). A literal run like `\w\w\w+` desugars to the SAME bytecode as `\w{3,}` and is
    // correctly recognized identically -- same matching semantics, same fast path.
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
            // A `\b`/`\B` wrap and an end anchor together are REFUSED, never combined: the wrap takes its
            // own branch in these routes (the WRAP rule) and that branch never sees an end limit, while the
            // assertion has already been peeled out of the program -- so nothing downstream could
            // re-derive it. Both were measured as real divergences: `[a-z]+\b$` (trail) against the
            // seam, and `\b(?>\w)$` (LEAD) against Python's re, by the binding's differential fuzz.
            if (ok && close.ok
                && (close.end_anchor == 0 || (lead.wb_lead == 0 && close.wb_trail == 0))
                && cls >= 0 && static_cast<std::size_t>(cls) < classes.size()
                && k <= 65535) {
              const char_class& cc        {classes[static_cast<std::size_t>(cls)]};
              std::uint8_t      out_lead  {0};
              std::uint8_t      out_trail {0};
              // This shape structurally requires the split/loop matched above -- always a maximal
              // `+`-family run, never a single code point -- so the DROP rule's redundancy argument always
              // applies regardless of k.
              if (resolve_class_wb_hints(is_full_ascii_word_class(cc), is_ascii_word_subset_class(cc),
                                         /*maximal_run=*/ true, lead.wb_lead, close.wb_trail, out_lead,
                                         out_trail)) {
                // A `\b`/`\B` wrap and an end anchor together are REFUSED, not combined: the wrap takes
                // its own branch in the route (the WRAP rule), that branch never sees the limit, and the
                // assertion has already been peeled out of the program -- so nothing downstream could
                // re-derive it. Measured as a real divergence on `[a-z]+\b$` before this guard.
                if (close.end_anchor != 0 && (lead.wb_lead != 0 || close.wb_trail != 0)) {
                  return; // see the note above: the wrap branch cannot honour a peeled limit
                }
                hints.greedy_class_loop     = cls;
                hints.greedy_class_loop_end = close.end_anchor;
                hints.greedy_class_loop_min = static_cast<std::uint16_t>(k);
                hints.greedy_group_start    = gs;
                hints.greedy_group_end      = ge;
                hints.wb_lead               = out_lead;
                hints.wb_trail              = out_trail;
                // the DROP rule removed a genuine leading \b (wb_lead was 1, out_lead came back 0): the
                // runner's search-mode fast path needs the start>0 window-edge guard -- see
                // pattern_hints::wb_lead_maximal_run's own doc comment for the full argument.
                hints.wb_lead_maximal_run = (lead.wb_lead == 1 && out_lead == 0);
              }
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

    // Code-point class (klass_cp + three klass continuations){k}, optional greedy `+` (a self-loop
    // of the LAST block), optional `\b`/`\B` wraps, optional one capturing group. Unicode
    // `\w+` / `\d+` / `\s+` / `\w{k,}` via peel + resolve. R3: the outer envelope (open/close) is
    // \ref parse_shape_lead / \ref parse_shape_close.
    // k >= 1 consecutive copies of the IDENTICAL 4-instruction klass_cp block --
    // `\w{k,}` desugars to k-1 mandatory copies then a k-th copy that doubles as the loop body
    // (compiler.hpp's emit_repeat). intern_cp_class/intern_class content-based dedup (compiler.hpp)
    // guarantees repeated blocks are byte-identical (same cp_idx, same 3 continuation class
    // indices) -- verified explicitly below rather than assumed, so a future emitter change that
    // broke the guarantee would just decline this shape, never misrecognize it.
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
          // Three shapes reach the route now. `X` and `X+` (k == 1) and `X{k,}` (k copies then a
          // self-loop) are unbounded above and leave the max at 0. `X{k}` -- k copies and NO self-loop --
          // is bounded at k, which is the whole reason it can be accepted: the route extends greedily and
          // used to bound the result only from below, and an exact count needs it stopped from above,
          // since `\w{8}` over a nine-letter word matches the first eight and not the nine.
          const std::uint16_t cp_max {plus || k == 1 ? std::uint16_t {0} : static_cast<std::uint16_t>(k)};
          // A counted run declines a word boundary. Both retry loops in run_cp_class_loop advance by the
          // whole run when a candidate fails, which is right for a MAXIMAL run -- its end IS the boundary,
          // so no shorter start inside it can end on one -- and wrong for a bounded one: `\w{4}\b` over
          // "abcdefghi" fails at 0 and matches at 5, and skipping to 4 loses it. Caught by differencing
          // this route against the general VM, which is the only reason the shape is narrowed here rather
          // than shipped wrong; lifting it means teaching those loops to step by one code point.
          const bool counted_wb {cp_max != 0 && (lead.wb_lead != 0 || close.wb_trail != 0)};
          // A counted run declines an END ANCHOR for the same reason, and it is the same defect one
          // assertion over: the retry loops skip past a maximal run because "a maximal run that stops
          // short of the limit can never be the match", which holds for `+` and `{k,}` -- their run IS
          // maximal, so no later start inside it can succeed -- and fails for `{k}`, whose run is
          // bounded from above. `\w{2}\Z` over "xab" fails at 0, would match at 1, and the skip lands
          // on 2: only starts at a multiple of the width are ever tried, so `\w{3}\Z` succeeds iff
          // `(len - 3) % 3 == 0`.
          //
          // Refused rather than taught to step by one code point: that lift is the same one
          // `counted_wb` names and defers, and both loops would need it. `{k,}` and `+` keep the route
          // because they leave `cp_max` at 0.
          const bool counted_end {cp_max != 0 && close.end_anchor != 0};
          // A `\b`/`\B` wrap and an end anchor together are REFUSED, never combined: the wrap takes its
          // own branch in these routes (the WRAP rule) and that branch never sees an end limit, while the
          // assertion has already been peeled out of the program -- so nothing downstream could
          // re-derive it. Both were measured as real divergences: `[a-z]+\b$` (trail) against the
          // seam, and `\b(?>\w)$` (LEAD) against Python's re, by the binding's differential fuzz.
          if (ok && close.ok
              && (close.end_anchor == 0 || (lead.wb_lead == 0 && close.wb_trail == 0))
              && !counted_wb && !counted_end && cp_idx >= 0
              && static_cast<std::size_t>(cp_idx) < cp_classes.size() && k <= 65535) {
            const bool has_wb {lead.wb_lead != 0 || close.wb_trail != 0};
            // Bare path: no Unicode table walk (keeps constexpr light for static_regex).
            if (!has_wb) {
              // A `\b`/`\B` wrap and an end anchor together are REFUSED, not combined: the wrap takes
              // its own branch in the route (the WRAP rule), that branch never sees the limit, and the
              // assertion has already been peeled out of the program -- so nothing downstream could
              // re-derive it. Measured as a real divergence on `[a-z]+\b$` before this guard.
              if (close.end_anchor != 0 && (lead.wb_lead != 0 || close.wb_trail != 0)) {
                return; // see the note above: the wrap branch cannot honour a peeled limit
              }
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
              // Unlike the ASCII class+ shape above, `plus` here is genuinely optional (this
              // recognizer accepts both `\b\w+` and bare `\b\w`) -- the DROP rule's redundancy argument
              // only holds for the former, so it must gate on the ACTUAL shape, not assume it.
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
          // A bare single byte-class (`[a-z]`, `[aeiou]`) -- the batchable sub-case of the shape just
          // armed. The test is the whole program, not a property of it: exactly `save 0`, `klass`,
          // `save 1`, `match`. That excludes a capture wrap (`([a-z])`, 6 ops), a `\b` wrap, an anchor
          // and a single literal byte (`byte`, which takes exact_literal). See
          // pattern_hints::single_class for why this is its own field rather than a flag on
          // greedy_class_loop.
          // `code.size() == 4` already implies slot_count 2: any inner capturing group contributes its
          // own pair of saves, which would push the program past four instructions.
          if (code.size() == 4 && wb_lead == 0 && wb_trail == 0 && body_pc == 1
              && code[1].op == opcode::klass) {
            hints.single_class = code[1].arg16;
          }
        }
      }

      // SIMD verify eligibility: the run above qualifies for a vectorized scan+verify (pike.hpp
      // run_fixed_shape) only when it is also HOMOGENEOUS -- every byte/klass position accepts the
      // identical set, itself <= 2 contiguous ranges -- because the sound "skip to the first failing
      // lane" only holds when a mismatch at any position rules out every position (same required set
      // everywhere). Mixed shapes ((\d{4})-(\d{2})-(\d{2})) stay on the scalar walk. Lead/trail `\b`
      // are zero-width and do not affect homogeneity of the consuming run.
      if (hints.fixed_shape) {
        // Collect every position's accepted set ONCE, then decide: homogeneous (the fused scan+verify
        // below) or, failing that, the two-position pair filter (\ref pattern_hints::fs_pair_width).
        // The walk used to `break` the moment homogeneity died, which is why a mixed shape got no
        // vector help at all; it now records and keeps going.
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
        // A PEELED ANCHOR REFUSES THE PAIR PREFILTER, and the reason is a contract that stops holding.
        // `fixed_shape_pair` documents itself as transparent -- "it only FILTERS candidates; the same
        // match_fixed_body_wb verify decides every one of them" -- and that is true while the shape may
        // start anywhere. It stops being true the moment `^`/`\A` or `\Z`/`$` is peeled out of the
        // program: the filter does not read `anchored_start` or `fs_end_anchor`, so it happily returns a
        // candidate the peeled assertion forbids. Measured: `^[0-9]{4}-[0-9]{2}-[0-9]{2}$` on
        // "2026-08-10_11:43:27" reported [0,10) through this route where the general VM finds no match --
        // 3 of 253 differential cases, all of them the shape whose `fs_pair_width` was armed.
        //
        // Refusing costs nothing that matters: the prefilter exists to SKIP candidate positions, and an
        // anchored shape has exactly ONE candidate. There is nothing left to skip. `fixed_shape` itself
        // stays armed and honours both anchors in its own gate.
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
            // Pick the two most selective positions: smallest accepted-set cardinality, ties broken
            // toward the widest separation so the two probes decorrelate (adjacent bytes of real text
            // correlate; distant ones much less). O(len^2) over len <= 16, at compile time.
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
      // emitted the block (emit_any_codepoint_class) — we never reverse-engineer
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
        // Only set wb_* here if exact_literal / fixed_shape did not already claim them
        // (a pure literal alternation is rare; prefer not clobbering an earlier path).
        if (!hints.fixed_shape && hints.exact_literal_len == 0) {
          hints.wb_lead  = wb_lead;
          hints.wb_trail = wb_trail;
          hints.body_pc  = body_pc;
        }
      }
    }

    // possessive class+/cp-class+ loop -- UNBOUNDED only (X*+/X++, self-loop via
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
              // A literal byte has no "word class" to resolve DROP eligibility against --
              // the wb-wrapped byte-possessive shape (`\ba++\b`) stays on the general VM, documented
              // rather than silently dropped; the bare/suffixed shape (`a++`, `a++x`) still arms
              // (arm starts true whenever there is no wb at all, regardless of kind).
              bool         arm       {!has_wb};
              std::uint8_t out_lead  {0};
              std::uint8_t out_trail {0};
              if (has_wb && loop_kind != class_kind::byte) {
                // Unbounded possessive: always a maximal run wherever it starts (no upper bound to cut
                // it short at different lengths for different starts), so the DROP rule's redundancy argument --
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
              // A BARE unbounded possessive byte-CLASS loop (`[a-z]++`, `(?>[a-z]+)`) is redirected to
              // the GREEDY class-loop selector instead of arming a possessive one, because it is the
              // same language: possessive means "take the maximal run and never give it back", and
              // with nothing after the loop there is nothing to give back to. Verified by match count
              // rather than by argument -- `[a-z]+` and `[a-z]++` both report 4 on "abc,de f 42 ghij".
              //
              // The gain is that the greedy selector is BATCHED, and the possessive one is not: without
              // the redirect, a quantifier doing strictly LESS work is several times slower than the
              // greedy form it is equivalent to. Doing it HERE, in the recognizer, is what makes it
              // affordable: three attempts to reach the same result from the runtime side -- teaching
              // the fillers a class-index parameter, with and without the code-point half -- each cost
              // the Unicode class rows 5.7 to 9.1 % (`\p{L}+`, `\p{N}+`, `\w+`, all 16 draws of 16
              // against their floors, benchmarks/bench_layout.py). This translation unit is at its
              // inlining budget; a hint set at compile time spends none of it.
              //
              // Scope is deliberately the byte class only, and bare only. `X*+` can match empty (the
              // guard above already requires has_mandatory here for a suffix-free shape). A suffix
              // (`[a-z]++x`) means the match is not the run. An enveloping capture keeps its slots in
              // possessive_group_start, which the greedy path does not read. A `\b` wrap is excluded
              // upstream (`arm` is false). And the code-point kind is left alone: its own route is not
              // batched for `{k,}` either, for the same budget reason.
              const bool redirect {arm && suffix_len == 0 && gs < 0 && has_mandatory && !has_wb
                                   && (loop_ref.kind == class_kind::klass
                                       || loop_ref.kind == class_kind::klass_cp)};
              if (redirect && loop_ref.kind == class_kind::klass) {
                hints.greedy_class_loop     = loop_ref.index;
                hints.greedy_class_loop_min = 1;
                hints.greedy_class_loop_end = 0;
              }
              else if (redirect) {
                // The CODE-POINT twin, and it is here because the first version of this redirect left
                // it out on a reason that does not survive reading: "its own route is not batched
                // either". \ref pattern_hints::greedy_cp_class IS batched -- it is what `\w+` takes, with
                // zero route entries per match. What was actually costly was teaching the cp FILLER a new
                // parameter, which charges every code-point-class pattern; a hint decided here pays none
                // of it.
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

    // possessive delimited ("quoted") shape -- literal PREFIX (1+ bytes) + possessive
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
    // UTF-8 bytes are NOT rare, and ranking them at 1 picked the single most common byte of an
    // accented corpus as the memchr target: `café`'s rarest-ranked byte was the 0xC3 that leads EVERY
    // Latin-1 letter, one per few bytes of French prose. Against an ASCII literal of the same match
    // density in the same corpus that costs several times the scan.
    //
    // The ranking that matters is not corpus frequency but SELECTIVITY: a lead byte stands for 64 or
    // more distinct characters, so it can never discriminate like one ASCII byte. These values put
    // the Latin-1 leads and every continuation byte above the absolute threshold below, so they are
    // no longer chosen at all, while the rarer leads stay eligible for scripts whose text is made of
    // nothing else (CJK picks its lead exactly as before).
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
   * Unlike \ref extract_rare_byte, the disc need not sit at a *fixed* match offset (the optional
   * changes it). Search memchr's the disc and back-verifies the optional shape — never memmem.
   * Supersedes a weak literal-prefix scan when the disc is several times rarer than the first byte.
   * Always sound: only filters candidates; the VM confirms.
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
    // Discriminant = rarest byte in the fixed run *after* the optional site (or whole run if no opt).
    // For `http` + s? + `://`, that is among `://`. For pure `https://`, among the whole string.
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
    // Prefix before the disc: either fixed[0..best_idx) with no opt, or fixed[0..opt_at) with opt
    // between prefix and disc (disc must be the first mid byte after opt for the simple shape).
    std::uint8_t prefix_len {};
    if (saw_opt) {
      // Require disc immediately after the optional site in the fixed mid (URL `://` after `s?`).
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
   * \brief The STRUCTURAL half of \ref pattern_hints::capture_free_walk -- `save 0` is the program's first
   *        instruction.
   *
   * Split out because the two halves have different owners. This half is a property of the PROGRAM and is
   * never negotiable: the capture-free walk keeps group 0's start in one `std::size_t` local shared by a
   * whole epsilon closure, which is only correct while `save 0` cannot be skipped. Behind a split, a branch
   * that bypassed it would inherit its sibling's start — a wrong ANSWER, not a slow one. The other half
   * (no `save` past slot 1, and `slot_count == 2`) is a property of what the CALLER WANTS: it says the
   * pattern has no user groups, so ignoring their writes costs nothing. A caller that does not read
   * captures — \ref real::basic_regex::count_matches — may set the flag on its own view of the program on
   * this condition alone.
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

    // Derived by SCAN, not from the group count: any `save` past slot 1 means a thread carries capture
    // positions someone asked for, and the epsilon walk must keep its refcounted block. See
    // \ref pattern_hints::capture_free_walk for what the negative case licenses.
    // Two conditions, and the SECOND is the one the walk's single scalar rests on: `save 0` must be the
    // program's first instruction. If it sat behind a split, a branch that skipped it would inherit its
    // sibling's start -- a wrong ANSWER. Every other `save` must write slot 1, which capture-free ignores
    // because the end IS `pos` at the match. Lookaround sub-programs are regions of this same `code` and
    // emit no saves of their own (checked: `a(?=b)` and `a(?<=b)c` both carry exactly [pc=0 slot=0] and
    // [pc=n-2 slot=1]), so a bounded lookaround does not disqualify a pattern here.
    hints.capture_free_walk = capture_free_walk_structural(code);
    for (std::size_t i = 1; hints.capture_free_walk && i < code.size(); ++i) {
      if (code[i].op == opcode::save && code[i].arg16 != 1U) {
        hints.capture_free_walk = false;
      }
    }

    // A lookaround forces the general Pike VM: no DFA, no pure class-loop — EXCEPT the measured
    // trailing-LA class+ shape, which arms trailing_lookaround + trailing_la_class (not
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
      hints.fs_pair_width         = 0;    // paired with fixed_shape: never read without it
      hints.single_class          = -1;   // likewise paired with fixed_shape
      hints.codepoint_class_ascii = -1;
      hints.fixed_alternation     = false;
    }

    if (hints.prefix_size > 0) {
      hints.single_first = static_cast<unsigned char>(hints.prefix[0]);
    }
    else if (hints.first_bytes_valid) {
      // Enumerate the set, stopping once it exceeds eight -- the recognizer's own cap now matches
      // run_alternation's L-SIMD masked-block scan (pike.hpp), which has always gated on
      // small_set_size <= 8; only this enumeration cap was left at 4 (the alternation gap:
      // a 5-8-distinct-first-byte pattern like `cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog`
      // fell all the way to the bitmap loop, un-accelerated). A single member drives find_byte (one
      // memchr); two-to-eight members drive the memchr-cascade/SIMD scan (small_set); nine or more
      // stay on the bitmap loop.
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

    // For a whole-pattern `class+` run (run_class_loop — a byte-wise scan), record the STOP
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
    // A whole-pattern code-point-class run (`.`/`[^x]` in text/ascii mode) accepts EVERY valid
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
    if (hints.prefix_size >= 2) {
      hints.prefix_rare = literal_rarest_offset(std::string_view {hints.prefix.data(), hints.prefix_size});
    }
    // Rare discriminant past an optional mono-byte (URL `https?://`): memchr the disc, back-verify
    // prefix+opt+after. Preferable to a weak literal prefix (`http`) when the disc is rarer.
    extract_rare_discriminant(code, hints);

    // Veto the heterogeneous fixed-shape pair filter when the ordinary prefilter already has a
    // single-byte `memchr` for this pattern -- a required rare byte at a fixed offset (\ref
    // pattern_hints::rare_byte) or a unique first byte (\ref pattern_hints::single_first). Placed here,
    // not in detect_fast_shapes, because neither hint exists yet at that point (both are computed
    // below/above this line, after the shape walk).
    //
    // This is a measured veto, and the reason it is SEMANTIC rather than an ISA gate. A pattern like
    // `[0-9]{2}:[0-9]{2}` carries `rare_byte = ':'`, so next_candidate memchrs it -- and where the
    // platform's memchr is wider than this filter's 128-bit block, the pair path loses to it, while on the
    // other ISA it wins. The literal filter met the same trap while it ran on NEON alone, until it learned
    // to scan the rarest byte first. Here the discriminator is not the ISA either: it is whether a single
    // byte suffices at all. An icase
    // literal has no single-byte position (`(?i)cafe` is four 2-sets, rare_byte and single_first both -1),
    // so nothing memchrs it and the pair filter wins on BOTH ISAs. Vetoing on the hint keeps that win
    // everywhere instead of surrendering one ISA to a gate.
    if (hints.rare_byte >= 0 || hints.single_first >= 0) {
      hints.fs_pair_width = 0;
    }

    // Fold the exact-literal one-search decision, LAST: it reads anchored_start (extract_anchoring),
    // prefix_size/exact_literal_len (extract_prefix, already zeroed by the lookaround wipe above when
    // one is present) and rare_disc (extract_rare_discriminant, just above) -- every contributor has
    // run by here. See pattern_hints::literal_one_search for why this is one precomputed bit and not
    // a per-match condition chain.
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
   * The platform `memchr` covers 64 bytes per round too, but measured 1250 ns over 64 KB with no hit
   * (arm64, 2026-09-26) against 625 ns for this loop: a round of four independent compares and one
   * branch. A round with a hit takes its first set lane in block order, so the first hit returned is the
   * leftmost. x86-64 keeps the platform `memchr`, whose vectors are wider than this 128-bit floor.
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
   * Scans for \p hints.rare_disc with \ref find_byte (memchr/SIMD), then back-verifies
   * `[prefix][opt?][disc][after]`. Returns the verified match start if it is ≥ \p pos.
   *
   * Density abandon: when the disc is dense (many hits that fail back-verify), sets
   * \p density_abandon and returns npos so the caller can sticky-switch to the prefix path
   * for the rest of the haystack (same contract as the IL density gate — never miss a match).
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
    const auto        disc    {static_cast<char>(hints.rare_disc)};
    const std::size_t pref    {hints.rare_disc_prefix_len};
    const bool        has_opt {hints.rare_disc_opt >= 0};
    const auto        opt_ch  {static_cast<char>(hints.rare_disc_opt)};
    const std::size_t after   {hints.rare_disc_after_len};
    // Shortest legal back-span is prefix alone (http://); with opt, https:// is longer —
    // must still scan from pos+pref so the no-opt shape is not skipped at the start.
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
   * One vector compare answers "could the needle start here?" for 16 candidate positions at once, at
   * *two* needle offsets (the first byte and the last) — so a block with no surviving candidate is
   * skipped 16 bytes at a time for the cost of two loads and an AND. Two probes rather than one is what
   * makes it worth the vector: a single-byte filter is what `memchr` already gives, and on text where
   * the lead byte is common (`d` for `dog`) it survives constantly.
   *
   * Written ONCE against simd.hpp's uniform \ref mask_t interface (\ref load_pair_mask, \ref empty,
   * \ref first_lane, \ref clear_first) — no `#if` ISA branch of its own, the same split
   * `simd_fixed_shape_scan` documents: the intrinsics are ISA-exclusive and live in simd.hpp, this
   * loop is the same C++ everywhere and is what the test suite exercises on either leg.
   *
   * **When it is taken.** A scan of one byte wins while that byte is rare in the subject: x86-64's `memchr`
   * is twice this block's width, and on 500 KB of log lines with `x` rare it answers `example.com` in
   * 0.014 ms against this filter's 0.044. It loses once the byte stops being rare, one call per stop: `the`
   * 0.25 ms by the first byte against 0.051 here, `error` 0.26 against 0.075 (x86-64, g++ 13.3, 2026-09-27;
   * arm64 alike, where `find_byte` is a 128-bit loop). So \ref find_literal_adaptive scans the rarest byte
   * first and hands the subject to this filter once its stops come dense, on both ISAs.
   *
   * Linearity is unchanged from the scalar path it replaces: the block loop advances 16 per iteration
   * and each block verifies at most 16 candidates of \p literal bytes each, so the work stays
   * `O(n · |literal|)` with `|literal|` capped by the hint arrays (16) — the same bound the
   * memchr-lead-plus-compare scan carried. The caller does the work-counter billing (see
   * \ref find_prefix), once per call, so this function must not be entered per candidate.
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
#  if defined(REAL_TEST_INSTRUMENT)
      literal_avx2_scans().fetch_add(1, std::memory_order_relaxed);
#  endif
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
    // Four blocks (64 candidates) per round. A no-match scan spends all its time in the reject test, and
    // libc `memchr` sets the bar there by covering 64 B per round: at one block per round this filter
    // measured ~13 % SLOWER than the platform `find` on a pure miss, despite rejecting on two
    // bytes instead of one. Four independent load pairs per round (ILP, one branch) turn that into ~2.5x
    // FASTER than memchr — the two-byte selectivity finally paying at memchr's throughput. Masks are
    // consumed in block order, and within a mask in lane order, so candidates are still visited strictly
    // left to right: the first verified hit is the leftmost, which the callers require.
    // A round is rejected by ONE test on the OR of its four blocks (any_pair64); the per-block masks are
    // narrowed only in a round that holds a candidate, which on NEON saves four mask moves a round.
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
   * Kept by the caller for the whole subject (a search state resets it when the subject changes), so a
   * subject found dense takes the pair filter on every later search at once instead of re-proving it per
   * match. A fresh value per call is correct too; it only re-learns.
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
   * \brief The two literal densities of one subject. A search state keeps them in a `std::optional` beside the
   *        subject they refer to, built at its first literal search: constructing a state, which every search
   *        does, then writes a pointer and a flag for them, not two densities.
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
   * Per ISA, since the single-byte scan is not the same width. Over 1 MB with the byte recurring every
   * `gap` bytes and no match (2026-09-27, best of 15): x86-64's `memchr` (g++ 13.3) equals the filter near
   * 70 bytes (0.097 against 0.089 ms at 64, 0.067 against 0.089 at 96); arm64's 128-bit loop (Apple clang)
   * near 190 (0.067 against 0.050 at 128, 0.048 against 0.049 at 192).
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
   * Out of line: its callers include next_candidate, which the class and rare-byte routes run per candidate
   * without ever reaching a literal. Inlined there, it cost `\d{4}-\d{2}-\d{2}` 7 % on arm64.
   * \param[in]     text    The subject text.
   * \param[in]     pos     Index to start from (a stop the caller saw fail, or where a dense search starts).
   * \param[in]     literal The needle (>= 2 bytes).
   * \param[in]     rare    Offset of its rarest byte by the hints.
   * \param[in,out] density What this subject has shown so far.
   * \return The index of the first occurrence at or after \p pos, else \ref real::npos.
   */
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((noinline))
#endif
  inline std::size_t find_literal_adaptive_rest(std::string_view text,
                                                std::size_t      pos,
                                                std::string_view literal,
                                                std::size_t      rare,
                                                literal_density& density)
  {
    note_literal_rest_scan();
    const std::size_t len {literal.size()};
#if defined(__ARM_NEON) || defined(__SSE2__)
    if (density.dense) {
      note_literal_pair_scan();
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
        note_literal_pair_scan();
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
   * The rarest byte (offset \p rare) is scanned with `memchr` on x86-64 and `simd_byte_scan` on NEON,
   * each stop verified. Once \ref literal_dense_min_cands stops sit less than \ref literal_dense_gap bytes
   * apart on average, \p density turns dense and `simd_literal_scan` takes over from the stop after
   * the last one -- from the stop's START plus one, so an occurrence overlapping it is still found. The
   * decision depends on the subject's bytes only: the same subject always takes the same route. Every
   * position is a candidate at most once and a verify costs `O(|literal|)`, so the search stays linear.
   * Billed once per call to the test work counter, as \ref find_prefix always was.
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
#if defined(REAL_TEST_INSTRUMENT)
    prefilter_note_scan(text.size() - pos);
#endif
    // A dense subject goes to the pair filter inline, as the lead-pair search always did; the first stop of a
    // sparse one inline too, since where matches are dense most searches end on it. The rest, out of line.
#if defined(__ARM_NEON) || defined(__SSE2__)
    if (density.dense) {
      note_literal_pair_scan();
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
  //!        ISAs, where the prototype's first-byte scan and pair filter crossed on 500 KB of log lines).
  inline constexpr std::size_t alternation_dense_gap {32};

  //! \brief The same threshold for a plan that masks by the nibble fingerprint, whose cost per block is fixed:
  //!        against the first-byte loop it crossed at 256-512 bytes between false stops (1 MB subjects with no
  //!        match, arm64 and x86-64 AVX2, 3 to 10 branches, 2026-09-29), and at 128 its worst ratio measured
  //!        0.77 of the loop's time.
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
   * \brief The block filter a dense alternation's scan runs: the nibble fingerprint of each branch's first three
   *        bytes where \p nibbles (a table lookup per nibble, a cost per block that does not grow with the
   *        branches; see \ref alternation_nibbles_supported), else the byte pairs. The sample's filter: the scan
   *        runs its own loop for each (pike_vm::alternation_nibble_scan).
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
   * A single byte delegates to \ref find_byte (one `memchr`). A multi-byte literal takes
   * \ref find_literal_adaptive with its rarest byte found here and a density that lasts this call only;
   * the engine's routes keep one per subject and pass the rarest offset their hints computed. No
   * platform `memmem` (absent on MSVC); a plain loop during constant evaluation.
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
   * \brief First position >= \p pos where \p prefix occurs in \p text, or npos.
   *
   * One byte is \ref find_byte's `memchr`; a longer prefix takes \ref find_literal_adaptive with its rarest
   * byte found here and a density that lasts this call. The engine's routes keep a density per subject
   * and pass the offset their hints computed.
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
      // A single byte has no second probe to AND, so it stays on find_byte's one memchr. A longer prefix
      // takes the adaptive search, billed once per call there (a correct O(n) miss bills ~1x the size; a
      // per-position restart would bill ~N^2/2).
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
   * \brief Whether a consumer should ARM a filter on \ref find_members -- one ISA only, and measured.
   *
   * Not "does the scan compile" but "does it beat what it replaces", which is per-ISA, exactly as
   * `simd_literal_scan`'s own ISA note already found for the two-byte literal filter (named as code, not
   * cross-referenced: it lives behind an `#if` and Doxygen builds with no vector ISA defined). A set that
   * skips N per-member searches by scanning their first-byte union once is competing against the
   * platform's `memchr`, once per member -- and it wins on the ISA where that memchr is no wider than
   * this loop, while losing by a comparable factor on the ISA where it is twice as wide.
   *
   * The asymmetry is structural, not a stray reading: a miss over an eight-byte union does roughly twice
   * the vector compares of eight sweeps on registers half as wide again. No amount of tuning a 128-bit
   * loop closes that. A leg with a wider `mask_t` is the honest way to carry this across.
   *
   * \ref find_members itself stays compiled under SSE2: the test suite exercises it on either leg, and a
   * future AVX2 leg plugs in there. What this constant gates is whether a consumer builds a mechanism on
   * top of it. Shipping the SSE2 leg armed would only invite someone to keep it.
   */
#if defined(__ARM_NEON)
  inline constexpr bool have_members_scan {true};
#else
  inline constexpr bool have_members_scan {false};
#endif

  /*!
   * \brief Least index at or after \p pos whose byte is one of \p n members, in ONE pass.
   *
   * The difference from \ref find_bytes_cascade is the whole reason this exists: that function runs one
   * `memchr` PER member, so an eight-member set costs eight sweeps of the subject. A consumer that wants to
   * skip N per-member searches by scanning once therefore gains nothing from it -- measured: a six-byte
   * union over a 8 KB subject cost what the six searches it replaced cost, for no net gain at all. This
   * loop tests all \p n members against sixteen bytes at a time, which is what the engine's own
   * `small_set` band (2..8) is served by.
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
    // The members arrive ALREADY in the layout the mask load wants. An earlier cut took `const char*` and
    // copied eight bytes into a local array per call: on a subject where the caller's first member matches
    // immediately -- the case a set's early-exit walk is fastest on -- that copy was most of a measured
    // +16 % on a 55 ns baseline. A filter consulted on the fast path must cost nothing to consult.
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
   * \brief Index of the first byte in `text[pos..)` that belongs to a small (2..4) first-byte set.
   *
   * A cascade of `std::memchr` — one per set member — taking the minimum hit position. After each hit
   * the scan window is narrowed to `[pos, best)`, so later members only search the shorter prefix and a
   * near hit makes the remaining calls cheap. This beats the one-test-per-byte bitmap loop when the set
   * is small: `memchr` is vectorised in libc, so even four sparse scans cover ground far faster than a
   * scalar byte loop. During constant evaluation the plain member-wise scan runs instead (the home-made
   * path — the same shape the bitmap loop takes).
   *
   * THE QUADRATIC FIX: for **2+ members**, the window is grown **exponentially** (galloping
   * search) from a modest initial probe, doubling each round, rather than handed the full
   * remaining haystack up front. A caller that invokes this once per rejected candidate
   * (`next_candidate`'s icase small-set route) would otherwise pay one `memchr` per set member over
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
   * loop exactly as the galloping fix landed it, untouched.
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
      // parameter: measured across four adversarial shapes (a stop-byte set at a period just under this
      // window, and (?i)<literal> sparse/no-match/dense), the stop-set win SATURATES near this size while
      // the icase-sparse/no-match cost keeps climbing past it (a couple of percent at twice the window,
      // 1024 B) -- so 512-1024 would trade a bounded stop-set win for an
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
