#pragma once

#include <stdexcept>
#include <string>
#include <vector>
#include <cuchar>
#include <format>

#include "CheckedCast.h"


std::wstring Utf8ToWString( const char* s );
std::wstring Utf8ToWString( const std::string& s );

std::string WStringToUtf8( const wchar_t* s );
std::string WStringToUtf8( const std::wstring& s );
std::string WStringToUtf8( const std::wstring& s, std::vector<int>* indices );
