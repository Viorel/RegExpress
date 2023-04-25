// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here

#include <crtversion.h>
#include <strsafe.h>
#include <iostream>

#include "RegExpressCppLibraryPCH.h"

extern long Variable_REGEX_MAX_STACK_COUNT;
extern long Variable_REGEX_MAX_COMPLEXITY_COUNT;

#define _REGEX_MAX_STACK_COUNT          Variable_REGEX_MAX_STACK_COUNT
#define _REGEX_MAX_COMPLEXITY_COUNT     Variable_REGEX_MAX_COMPLEXITY_COUNT

#include <regex>

#endif //PCH_H
