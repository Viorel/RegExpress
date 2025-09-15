// StdWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "CheckedCast.h"
#include "Convert.h"
#include "SEHFilter.h"


using namespace std;


#define TO_STR2(s) L#s
#define TO_STR(s) TO_STR2(s)


long Variable_REGEX_MAX_STACK_COUNT;
long Variable_REGEX_MAX_COMPLEXITY_COUNT;
extern long Default_REGEX_MAX_STACK_COUNT;
extern long Default_REGEX_MAX_COMPLEXITY_COUNT;


static void DoMatch( BinaryWriterW& outbw, const wstring& pattern, const wstring& text, const wstring& localeName, wregex::flag_type regexFlags, regex_constants::match_flag_type matchFlags )
{
    ULONG ss = 1024 * 10;
    SetThreadStackGuarantee( &ss );

    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
            {
                wregex regex{};

                std::locale loc( WStringToUtf8( localeName ) ); // "" -- use default system locale, "C" -- C language locale, "POSIX" -- does not seem supported
                regex.imbue( loc );

                regex.assign( pattern, regexFlags );

                wcregex_iterator results_begin( text.c_str( ), text.c_str( ) + text.length( ), regex, matchFlags );
                wcregex_iterator results_end{};

                outbw.WriteT<char>( 'b' );

                for( auto i = results_begin; i != results_end; ++i )
                {
                    const std::wcmatch& match = *i;

                    outbw.WriteT<char>( 'm' );
                    outbw.WriteT<int64_t>( match.position( ) );
                    outbw.WriteT<int64_t>( match.length( ) );

                    int j = 0;

                    for( auto k = match.cbegin( ); k != match.cend( ); ++k, ++j )
                    {
                        const std::wcsub_match& submatch = *k;

                        outbw.WriteT<char>( 'g' );

                        if( !submatch.matched )
                        {
                            outbw.WriteT<int64_t>( -1 );
                            outbw.WriteT<int64_t>( -1 );
                        }
                        else
                        {
                            outbw.WriteT<int64_t>( match.position( j ) );
                            outbw.WriteT<int64_t>( match.length( j ) );
                        }
                    }
                }

                outbw.WriteT<char>( 'e' );
            }( );

        return;
    }
    __except( code = GetExceptionCode( ), SEHFilter( code, error_text, _countof( error_text ) ) )
    {
        // NOTE. Destructors were not called, and will not be called

        if( code == EXCEPTION_STACK_OVERFLOW )
        {
            if( _resetstkoflw( ) == 0 )
            {
                // TODO: consider returning exit codes in case of dangerous exceptions
                //_exit( ... );
            }
        }

        // more things done in filter
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

            // see "crtversion.h"

            auto v = TO_STR( _VC_CRT_MAJOR_VERSION ) L"." TO_STR( _VC_CRT_MINOR_VERSION ) L"." TO_STR( _VC_CRT_BUILD_VERSION );

            outbw.Write( v );

            return 0;
        }

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );

            wregex::flag_type regex_flags{};

            std::wstring grammar_s = inbr.ReadString( );
            if( grammar_s == L"ECMAScript" ) regex_flags |= regex_constants::syntax_option_type::ECMAScript;
            else if( grammar_s == L"basic" ) regex_flags |= regex_constants::syntax_option_type::basic;
            else if( grammar_s == L"extended" ) regex_flags |= regex_constants::syntax_option_type::extended;
            else if( grammar_s == L"awk" ) regex_flags |= regex_constants::syntax_option_type::awk;
            else if( grammar_s == L"grep" ) regex_flags |= regex_constants::syntax_option_type::grep;
            else if( grammar_s == L"egrep" ) regex_flags |= regex_constants::syntax_option_type::egrep;

            std::wstring locale_s = inbr.ReadString( );

            if( inbr.ReadByte( ) ) regex_flags |= regex_constants::syntax_option_type::icase;
            if( inbr.ReadByte( ) ) regex_flags |= regex_constants::syntax_option_type::nosubs;
            if( inbr.ReadByte( ) ) regex_flags |= regex_constants::syntax_option_type::optimize;
            if( inbr.ReadByte( ) ) regex_flags |= regex_constants::syntax_option_type::collate;

            regex_constants::match_flag_type match_flags = regex_constants::match_flag_type::match_default;

            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_not_bol;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_not_eol;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_not_bow;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_not_eow;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_any;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_not_null;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_continuous;
            if( inbr.ReadByte( ) ) match_flags |= regex_constants::match_flag_type::match_prev_avail;

            auto REGEX_MAX_COMPLEXITY_COUNT = inbr.ReadOptional<int32_t>( );
            Variable_REGEX_MAX_COMPLEXITY_COUNT = REGEX_MAX_COMPLEXITY_COUNT.value_or( Default_REGEX_MAX_COMPLEXITY_COUNT );

            auto REGEX_MAX_STACK_COUNT = inbr.ReadOptional<int32_t>( );
            Variable_REGEX_MAX_STACK_COUNT = REGEX_MAX_STACK_COUNT.value_or( Default_REGEX_MAX_STACK_COUNT );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            DoMatch( outbw, pattern, text, locale_s, regex_flags, match_flags );

            return 0;
        }

        errwr.WriteStringF( L"Unsupported command: '{}'.", command );

        return 1;
    }
    catch( const std::exception& exc )
    {
        errwr.WriteString( ToWString( exc.what( ) ) );

        return 12;
    }
    catch( ... )
    {
        errwr.WriteString( L"Internal error" );

        return 14;
    }
}
