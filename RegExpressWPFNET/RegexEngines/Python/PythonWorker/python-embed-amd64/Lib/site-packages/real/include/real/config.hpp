/*!
 * \file config.hpp
 * \brief Resource limits guarding against pattern-driven resource exhaustion.
 *
 * Bounded-repeat unrolling, parser nesting, capture-group count and compiled
 * program size are each capped. Values are conservative for portability while
 * still admitting practical patterns (e.g. `a{1000}`).
 */
#ifndef REAL_CONFIG_HPP
#define REAL_CONFIG_HPP

#include "version.hpp"

#include <cstddef>
#include <cstdint>

namespace real::detail {

  /*!
   * \brief Maximum number of NFA instructions in a compiled program.
   *
   * Bounds the compiler's bounded-repeat unrolling: without it, nested
   * `{1000}` quantifiers expand to hundreds of millions of instructions. Caps
   * peak match-state memory to a few MiB at the limit.
   */
  inline constexpr std::size_t max_program_size {262144}; //!< 256 Ki instructions

  //! \brief Per-quantifier bounded-repeat cap, enforced at parse time.
  inline constexpr std::int32_t max_repeat_count {1000};

  //! \brief Maximum capture groups; bounds `slot_count` = `2 * (groups + 1)`.
  inline constexpr std::int32_t max_group_count {32766};

  //! \brief Maximum parser recursion depth; prevents stack overflow on deep nesting.
  inline constexpr std::int32_t max_nesting_depth {200};

  //! \brief Maximum bytes a bounded lookaround sub-pattern may consume (its L_max).
  //!        Bounds the per-position lookaround evaluation, preserving linear time.
  inline constexpr std::int32_t max_lookaround_length {255};

  //! \brief Maximum DFA states (opt-in `real::dfa`). Subset construction is 2^NFA in the
  //!        worst case; this caps it so a pathological pattern throws \ref real::dfa_error
  //!        instead of exhausting memory. Generous: real lexer DFAs use far fewer.
  inline constexpr std::size_t max_dfa_states {65536};
} // namespace real::detail

#endif // REAL_CONFIG_HPP
