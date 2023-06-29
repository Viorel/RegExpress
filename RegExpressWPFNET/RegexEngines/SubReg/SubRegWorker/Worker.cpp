// SubRegWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"

#include "subreg.h"


using namespace std;


static void DoMatch( BinaryWriterA& outbw, const string& pattern, const string& text, int32_t maxDepth )
{

    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
        {
            // TODO: optimise, avoid duplicates of Unicode and UTF-8 strings.

            const int MAX_CAPTURES = 100;
            std::unique_ptr<subreg_capture_t[]> captures( new subreg_capture_t[MAX_CAPTURES]( ) );

            int number_of_matches = subreg_match( pattern.c_str(), text.c_str(), captures.get( ), MAX_CAPTURES, maxDepth);

            if( number_of_matches < 0 )
            {
                const char* err;

                switch( number_of_matches )
                {
                case SUBREG_RESULT_INVALID_ARGUMENT: err = "Invalid argument passed to function."; break;
                case SUBREG_RESULT_ILLEGAL_EXPRESSION: err = "Syntax error found in regular expression."; break;
                case SUBREG_RESULT_MISSING_BRACKET: err = "A closing group bracket is missing from the regular expression."; break;
                case SUBREG_RESULT_SURPLUS_BRACKET: err = "A closing group bracket without a matching opening group bracket has been found."; break;
                case SUBREG_RESULT_INVALID_METACHARACTER: err = "The regular expression contains an invalid metacharacter (typically a malformed \\ escape sequence)"; break;
                case SUBREG_RESULT_MAX_DEPTH_EXCEEDED: err = "The nesting depth of groups contained within the regular expression exceeds the limit specified by max_depth."; break;
                case SUBREG_RESULT_CAPTURE_OVERFLOW: err = "Capture array not large enough."; break;
                case SUBREG_RESULT_INVALID_OPTION: err = "Invalid inline option specified."; break;
                default: err = "Unknown error";
                }

                throw std::runtime_error( err );
            }

            outbw.WriteT<char>( 'b' );

            if( number_of_matches > 0 )
            {
                const subreg_capture_t& capture = captures[0];
                int index = CheckedCast( capture.start - text.c_str() );
                int length = capture.length;

                // match
                outbw.WriteT<char>( 'm' );
                outbw.WriteT<int64_t>( index );
                outbw.WriteT<int64_t>( length );

                // groups
                for( int i = 0; i < number_of_matches; ++i )
                {
                    // (the first is the entire input)

                    const subreg_capture_t& capture = captures[i];
                    int index = CheckedCast( capture.start - text.c_str() );
                    int length = capture.length;

                    outbw.WriteT<char>( 'g' );
                    outbw.WriteT<int64_t>( index );
                    outbw.WriteT<int64_t>( length );
                }
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

            auto v = "2022-01-01";

            outbw.Write( v );

            return 0;
        }

        if( command == "m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::string pattern = inbr.ReadString( );
            std::string text = inbr.ReadString( );

            int32_t max_depth = inbr.ReadT<int32_t>( );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            DoMatch( outbw, pattern, text, max_depth );

            return 0;
        }

        errwr.WriteStringF( "Unsupported command: '%s'", command.c_str( ) );

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

