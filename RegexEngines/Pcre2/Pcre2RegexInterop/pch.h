// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here



// PCRE2

// See "NON-AUTOTOOLS-BUILD" files from PCRE2

// See also "pch-pcre2.h"
#pragma unmanaged
#include "pch-pcre2.h"
#include "PCRE2.h"
#pragma managed

#include <msclr\marshal_cppstd.h>
#include <exception>

#endif //PCH_H
