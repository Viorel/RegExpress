#pragma once

#include <string>
#include <vector>

#include "CheckedCast.h"

// NOTE. 'wstring_convert' and 'codecvt_utf8' was deprecated in C++ 20.

std::wstring Utf8ToWString( const char* s );
std::wstring Utf8ToWString( const std::string& s );
std::wstring ToWString( const char* s ); // (simple unsigned widening)
std::wstring ToWString( const std::string& s ); // (simple unsigned widening)

std::string WStringToUtf8( const wchar_t* s );
std::string WStringToUtf8( const std::wstring& s );
std::string WStringToUtf8( const std::wstring& s, std::vector<int>* indices );
