#include <cassert>
#include <iostream>
#include <string>
//#include <format> // does not seem to be implemented
#include <locale>
#include <stdexcept>
#include <regex>


bool ParseString( std::string* destination, const char* source )
{
    if( source == nullptr ) throw std::runtime_error( "Source is null" );
    if( destination == nullptr ) throw std::runtime_error( "Destination is null" );
    if( destination->c_str( ) == source ) throw std::runtime_error( "Source and destination cannot overlap" ); // TODO: perform correct tests

    auto& dest = *destination;

    dest.clear( );

    if( *source != '"' ) return false;

    for( ;; )
    {
        char c = *++source;

        if( c == '"' ) break;

        switch( c )
        {
        case '\0':
            throw std::runtime_error( "Unterminated string" );
        case '\\':
        {
            c = *++source;

            switch( c )
            {
            case '\0':
                throw std::runtime_error( "Unterminated string" );
            case '"':
            case '\\':
            case '/':
                dest += c;
                break;
            case 'b':
                dest += '\b';
                break;
            case 'f':
                dest += '\f';
                break;
            case 'n':
                dest += '\n';
                break;
            case 'r':
                dest += '\r';
                break;
            case 't':
                dest += '\t';
                break;
            case 'u':
            {
                assert( *source == 'u' );

                ++source;
                if( !std::isxdigit( source[0] ) || !std::isxdigit( source[1] ) || !std::isxdigit( source[2] ) || !std::isxdigit( source[3] ) )
                    throw std::runtime_error( std::format( "Invalid hexadecimal: \\u{:.4}", source ) );

                unsigned int value = 0;

                auto n = sscanf_s( source, "%4x", &value ); // TODO: optimise, use bit manipulations
                assert( n == 1 );

                // to UTF-8
                if( value <= 0x7F ) {
                    dest += char( value );
                }
                else if( value <= 0x7FF ) {
                    dest += char( 0xC0 | ( ( value >> 6 ) & 0x1F ) );
                    dest += char( 0x80 | ( value & 0x3F ) );
                }
                else
                {
                    dest += char( 0xE0 | ( ( value >> 12 ) & 0x0F ) );
                    dest += char( 0x80 | ( ( value >> 6 ) & 0x3F ) );
                    dest += char( 0x80 | ( value & 0x3F ) );
                }

                source += 3;
            }
            break;
            default:
                throw std::runtime_error( std::format( "Invalid escaped character: 0x{:04X}", c ) );
            }
        }
        break;

        default:
            if( c < ' ' ) throw std::runtime_error( std::format( "Invalid character: 0x{:04X}", c ) );

            dest += c;
        }
    }

    return true;
}

std::wstring UTF8_to_wchar( const char* in ) // (see also 'Utf8ToWString' from 'Convert.h')
{
    // based on https://stackoverflow.com/questions/148403/utf8-to-from-wide-char-conversion-in-stl

    std::wstring out;
    unsigned int codepoint = 0;

    while( *in != 0 )
    {
        unsigned char ch = static_cast<unsigned char>( *in );

        if( ch <= 0x7f )
            codepoint = ch;
        else if( ch <= 0xbf )
            codepoint = ( codepoint << 6 ) | ( ch & 0x3f );
        else if( ch <= 0xdf )
            codepoint = ch & 0x1f;
        else if( ch <= 0xef )
            codepoint = ch & 0x0f;
        else
            codepoint = ch & 0x07;

        ++in;

        if( ( ( *in & 0xc0 ) != 0x80 ) && ( codepoint <= 0x10ffff ) )
        {
            if( sizeof( std::wstring::value_type ) > 2 )
            {
                assert( false ); // not expected yet
                assert( sizeof( std::wstring::value_type ) == 2 );

                out.append( 1, std::wstring::value_type( codepoint ) );
            }
            else if( codepoint > 0xffff )
            {
                codepoint -= 0x10000;
                out.append( 1, std::wstring::value_type( 0xd800 + ( codepoint >> 10 ) ) );
                out.append( 1, std::wstring::value_type( 0xdc00 + ( codepoint & 0x03ff ) ) );
            }
            else if( codepoint < 0xd800 || codepoint >= 0xe000 )
                out.append( 1, std::wstring::value_type( codepoint ) );
            else
            {
                // high surrogate: U+D800..U+DBFF, low surrogate U+DC00..U+DFFF
                // TODO: validate surrogate pair
                out.append( 1, std::wstring::value_type( codepoint ) );
            }
        }
    }

    return out;
}

using namespace std;

