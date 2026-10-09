/*!
 * \file version.hpp
 * \brief REAL's version macros and the C++20 language-standard guard.
 *
 * Must stay include-free (only `#define` / `#error`): headers include it first, so the macros and the
 * standard guard reach every entry point. `make release` bumps the three numeric macros;
 * \ref REAL_VERSION_STRING is derived.
 */
#ifndef REAL_VERSION_HPP
#define REAL_VERSION_HPP

// Language-standard guard. _MSVC_LANG first: MSVC leaves __cplusplus at 199711L without /Zc:__cplusplus.
#if defined(_MSVC_LANG)
#  if _MSVC_LANG < 202002L
#    error "real requires C++20 or newer"
#  endif
#elif __cplusplus < 202002L
#  error "real requires C++20 or newer (compile with -std=c++20 or later)"
#endif

// Macros, not constants: consumers test them with `#if` and REAL_VERSION_STRING stringizes them, hence
// the NOLINT. `make release` rewrites these three lines whole, so their docs must sit above them.
// NOLINTBEGIN(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)
/*! \brief Major version (the calendar year). */
#define REAL_VERSION_MAJOR 2026
/*! \brief Minor version (the calendar month). */
#define REAL_VERSION_MINOR 10
/*! \brief Patch version (the release count within the month). */
#define REAL_VERSION_PATCH 5
// NOLINTEND(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-macro-usage)

// Stringization is preprocessor-only, hence the NOLINT.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
/*! \brief Inner half of the two-level stringize: turns its argument into a string literal. */
#define REAL_STRINGIZE_IMPL(x) #x
/*! \brief Stringizes the *expansion* of \p x — what the second level buys. */
#define REAL_STRINGIZE(x)      REAL_STRINGIZE_IMPL(x)
// NOLINTEND(cppcoreguidelines-macro-usage)

/*! \brief The version as "MAJOR.MINOR.PATCH" — derived from the three numeric macros. */
#define REAL_VERSION_STRING                       \
        REAL_STRINGIZE(REAL_VERSION_MAJOR) "."    \
        REAL_STRINGIZE(REAL_VERSION_MINOR) "."    \
        REAL_STRINGIZE(REAL_VERSION_PATCH)

#endif // REAL_VERSION_HPP
