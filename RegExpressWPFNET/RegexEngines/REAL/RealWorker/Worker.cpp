// SubRegWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"

#include "real/real.hpp"


static void DoMatch( BinaryWriterW& outbw, const std::wstring& pattern, const std::wstring& text, real::flags flags )
{

	DWORD code;
	char error_text[128] = "";

	__try
	{
		[&]( )
			{
				outbw.WriteT<char>( 'b' );

				real::regex re( WStringToUtf8( pattern ), flags );

				const std::vector< std::pair< std::string_view, std::size_t > >& names = re.named_groups( );

				for( const auto& p : names )
				{
					outbw.WriteT<char>( 'n' );
					outbw.Write( Utf8ToWString( p.first.data( ), CheckedCast( p.first.length( ) ) ) );
					outbw.WriteT<uint64_t>( p.second );
				}

				outbw.WriteT<char>( '-' );

				const std::string textUTF8 = WStringToUtf8( text );

				for( const auto& match : re.find_iter( textUTF8 ) )
				{
					outbw.WriteT<char>( 'm' );
					outbw.WriteT<uint64_t>( match.start( ) );
					outbw.WriteT<uint64_t>( match.end( ) );

					for( size_t i = 1; i <= re.group_count( ); ++i )
					{
						outbw.WriteT<char>( 'g' );

						static_assert( real::npos == std::numeric_limits<uint64_t>::max( ) );

						outbw.WriteT<uint64_t>( match.start( i ) ); // 'real::npos' if group failed
						outbw.WriteT<uint64_t>( match.end( i ) );
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
		BinaryWriterW outbw( hout );
		BinaryReaderW inbr( hin );

		if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

		std::wstring pattern = inbr.ReadString( );
		std::wstring text = inbr.ReadString( );

		real::flags flags = real::flags::none;

		if( inbr.ReadByte( ) ) flags = flags | real::flags::icase;
		if( inbr.ReadByte( ) ) flags = flags | real::flags::multiline;
		if( inbr.ReadByte( ) ) flags = flags | real::flags::dotall;
		//if( inbr.ReadByte( ) ) flags = flags | real::flags::bytes; // not supported here
		if( inbr.ReadByte( ) ) flags = flags | real::flags::verbose;

		if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

		DoMatch( outbw, pattern, text, flags );

		return 0;
	}
	// the byte offset is already mentioned in the 'exc.what( )' message
	//catch( const real::regex_error& exc )
	//{
	//	errwr.WriteStringF( "{}\r\nat byte offset {}", exc.what( ), exc.position( ) );

	//	return 11;
	//}
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

