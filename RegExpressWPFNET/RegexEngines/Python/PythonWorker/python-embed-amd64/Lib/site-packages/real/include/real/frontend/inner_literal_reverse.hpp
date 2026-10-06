/*!
 * \file frontend/inner_literal_reverse.hpp
 * \brief Recovers an inner-literal candidate's match start by reverse-matching the pattern's prefix
 *        (everything before the literal) with `build_byte_program()` + `reverse_dfa`.
 *
 * Unrouted: \ref real::detail::prefix_reverse_start is called only by its test.
 */
#ifndef REAL_FRONTEND_INNER_LITERAL_REVERSE_HPP
#define REAL_FRONTEND_INNER_LITERAL_REVERSE_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include <real/version.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <real/automata/lazy_dfa.hpp> // build_byte_program, reverse_dfa
#include <real/frontend/ast.hpp>
#include <real/frontend/compiler.hpp> // compile
#include <real/frontend/inner_literal.hpp>

namespace real::detail {

  /*!
   * \brief The match start for a literal candidate at \p h: reverse-match the prefix (the first \p count
   *        top-level children) ending at \p h, bounded below by \p min_start. Runtime only (the reverse
   *        walk is not constexpr).
   *
   * Capturing groups compile to `save` ops, which \ref build_byte_program drops, so the reverse is
   * capture-free without a separate compile. No peeled lead is skipped: pass only a pattern whose
   * \ref inner_literal::prefix_skip is 0.
   *
   * \param[in] tree          The pattern's AST, which the prefix is rebuilt from.
   * \param[in] count         Top-level children forming the prefix; 0 means the literal is at the head.
   * \param[in] compile_flags Flags to compile the prefix with, matching the whole pattern's.
   * \param[in] text          Subject.
   * \param[in] h             Offset of the literal candidate (the prefix must end here).
   * \param[in] min_start     Lower bound the reverse walk will not cross.
   * \return The match start, or \ref real::npos when the prefix cannot reach one.
   */
  inline std::size_t prefix_reverse_start(const ast&       tree,
                                          std::int32_t     count,
                                          flags            compile_flags,
                                          std::string_view text,
                                          std::size_t      h,
                                          std::size_t      min_start)
  {
    if (count == 0) {
      return h; // literal at the head: start == candidate
    }
    const ast             prefix {build_prefix_ast(tree, count)};
    const dynamic_program prog   {compile(prefix, compile_flags | prefix.inline_flags)};
    const byte_program    bp     {build_byte_program(prog.view())};
    if (!bp.eligible) {
      return npos; // the prefix holds an op no byte DFA carries
    }
    reverse_dfa rev {bp.code, bp.classes};
    return rev.reverse_start(text, h, min_start);
  }
} // namespace real::detail

#endif // REAL_FRONTEND_INNER_LITERAL_REVERSE_HPP
