#include "pch.h"

#include <format>
#include <cctype>
#include "PartialJSON.h"

namespace PartialJSON
{
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
                char c = *++source;

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

    std::wstring UTF8_to_wchar( const char* in )
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
}
