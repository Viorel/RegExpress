#include "pch.h"

#include "Convert.h"
#include "CheckedCast.h"


// NOTE. 'wstring_convert' and 'codecvt_utf8' was deprecated in C++ 20.


std::wstring Utf8ToWString( const char* s )
{
    const auto size_needed = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0 );

    if( size_needed <= 0 )
    {
        throw std::runtime_error( "MultiByteToWideChar() failed [1]." );
    }

    std::wstring result;
    result.resize( size_needed );

    if( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, &result[0], size_needed ) <= 0 )
    {
        throw std::runtime_error( "MultiByteToWideChar() failed [2]." );
    }

    assert( result[size_needed - 1] == L'\0' );
    result.resize( size_needed - 1 ); // exclude '\0' from length
    assert( result.length( ) == size_needed - 1 );
    assert( result.c_str( )[result.length( )] == L'\0' );

    return result;
}


std::wstring Utf8ToWString( const std::string& s )
{
    const auto size_needed = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str( ), CheckedCast( s.length( ) ), nullptr, 0 );

    if( size_needed <= 0 )
    {
        throw std::runtime_error( "MultiByteToWideChar() failed [3]." );
    }

    std::wstring result;
    result.resize( size_needed );

    if( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str( ), CheckedCast( s.length( ) ), &result[0], size_needed ) <= 0 )
    {
        throw std::runtime_error( "MultiByteToWideChar() failed [4]." );
    }

    assert( result.length( ) == size_needed );
    assert( result.c_str( )[result.length( )] == L'\0' );

    return result;
}


std::string WStringToUtf8( const wchar_t* s )
{
    const auto size_needed = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, nullptr, 0, nullptr, nullptr );
    if( size_needed <= 0 )
    {
        throw std::runtime_error( "WideCharToMultiByte() failed [1]." );
    }

    std::string result;
    result.resize( size_needed );

    if( WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, &result[0], size_needed, nullptr, nullptr ) <= 0 )
    {
        throw std::runtime_error( "WideCharToMultiByte() failed [2]." );
    }

    assert( result[size_needed - 1] == '\0' );
    result.resize( size_needed - 1 ); // exclude '\0' from length
    assert( result.length( ) == size_needed - 1 );
    assert( result.c_str( )[result.length( )] == '\0' );

    return result;
}


std::string WStringToUtf8( const std::wstring& s )
{
    const auto size_needed = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, s.c_str( ), CheckedCast( s.length( ) ), nullptr, 0, nullptr, nullptr );
    if( size_needed <= 0 )
    {
        throw std::runtime_error( "WideCharToMultiByte() failed [3]." );
    }

    std::string result;
    result.resize( size_needed );

    if( WideCharToMultiByte( CP_UTF8, 0, s.c_str( ), CheckedCast( s.length( ) ), &result.at( 0 ), size_needed, nullptr, nullptr ) <= 0 )
    {
        throw std::runtime_error( "WideCharToMultiByte() failed [4]." );
    }

    assert( result.length( ) == size_needed );
    assert( result.c_str( )[result.length( )] == '\0' );

    return result;
}


/// <summary>
/// Convert Unicode to UTF-8 and also build a table for conversion of character indices from UTF-8 to Unicode.
/// </summary>
/// <param name="s"></param>
/// <param name="indices"></param>
/// <returns></returns>
std::string WStringToUtf8( const std::wstring& s, std::vector<int>* indices )
{
    const char* old_locale = setlocale( LC_ALL, NULL );
    const char* new_locale = setlocale( LC_CTYPE, ".utf8" );

    if( new_locale == nullptr ) throw std::runtime_error( "Failed to set locale." );

    std::string dest;

    dest.reserve( s.length( ) + 1 );
    indices->reserve( s.length( ) + 1 );

    mbstate_t mbstate = { 0 };
    char buffer[MB_LEN_MAX] = { 0 };

    size_t bytes_written = 0;
    bool is_surrogate_pair = false;

    const wchar_t* start = s.c_str( );

    for( const wchar_t* p = start; *p; ++p ) // (assume that the string is zero-terminated
    {
        indices->resize( bytes_written + 1, -1 ); // ('-1' will denote unset elements)
        if( !is_surrogate_pair )
        {
            ( *indices )[bytes_written] = CheckedCast( p - start );
        }

        size_t size_converted = c16rtomb( buffer, *p, &mbstate );
        assert( size_converted <= MB_LEN_MAX );

        if( size_converted == (size_t)-1 )
        {
            auto error_code = errno;

            setlocale( LC_ALL, old_locale ); // restore

            char error_text[512];
            std::string error_code_s = std::to_string( error_code );
            const char* err = strerror_s( error_text, _countof( error_text ), error_code ) == 0 ? error_text : error_code_s.c_str( );

            throw std::runtime_error( std::format( "Failed to convert to UTF-8: '{}'. Source index: {}.", err, p - start ) );
        }

        dest.append( buffer, size_converted );

        assert( !( is_surrogate_pair && size_converted == 0 ) );

        is_surrogate_pair = size_converted == 0;

        bytes_written += size_converted;
    }

    indices->resize( bytes_written, -1 );
    indices->push_back( CheckedCast( s.length( ) ) );

    setlocale( LC_ALL, old_locale ); // restore

    return dest;
}

