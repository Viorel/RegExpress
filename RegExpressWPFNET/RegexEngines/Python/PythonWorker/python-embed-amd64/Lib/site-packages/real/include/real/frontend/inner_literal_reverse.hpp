/*!
 * \file frontend/inner_literal_reverse.hpp
 * \brief The prefix-reverse of the inner-literal prefilter (INNER-LITERAL IL.1). Given a required inner
 *        literal found at a candidate position, reverse-match the pattern's PREFIX (everything before the
 *        literal) to recover the match start — the reverse-inner protocol. Reuses the engine's existing
 *        reverse machinery (build_byte_program() + reverse_dfa), just on the prefix sub-program.
 *        **Inert**: nothing routes on it yet (that is IL.2).
 */
#ifndef REAL_FRONTEND_INNER_LITERAL_REVERSE_HPP
#define REAL_FRONTEND_INNER_LITERAL_REVERSE_HPP

#include <real/version.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <real/automata/lazy_dfa.hpp> // build_byte_program, reverse_dfa
#include <real/frontend/ast.hpp>
#include <real/frontend/compiler.hpp> // compile
#include <real/frontend/inner_literal.hpp>

namespace real::detail {

  //! \brief The match start for a literal candidate at \p h: reverse-match the prefix (the first \p count
  //!        top-level children) ending at \p h, bounded below by \p min_start. \p count == 0 means the literal
  //!        is at the head, so the reverse is the identity (the match starts at the candidate). Returns \ref
  //!        npos when the prefix cannot reach a start (an orphan candidate). Runtime only — the reverse DFA is
  //!        not constexpr; a static_regex would keep the inner-literal path dynamic.
  //!
  //! The prefix is compiled through the normal path: its capturing groups become `save` ops, which \ref
  //! build_byte_program drops as zero-width, so the byte program (and the reverse over it) is capture-free by
  //! construction — no separate capture-free compile is needed. Top-level `\b`/`\B` are peeled before the
  //! prefix is built (D1a, \ref extract_inner_literal / \ref build_prefix_ast with skip); other anchors and
  //! lookarounds still decline extraction, so an extracted pattern's prefix stays byte-program eligible.
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
      return npos; // an assertion/lookaround in the prefix — unreachable for an extracted pattern
    }
    reverse_dfa rev {bp.code, bp.classes};
    return rev.reverse_start(text, h, min_start);
  }
} // namespace real::detail

#endif // REAL_FRONTEND_INNER_LITERAL_REVERSE_HPP
