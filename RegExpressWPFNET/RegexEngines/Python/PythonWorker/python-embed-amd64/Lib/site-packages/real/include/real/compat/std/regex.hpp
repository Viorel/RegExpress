/*!
 * \file std/regex.hpp
 * \brief `real::compat` — a `std::regex`-compatible drop-in (`<regex>` surface), char path.
 *
 * The one public entry point; it includes `std/regex_core.hpp` (constants, error, routing screens,
 * `basic_regex`), `std/regex_match.hpp` (`sub_match`, `match_results`, the free functions) and
 * `std/regex_iter.hpp` (the iterators).
 *
 * `real` runs with `flags::bytes | flags::ecma`, which aligns it with `std::basic_regex<char>`. A pattern
 * it cannot represent (a backreference, an unbounded lookbehind, a POSIX class, `collate` or `nosubs`, …)
 * is rejected under the default `policy::strict` and delegated to `std::regex` under `policy::fallback`.
 * A POSIX grammar that translates runs on `real` with leftmost-longest bounds; captures are the winning
 * thread's, not POSIX submatches. Wide `CharT` and custom traits always run on `std`.
 *
 * A search or match stays on `real` under `match_continuous`, `match_prev_avail`, `match_not_null`,
 * `match_not_eol` and `match_not_eow` as far as `detail::call_stays_real` allows; `match_not_bol` /
 * `match_not_bow` alone route that call to `std`. `regex_replace` and the iterators stay on `real` when
 * `basic_regex::uses_real_traversal()` holds and no constraining match flag is passed.
 *
 * Contract: behave as `std::regex` does, never a silent divergence; the documented ones are in
 * COMPATIBILITY.md and the "Drop-in for std::regex" guide.
 */
#ifndef REAL_STD_REGEX_HPP
#define REAL_STD_REGEX_HPP

// A public entry point; the three parts below are internal.

#include "regex_core.hpp"
#include "regex_match.hpp"
#include "regex_iter.hpp"

#endif // REAL_STD_REGEX_HPP
