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
// inline them — a further ~23% Ir measured post-split, named as headroom for a future pass.
//
// O2r-1c took part of that headroom, and NOT by inlining: forcing extend_run inline was tried and
// refuted (x86 Ir -10% on \w+, wall clock +34..37% on \d+ and +8% on \w+ under an interleaved A/B,
// with the untouched [a-z]+ also +5% — byte-identical in Ir, so the cost was code layout, not the
// loop). RE-CHECKED after the inline-budget defect was fixed, because that refusal's own evidence
// carried the defect's signature and could have been an artefact: the verdict stands, on cleaner
// grounds. always_inline on extend_run now costs the property/script band \p{N}+ +11.5%, scx=Cyrl
// +11.4%, sc=Han +11.2%, \w+ +9.3%, \p{L}+ +7.2% against a -9.4..+2.6% floor, and (\w+)@(\w+)
// +32.7% with the dyn gauge at -1.8%. The ARGUMENT was contaminated even though the conclusion was
// not: the [a-z]+ +5% cited above reads -4.7% today, a sign flip, so that row was never evidence
// about this change. Do not retry it; do read \ref g_inlinebudget before citing any figure here. What paid instead was making the call CHEAPER while leaving it out of line: extend_run
// captured `[&]`, so each call walked a closure of references that itself held `width`, another
// by-reference closure — two indirections per call. Explicit by-value capture of the scalars, with
// width's three lines inlined into it, removes both. x86 g++-14: \d+ -13% wall / -7.4% Ir over three
// interleaved rounds, \w+ within noise, [a-z]+ byte-identical in Ir. \d+ gains most because its
// class has ten ASCII members against \w's sixty-three, so runs are shorter and the per-call cost is
// paid more often per byte. M1 is unaffected by construction (clang never compiles this file:
// `c++ -E` finds zero occurrences of cp_class_hi_width) and by measurement (arm64 A/B identical).
//
// O2r-1d dropped the `noinline, cold` pair, so gcc inlines the body into all nine call sites and the
// out-of-line symbol disappears. It buys the property/script band, on the devbox under g++ 13.3.0:
// sc=Han -22.7%, scx=Cyrl -20.6%, \p{N}+ -17.0%, mixed-script \w+ -14.3%; \p{L}+ -12.2% and [a-z-accented]+
// -12.9% sit nearer the floor and are bounded rather than claimed. The gain lives in `width`, the scan
// predicate, and not in extend_run: a variant inlining only inside extend_run's three call sites was
// measured and delivered NONE of it (\p{N}+ +7.4%, sc=Han +6.9%, scx=Cyrl +4.7% -- the wrong sign).
//
// This was REFUSED before the state_type lift, and the reason it was refused had nothing to do with this
// file. Dropping the pair then cost `(?i)cafe` on an ASCII no-match scan 10.4 -> 33.0 us, +217%, on the
// toolchain that builds the manylinux wheels -- a pattern that compiles to four BYTE classes and zero cp
// classes, so it never enters this loop and never calls the function below. It was paying collateral
// codegen: a large inlinable body in a header included everywhere consumed the last of gcc's
// --param inline-unit-growth, and past that cap gcc declines in traversal order, so an unrelated scan lost
// an inlining it needed. The tell was the sign flip -- the same row read -25% under g++ 14 in a container
// against +217% on the devbox. A mechanism keeps its sign; a budget artefact does not.
//
// Keying a compile-time storage's scratch on dimensions instead of the pattern's value (\ref
// g_inlinebudget) cut what a pattern adds to a TU from ~45 KB to 7.7 and the refusals in a 32-pattern unit
// from 19 195 to 1457. Re-measured on the same devbox afterwards, the collateral is gone and inverted:
// `(?i)cafe` -9.0%, \b\w+\b -7.4%, \w+ -3.2%, all with the dyn gauge inside 1%. Hence this change lands
// now on evidence that was already valid, having been blocked by a defect elsewhere in the tree.
//
// Read the rows a change CANNOT reach before believing its numbers here: the floor in this translation
// unit is still wide (-8.8%..+11.6% on non-cp-class rows), which is why only the four largest gains above
// are stated as measured.
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
//        membership. Left to gcc's own inlining judgement since O2r-1d, which it takes at every call
//        site — see the file-level rationale above for what forcing it either way was measured to cost.
// \param[in] text     The subject text.
// \param[in] i        Index of the lead byte (>= 0x80); must be < text.size().
// \param[in] cp_index Index into dynamic_program::cp_classes for the pattern's class.
// \return The code point's byte width if it is a class member, or 0.
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
