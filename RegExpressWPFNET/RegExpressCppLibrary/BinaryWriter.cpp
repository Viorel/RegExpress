#include "pch.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "CheckedCast.h"


void BinaryWriter::WriteBytes( const void* buffer0, uint32_t size )
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

        if( written > to_write )
        {
            throw std::runtime_error( "System error" );
        }

        to_write -= written;

        if( to_write == 0 ) break;

        buffer += written;
    }
}


void BinaryWriter::Write7BitEncodedInt( int32_t value )
{
    // From the sources of .NET: https://referencesource.microsoft.com/#mscorlib/system/io/binarywriter.cs,cf806b417abe1a35

    // Write out an int 7 bits at a time.  The high bit of the byte,
    // when on, tells reader to continue reading more bytes.
    unsigned int v = (unsigned int)value;   // support negative numbers
    while( v >= 0x80 )
    {
        WriteT( (uint8_t)( v | 0x80 ) );
        v >>= 7;
    }

    WriteT( (uint8_t)v );
}


// ---


void BinaryWriterA::Write( LPCSTR s )
{
    int charlen = lstrlenA( s );

    Write( s, charlen );
}


void BinaryWriterA::Write( LPCSTR s, uint32_t charlen )
{
    int bytelen = charlen * sizeof( s[0] );

    Write7BitEncodedInt( bytelen );
    WriteBytes( s, bytelen );
}


void BinaryWriterA::Write( const std::string& s )
{
    Write( s.data( ), CheckedCast( s.size( ) * sizeof( s[0] ) ) );
}


// ---


void BinaryWriterW::Write( LPCWSTR s )
{
    int charlen = lstrlenW( s );

    Write( s, charlen );
}


void BinaryWriterW::Write( LPCWSTR s, uint32_t charlen )
{
    int bytelen = charlen * sizeof( s[0] );

    Write7BitEncodedInt( bytelen );
    WriteBytes( s, bytelen );
}


void BinaryWriterW::Write( const std::wstring& s )
{
    Write( s.data( ), CheckedCast( s.size( ) * sizeof( s[0] ) ) );
}

