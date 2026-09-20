// SrellWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"


static void FindPossibleNames( std::unordered_set<std::u8string>* set, const std::u8string pattern )
{
	static const std::u8string n = u8"n";
	static const srell::u8regex regex( u8R"REGEX(\(\s*\?\s*<\s*(?![=!])(?<n>.*?)\s*>)REGEX" );

	srell::u8cregex_iterator results_begin( pattern.c_str( ), pattern.c_str( ) + pattern.length( ), regex );
	srell::u8cregex_iterator results_end{};

	for( auto i = results_begin; i != results_end; ++i )
	{
		const std::u8string& name = i->str( n );

		set->insert( name );
	}
}

static void DoMatch( BinaryWriterA& outbw, const std::u8string& pattern, const std::u8string& text, const std::u8string& localeName,
	srel3::u8regex::flag_type regexFlags, srel3::regex_constants::match_flag_type matchFlags,
	std::optional<uint64_t> max_dfacache, std::optional<uint64_t> max_states )
{
	ULONG ss = 1024 * 10;
	SetThreadStackGuarantee( &ss );

	DWORD code;
	char error_text[128] = "";

	__try
	{
		[&]( )
			{
				std::unordered_set<std::u8string> possible_names;
				FindPossibleNames( &possible_names, pattern );

				srel3::u8regex regex( pattern.c_str( ), regexFlags );

				if( max_dfacache.has_value( ) ) regex.max_dfacache( max_dfacache.value( ) );
				if( max_states.has_value( ) ) regex.max_states( CheckedCast( max_states.value( ) ) );

				srel3::u8cregex_iterator results_begin( text.c_str( ), text.c_str( ) + text.length( ), regex, matchFlags );
				srel3::u8cregex_iterator results_end{};

				outbw.WriteT<char>( 'b' );

				for( auto i = results_begin; i != results_end; ++i )
				{
					const srel3::u8cmatch& match = *i;

					outbw.WriteT<char>( 'm' );
					outbw.WriteT<int64_t>( match.position( ) );
					outbw.WriteT<int64_t>( match.length( ) );

					int j = 0;

					for( auto k = match.cbegin( ); k != match.cend( ); ++k, ++j )
					{
						const srel3::u8csub_match& submatch = *k;

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
							for( const std::u8string& name : possible_names )
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

							if( !found ) outbw.Write( std::u8string{} );
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

	setlocale( LC_ALL, ".utf8" );

	auto herr = GetStdHandle( STD_ERROR_HANDLE );
	if( herr == INVALID_HANDLE_VALUE )
	{
		auto lerr = GetLastError( );

		return 1;
	}

	StreamWriterA errwr( herr );

	auto hin = GetStdHandle( STD_INPUT_HANDLE );
	if( hin == INVALID_HANDLE_VALUE )
	{
		errwr.WriteString( "Cannot get STDIN" );

		return 2;
	}

	auto hout = GetStdHandle( STD_OUTPUT_HANDLE );
	if( hout == INVALID_HANDLE_VALUE )
	{
		errwr.WriteString( "Cannot get STDOUT" );

		return 3;
	}

	try
	{
		BinaryWriterA outbw( hout );
		BinaryReaderA inbr( hin );

		std::u8string command = inbr.ReadU8String( );

		// 

		if( command == u8"v" )
		{
			// get version

			// example (from "SRELL/single-header/srel3.hpp"): 
			//   #define SRELL_HPP_ 202602
			auto v = std::format( "{}.{:02}", SREL3_HPP_ / 100, SREL3_HPP_ % 100 ); // TODO: make sure that it still works; it does not seem documented

			outbw.Write( v );

			return 0;
		}

		if( command == u8"m" )
		{
			if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

			std::u8string pattern = inbr.ReadU8String( );
			std::u8string text = inbr.ReadU8String( );

			srel3::u8regex::flag_type regex_flags{};

			std::u8string grammar_s = inbr.ReadU8String( );
			if( grammar_s == u8"ECMAScript" ) regex_flags |= srel3::regex_constants::syntax_option_type::ECMAScript;
			else if( grammar_s == u8"basic" ) regex_flags |= srel3::regex_constants::syntax_option_type::basic;
			else if( grammar_s == u8"extended" ) regex_flags |= srel3::regex_constants::syntax_option_type::extended;
			else if( grammar_s == u8"awk" ) regex_flags |= srel3::regex_constants::syntax_option_type::awk;
			else if( grammar_s == u8"grep" ) regex_flags |= srel3::regex_constants::syntax_option_type::grep;
			else if( grammar_s == u8"egrep" ) regex_flags |= srel3::regex_constants::syntax_option_type::egrep;

			std::u8string locale_s = inbr.ReadU8String( );

			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::icase;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::nosubs;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::optimize;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::collate;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::multiline;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::dotall;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::unicodesets;
			if( inbr.ReadByte( ) ) regex_flags |= srel3::regex_constants::syntax_option_type::vmode;

			srel3::regex_constants::match_flag_type match_flags = srel3::regex_constants::match_flag_type::match_default;

			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_not_bol;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_not_eol;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_not_bow;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_not_eow;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_any;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_not_null;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_continuous;
			if( inbr.ReadByte( ) ) match_flags |= srel3::regex_constants::match_flag_type::match_prev_avail;

			auto max_dfacache = inbr.ReadOptional<uint64_t>( );
			auto max_states = inbr.ReadOptional<uint64_t>( );

			if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

			DoMatch( outbw, pattern, text, locale_s, regex_flags, match_flags, max_dfacache, max_states );

			return 0;
		}

		errwr.WriteStringF( "Unsupported command: '{}'.", (const char*)command.c_str( ) );

		return 1;
	}
	catch( const std::exception& exc )
	{
		errwr.WriteString( exc.what( ) );

		return 12;
	}
	catch( ... )
	{
		errwr.WriteString( "Internal error" );

		return 14;
	}
}
