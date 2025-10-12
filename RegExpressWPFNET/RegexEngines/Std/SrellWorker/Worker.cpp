// SrellWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "CheckedCast.h"
#include "Convert.h"
#include "SEHFilter.h"


static void FindPossibleNames( std::unordered_set<std::wstring>* set, const std::wstring& pattern )
{
    static const std::wstring n = L"n";

    static const srell::wregex regex( LR"REGEX(\(\s*\?\s*<\s*(?![=!])(?<n>.*?)\s*>)REGEX" );

    srell::wcregex_iterator results_begin( pattern.c_str( ), pattern.c_str( ) + pattern.length( ), regex );
    srell::wcregex_iterator results_end{};

    for( auto i = results_begin; i != results_end; ++i )
    {
        const std::wstring& name = i->str( n );

        set->insert( name );
    }
}

static void DoMatch( BinaryWriterW& outbw, const std::wstring& pattern, const std::wstring& text, const std::wstring& localeName,
    srell::wregex::flag_type regexFlags, srell::regex_constants::match_flag_type matchFlags )
{
    ULONG ss = 1024 * 10;
    SetThreadStackGuarantee( &ss );

    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
            {
                std::unordered_set<std::wstring> possible_names;
                FindPossibleNames( &possible_names, pattern );

                srell::wregex regex( pattern.c_str( ), regexFlags );

                srell::wcregex_iterator results_begin( text.c_str( ), text.c_str( ) + text.length( ), regex, matchFlags );
                srell::wcregex_iterator results_end{};

                outbw.WriteT<char>( 'b' );

                for( auto i = results_begin; i != results_end; ++i )
                {
                    const srell::wcmatch& match = *i;

                    outbw.WriteT<char>( 'm' );
                    outbw.WriteT<int64_t>( match.position( ) );
                    outbw.WriteT<int64_t>( match.length( ) );

                    int j = 0;

                    for( auto k = match.cbegin( ); k != match.cend( ); ++k, ++j )
                    {
                        const srell::wcsub_match& submatch = *k;

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

                            // try to find the possible name
                            bool found = false;
                            for( const std::wstring& name : possible_names )
                            {
                                const auto& m = match.operator[]( name );
                                if( !m.matched ) continue; // name not found

                                if( match.position( name ) == match.position( j ) && match.length( name ) == match.length( j ) )
                                {
                                    outbw.Write( name );

                                    found = true;
                                    break;
                                }
                            }

                            if( !found ) outbw.Write( std::wstring{} );
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

            auto v = L"4.100"; // TODO: get it programmatically

            outbw.Write( v );

            return 0;
        }

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );

            srell::wregex::flag_type regex_flags{};

            std::wstring grammar_s = inbr.ReadString( );
            if( grammar_s == L"ECMAScript" ) regex_flags |= srell::regex_constants::syntax_option_type::ECMAScript;
            else if( grammar_s == L"basic" ) regex_flags |= srell::regex_constants::syntax_option_type::basic;
            else if( grammar_s == L"extended" ) regex_flags |= srell::regex_constants::syntax_option_type::extended;
            else if( grammar_s == L"awk" ) regex_flags |= srell::regex_constants::syntax_option_type::awk;
            else if( grammar_s == L"grep" ) regex_flags |= srell::regex_constants::syntax_option_type::grep;
            else if( grammar_s == L"egrep" ) regex_flags |= srell::regex_constants::syntax_option_type::egrep;

            std::wstring locale_s = inbr.ReadString( );

            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::icase;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::nosubs;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::optimize;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::collate;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::multiline;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::dotall;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::unicodesets;
            if( inbr.ReadByte( ) ) regex_flags |= srell::regex_constants::syntax_option_type::vmode;

            srell::regex_constants::match_flag_type match_flags = srell::regex_constants::match_flag_type::match_default;

            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_not_bol;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_not_eol;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_not_bow;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_not_eow;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_any;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_not_null;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_continuous;
            if( inbr.ReadByte( ) ) match_flags |= srell::regex_constants::match_flag_type::match_prev_avail;

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
