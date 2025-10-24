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
#   pragma comment(lib, "./Hyperscan-min/lib-debug/chimera.lib")
#else
#   pragma comment(lib, "./Hyperscan-min/lib-release/hs.lib")
#   pragma comment(lib, "./Hyperscan-min/lib-release/pcre.lib")
#   pragma comment(lib, "./Hyperscan-min/lib-release/chimera.lib")
#endif


int GetHyperscanVersion( BinaryWriterA& outwr, StreamWriterA& errwr );
int DoHyperscanMatch( BinaryWriterA& outwr, StreamWriterA& errwr, const std::string& pattern, const std::string& text,
    uint32_t remote_flags, uint32_t Levenshtein_distance, uint32_t Hamming_distance, uint32_t minOffset, uint32_t maxOffset, uint32_t minLength,
    uint8_t mode, uint8_t modeSom );

int GetChimeraVersion( BinaryWriterA& outwr, StreamWriterA& errwr );
int DoChimeraMatch( BinaryWriterA& outwr, StreamWriterA& errwr, const std::string& pattern, const std::string& text,
    uint32_t remote_flags, uint32_t matchLimit, uint32_t matchLimitRecursion, uint8_t mode );


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
        else if( command == "m" )
        {
            // get Hyperscan matches

            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::string pattern = inbr.ReadString( );
            std::string text = inbr.ReadString( );
            auto remote_flags = inbr.ReadT<uint32_t>( );
            auto Levenshtein_distance = inbr.ReadT<uint32_t>( );
            auto Hamming_distance = inbr.ReadT<uint32_t>( );
            auto min_offset = inbr.ReadT<uint32_t>( );
            auto max_offset = inbr.ReadT<uint32_t>( );
            auto min_length = inbr.ReadT<uint32_t>( );
            auto mode = inbr.ReadT<uint8_t>( );
            auto mode_som = inbr.ReadT<uint8_t>( );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            return DoHyperscanMatch( outbw, errwr, pattern, text, remote_flags, Levenshtein_distance, Hamming_distance, min_offset, max_offset, min_length, mode, mode_som );
        }
        else if( command == "chv" )
        {
            // get Chimera version

            return GetChimeraVersion( outbw, errwr );
        }
        else if( command == "chm" )
        {
            // get Chimera matches

            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [CH1]." );

            std::string pattern = inbr.ReadString( );
            std::string text = inbr.ReadString( );
            auto remote_flags = inbr.ReadT<uint32_t>( );
            auto match_limit = inbr.ReadT<uint32_t>( );
            auto match_limit_recursion = inbr.ReadT<uint32_t>( );
            auto mode = inbr.ReadT<uint8_t>( );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [CH2]." );

            return DoChimeraMatch( outbw, errwr, pattern, text, remote_flags, match_limit, match_limit_recursion, mode );
        }

        errwr.WriteStringF( "Unsupported command: '{}'.", command );

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


