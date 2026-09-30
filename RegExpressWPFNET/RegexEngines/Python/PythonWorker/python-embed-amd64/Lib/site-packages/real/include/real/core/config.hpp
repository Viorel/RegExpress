/*!
 * \file config.hpp
 * \brief Resource limits guarding against pattern-driven resource exhaustion.
 *
 * Bounded-repeat unrolling, parser nesting, capture-group count, program size,
 * lookaround width and DFA state count are each capped. Values are conservative
 * for portability while still admitting practical patterns (e.g. `a{1000}`).
 */
#ifndef REAL_CONFIG_HPP
#define REAL_CONFIG_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace real::detail {

  /*!
   * \brief Maximum number of NFA instructions in a compiled program — 256 Ki.
   *
   * Bounds the compiler's bounded-repeat unrolling: without it, nested
   * `{1000}` quantifiers expand to hundreds of millions of instructions. Caps
   * the program to a few MiB at the limit; what a match builds from it has its own bounds: per lazy DFA,
   * per direction and per thread, 4 096 states or \ref lazy_dfa_default_byte_budget, whichever comes first;
   * the Aho-Corasick automaton's \ref ac_memory_budget per regex.
   */
  inline constexpr std::size_t max_program_size       {262144};

  inline constexpr std::string_view program_too_large {"program too large"}; //!< The cause a program past \ref max_program_size is rejected with (\ref real::regex_error::cause).

  inline constexpr std::int32_t max_repeat_count      {1000};                //!< Per-quantifier bounded-repeat cap, enforced at parse time.

  inline constexpr std::int32_t max_group_count       {32766};               //!< Maximum capture groups; bounds `slot_count` = `2 * (groups + 1)`.

  inline constexpr std::int32_t max_nesting_depth     {200};                 //!< Maximum parser recursion depth; prevents stack overflow on deep nesting.

  inline constexpr std::int32_t max_lookaround_length {255};                 //!< Maximum bytes a bounded lookaround sub-pattern may consume (its L_max); bounding it keeps per-position evaluation linear.

  /*!
   * \brief Maximum DFA states (opt-in `real::dfa`).
   *
   * Subset construction is 2^NFA in the worst case; this caps it so a pathological
   * pattern throws \ref real::dfa_error instead of exhausting memory. Generous —
   * a lexer DFA uses far fewer.
   */
  inline constexpr std::size_t max_dfa_states {65536};
} // namespace real::detail

#endif // REAL_CONFIG_HPP
