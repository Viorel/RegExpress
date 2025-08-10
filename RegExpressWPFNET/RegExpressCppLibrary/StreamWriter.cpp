#include "pch.h"

#include "StreamWriter.h"
#include "CheckedCast.h"


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

            throw std::runtime_error( std::format( "Failed to write {} bytes (Error {} {:08X})", to_write, le, le ) );
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


// --- 


void StreamWriterW::WriteString( LPCWSTR text ) const
{
    WriteBytes( text, lstrlenW( text ) * sizeof( text[0] ) );
}


void StreamWriterW::WriteString( const std::wstring& text ) const
{
    WriteBytes( text.data( ), CheckedCast( text.size( ) * sizeof( text[0] ) ) );
}


