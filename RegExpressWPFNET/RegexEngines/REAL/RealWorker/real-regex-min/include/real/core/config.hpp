/*!
 * \file config.hpp
 * \brief Resource limits guarding against pattern-driven resource exhaustion.
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
   * Bounds bounded-repeat unrolling (nested `{1000}` would expand to hundreds of millions of
   * instructions) to a few MiB. Match-time structures have their own bounds: per lazy DFA, direction and
   * thread, 65 536 states or \ref lazy_dfa_default_byte_budget; per regex, \ref ac_memory_budget.
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
   * Caps exponential subset construction: a pathological pattern throws \ref real::dfa_error instead of
   * exhausting memory.
   */
  inline constexpr std::size_t max_dfa_states {65536};
} // namespace real::detail

#endif // REAL_CONFIG_HPP
