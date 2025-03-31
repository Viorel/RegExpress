// TREWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"

#define HAVE_CONFIG_H
//#define USE_LOCAL_TRE_H
#define TRE_WCHAR 1
#include "../TRE/TRE/local_includes/tre.h"


using namespace std;

static std::wstring FormatError( reg_errcode_t code )
{
    const wchar_t* symbol = nullptr;
    const wchar_t* message = nullptr;

#define ER(c, m) \
    case c: symbol = L#c; message = m; break;

    switch( code )
    {
        ER( REG_OK, L"No error." );
        ER( REG_NOMATCH, L"No match." );
        ER( REG_BADPAT, L"Invalid regexp." );
        ER( REG_ECOLLATE, L"Unknown collating element." );
        ER( REG_ECTYPE, L"Unknown character class name." );
        ER( REG_EESCAPE, L"Trailing backslash." );
        ER( REG_ESUBREG, L"Invalid back reference." );
        ER( REG_EBRACK, L"\"[]\" imbalance." );
        ER( REG_EPAREN, L"\"\\(\\)\" or \"()\" imbalance." );
        ER( REG_EBRACE, L"\"\\{\\}\" or \"{}\" imbalance." );
        ER( REG_BADBR, L"Invalid content of {}." );
        ER( REG_ERANGE, L"Invalid use of range operator." );
        ER( REG_ESPACE, L"Out of memory." );
        ER( REG_BADRPT, L"Invalid use of repetition operators." );
        ER( REG_BADMAX, L"Maximum repetition in {} too large." );
    }

    if( symbol == nullptr )
    {
        return std::format( L"({}) Unknown error code.", (int)code );
    }
    else
    {
        return std::format( L"({}) {}", symbol, message );
    }
}


static void DoMatch( BinaryWriterW& outbw, StreamWriterW& errwr, const wstring& pattern, const wstring& text, int cflags, int eflags, bool matchAll )
{

    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
            {
                regex_t preg;
                int r;

                r = tre_regwcomp( &preg, pattern.c_str( ), cflags );

                if( r != 0 )
                {
                    errwr.WriteStringF( L"tre_regwcomp failed: {}", FormatError( (reg_errcode_t)r ) );

                    return;
                }

                std::vector<regmatch_t> match( preg.re_nsub + 1 );

                outbw.WriteT<char>( 'b' );

                int start = 0;

                while( start < text.length( ) )
                {
                    r = tre_regwexec( &preg, text.c_str( ) + start, match.size( ), match.data( ), eflags );

                    if( r == REG_NOMATCH )
                    {
                        // no match

                        break;
                    }
                    else if( r != 0 )
                    {
                        // error

                        errwr.WriteStringF( L"tre_regwexec failed: {}", FormatError( (reg_errcode_t)r ) );

                        return;
                    }
                    else
                    {
                        // match found

                        outbw.WriteT<char>( 'm' );
                        outbw.WriteT<int32_t>( start + match[0].rm_so );
                        outbw.WriteT<int32_t>( start + match[0].rm_eo );

                        for( int i = 1; i < match.size( ); ++i )
                        {
                            outbw.WriteT<char>( 'g' );

                            if( match[i].rm_so >= 0 )
                            {
                                outbw.WriteT<int32_t>( start + match[i].rm_so );
                                outbw.WriteT<int32_t>( start + match[i].rm_eo );
                            }
                            else
                            {
                                outbw.WriteT<int32_t>( -1 );
                                outbw.WriteT<int32_t>( -1 );
                            }
                        }

                        if( !matchAll ) break;

                        if( match[0].rm_eo <= 0 )
                        {
                            ++start;
                        }
                        else
                        {
                            start += match[0].rm_eo;
                        }
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

            auto v = L"" TRE_VERSION;

            outbw.Write( v );

            return 0;
        }

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );

            int cflags = 0;

            if( inbr.ReadByte( ) ) cflags |= REG_EXTENDED;
            if( inbr.ReadByte( ) ) cflags |= REG_ICASE;
            if( inbr.ReadByte( ) ) cflags |= REG_NOSUB;
            if( inbr.ReadByte( ) ) cflags |= REG_NEWLINE;
            if( inbr.ReadByte( ) ) cflags |= REG_LITERAL;
            if( inbr.ReadByte( ) ) cflags |= REG_RIGHT_ASSOC;
            if( inbr.ReadByte( ) ) cflags |= REG_UNGREEDY;

            int eflags = 0;

            if( inbr.ReadByte( ) ) eflags |= REG_NOTBOL;
            if( inbr.ReadByte( ) ) eflags |= REG_NOTEOL;

            bool match_all = inbr.ReadByte( ) != 0;

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            DoMatch( outbw, errwr, pattern, text, cflags, eflags, match_all );

            return 0;
        }

        errwr.WriteStringF( L"Unsupported command: '{}'.", command );

        return 1;
    }
    catch( const std::exception& exc )
    {
        errwr.WriteString( Utf8ToWString( exc.what( ) ) ); // (it is UTF-8)

        return 12;
    }
    catch( ... )
    {
        errwr.WriteString( L"Internal error" );

        return 14;
    }
}

