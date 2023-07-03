#include "pch.h"

#include "StreamWriter.h"
#include "CheckedCast.h"


namespace
{
    template<typename T>
    struct StdDeleter
    {
        void operator ()( T* ptr )
        {
            _free_dbg( ptr, _NORMAL_BLOCK );
        };
    };
}


void StreamWriter::WriteBytes( const void* buffer0, uint32_t size ) const
{
    static_assert( sizeof( size ) == sizeof( DWORD ), "" );
    static_assert( std::is_signed_v<decltype( size )> == std::is_signed_v<DWORD>, "" );

    const char* buffer = (const char*)buffer0;
    DWORD to_write = size;
    DWORD written;

    for( ;;)
    {
        if( !WriteFile( mHandle, buffer, to_write, &written, NULL ) )
        {
            auto le = GetLastError( );

            throw std::runtime_error( StreamWriterA::Printf( "Failed to write %i bytes (Error %i %08X)", to_write, le, le ) );
        }

        if( written > to_write ) throw std::runtime_error( "System error" );

        to_write -= written;

        if( to_write == 0 ) break;

        buffer += written;
    }
}


// ---


void StreamWriterA::WriteString( LPCSTR text ) const
{
    WriteBytes( text, lstrlenA( text ) * sizeof( text[0] ) );
}


void StreamWriterA::WriteString( const std::string& text ) const
{
    WriteBytes( text.data( ), CheckedCast( text.size( ) * sizeof( text[0] ) ) );
}


void __cdecl StreamWriterA::WriteStringF( LPCSTR format, ... ) const
{
    char buffer[256];
    bool success = false;

    va_list argptr;
    va_start( argptr, format );

    HRESULT hr = StringCbVPrintfA( buffer, sizeof( buffer ), format, argptr );

    if( SUCCEEDED( hr ) )
    {
        WriteString( buffer );
    }
    else
    {
        int size = lstrlenA( format ) + 128;
        const int step = 512;

        std::unique_ptr<char, StdDeleter<char>> dynbuff;

        for( ;;)
        {
            if( hr != STRSAFE_E_INSUFFICIENT_BUFFER ) throw std::runtime_error( StreamWriterA::Printf( "Failed to write formatted string (%08X)", hr ) );
            if( size >= STRSAFE_MAX_CCH - step ) throw std::runtime_error( "Too long formatted string" );

            size += step;
            char* newbuff = (char*)_realloc_dbg( dynbuff.get( ), size * sizeof( format[0] ), _NORMAL_BLOCK, __FILE__, __LINE__ );

            if( newbuff == 0 ) throw std::runtime_error( "Insufficient memory to format the string" );

            dynbuff.release( );
            dynbuff.reset( newbuff );

            hr = StringCchVPrintfA( dynbuff.get( ), size, format, argptr );

            if( SUCCEEDED( hr ) ) break;
        }

        WriteString( dynbuff.get( ) );
    }

    va_end( argptr );
}


std::string StreamWriterA::Printf( LPCSTR format, ... )
{
    va_list argptr;
    va_start( argptr, format );

    int size = lstrlenA( format ) + 128;
    const int step = 512;
    HRESULT hr;

    std::string result;

    for( ;;)
    {
        result.resize( size );

        hr = StringCchVPrintfA( &result[0], result.size( ), format, argptr );

        if( SUCCEEDED( hr ) ) break;

        if( hr != STRSAFE_E_INSUFFICIENT_BUFFER ) throw std::runtime_error( "Failed to write formatted string" );
        if( size >= STRSAFE_MAX_CCH - step ) throw std::runtime_error( "Too long formatted string" );

        size += step;
    }

    size_t len;

    hr = StringCchLengthA( result.c_str( ), result.size( ), &len );

    if( !SUCCEEDED( hr ) ) throw std::runtime_error( "Failed to get the formatted string length" );

    result.resize( len );
    result.shrink_to_fit( );

    va_end( argptr );

    return result;
}


// --- 


void StreamWriterW::WriteString( LPCWSTR text ) const
{
    WriteBytes( text, lstrlenW( text ) * sizeof( text[0] ) );
}


void StreamWriterW::WriteString( const std::wstring& text ) const
{
    WriteBytes( text.data( ), CheckedCast( text.size( ) * sizeof( text[0] ) ) );
}


void __cdecl StreamWriterW::WriteStringF( LPCWSTR format, ... ) const
{
    wchar_t buffer[256];
    bool success = false;

    va_list argptr;
    va_start( argptr, format );

    HRESULT hr = StringCbVPrintfW( buffer, sizeof( buffer ), format, argptr );

    if( SUCCEEDED( hr ) )
    {
        WriteString( buffer );
    }
    else
    {
        int size = lstrlenW( format ) + 128; // in wchar
        const int step = 512; // in wchar

        std::unique_ptr<wchar_t, StdDeleter<wchar_t>> dynbuff;

        for( ;;)
        {
            if( hr != STRSAFE_E_INSUFFICIENT_BUFFER ) throw std::runtime_error( "Failed to write the formatted string" );

            if( size >= STRSAFE_MAX_CCH - step ) throw std::runtime_error( "Too long formatted string" );

            size += step;
            wchar_t* newbuff = (wchar_t*)_realloc_dbg( dynbuff.get( ), size * sizeof( format[0] ), _NORMAL_BLOCK, __FILE__, __LINE__ );

            if( newbuff == 0 ) throw std::runtime_error( "Insufficient memory to format the string" );

            dynbuff.release( );
            dynbuff.reset( newbuff );

            hr = StringCchVPrintfW( dynbuff.get( ), size, format, argptr );

            if( SUCCEEDED( hr ) ) break;
        }

        WriteString( dynbuff.get( ) );
    }

    va_end( argptr );
}


std::wstring StreamWriterW::Printf( LPCWSTR format, ... )
{
    va_list argptr;
    va_start( argptr, format );

    int size = lstrlenW( format ) + 128; // in wchar
    const int step = 512; // in wchar
    HRESULT hr;

    std::wstring result;

    for( ;;)
    {
        result.resize( size );

        hr = StringCchVPrintfW( &result[0], result.size( ), format, argptr );

        if( SUCCEEDED( hr ) ) break;

        if( hr != STRSAFE_E_INSUFFICIENT_BUFFER ) throw std::runtime_error( "Failed to write the formatted string" );
        if( size >= STRSAFE_MAX_CCH - step ) throw std::runtime_error( "Too long formatted string" );

        size += step;
    }

    size_t len;

    hr = StringCchLengthW( result.c_str( ), result.size( ), &len );

    if( !SUCCEEDED( hr ) ) throw std::runtime_error( "Failed to get the formatted string length" );

    result.resize( len );
    result.shrink_to_fit( );

    va_end( argptr );

    return result;
}

