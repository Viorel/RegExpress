#pragma once

#include <string>
#include <vector>

#include "CheckedCast.h"


std::wstring Utf8ToWString( const char* s );
std::wstring Utf8ToWString( const std::string& s );
std::wstring ToWString( const char* s );
std::wstring ToWString( const std::string& s );

std::string WStringToUtf8( const wchar_t* s );
std::string WStringToUtf8( const std::wstring& s );
std::string WStringToUtf8( const std::wstring& s, std::vector<int>* indices );
