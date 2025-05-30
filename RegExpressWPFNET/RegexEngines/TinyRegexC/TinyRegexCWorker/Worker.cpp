// TinyRegexCWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"

#include "re.h"


using namespace std;


static void DoMatch( BinaryWriterA& outbw, const string& pattern, const string& text, const string& flags )
{

    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
            {
                bool match_all = flags.find( 'A' ) != string::npos;

                re_t re = re_compile( pattern.c_str( ) );

                if( re == nullptr )
                {
                    throw std::runtime_error( "'re_compile' failed." );
                }

                outbw.WriteT<char>( 'b' );

                for( int start_index = 0; start_index < text.length( );)
                {
                    int match_length;
                    int match_index = re_matchp( re, text.c_str( ) + start_index, &match_length );

                    if( match_index < 0 ) break;

                    int absolute_index = match_index + start_index;

                    // validate

                    if( absolute_index > text.length( ) )
                    {
                        throw std::runtime_error( std::format( "'match_index' ({}) returned by 're_matchp' exceeds the length of text ({}).", absolute_index, text.length( ) ) );
                    }

                    if( match_length < 0 )
                    {
                        throw std::runtime_error( std::format( "'match_length' ({}) returned by 're_matchp' is negative.", match_length ) );
                    }

                    if( absolute_index + match_length > text.length( ) )
                    {
                        throw std::runtime_error( std::format( "'match_length' ({}) returned by 're_matchp' at {} exceeds the length of text ({}).", match_length, absolute_index, text.length( ) ) );
                    }

                    // write match

                    outbw.WriteT<char>( 'm' );
                    outbw.WriteT<int64_t>( absolute_index );
                    outbw.WriteT<int64_t>( match_length );

                    if( !match_all ) break;

                    int next_start_index = absolute_index + match_length;

                    start_index = next_start_index <= start_index ? start_index + 1 : next_start_index;
                }

                outbw.WriteT<char>( 'e' );

            }( );

        return;
    }
    __except( code = GetExceptionCode( ), SEHFilter( code, error_text, _countof( error_text ) ) )
    {
        // things done in filter
    }

    throw std::runtime_error( error_text );
}


int APIENTRY wWinMain( _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    auto herr = GetStdHandle( STD_ERROR_HANDLE );
    if( herr == INVALID_HANDLE_VALUE )
    {
        auto lerr = GetLastError( );

        return 1;
    }

    StreamWriterA errwr( herr );

    auto hin = GetStdHandle( STD_INPUT_HANDLE );
    if( hin == INVALID_HANDLE_VALUE )
    {
        errwr.WriteString( "Cannot get STDIN" );

        return 2;
    }

    auto hout = GetStdHandle( STD_OUTPUT_HANDLE );
    if( hout == INVALID_HANDLE_VALUE )
    {
        errwr.WriteString( "Cannot get STDOUT" );

        return 3;
    }

    try
    {
        BinaryWriterA outbw( hout );
        BinaryReaderA inbr( hin );

        std::string command = inbr.ReadString( );

        // 

        if( command == "v" )
        {
            // get version

            auto v = "2022-06-21";

            outbw.Write( v );

            return 0;
        }

        if( command == "m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::string pattern = inbr.ReadString( );
            std::string text = inbr.ReadString( );
            std::string flags = inbr.ReadString( );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            DoMatch( outbw, pattern, text, flags );

            return 0;
        }

        errwr.WriteStringF( "Unsupported command: '{}'.", command );

        return 1;
    }
    catch( const std::exception& exc )
    {
        errwr.WriteString( exc.what( ) );

        return 12;
    }
    catch( ... )
    {
        errwr.WriteString( "Internal error" );

        return 14;
    }
}

