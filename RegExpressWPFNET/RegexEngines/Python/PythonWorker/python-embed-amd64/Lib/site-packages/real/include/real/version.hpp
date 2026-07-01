/*!
 * \file version.hpp
 * \brief REAL's version macros and the C++20 language-standard guard.
 *
 * Zero dependency by design: only preprocessor `#define` / `#error`, never `#include`. Every public
 * header includes this first, so the version macros are visible from any entry point
 * (the umbrella \c real.hpp, the opt-in \c dfa.hpp, or any sub-header pulled in directly)
 * and the standard guard is evaluated once per translation unit (the include guard
 * deduplicates it). `make release` bumps the three numeric macros; \ref REAL_VERSION_STRING
 * is derived, never edited by hand.
 */
#ifndef REAL_VERSION_HPP
#define REAL_VERSION_HPP

// Language-standard guard. REAL is C++20 throughout (concepts, <span>, constexpr engine), so a
// pre-C++20 compile fails here with a clear message instead of a wall of template errors. The
// _MSVC_LANG branch is required because MSVC leaves __cplusplus at 199711L unless /Zc:__cplusplus
// is passed — a non-CMake MSVC consumer that forgets it would otherwise slip past a bare
// __cplusplus check. This is the only thing exercising the _MSVC_LANG path, so keep it.
#if defined(_MSVC_LANG)
#  if _MSVC_LANG < 202002L
#    error "real requires C++20 or newer"
#  endif
#elif __cplusplus < 202002L
#  error "real requires C++20 or newer (compile with -std=c++20 or later)"
#endif

// These three are deliberately preprocessor macros, not an enum or constexpr constants: a
// version header must expose them to the *preprocessor* so a consumer can branch on
// `#if REAL_VERSION_MAJOR >= …`, and they feed REAL_VERSION_STRING's stringization. macro-to-enum
// / macro-usage are sound style defaults that simply do not fit the universal LIBFOO_VERSION_*
// idiom — a documented, narrowly-scoped deviation, not a hidden bug.
// NOLINTBEGIN(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)
//! \brief Major version (the calendar year).
#define REAL_VERSION_MAJOR 2026
//! \brief Minor version (the calendar month).
#define REAL_VERSION_MINOR 7
//! \brief Patch version (the release count within the month).
#define REAL_VERSION_PATCH 1
// NOLINTEND(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)

// Two-level stringize so the macro *values* (not their names) are pasted into the string.
// Stringization (#x) is a preprocessor-only operation — the suggested constexpr template cannot
// express it — so macro-usage's advice does not apply here.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define REAL_STRINGIZE_IMPL(x) #x
#define REAL_STRINGIZE(x)      REAL_STRINGIZE_IMPL(x)
// NOLINTEND(cppcoreguidelines-macro-usage)

//! \brief The version as "MAJOR.MINOR.PATCH" — derived from the three numeric macros.
#define REAL_VERSION_STRING                       \
        REAL_STRINGIZE(REAL_VERSION_MAJOR) "."    \
        REAL_STRINGIZE(REAL_VERSION_MINOR) "."    \
        REAL_STRINGIZE(REAL_VERSION_PATCH)

#endif // REAL_VERSION_HPP
