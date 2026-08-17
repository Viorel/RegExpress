/*!
 * \file std/regex.hpp
 * \brief `real::compat` — a `std::regex`-compatible drop-in (`<regex>` surface), char path.
 *
 * The umbrella header: it includes the three parts below; `#include <real/compat/std/regex.hpp>` is the one
 * public entry point. The `real::compat` API is split purely organizationally —
 * `std/regex_core.hpp` (constants, error, backend-routing screens, `basic_regex`),
 * `std/regex_match.hpp` (`sub_match`, `match_results`, the runner, and the `regex_search` /
 * `regex_match` / `regex_replace` free functions), and `std/regex_iter.hpp` (`regex_iterator`,
 * `regex_token_iterator`).
 *
 * Contract: behave identically to `std::regex` (ECMAScript) where `real` can prove it, and
 * fall back to `std::regex` everywhere else — never a silent divergence. A pattern is run on
 * `real` (linear-time, ReDoS-safe) when possible; backreferences, unbounded/oversized
 * lookarounds, POSIX classes and non-ASCII inside `[...]` route to `std::regex` via a
 * compile-time screen plus a compile-failure catch. The five POSIX grammars are NOT among them:
 * each is translated to its ECMAScript equivalent and run on `real` with leftmost-longest bounds
 * when the pattern translates, falling back to `std::regex` only when it does not.
 *
 * `real` is always built with `flags::bytes | flags::ecma` so its byte-oriented, ECMAScript-`$`,
 * ECMAScript-`.` semantics align with `std::basic_regex<char>` (validated by a differential).
 *
 * Surface: `basic_regex` / `sub_match` / `match_results` / `regex_error`, `regex_search`,
 * `regex_match`, `regex_replace`, `regex_iterator` / `regex_token_iterator`, the full
 * `match_flag_type`, `wregex`, the POSIX grammars and `nosubs`. `real` runs the `char` /
 * default-traits / every-group path (see `detail::real_eligible`); wide `CharT`, custom traits,
 * `collate` and `nosubs` are always `std`. `regex_replace` and the iterators route a nullable
 * pattern to `std::regex` — the empty-match traversal differs from ECMAScript, see
 * `basic_regex::nullable` — and a constraining `match_flag` routes that one operation to `std`.
 *
 * See the "Drop-in for std::regex" migration guide and the compatibility reference (COMPATIBILITY.md)
 * in the rendered documentation.
 */
#ifndef REAL_STD_REGEX_HPP
#define REAL_STD_REGEX_HPP

// A public entry point: #include <real/compat/std/regex.hpp>. The three parts it pulls in are
// internal and carry the usual banner.

#include "regex_core.hpp"
#include "regex_match.hpp"
#include "regex_iter.hpp"

#endif // REAL_STD_REGEX_HPP
