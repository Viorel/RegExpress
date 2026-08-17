// gcc-only outline of real::detail::pike_vm::run_cp_class_loop's >= 0x80 byte handling.
//
// Internal — do not include directly. A body fragment, not a standalone translation unit: valid only
// spliced into real::detail::pike_vm at class scope by pike.hpp, under the same
// #if defined(__GNUC__) && !defined(__clang__) that guards this #include (clang defines __GNUC__ too,
// hence the explicit exclusion).
//
// WHY IT EXISTS. gcc leaves the nested closures of the original shape out of line and charges a large
// share of cp_class_loop's instructions to them, where clang inlines the same shape cleanly — so the
// split is compiler-guarded rather than unconditional. The decode plus page-bitmap / range membership
// becomes the function below; the ASCII fast path stays a plain table load with no closure call at all.
// `width` and `extend_run` (cpclass_gcc_loop.hpp) capture their scalars BY VALUE for the same reason: a
// `[&]` capture makes each call walk a closure of references that itself holds another one.
//
// WHY A SEPARATE FILE, rather than an #if inside pike.hpp: coverage line-accounting tallies
// preprocessor-eliminated physical lines into the enclosing file's total, so a branch clang never
// compiles would inflate pike.hpp's floor. Isolated, it is excluded through COV_FLOOR_IGNORE — the
// simd.hpp precedent, code a single-toolchain run can never line-cover by construction.
//
// WHAT NOT TO RETRY. Three forcings of the inlining decision have been measured and refused:
// outlining unconditionally (it regresses clang, which needed nothing), `always_inline` on extend_run,
// and the `noinline, cold` pair on the function below. gcc's own judgement is what stands here.
// Forcings mislead because this header is included everywhere and a large inlinable body consumes
// gcc's --param inline-unit-growth; past that cap gcc declines in traversal order, so a scan that never
// enters this loop can lose an inlining of its own. Measure any change in the full harness and read the
// rows the change CANNOT reach: a mechanism keeps its sign across toolchains, a budget artefact does
// not. See \ref g_inlinebudget before citing a figure about this file.

/*!
 * \brief Non-ASCII width for run_cp_class_loop (byte >= 0x80 only): decode + page-bitmap / range
 *        membership. Left to gcc's own inlining judgement, which it takes at every call site — see the
 *        file-level rationale for what forcing it either way was measured to cost.
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
  // European page + sparse hi table (same split as run_cp_class_loop's member_hi).
  const bool m {dc.cp <= cp_page_max ? cp_member_page(cp_index, dc.cp)
                                     : cp_member_high(cp_index, dc.cp)};
  return m ? dc.length : 0;
}
