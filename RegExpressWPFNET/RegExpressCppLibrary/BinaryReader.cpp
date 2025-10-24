#include "pch.h"

#include "BinaryReader.h"
#include "StreamWriter.h"


uint8_t BinaryReader::ReadByte( ) const
{
    uint8_t b;
    DWORD n;

    if( !ReadFile( mHandle, &b, sizeof( b ), &n, NULL ) || n != sizeof( b ) )
    {
        auto le = GetLastError( );

        throw std::runtime_error( std::format( "Cannot read byte (Error {} {:08X})", le, le ) );
    }

    return b;
}


void BinaryReader::ReadBytes( void* buffer0, uint32_t size ) const
{
    static_assert( sizeof( size ) == sizeof( DWORD ), "" );
    static_assert( std::is_signed_v<decltype( size )> == std::is_signed_v<DWORD>, "" );

    char* dest = (char*)buffer0;

    DWORD to_read = size;

    for( ;;)
    {
        DWORD n;
        if( !ReadFile( mHandle, dest, to_read, &n, NULL ) )
        {
            auto le = GetLastError( );

            throw std::runtime_error( std::format( "Failed to read {} bytes (Error {} {:08X})", to_read, le, le ) );
        }

        if( n == 0 && to_read != 0 )
        {
            throw std::runtime_error( std::format( "Failed to read {} bytes", to_read ) );
        }

        if( n > to_read )
        {
            throw std::runtime_error( "System error" );
        }

        to_read -= n;

        if( to_read == 0 ) break;

        dest += n;
    }
}


int BinaryReader::Read7BitEncodedInt( ) const
{
    // From the sources of .NET: https://referencesource.microsoft.com/#mscorlib/system/io/binaryreader.cs,f30b8b6e8ca06e0f

    // Read out an Int32 7 bits at a time.  The high bit
    // of the byte when on means to continue reading more bytes.
    int count = 0;
    int shift = 0;
    uint8_t b;
    do
    {
        // Check for a corrupted stream.  Read a max of 5 bytes.
        // In a future version, add a DataFormatException.
        if( shift == 5 * 7 )  // 5 bytes max per Int32, shift += 7
            throw std::runtime_error( "Format_Bad7BitInt32" );

        // ReadByte handles end of stream cases for us.
        b = ReadByte( );
        count |= ( b & 0x7F ) << shift;
        shift += 7;
    } while( ( b & 0x80 ) != 0 );

    return count;
}


// ---


std::string BinaryReaderA::ReadString( ) const
{
    auto bytelen = Read7BitEncodedInt( );

    std::string s;

    if( bytelen != 0 )
    {
        static_assert( sizeof( s[0] ) == 1, "" );

        s.resize( bytelen / sizeof( s[0] ) );

        ReadBytes( &s[0], bytelen );
    }

    return s;
}


// ---


std::wstring BinaryReaderW::ReadString( ) const
{
    auto bytelen = Read7BitEncodedInt( );

    std::wstring s;

    if( ( bytelen % sizeof( s[0] ) ) != 0 )
    {
        throw std::runtime_error( "Invalid odd string length" );
    }

    s.resize( bytelen / sizeof( s[0] ) );

    ReadBytes( &s[0], bytelen );

    return s;
}

