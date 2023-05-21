// HyperscanWorker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"


#if _DEBUG
#   pragma comment(lib, "./Hyperscan-min/lib-debug/hs.lib")
#   pragma comment(lib, "./Hyperscan-min/lib-debug/pcred.lib")
#else
#   pragma comment(lib, "./Hyperscan-min/lib-release/hs.lib")
#   pragma comment(lib, "./Hyperscan-min/lib-release/pcre.lib")
#endif


int GetHyperscanVersion( BinaryWriterA& outwr, StreamWriterA& errwr );
int DoHyperscanMatch( BinaryWriterA& outwr, StreamWriterA& errwr, const std::string& pattern, const std::string& text,
    uint32_t remote_flags, uint32_t Levenshtein_distance, uint32_t Hamming_distance, uint32_t minOffset, uint32_t maxOffset, uint32_t minLength );


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

        std::string command = inbr.ReadString( );

        //

        if( command == "v" )
        {
            // get Hyperscan version

            return GetHyperscanVersion( outbw, errwr );
        }

        //

        if( command == "m" )
        {
            // get matches

            std::string pattern = inbr.ReadString( );
            std::string text = inbr.ReadString( );
            auto remote_flags = inbr.ReadT<uint32_t>( );
            auto Levenshtein_distance = inbr.ReadT<uint32_t>( );
            auto Hamming_distance = inbr.ReadT<uint32_t>( );
            auto min_offset = inbr.ReadT<uint32_t>( );
            auto max_offset = inbr.ReadT<uint32_t>( );
            auto min_length = inbr.ReadT<uint32_t>( );

            return DoHyperscanMatch( outbw, errwr, pattern, text, remote_flags, Levenshtein_distance, Hamming_distance, min_offset, max_offset, min_length );
        }

        errwr.WriteStringF( "Unsupported command: '%s'", command.c_str( ) );

        return 10;
    }
    catch( const std::exception& exc )
    {
        errwr.WriteString( exc.what( ) );

        return 14;
    }
    catch( ... )
    {
        errwr.WriteString( "Internal error" );

        return 215;
    }

    return 101;
}


static int GetHyperscanVersion( BinaryWriterA& outwr, StreamWriterA& errwr )
{
    //const char* v = hs_version( ); // includes the date too

    std::string version = std::format( "{}.{}.{}", HS_MAJOR, HS_MINOR, HS_PATCH );

    outwr.Write( version );

    return 0;
}

struct Match
{
    bool const success;
    uint64_t const index;  // (byte index)
    uint64_t const length; // (byte length)

    Match( bool s, uint64_t i, uint64_t l ) : success( s ), index( i ), length( l ) {}

    std::vector<Match> groups; // (for Chimera)
};


struct Context
{
    std::vector<Match> matches;

    std::string error;
};


static int HSEventHandler( unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx0 )
{
    Context& ctx = *(Context*)ctx0;

    ctx.matches.emplace_back( true, from, to - from );

    return 0;
}


static int DoHyperscanMatch( BinaryWriterA& outwr, StreamWriterA& errwr, const std::string& pattern, const std::string& text,
    uint32_t remoteFlags, uint32_t LevenshteinDistance, uint32_t HammingDistance, uint32_t minOffset, uint32_t maxOffset, uint32_t minLength )
{
    unsigned int compiler_flags = 0;

    if( remoteFlags & ( 1 << 0 ) ) compiler_flags |= HS_FLAG_CASELESS;
    if( remoteFlags & ( 1 << 1 ) ) compiler_flags |= HS_FLAG_DOTALL;
    if( remoteFlags & ( 1 << 2 ) ) compiler_flags |= HS_FLAG_MULTILINE;
    if( remoteFlags & ( 1 << 3 ) ) compiler_flags |= HS_FLAG_SINGLEMATCH;
    if( remoteFlags & ( 1 << 4 ) ) compiler_flags |= HS_FLAG_ALLOWEMPTY;
    if( remoteFlags & ( 1 << 5 ) ) compiler_flags |= HS_FLAG_UTF8;
    if( remoteFlags & ( 1 << 6 ) ) compiler_flags |= HS_FLAG_UCP;
    if( remoteFlags & ( 1 << 7 ) ) compiler_flags |= HS_FLAG_PREFILTER;
    if( remoteFlags & ( 1 << 8 ) ) compiler_flags |= HS_FLAG_SOM_LEFTMOST;
    //if( remoteFlags & ( 1 << 9 ) ) compiler_flags |= HS_FLAG_COMBINATION;
    if( remoteFlags & ( 1 << 10 ) ) compiler_flags |= HS_FLAG_QUIET;

    int scanner_flags = 0; // (from documentation: "This parameter is provided for future use and is unused at present")

    hs_database_t* database;
    hs_compile_error_t* compile_err;

    //if( hs_compile( pattern.c_str( ), compiler_flags, HS_MODE_BLOCK, NULL, &database, &compile_err ) != HS_SUCCESS )
    //{
    //    errwr.WriteStringF( "Unable to compile pattern \"%s\": %s", pattern.c_str( ), compile_err->message );
    //    hs_free_compile_error( compile_err );

    //    return -1;
    //}

    const char* patterns[1] = { pattern.c_str( ) };
    unsigned int flags[1] = { compiler_flags };

    hs_expr_ext_t ext = { 0 };

    constexpr uint32_t empty_indicator = std::numeric_limits<uint32_t>::max( );

    if( LevenshteinDistance != empty_indicator ) ext.flags |= HS_EXT_FLAG_EDIT_DISTANCE;
    if( HammingDistance != empty_indicator ) ext.flags |= HS_EXT_FLAG_HAMMING_DISTANCE;
    if( minOffset != empty_indicator ) ext.flags |= HS_EXT_FLAG_MIN_OFFSET;
    if( maxOffset != empty_indicator ) ext.flags |= HS_EXT_FLAG_MAX_OFFSET;
    if( minLength != empty_indicator ) ext.flags |= HS_EXT_FLAG_MIN_LENGTH;

    ext.edit_distance = LevenshteinDistance;
    ext.hamming_distance = HammingDistance;
    ext.min_offset = minOffset;
    ext.max_offset = maxOffset;
    ext.min_length = minLength;

    hs_expr_ext_t* extended_flags[1] = { &ext };

    if( hs_compile_ext_multi( patterns, flags, nullptr, extended_flags, 1, HS_MODE_BLOCK, nullptr, &database, &compile_err ) )
    {
        errwr.WriteStringF( "Unable to compile pattern \"%s\": %s", pattern.c_str( ), compile_err->message );
        hs_free_compile_error( compile_err );

        return -1;
    }

    hs_scratch_t* scratch = NULL;

    if( hs_alloc_scratch( database, &scratch ) != HS_SUCCESS )
    {
        errwr.WriteStringF( "Unable to allocate scratch space." );
        hs_free_database( database );

        return -1;
    }

    Context ctx;

    if( hs_scan( database, text.data( ), CheckedCast( text.size( ) ), scanner_flags, scratch, HSEventHandler, &ctx ) != HS_SUCCESS )
    {
        errwr.WriteStringF( "Unable to scan input buffer." );

        hs_free_scratch( scratch );
        hs_free_database( database );

        return -1;
    }

    hs_free_scratch( scratch );
    hs_free_database( database );

    outwr.Write( "r" );
    outwr.WriteT( (uint64_t)ctx.matches.size( ) );

    for( const Match& m : ctx.matches )
    {
        outwr.WriteT( m.index );
        outwr.WriteT( m.length );
    }

    return 0;
}



