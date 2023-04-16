// BoostClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"


using namespace std;


#define TO_STR2(s) L#s
#define TO_STR(s) TO_STR2(s)


static void WriteMatch( BinaryWriterW& outbw, const boost::wcmatch& match, const std::map<int, std::wstring>& names, const wchar_t* text )
{

    outbw.WriteT<char>( 'm' );

    outbw.WriteT<int32_t>( CheckedCast( match.position( ) ) );
    outbw.WriteT<int32_t>( CheckedCast( match.length( ) ) );

    int j = 0;

    for( auto iter = match.begin( ); iter != match.end( ); ++iter, ++j )
    {
        const boost::wcsub_match& submatch = *iter;

        std::wstring name;

        auto ni = names.find( j );
        if( ni == names.cend( ) )
        {
            name = std::to_wstring( j );
        }
        else
        {
            name = ni->second;
        }

        outbw.WriteT<char>( 'g' );
        outbw.WriteT<char>( submatch.matched );
        outbw.WriteT<int32_t>( CheckedCast( submatch.matched ? match.position( j ) : 0 ) );
        outbw.WriteT<int32_t>( CheckedCast( submatch.matched ? submatch.length( ) : 0 ) );
        outbw.Write( name );

        for( const boost::wcsub_match& c : submatch.captures( ) )
        {
            if( !c.matched ) continue;

            auto index = c.first - text;

            // WORKAROUND for an apparent problem of Boost Regex: the collection includes captures from other groups
            if( index < match.position( ) ) continue;

            outbw.WriteT<char>( 'c' );
            outbw.WriteT<int32_t>( CheckedCast( index ) );
            outbw.WriteT<int32_t>( CheckedCast( c.length( ) ) );
        }
    }
}


static void DoMatch( BinaryWriterW& outbw, const wstring& pattern, const wstring& text, boost::wregex::flag_type regex_flags, boost::regex_constants::match_flag_type match_flags,
    const std::vector<wstring>& possible_group_names )
{
    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
        {
            boost::wregex re( pattern, regex_flags );

            boost::wcregex_iterator results_begin( text.c_str( ), text.c_str( ) + text.length( ), re, match_flags );
            boost::wcregex_iterator results_end{};

            outbw.WriteT<char>( 'b' );

            for( auto iter = results_begin; iter != results_end; ++iter )
            {
                const boost::wcmatch& match = *iter;

                std::map<int, std::wstring> names;

                for( auto& name : possible_group_names )
                {
                    int i = match.named_subexpression_index( name.c_str( ), name.c_str( ) + name.length( ) );
                    if( i >= 0 )
                    {
                        names[i] = name;
                    }
                }

                WriteMatch( outbw, match, names, text.c_str( ) );
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

            /*
            From Boost documentation:
                BOOST_VERSION
                <boost/version.hpp>
                Describes the boost version number in XYYYZZ format such that:
                (BOOST_VERSION % 100) is the sub-minor version,
                ((BOOST_VERSION / 100) % 1000) is the minor version,
                and (BOOST_VERSION / 100000) is the major version.
            */

            auto version = std::format( L"{}.{}.{}", BOOST_VERSION / 100000, ( BOOST_VERSION / 100 ) % 1000, BOOST_VERSION % 100 );

            outbw.Write( version );

            return 0;
        }

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );

            std::wstring grammar = inbr.ReadString( );

            // Syntax options

            boost::wregex::flag_type regex_flags{};

            if( grammar == L"normal" ) regex_flags |= boost::regex_constants::normal;
            else if( grammar == L"ECMAScript" ) regex_flags |= boost::regex_constants::ECMAScript;
            else if( grammar == L"JavaScript" ) regex_flags |= boost::regex_constants::JavaScript;
            else if( grammar == L"JScript" ) regex_flags |= boost::regex_constants::JScript;
            else if( grammar == L"perl" ) regex_flags |= boost::regex_constants::perl;
            else if( grammar == L"extended" ) regex_flags |= boost::regex_constants::extended;
            else if( grammar == L"egrep" ) regex_flags |= boost::regex_constants::egrep;
            else if( grammar == L"awk" ) regex_flags |= boost::regex_constants::awk;
            else if( grammar == L"basic" ) regex_flags |= boost::regex_constants::basic;
            else if( grammar == L"sed" ) regex_flags |= boost::regex_constants::sed;
            else if( grammar == L"grep" ) regex_flags |= boost::regex_constants::grep;
            else if( grammar == L"emacs" ) regex_flags |= boost::regex_constants::emacs;
            else if( grammar == L"literal" ) regex_flags |= boost::regex_constants::literal;

            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::icase;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::nosubs;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::optimize;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::collate;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::no_except;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::no_mod_m;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::no_mod_s;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::mod_s;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::mod_x;
            if( inbr.ReadByte( ) ) regex_flags |= boost::regex_constants::no_empty_expressions;

            // Match options

            boost::regex_constants::match_flag_type match_flags = boost::regex_constants::match_flag_type::match_default;

            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_bob;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_eob;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_bol;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_eol;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_bow;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_eow;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_any;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_null;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_continuous;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_partial;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_extra;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_single_line;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_prev_avail;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_dot_newline;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_not_dot_null;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_posix;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_perl;
            if( inbr.ReadByte( ) ) match_flags |= boost::regex_constants::match_flag_type::match_nosubs;

            // Possible group names

            std::vector<wstring> possible_group_names;

            std::int16_t number_of_possible_group_names = inbr.ReadT<std::int16_t>( );
            for( int i = 0; i < number_of_possible_group_names; ++i ) possible_group_names.push_back( inbr.ReadString( ) );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            DoMatch( outbw, pattern, text, regex_flags, match_flags, possible_group_names );

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