const char* ErrorText( hs_error_t err )
{
#define E(e) \
    case e: return #e

    switch( err )
    {
        E( HS_SUCCESS );
        E( HS_INVALID );
        E( HS_NOMEM );
        E( HS_SCAN_TERMINATED );
        E( HS_COMPILER_ERROR );
        E( HS_DB_VERSION_ERROR );
        E( HS_DB_PLATFORM_ERROR );
        E( HS_DB_MODE_ERROR );
        E( HS_BAD_ALIGN );
        E( HS_BAD_ALLOC );
        E( HS_SCRATCH_IN_USE );
        E( HS_ARCH_ERROR );
        E( HS_INSUFFICIENT_SPACE );
        E( HS_UNKNOWN_ERROR );
    default: return "Unknown error";
    }
#undef E
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


static int HyperscanEventHandler( unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx0 )
{
    Context& ctx = *(Context*)ctx0;

    ctx.matches.emplace_back( true, from, to - from );

    return 0;
}


static int DoHyperscanMatch( BinaryWriterA& outwr, StreamWriterA& errwr, const std::string& pattern, const std::string& text,
    uint32_t remoteFlags, uint32_t LevenshteinDistance, uint32_t HammingDistance, uint32_t minOffset, uint32_t maxOffset, uint32_t minLength,
    uint8_t mode, uint8_t modeSom )
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

    hs_database_t* database;
    hs_compile_error_t* compile_err;

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

    unsigned int hs_mode = 0;

    if( mode == 1 ) // HS_MODE_BLOCK
    {
        hs_mode = HS_MODE_BLOCK;
    }
    else if( mode == 2 ) // HS_MODE_STREAM
    {
        hs_mode = HS_MODE_STREAM;
    }
    else if( mode == 3 ) // HS_MODE_VECTOR
    {
        hs_mode = HS_MODE_VECTORED;
    }
    else
    {
        errwr.WriteStringF( "Invalid mode." );

        return -1;
    }

    unsigned int hs_mode_som = 0;

    if( modeSom == 0 ) // none
    {
        hs_mode_som = 0;
    }
    else if( modeSom == 1 ) // HS_MODE_SOM_HORIZON_LARGE
    {
        hs_mode_som = HS_MODE_SOM_HORIZON_LARGE;
    }
    else if( modeSom == 2 ) // HS_MODE_SOM_HORIZON_MEDIUM
    {
        hs_mode_som = HS_MODE_SOM_HORIZON_MEDIUM;
    }
    else if( modeSom == 3 ) // HS_MODE_SOM_HORIZON_SMALL
    {
        hs_mode_som = HS_MODE_SOM_HORIZON_SMALL;
    }
    else
    {
        errwr.WriteStringF( "Invalid mode_som." );

        return -1;
    }

    hs_error_t hs;

    if( ( hs = hs_compile_ext_multi( patterns, flags, nullptr, extended_flags, 1, hs_mode | hs_mode_som, nullptr, &database, &compile_err ) ) != HS_SUCCESS )
    {
        if( hs == HS_COMPILER_ERROR )
        {
            errwr.WriteStringF( "Unable to compile pattern \"{}\": {}.", pattern, compile_err->message );
            hs_free_compile_error( compile_err );
        }
        else
        {
            errwr.WriteStringF( "Unable to compile pattern \"{}\": {}.", pattern, ErrorText( hs ) );
        }

        return -1;
    }

    hs_scratch_t* scratch = NULL;

    if( ( hs = hs_alloc_scratch( database, &scratch ) ) != HS_SUCCESS )
    {
        errwr.WriteStringF( "Unable to allocate scratch space ({}).", ErrorText( hs ) );
        hs_free_database( database );

        return -1;
    }

    Context ctx;

    if( hs_mode == HS_MODE_BLOCK )
    {
        if( ( hs = hs_scan( database, text.data( ), CheckedCast( text.size( ) ), 0, scratch, HyperscanEventHandler, &ctx ) ) != HS_SUCCESS )
        {
            errwr.WriteStringF( "Unable to scan input buffer ({}).", ErrorText( hs ) );

            hs_free_scratch( scratch );
            hs_free_database( database );

            return -1;
        }
    }
    else if( hs_mode == HS_MODE_STREAM )
    {
        hs_stream_t* stream;

        if( ( hs = hs_open_stream( database, 0, &stream ) ) != HS_SUCCESS )
        {
            errwr.WriteStringF( "Unable to open stream ({}).", ErrorText( hs ) );

            hs_free_scratch( scratch );
            hs_free_database( database );

            return -1;
        }

        if( ( hs = hs_scan_stream( stream, text.data( ), CheckedCast( text.size( ) ), 0, scratch, HyperscanEventHandler, &ctx ) ) != HS_SUCCESS )
        {
            errwr.WriteStringF( "Unable to scan the stream ({}).", ErrorText( hs ) );

            hs_close_stream( stream, nullptr, nullptr, nullptr );
            hs_free_scratch( scratch );
            hs_free_database( database );

            return -1;
        }
    }
    else if( hs_mode == HS_MODE_VECTORED )
    {
        const char* data[1] = { text.data( ) };
        const unsigned int length[1] = { CheckedCast( text.size( ) ) };

        if( ( hs = hs_scan_vector( database, data, length, 1, 0, scratch, HyperscanEventHandler, &ctx ) ) != HS_SUCCESS )
        {
            errwr.WriteStringF( "Unable to scan vector ({}).", ErrorText( hs ) );

            hs_free_scratch( scratch );
            hs_free_database( database );

            return -1;
        }
    }
    else
    {
        errwr.WriteStringF( "Invalid mode [2]." );

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


const char* ChimeraErrorText( hs_error_t err )
{
#define E(e) \
    case e: return #e

    switch( err )
    {
        E( CH_SUCCESS );
        E( CH_INVALID );
        E( CH_NOMEM );
        E( CH_SCAN_TERMINATED );
        E( CH_COMPILER_ERROR );
        E( CH_DB_VERSION_ERROR );
        E( CH_DB_PLATFORM_ERROR );
        E( CH_DB_MODE_ERROR );
        E( CH_BAD_ALIGN );
        E( CH_BAD_ALLOC );
        E( CH_SCRATCH_IN_USE );
        E( CH_UNKNOWN_HS_ERROR );
        E( CH_FAIL_INTERNAL );
    default: return "Unknown error";
    }
#undef E
}


static int GetChimeraVersion( BinaryWriterA& outwr, StreamWriterA& errwr )
{
    std::string version = std::format( "{}.{}.{}", HS_MAJOR, HS_MINOR, HS_PATCH );

    outwr.Write( version );

    return 0;

}


static ch_callback_t ChimeraEventHandler( unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, unsigned int size, const ch_capture_t* captured, void* ctx0 )
{
    Context& ctx = *(Context*)ctx0;

    ctx.matches.emplace_back( true, from, to - from );

    Match& m = ctx.matches.back( );

    for( unsigned int i = 0; i < size; ++i )
    {
        assert( !( i == 0 && ( captured[i].flags & CH_CAPTURE_FLAG_ACTIVE ) == 0 ) );

        m.groups.emplace_back( ( captured[i].flags & CH_CAPTURE_FLAG_ACTIVE ) != 0, captured[i].from, captured[i].to - captured[i].from );
    }

    return CH_CALLBACK_CONTINUE;
}


static ch_callback_t ChimeraErrorHandler( ch_error_event_t error_type, unsigned int id, void* info, void* ctx0 )
{
    Context& ctx = *(Context*)ctx0;

    switch( error_type )
    {
    case CH_ERROR_MATCHLIMIT:
        ctx.error = "Match limit achieved.";
        break;
    case CH_ERROR_RECURSIONLIMIT:
        ctx.error = "Recursion limit achieved.";
        break;
    default:
        ctx.error = "Unknown scan error";
        break;
    }

    return CH_CALLBACK_TERMINATE;
}


static int DoChimeraMatch( BinaryWriterA& outwr, StreamWriterA& errwr, const std::string& pattern, const std::string& text,
    uint32_t remoteFlags, uint32_t matchLimit, uint32_t matchLimitRecursion, uint8_t mode )
{
    unsigned int compiler_flags = 0;

    if( remoteFlags & ( 1 << 0 ) ) compiler_flags |= CH_FLAG_CASELESS;
    if( remoteFlags & ( 1 << 1 ) ) compiler_flags |= CH_FLAG_DOTALL;
    if( remoteFlags & ( 1 << 2 ) ) compiler_flags |= CH_FLAG_MULTILINE;
    if( remoteFlags & ( 1 << 3 ) ) compiler_flags |= CH_FLAG_SINGLEMATCH;
    if( remoteFlags & ( 1 << 4 ) ) compiler_flags |= CH_FLAG_UTF8;
    if( remoteFlags & ( 1 << 5 ) ) compiler_flags |= CH_FLAG_UCP;

    unsigned int ch_mode = 0;

    switch( mode )
    {
    case 1: ch_mode = CH_MODE_NOGROUPS; break;
    case 2: ch_mode = CH_MODE_GROUPS; break;
    default:
        errwr.WriteStringF( "Invalid mode." );

        return -1;
    }

    constexpr uint32_t empty_indicator = std::numeric_limits<uint32_t>::max( );

    if( matchLimit == empty_indicator ) matchLimit = 0;
    if( matchLimitRecursion == empty_indicator ) matchLimitRecursion = 0;

    ch_database_t* database;
    ch_compile_error_t* compile_err;

    const char* patterns[1] = { pattern.c_str( ) };
    unsigned int flags[1] = { compiler_flags };

    ch_error_event_t ch;

    if( ( ch = ch_compile_ext_multi(
        patterns,
        flags,
        nullptr, // ids
        1, // elements
        ch_mode,
        matchLimit,
        matchLimitRecursion,
        nullptr, // platform
        &database,
        &compile_err ) ) != CH_SUCCESS )
    {
        if( ch == CH_COMPILER_ERROR )
        {
            errwr.WriteStringF( "Unable to compile pattern \"{}\": {}.", pattern, compile_err->message );
            ch_free_compile_error( compile_err );
        }
        else
        {
            errwr.WriteStringF( "Unable to compile pattern \"{}\": {}.", pattern, ChimeraErrorText( ch ) );
        }

        return -1;
    }

    ch_scratch_t* scratch = NULL;

    if( ( ch = ch_alloc_scratch( database, &scratch ) ) != CH_SUCCESS )
    {
        errwr.WriteStringF( "Unable to allocate scratch space ({}).", ChimeraErrorText( ch ) );
        ch_free_database( database );

        return -1;
    }

    Context ctx;

    if( ( ch = ch_scan(
        database,
        text.data( ),
        CheckedCast( text.size( ) ),
        0,
        scratch,
        ChimeraEventHandler,
        ChimeraErrorHandler,
        &ctx ) ) != CH_SUCCESS )
    {
        if( ctx.error.empty( ) )
        {
            errwr.WriteStringF( "Unable to scan input buffer ({}).", ChimeraErrorText( ch ) );

            ch_free_scratch( scratch );
            ch_free_database( database );

            return -1;
        }
    }

    ch_free_scratch( scratch );
    ch_free_database( database );

    outwr.Write( "r" );

    if( !ctx.error.empty( ) )
    {
        outwr.Write( "e" );

        outwr.Write( ctx.error );
    }
    else
    {
        outwr.Write( "m" );

        outwr.WriteT( (uint64_t)ctx.matches.size( ) );

        for( const Match& m : ctx.matches )
        {
            outwr.WriteT( m.index );
            outwr.WriteT( m.length );

            outwr.WriteT( (uint64_t)m.groups.size( ) );
            for( const Match& g : m.groups )
            {
                outwr.WriteT( (uint32_t)g.success );
                outwr.WriteT( g.index );
                outwr.WriteT( g.length );
            }
        }
    }

    return 0;
}
