// GretaWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <exception>
#include <cassert>
#include <iostream>
#include <iterator>
#include <string>

#include "./Greta/regexpr2.h"

namespace
{
	// Copied here and simplified for C++14.

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
						throw std::runtime_error( "Invalid hexadecimal" );

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
					throw std::runtime_error( "Invalid escaped character" );
				}
			}
			break;

			default:
				if( c < ' ' ) throw std::runtime_error( "Invalid character" );

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


int main( )
{
	using namespace regex;

	try
	{
		std::wstring pattern;
		std::wstring text;
		std::string flags;

#if 0
		// for experiments
		//pattern = L"\\$(\\d+)(\\.(\\d\\d))?";
		pattern = L".*";
		//.......0123456789012345678901234567890123456789
		//text = L"The book cost $12.34, $777";
		text = L"a\r\nb\r\nc";
		flags = "";
#else
		std::string patternS;
		std::string textS;
		std::string syntaxS;
		std::string localeS;
		std::string flagsS;

		std::getline( std::cin, patternS );
		std::getline( std::cin, textS );
		std::getline( std::cin, flagsS );

		std::string patternA;
		std::string textA;

		if( !ParseString( &patternA, patternS.c_str( ) ) ) throw std::runtime_error( "cannot read pattern: '" + patternS + '\'' );
		if( !ParseString( &textA, textS.c_str( ) ) ) throw std::runtime_error( "cannot read text: '" + textS + '\'' );
		if( !ParseString( &flags, flagsS.c_str( ) ) ) throw std::runtime_error( "cannot read flags: '" + flagsS + '\'' );

		pattern = UTF8_to_wchar( patternA.c_str( ) );
		text = UTF8_to_wchar( textA.c_str( ) );
#endif

		REGEX_FLAGS patflags = ALLBACKREFS;
		REGEX_MODE mode = REGEX_MODE::MODE_DEFAULT;

		for( auto const f : flags )
		{
			switch( f )
			{
			case ' ': break; // ignore

			case 'i': patflags |= NOCASE; break;
			case 'm': patflags |= MULTILINE; break;
			case 's': patflags |= SINGLELINE; break;
			case 'x': patflags |= EXTENDED; break;
			case 'R': patflags |= RIGHTMOST; break;
			case 'N': patflags |= NORMALIZE; break;

			case 'F': mode = REGEX_MODE::MODE_FAST; break;
			case 'S': mode = REGEX_MODE::MODE_SAFE; break;
			case 'M': mode = REGEX_MODE::MODE_MIXED; break;

			default: throw std::runtime_error( std::string( "Invalid flag: " ) + f );
			}
		}

		rpattern pat( pattern, patflags, mode );

		// pat.cgroups( ) -- number of groups, including the default one

		for( rpattern::size_type pos = 0; pos < text.length( );)
		{
			match_results results;

			match_results::backref_type br = pat.match( text, results, pos );

			if( !br.matched ) break;

			std::cout << "M " << std::distance( text.cbegin( ), br.begin( ) ) << " " << std::distance( br.begin( ), br.end( ) ) << std::endl;

			for( auto i = 1; i < results.cbackrefs( ); ++i )
			{
				const auto& b = results.backref( i );

				if( !b.matched )
				{
					std::cout << "g -1 -1" << std::endl;
				}
				else
				{
					std::cout << "g " << std::distance( text.cbegin( ), b.begin( ) ) << " " << std::distance( b.begin( ), b.end( ) ) << std::endl;
				}
			}

			auto old_pos = pos;
			pos += results.rstart( ) + results.rlength( );

			if( pos == old_pos )
			{
				if( text[pos] >= 0xD800 && text[pos] <= 0xDBFF ) // surrogate pair
				{
					pos += 2;
				}
				else
				{
					++pos;
				}
			}
		}

		return 0;
	}
	catch( const std::exception& exc ) // including 'regex::bad_regexpr'
	{
		std::cerr << exc.what( ) << std::endl;
	}
	catch( ... )
	{
		std::cerr << "Unknown error" << std::endl;
	}

	return 1;
}
