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

// Function attributes for the engine's inlining decisions, which are measured choices (a callee's size
// moves its callers' inline budget): `REAL_COLD` keeps a slow path out of its caller, `REAL_NOINLINE`
// keeps a body out of line, `REAL_ALWAYS_INLINE` forces one in, `REAL_BUILD_COLD` marks build-time code that
// may still inline. Empty where the GNU spelling is absent.
#if defined(__GNUC__) || defined(__clang__)
#  define REAL_NOINLINE      __attribute__((noinline))
#  define REAL_ALWAYS_INLINE __attribute__((always_inline))
#  define REAL_COLD          __attribute__((noinline, cold))
#  define REAL_BUILD_COLD    __attribute__((cold))
#else
#  define REAL_NOINLINE
#  define REAL_ALWAYS_INLINE
#  define REAL_COLD
#  define REAL_BUILD_COLD
#endif

namespace real::detail {

  // The limits below guard against pattern-driven resource exhaustion. A build that accepts untrusted patterns
  // may LOWER any of them by defining the macro named beside it (`-DREAL_MAX_REPEAT_COUNT=100`); raising one
  // is refused, as each default is also a storage bound (a group index is 16 bits, a lookaround length 8).
  // Every translation unit of a program must see the same values.

  inline constexpr std::size_t  default_max_program_size      {262144}; //!< \ref max_program_size's default.
  inline constexpr std::int32_t default_max_repeat_count      {1000};   //!< \ref max_repeat_count's default.
  inline constexpr std::int32_t default_max_group_count       {32766};  //!< \ref max_group_count's default.
  inline constexpr std::int32_t default_max_nesting_depth     {200};    //!< \ref max_nesting_depth's default.
  inline constexpr std::int32_t default_max_lookaround_length {255};    //!< \ref max_lookaround_length's default.
  inline constexpr std::size_t  default_max_dfa_states        {65536};  //!< \ref max_dfa_states's default.

  /*!
   * \brief Maximum number of NFA instructions in a compiled program — 256 Ki by default (`REAL_MAX_PROGRAM_SIZE`).
   *
   * Bounds bounded-repeat unrolling (nested `{1000}` would expand to hundreds of millions of
   * instructions) to a few MiB. Match-time structures have their own bounds: per lazy DFA, direction and
   * thread, 65 536 states or \ref lazy_dfa_default_byte_budget; per regex, \ref ac_memory_budget.
   */
#if defined(REAL_MAX_PROGRAM_SIZE)
  inline constexpr std::size_t max_program_size {REAL_MAX_PROGRAM_SIZE};
#else
  inline constexpr std::size_t max_program_size {default_max_program_size};
#endif

  inline constexpr std::string_view program_too_large {"program too large"}; //!< The cause a program past \ref max_program_size is rejected with (\ref real::regex_error::cause).

  //! \brief Per-quantifier bounded-repeat cap, enforced at parse time (`REAL_MAX_REPEAT_COUNT`).
#if defined(REAL_MAX_REPEAT_COUNT)
  inline constexpr std::int32_t max_repeat_count {REAL_MAX_REPEAT_COUNT};
#else
  inline constexpr std::int32_t max_repeat_count {default_max_repeat_count};
#endif

  //! \brief Maximum capture groups; bounds `slot_count` = `2 * (groups + 1)` (`REAL_MAX_GROUP_COUNT`).
#if defined(REAL_MAX_GROUP_COUNT)
  inline constexpr std::int32_t max_group_count {REAL_MAX_GROUP_COUNT};
#else
  inline constexpr std::int32_t max_group_count {default_max_group_count};
#endif

  //! \brief Maximum parser recursion depth; prevents stack overflow on deep nesting (`REAL_MAX_NESTING_DEPTH`).
#if defined(REAL_MAX_NESTING_DEPTH)
  inline constexpr std::int32_t max_nesting_depth {REAL_MAX_NESTING_DEPTH};
#else
  inline constexpr std::int32_t max_nesting_depth {default_max_nesting_depth};
#endif

  //! \brief Maximum bytes a bounded lookaround sub-pattern may consume (its L_max); bounding it keeps
  //!        per-position evaluation linear (`REAL_MAX_LOOKAROUND_LENGTH`).
#if defined(REAL_MAX_LOOKAROUND_LENGTH)
  inline constexpr std::int32_t max_lookaround_length {REAL_MAX_LOOKAROUND_LENGTH};
#else
  inline constexpr std::int32_t max_lookaround_length {default_max_lookaround_length};
#endif

  /*!
   * \brief Maximum DFA states (opt-in `real::dfa`; `REAL_MAX_DFA_STATES`).
   *
   * Caps exponential subset construction: a pathological pattern throws \ref real::dfa_error instead of
   * exhausting memory.
   */
#if defined(REAL_MAX_DFA_STATES)
  inline constexpr std::size_t max_dfa_states {REAL_MAX_DFA_STATES};
#else
  inline constexpr std::size_t max_dfa_states {default_max_dfa_states};
#endif

  inline constexpr std::size_t default_dfa_memory {std::size_t {64} << 20U}; //!< Default bytes one lazy DFA's state cache may hold per direction (\ref real::limits::dfa_memory).

  static_assert(max_program_size >= 64U && max_program_size <= default_max_program_size,
                "REAL_MAX_PROGRAM_SIZE may only lower the default, to 64 or more");
  static_assert(max_repeat_count >= 1 && max_repeat_count <= default_max_repeat_count,
                "REAL_MAX_REPEAT_COUNT may only lower the default, to 1 or more");
  static_assert(max_group_count >= 1 && max_group_count <= default_max_group_count,
                "REAL_MAX_GROUP_COUNT may only lower the default, to 1 or more");
  static_assert(max_nesting_depth >= 1 && max_nesting_depth <= default_max_nesting_depth,
                "REAL_MAX_NESTING_DEPTH may only lower the default, to 1 or more");
  static_assert(max_lookaround_length >= 1 && max_lookaround_length <= default_max_lookaround_length,
                "REAL_MAX_LOOKAROUND_LENGTH may only lower the default, to 1 or more");
  static_assert(max_dfa_states >= 2U && max_dfa_states <= default_max_dfa_states,
                "REAL_MAX_DFA_STATES may only lower the default, to 2 or more");
} // namespace real::detail

#endif // REAL_CONFIG_HPP
