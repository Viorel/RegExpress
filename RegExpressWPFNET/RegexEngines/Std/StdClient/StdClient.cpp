// StdClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "CheckedCast.h"


using namespace std;


static wstring Utf8ToWString( const char* s )
{
    wstring_convert<codecvt_utf8<wchar_t>> myconv;

    return myconv.from_bytes( s );
}


#define TO_STR2(s) L#s
#define TO_STR(s) TO_STR2(s)


long Variable_REGEX_MAX_STACK_COUNT;
long Variable_REGEX_MAX_COMPLEXITY_COUNT;
extern long Default_REGEX_MAX_STACK_COUNT;
extern long Default_REGEX_MAX_COMPLEXITY_COUNT;


static DWORD SEHFilter( DWORD code, char* errorText, size_t errorTextSize )
{
    const char* text;

    switch( code )
    {

#define E(e) case e: text = #e; break;

        E( EXCEPTION_ACCESS_VIOLATION )
            E( EXCEPTION_DATATYPE_MISALIGNMENT )
            E( EXCEPTION_BREAKPOINT )
            E( EXCEPTION_SINGLE_STEP )
            E( EXCEPTION_ARRAY_BOUNDS_EXCEEDED )
            E( EXCEPTION_FLT_DENORMAL_OPERAND )
            E( EXCEPTION_FLT_DIVIDE_BY_ZERO )
            E( EXCEPTION_FLT_INEXACT_RESULT )
            E( EXCEPTION_FLT_INVALID_OPERATION )
            E( EXCEPTION_FLT_OVERFLOW )
            E( EXCEPTION_FLT_STACK_CHECK )
            E( EXCEPTION_FLT_UNDERFLOW )
            E( EXCEPTION_INT_DIVIDE_BY_ZERO )
            E( EXCEPTION_INT_OVERFLOW )
            E( EXCEPTION_PRIV_INSTRUCTION )
            E( EXCEPTION_IN_PAGE_ERROR )
            E( EXCEPTION_ILLEGAL_INSTRUCTION )
            E( EXCEPTION_NONCONTINUABLE_EXCEPTION )
            E( EXCEPTION_STACK_OVERFLOW )
            E( EXCEPTION_INVALID_DISPOSITION )
            E( EXCEPTION_GUARD_PAGE )
            E( EXCEPTION_INVALID_HANDLE )
            //?E( EXCEPTION_POSSIBLE_DEADLOCK         )

#undef E

    default:
        return EXCEPTION_CONTINUE_SEARCH; // also covers code E06D7363, probably associated with 'throw std::exception'
    }

    StringCchCopyA( errorText, errorTextSize, "SEH Error: " );
    StringCchCatA( errorText, errorTextSize, text );

    return EXCEPTION_EXECUTE_HANDLER;
}


static void DoMatch( BinaryWriterW& outbw, const wstring& pattern, const wstring& text, wregex::flag_type regexFlags, regex_constants::match_flag_type matchFlags )
{
    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
        {
            wregex regex( pattern, regexFlags );

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

                for( auto i = match.cbegin( ); i != match.cend( ); ++i, ++j )
                {
                    const std::wcsub_match& submatch = *i;

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
        // things done in filter
    }

    throw std::exception( error_text );
}


int main( )
{
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

            //auto v = std::to_wstring( _VC_CRT_MAJOR_VERSION ) + L".";// , _VC_CRT_MINOR_VERSION, _VC_CRT_BUILD_VERSION );
            auto v = TO_STR( _VC_CRT_MAJOR_VERSION ) L"." TO_STR( _VC_CRT_MINOR_VERSION ) L"." TO_STR( _VC_CRT_BUILD_VERSION );

            outbw.Write( v );

            return 0;
        }

        if( command == L"m" )
        {
            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );


            wregex::flag_type regex_flags{};

            std::wstring grammar_s = inbr.ReadString( );
            if( grammar_s == L"ECMAScript" )
            {
                regex_flags |= regex_constants::syntax_option_type::ECMAScript;
            }
            else if( grammar_s == L"basic" )
            {
                regex_flags |= regex_constants::syntax_option_type::basic;
            }
            else if( grammar_s == L"extended" )
            {
                regex_flags |= regex_constants::syntax_option_type::extended;
            }
            else if( grammar_s == L"awk" )
            {
                regex_flags |= regex_constants::syntax_option_type::awk;
            }
            else if( grammar_s == L"grep" )
            {
                regex_flags |= regex_constants::syntax_option_type::grep;
            }
            else if( grammar_s == L"egrep" )
            {
                regex_flags |= regex_constants::syntax_option_type::egrep;
            }

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


            if( inbr.ReadByte( ) )
            {
                Variable_REGEX_MAX_COMPLEXITY_COUNT = inbr.ReadT<int32_t>( );
            }
            else
            {
                Variable_REGEX_MAX_COMPLEXITY_COUNT = Default_REGEX_MAX_COMPLEXITY_COUNT;
            }

            if( inbr.ReadByte( ) )
            {
                Variable_REGEX_MAX_STACK_COUNT = inbr.ReadT<int32_t>( );
            }
            else
            {
                Variable_REGEX_MAX_STACK_COUNT = Default_REGEX_MAX_STACK_COUNT;
            }

            DoMatch( outbw, pattern, text, regex_flags, match_flags );


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

