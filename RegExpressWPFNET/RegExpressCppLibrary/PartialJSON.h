#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>

namespace PartialJSON
{
    bool ParseString( std::string* destination, const char* source );

    /*
    * (see also 'Utf8ToWString' from 'Convert.h'; can be used (copied) in some circumstances instead of 'Utf8ToWString')
    * 
    std::wstring UTF8_to_wchar( const char* in );
    */
}
