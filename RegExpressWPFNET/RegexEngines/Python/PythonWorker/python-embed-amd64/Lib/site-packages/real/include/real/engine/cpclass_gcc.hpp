// O2r-1b: gcc-only outline for real::detail::pike_vm::run_cp_class_loop's >= 0x80 byte handling.
//
// P0-callgrind (x86/gcc) charged 28% of cp_class_loop's Ir to two nested closures gcc never inlined
// (member_hi 23.1%, the width() call into it 13.3%) — gcc/manylinux builds the Linux PyPI wheels, so
// this cost ships to users. clang was already inlining the original nested-closure shape cleanly (M1
// baseline "healthy"); forcing the same outline unconditionally (O2r-1, no compiler guard) cost M1 +7%
// on \w+/\b\w+\b and was retired pre-commit (see .recovery/o2r1-rejected-m1-regression.patch).
//
// This file and cpclass_gcc_loop.hpp hold the gcc-only outline: member_hi's decode + page-bitmap /
// range membership (the 23.1% Ir line) becomes the outlined, cold cp_class_hi_width below; the ASCII
// fast path stays a plain asc[b] table load with no lambda/closure call. Elimination is partial by
// design: width()/extend_run() (cpclass_gcc_loop.hpp) remain lambdas, and gcc still doesn't fully
// inline them — a further ~23% Ir measured post-split, named as headroom for a future pass, not
// touched here.
//
// Measured x86 devbox A/B (land threshold >=10%, paid): \w+ -24.7%, \d+ -66%; witnesses ([a-z]+,
// trailing-LA) within noise. M1 is unaffected by construction: this file, and the branch selecting it
// (#if defined(__GNUC__) && !defined(__clang__); clang defines __GNUC__ too, hence the explicit
// exclusion), never compile under clang — confirmed by `c++ -E` (zero occurrences of
// cp_class_hi_width) and by an interleaved M1 A/B (+-1%, noise).
//
// Split out of pike.hpp (rather than kept inline under #if) so this compiler-exclusive code can be
// excluded from the coverage floor via COV_FLOOR_IGNORE (the simd.hpp precedent: code a single-
// toolchain coverage run can never line-cover by construction) instead of inflating pike.hpp's own
// line count with a branch clang never compiles — llvm-cov report was tallying those preprocessor-
// eliminated physical lines into the file's total: invisible to the compiler, not to the coverage
// line-accounting.
//
// Internal — do not include directly. A body fragment, not a standalone translation unit: valid only
// spliced into real::detail::pike_vm at class scope by pike.hpp, under the same
// #if defined(__GNUC__) && !defined(__clang__) that guards this #include.

// \brief Non-ASCII width for run_cp_class_loop (byte >= 0x80 only): decode + page-bitmap / range
//        membership. Outlined and cold — see the file-level rationale above.
// \param[in] text     The subject text.
// \param[in] i        Index of the lead byte (>= 0x80); must be < text.size().
// \param[in] cp_index Index into dynamic_program::cp_classes for the pattern's class.
// \return The code point's byte width if it is a class member, or 0.
__attribute__((noinline, cold))
constexpr std::size_t cp_class_hi_width(std::string_view text,
                                        std::size_t      i,
                                        std::size_t      cp_index)
{
  const detail::decoded_codepoint dc {detail::decode_codepoint_strict(text, i)};
  if (!dc.valid) {
    return 0;
  }
  if (dc.cp <= cp_page_max) {
    const std::uint64_t* const page {cp_page_table(cp_index)};
    const std::uint32_t        bit  {static_cast<std::uint32_t>(dc.cp) - 0x80U};
    return ((page[bit >> 6U] >> (bit & 63U)) & std::uint64_t {1}) != 0U ? dc.length : 0;
  }
  return cp_class_matches(prog_.cp_classes[cp_index], dc.cp) ? dc.length : 0;
}