int main( )
{
    try
    {
        string commandS;
        std::getline( std::cin, commandS );

        string commandA;
        if( !ParseString( &commandA, commandS.c_str( ) ) ) throw std::runtime_error( "cannot read command: '" + commandS + '\'' );

        if( commandA == "v" )
        {
            wcout << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__ << endl;
        }
        else if( commandA == "m" )
        {
            string patternS;
            string textS;
            string syntaxS;
            string localeS;
            string flagsS;

            std::getline( std::cin, patternS );
            std::getline( std::cin, textS );
            std::getline( std::cin, syntaxS );
            std::getline( std::cin, localeS );
            std::getline( std::cin, flagsS );

            string patternA;
            string textA;
            string syntaxA;
            string localeA;
            string flagsA;

            if( !ParseString( &patternA, patternS.c_str( ) ) ) throw std::runtime_error( "cannot read pattern: '" + patternS + '\'' );
            if( !ParseString( &textA, textS.c_str( ) ) ) throw std::runtime_error( "cannot read text: '" + textS + '\'' );
            if( !ParseString( &syntaxA, syntaxS.c_str( ) ) ) throw std::runtime_error( "cannot read syntax: '" + syntaxS + '\'' );
            if( !ParseString( &localeA, localeS.c_str( ) ) ) throw std::runtime_error( "cannot read locale: '" + localeS + '\'' );
            if( !ParseString( &flagsA, flagsS.c_str( ) ) ) throw std::runtime_error( "cannot read flags: '" + flagsS + '\'' );

            wstring patternW = UTF8_to_wchar( patternA.c_str( ) );
            wstring textW = UTF8_to_wchar( textA.c_str( ) );

            wregex::flag_type regexFlags{};

            if( syntaxA == "ECMAScript" || syntaxA.length( ) == 0 ) regexFlags |= regex_constants::ECMAScript;
            else if( syntaxA == "basic" ) regexFlags |= regex_constants::basic;
            else if( syntaxA == "extended" ) regexFlags |= regex_constants::extended;
            else if( syntaxA == "awk" ) regexFlags |= regex_constants::awk;
            else if( syntaxA == "grep" ) regexFlags |= regex_constants::grep;
            else if( syntaxA == "egrep" ) regexFlags |= regex_constants::egrep;
            else throw std::runtime_error( "Invalid syntax: '" + syntaxA + '\'' );

            flagsA = ' ' + flagsA + ' ';

            // does not seem implemented
            //if(flagsA.contains(" icase "));

            if( flagsA.find( " icase " ) != std::string::npos ) regexFlags |= regex_constants::icase;
            if( flagsA.find( " nosubs " ) != std::string::npos ) regexFlags |= regex_constants::nosubs;
            if( flagsA.find( " optimize " ) != std::string::npos ) regexFlags |= regex_constants::optimize;
            if( flagsA.find( " collate " ) != std::string::npos ) regexFlags |= regex_constants::collate;
            if( flagsA.find( " multiline " ) != std::string::npos ) regexFlags |= regex_constants::multiline;
            if( flagsA.find( " polynomial " ) != std::string::npos ) regexFlags |= regex_constants::__polynomial;

            regex_constants::match_flag_type matchFlags = regex_constants::match_default;

            if( flagsA.find( " match_not_bol " ) != std::string::npos ) matchFlags |= regex_constants::match_not_bol;
            if( flagsA.find( " match_not_eol " ) != std::string::npos ) matchFlags |= regex_constants::match_not_eol;
            if( flagsA.find( " match_not_bow " ) != std::string::npos ) matchFlags |= regex_constants::match_not_bow;
            if( flagsA.find( " match_not_eow " ) != std::string::npos ) matchFlags |= regex_constants::match_not_eow;
            if( flagsA.find( " match_any " ) != std::string::npos ) matchFlags |= regex_constants::match_any;
            if( flagsA.find( " match_not_null " ) != std::string::npos ) matchFlags |= regex_constants::match_not_null;
            if( flagsA.find( " match_continuous " ) != std::string::npos ) matchFlags |= regex_constants::match_continuous;
            if( flagsA.find( " match_prev_avail " ) != std::string::npos ) matchFlags |= regex_constants::match_prev_avail;

            wregex regex;

            std::locale loc( localeA ); // "" -- use default system locale, "C" -- C language locale, "POSIX" -- POSIX
            regex.imbue( loc );

            regex.assign( patternW, regexFlags );

            wcregex_iterator results_begin( textW.c_str( ), textW.c_str( ) + textW.length( ), regex, matchFlags );
            wcregex_iterator results_end{};

            for( auto i = results_begin; i != results_end; ++i )
            {
                const std::wcmatch& match = *i;

                wcout << L"m " << i->position( ) << L" " << i->length( ) << endl; // ('wchar_t' units)

                int j = 0;

                for( auto k = match.cbegin( ); k != match.cend( ); ++k, ++j )
                {
                    if( j == 0 ) continue; // (first is the full match)

                    const std::wcsub_match& submatch = *k;

                    if( !submatch.matched )
                    {
                        wcout << L" g -1 -1" << endl;
                    }
                    else
                    {
                        wcout << L" g " << match.position( j ) << L" " << match.length( j ) << endl; // ('wchar_t' units)
                    }
                }
            }
        }
        else
        {
            throw std::runtime_error( "Invalid command: '" + commandA + "'" );
        }
    }
    catch( const std::regex_error& exc )
    {
        wcerr << "[code: " << exc.code( ) << "] " << exc.what( ) << endl;
    }
    catch( const std::exception& exc )
    {
        wcerr << exc.what( ) << endl;
    }
    catch( ... )
    {
        wcerr << L"Unknown error" << endl;
    }
}
