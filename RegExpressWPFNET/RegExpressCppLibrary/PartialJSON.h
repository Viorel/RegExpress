#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>

namespace PartialJSON
{
    bool ParseString( std::string* destination, const char* source );

    std::wstring UTF8_to_wchar( const char* in );
}
