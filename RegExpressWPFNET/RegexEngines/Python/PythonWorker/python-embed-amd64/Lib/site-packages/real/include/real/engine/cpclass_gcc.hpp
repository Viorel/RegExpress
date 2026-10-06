// gcc-only outline of real::detail::pike_vm::run_cp_class_loop's >= 0x80 byte handling.
//
// Internal — do not include directly. A body fragment spliced into real::detail::pike_vm at class scope
// by pike.hpp, under #if defined(__GNUC__) && !defined(__clang__) (clang defines __GNUC__ too).
//
// Why it exists: gcc leaves the generic shape's nested closures out of line and charges them a large
// share of the loop's instructions; clang inlines that shape, hence the compiler guard. The ASCII path
// stays a table load; `extend_run` and its `width_at` (cpclass_gcc_loop.hpp) capture scalars by value,
// since a `[&]` closure makes each call walk references to another closure.
//
// Why a separate file: coverage counts preprocessor-eliminated lines in the enclosing file, so an #if
// branch in pike.hpp would inflate its floor; this file is in COV_FLOOR_IGNORE, as simd.hpp is.
//
// Do not force the inlining: unconditional outlining (regresses clang), `always_inline` on extend_run and
// `noinline, cold` here all measured worse. This header is included everywhere, so a forcing moves gcc's
// inline-unit-growth budget and can cost rows that never enter this loop. Measure in the full harness,
// rows the change cannot reach included (docs/design.dox, g_inlinebudget).

#if !defined(REAL_CPCLASS_FRAGMENT_SITE)
#  error "real/engine/cpclass_gcc.hpp is a fragment of real/engine/pike.hpp, not a header: include <real/real.hpp>"
#endif

/*!
 * \brief Non-ASCII width for run_cp_class_loop (byte >= 0x80 only): decode, then page-bitmap / range
 *        membership. No inlining attribute (see the file header).
 * \param[in] text     The subject text.
 * \param[in] i        Index of the lead byte (>= 0x80); must be < text.size().
 * \param[in] cp_index Index into dynamic_program::cp_classes for the pattern's class.
 * \return The code point's byte width if it is a class member, or 0.
 */
constexpr std::size_t cp_class_hi_width(std::string_view text,
                                        std::size_t      i,
                                        std::size_t      cp_index)
{
  const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
  if (!dc.valid) {
    return 0;
  }
  // Same page / sparse-high split as run_cp_class_loop's member_hi.
  const bool m {dc.cp <= cp_page_max ? cp_member_page(cp_index, dc.cp)
                                     : cp_member_high(cp_index, dc.cp)};
  return m ? dc.length : 0;
}
