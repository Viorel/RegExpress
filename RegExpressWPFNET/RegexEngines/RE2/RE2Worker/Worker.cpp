// RE2Worker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"

#include "re2/re2.h"


using namespace std;


static void DoMatch( BinaryWriterW& outbw, const wstring& wpattern, const wstring& wtext, RE2::Options& re2Options, RE2::Anchor anchor )
{

    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
        {
            // TODO: optimise, avoid duplicates of Unicode and UTF-8 strings.

            re2Options.set_log_errors( false ); // do not write logs to STDERR

            std::string pattern = WStringToUtf8( wpattern.c_str( ) );
            re2::StringPiece sp_pattern( pattern );

            re2::RE2 re( sp_pattern, re2Options );

            if( !re.ok( ) )
            {
                throw std::runtime_error( std::format( "Error {}: {}", (int)re.error_code( ), re.error( ) ) );
            }

            std::vector<int> indices;
            std::string text = WStringToUtf8( wtext.c_str( ), &indices );
            re2::StringPiece sp_text( text ); 

            int number_of_capturing_groups = re.NumberOfCapturingGroups( );

            std::vector<re2::StringPiece> found_groups;
            found_groups.resize( number_of_capturing_groups + 1 ); // (including main match)

            const std::map<int, std::string>& group_names = re.CapturingGroupNames( );

            int start_pos = 0;
            int previous_start_pos = 0;

            outbw.WriteT<char>( 'b' );

            while( re.Match(
                sp_text,
                start_pos,
                sp_text.size( ),
                anchor,
                found_groups.data( ),
                CheckedCast( found_groups.size( ) ) )
                )
            {
                const re2::StringPiece& main_group = found_groups.front( );

                // output the match and groups

                int utf8index = CheckedCast( main_group.data( ) - text.data( ) );
                int index = indices.at( utf8index );
                if( index < 0 )
                {
                    // for example, '\B' with surrogate pairs
                    throw std::runtime_error( std::format( "Index error. (UTF8 Index A = {}).", utf8index ) );
                }

                int next_index = indices.at( utf8index + main_group.size( ) );
                if( next_index < 0 )
                {
                    // for example, '\C' in pattern -- match one byte
                    // TODO: find a more appropriate error text
                    throw std::runtime_error( std::format( "Index error. (UTF8 Index B = {}).", utf8index ) );
                }

                int length = next_index - index;

                // match
                outbw.WriteT<char>( 'm' );
                outbw.WriteT<int64_t>( index );
                outbw.WriteT<int64_t>( length );

                // default group
                outbw.WriteT<char>( 'g' );
                outbw.WriteT<char>( 1 );
                outbw.WriteT<int64_t>( index );
                outbw.WriteT<int64_t>( length );
                outbw.Write( L"0" );

                // groups
                for( int i = 1; i < found_groups.size( ); ++i )
                {
                    const re2::StringPiece& g = found_groups[i];

                    std::string group_name;
                    auto f = group_names.find( i );
                    if( f != group_names.cend( ) )
                    {
                        group_name = f->second;
                    }
                    else
                    {
                        group_name = std::to_string( i );
                    }

                    outbw.WriteT<char>( 'g' );

                    if( g.data( ) == nullptr ) // failed group
                    {
                        outbw.WriteT<char>( 0 );
                        outbw.WriteT<int64_t>( 0 );
                        outbw.WriteT<int64_t>( 0 );
                        outbw.Write( Utf8ToWString( group_name ) ); // (it is UTF-8)
                    }
                    else
                    {
                        int utf8index = CheckedCast( g.data( ) - text.data( ) );
                        int index = indices.at( utf8index );
                        if( index < 0 )
                        {
                            throw std::runtime_error( std::format( "Index error. (UTF8 Index C = {}).", utf8index ) );
                        }

                        int next_index = indices.at( utf8index + g.size( ) );
                        if( next_index < 0 )
                        {
                            throw std::runtime_error( std::format( "Index error. (UTF8 Index D = {}).", utf8index ) );
                        }

                        outbw.WriteT<char>( 1 );
                        outbw.WriteT<int64_t>( index );
                        outbw.WriteT<int64_t>( next_index - index );
                        outbw.Write( Utf8ToWString( group_name ) ); // (it is UTF-8)
                    }
                }

                // advance to the next character after the found match

                start_pos = CheckedCast( main_group.data( ) + main_group.size( ) - text.c_str( ) );

                if( start_pos == previous_start_pos ) // was empty match
                {
                    assert( main_group.size( ) == 0 );

                    // advance by the size of current utf-8 element

                    do { ++start_pos; } while( start_pos < indices.size( ) && indices.at( start_pos ) < 0 );
                }

                if( start_pos > text.length( ) ) break; // end of matches

                previous_start_pos = start_pos;
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

            auto v = L"2023-03-01";

            outbw.Write( v );

            return 0;
        }

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring wpattern = inbr.ReadString( );
            std::wstring wtext = inbr.ReadString( );

            RE2::Options re2_options{};
            RE2::Anchor re2_anchor = RE2::Anchor::UNANCHORED;

            re2_options.set_posix_syntax( inbr.ReadByte( ) != 0 );
            re2_options.set_longest_match( inbr.ReadByte( ) != 0 );
            re2_options.set_literal( inbr.ReadByte( ) != 0 );
            re2_options.set_never_nl( inbr.ReadByte( ) != 0 );
            re2_options.set_dot_nl( inbr.ReadByte( ) != 0 );
            re2_options.set_never_capture( inbr.ReadByte( ) != 0 );
            re2_options.set_case_sensitive( inbr.ReadByte( ) != 0 );
            re2_options.set_perl_classes( inbr.ReadByte( ) != 0 );
            re2_options.set_word_boundary( inbr.ReadByte( ) != 0 );
            re2_options.set_one_line( inbr.ReadByte( ) != 0 );

            std::wstring anchor_s = inbr.ReadString( );

            if( anchor_s == L"UNANCHORED" ) re2_anchor = RE2::Anchor::UNANCHORED;
            else if( anchor_s == L"ANCHOR_START" ) re2_anchor = RE2::Anchor::ANCHOR_START;
            else if( anchor_s == L"ANCHOR_BOTH" ) re2_anchor = RE2::Anchor::ANCHOR_BOTH;

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );


            DoMatch( outbw, wpattern, wtext, re2_options, re2_anchor );

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

