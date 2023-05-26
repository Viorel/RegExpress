// ICUWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"


static void Check( UErrorCode status );
static void DoMatch( BinaryWriterW& outbw, const std::wstring& pattern, const std::wstring& text, uint32_t flags, uint32_t limit );


int APIENTRY wWinMain( _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    //{
    //    int argc = 0;
    //    LPWSTR* argv = CommandLineToArgvW( lpCmdLine, &argc );

    //    if( argc < 2 )
    //    {
    //        OutputDebugStringW( L"ICUWorker -- no arguments\r\n" );
    //        OutputDebugStringW( lpCmdLine );

    //        return 30;
    //    }

    //    // argv[1] -- path to ICU DLL files
    //    SetDllDirectoryW( argv[1] );
    //}


    auto herr = GetStdHandle( STD_ERROR_HANDLE );
    if( herr == INVALID_HANDLE_VALUE )
    {
        auto lerr = GetLastError( );

        return 1;
    }

    StreamWriterW errwr( herr );

    auto hin = GetStdHandle( STD_INPUT_HANDLE );
    if( hin == INVALID_HANDLE_VALUE )
    {
        errwr.WriteString( L"Cannot get STDIN" );

        return 2;
    }

    auto hout = GetStdHandle( STD_OUTPUT_HANDLE );
    if( hout == INVALID_HANDLE_VALUE )
    {
        errwr.WriteString( L"Cannot get STDOUT" );

        return 3;
    }

    try
    {
        BinaryWriterW outbw( hout );
        BinaryReaderW inbr( hin );

        std::wstring command = inbr.ReadString( );

        // 

        if( command == L"v" )
        {
            // get version

            auto v = L"" U_ICU_VERSION;

            outbw.Write( v );

            return 0;
        }

        //

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid B." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );
            auto remote_flags = inbr.ReadT<uint32_t>( );
            auto limit = inbr.ReadT<uint32_t>( );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid E." );

            uint32_t flags = 0;
            //if( remote_flags & ( 1 << 0 ) ) flags |= UREGEX_CANON_EQ; // not implemented by ICU
            if( remote_flags & ( 1 << 1 ) ) flags |= UREGEX_CASE_INSENSITIVE;
            if( remote_flags & ( 1 << 2 ) ) flags |= UREGEX_COMMENTS;
            if( remote_flags & ( 1 << 3 ) ) flags |= UREGEX_DOTALL;
            if( remote_flags & ( 1 << 4 ) ) flags |= UREGEX_LITERAL;
            if( remote_flags & ( 1 << 5 ) ) flags |= UREGEX_MULTILINE;
            if( remote_flags & ( 1 << 6 ) ) flags |= UREGEX_UNIX_LINES;
            if( remote_flags & ( 1 << 7 ) ) flags |= UREGEX_UWORD;
            if( remote_flags & ( 1 << 8 ) ) flags |= UREGEX_ERROR_ON_UNKNOWN_ESCAPES;

            DoMatch( outbw, pattern, text, flags, limit );

            return 0;
        }

        errwr.WriteStringF( L"Unsupported command: '%s'", command.c_str( ) );

        return 1;
    }
    catch( const std::exception& exc )
    {
        errwr.WriteString( Utf8ToWString( exc.what( ) ) );

        return 12;
    }
    catch( ... )
    {
        errwr.WriteString( L"Internal error" );

        return 14;
    }
}


static void Check( UErrorCode status )
{
    if( U_FAILURE( status ) )
    {
        LPCSTR error_name = u_errorName( status );

        throw std::runtime_error( std::format( "Error {} ({})", error_name, (unsigned)status ) );
    }
}


static void DoMatch( BinaryWriterW& outbw, const std::wstring& pattern, const std::wstring& text, uint32_t flags, uint32_t limit )
{
    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
        {
            UErrorCode status = U_ZERO_ERROR;
            UParseError parse_error{};

            int32_t pattern_length = CheckedCast( pattern.length( ) );

            icu::UnicodeString us_pattern( pattern.c_str( ), pattern_length );

            icu::RegexPattern* icu_pattern = icu::RegexPattern::compile( us_pattern, flags, parse_error, status );

            if( U_FAILURE( status ) )
            {
                LPCSTR error_name = u_errorName( status );

                throw std::runtime_error( std::format( "Invalid pattern at line {}, column {}.\r\n\r\n({}, {})",
                    parse_error.line, parse_error.offset, error_name, (unsigned)status ) );
            }

            outbw.WriteT<char>( 'b' );

            // try identifying named groups; (ICU does not seem to offer such feature)
            {
                icu::UnicodeString up( LR"REGEX(\(\s*\?\s*<\s*(?![=!])(?<n>.*?)\s*>)REGEX" );
                icu::RegexPattern* p = icu::RegexPattern::compile( up, 0, parse_error, status );
                Check( status );

                icu::RegexMatcher* m = p->matcher( us_pattern, status );
                Check( status );

                for( ;; )
                {
                    status = U_ZERO_ERROR;

                    if( !m->find( status ) )
                    {
                        Check( status );

                        break;
                    }

                    int32_t start = m->start( 1, status );
                    Check( status );

                    int32_t end = m->end( 1, status );
                    Check( status );

                    icu::UnicodeString possible_name;
                    us_pattern.extract( start, end - start, possible_name );
                    Check( status );

                    int32_t group_number = icu_pattern->groupNumberFromName( possible_name, status );
                    // TODO: detect and show errors
                    if( !U_FAILURE( status ) )
                    {
                        outbw.WriteT<int32_t>( group_number );
                        outbw.Write( (LPCWSTR)possible_name.getBuffer( ), possible_name.length( ) );
                    }
                }

                outbw.WriteT<int32_t>( -1 ); // end of names
            }

            // find matches

            int32_t text_length = CheckedCast( text.length( ) );

            icu::UnicodeString us_text( text.c_str( ), text_length );

            icu::RegexMatcher* icu_matcher = icu_pattern->matcher( us_text, status );
            Check( status );

            icu_matcher->setTimeLimit( limit == std::numeric_limits<uint32_t>::max( ) ? 0 : limit, status );
            Check( status );

            for( ;; )
            {
                if( !icu_matcher->find( status ) )
                {
                    Check( status );

                    break;
                }

                int group_count = icu_matcher->groupCount( );

                outbw.WriteT<int32_t>( group_count );

                for( int i = 0; i <= group_count; ++i )
                {
                    int32_t start = icu_matcher->start( i, status );
                    Check( status );
                    outbw.WriteT<int32_t>( start );

                    if( start >= 0 )
                    {
                        int32_t end = icu_matcher->end( i, status );
                        Check( status );
                        outbw.WriteT<int32_t>( end );
                    }
                }
            }

            outbw.WriteT<int32_t>( -1 );
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
