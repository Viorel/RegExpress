// PCRE2Worker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "StreamWriter.h"
#include "Convert.h"
#include "CheckedCast.h"
#include "SEHFilter.h"

#include "PCRE2.h"


using namespace std;


#define TO_STR2(s) L#s
#define TO_STR(s) TO_STR2(s)


static void WriteMatch( BinaryWriterW& outbw, pcre2_code* re, PCRE2_SIZE* ovector, int rc )
{
    if( ovector[0] > ovector[1] )
    {
        // TODO: show more details; see 'pcre2demo.c'
        throw std::runtime_error( "\\K was used in an assertion to set the match start after its end." );
    }

    // get group names

    std::vector<std::wstring> names;
    uint32_t namecount;

    (void)pcre2_pattern_info(
        re,                   /* the compiled pattern */
        PCRE2_INFO_NAMECOUNT, /* get the number of named substrings */
        &namecount );         /* where to put the answer */

    if( namecount > 0 )
    {
        PCRE2_SPTR name_table;
        uint32_t name_entry_size;

        (void)pcre2_pattern_info(
            re,                       /* the compiled pattern */
            PCRE2_INFO_NAMETABLE,     /* address of the table */
            &name_table );            /* where to put the answer */

        (void)pcre2_pattern_info(
            re,                       /* the compiled pattern */
            PCRE2_INFO_NAMEENTRYSIZE, /* size of each entry in the table */
            &name_entry_size );       /* where to put the answer */

        PCRE2_SPTR p = name_table;
        for( int i = 0;
            i < (int)CheckedCast( namecount );
            i++, p += name_entry_size )
        {
            int n = *( (int16_t*)p );
            const wchar_t* startname = (wchar_t*)( ( (int16_t*)p ) + 1 );
            std::wstring name( startname, name_entry_size - 2 );

            // 
            auto pos = name.find_first_of( L'\0', 0 );
            if( pos != wstring::npos ) name.resize( pos );

            if( names.size( ) <= n ) names.resize( n + 1 );
            assert( names[n].empty( ) );

            names[n] = std::move( name );
        }
    }

    outbw.WriteT<char>( 'm' );
    outbw.WriteT<int32_t>( CheckedCast( ovector[0] ) );
    outbw.WriteT<int32_t>( CheckedCast( ovector[1] - ovector[0] ) );

    // groups (including the default one)

    for( int i = 0; i < rc; ++i )
    {
        outbw.WriteT<char>( 'g' );
        auto index = ovector[2 * i];
        bool success = index != -1;
        outbw.WriteT<char>( success );
        outbw.WriteT<int32_t>( CheckedCast( success ? ovector[2 * i] : 0 ) );
        outbw.WriteT<int32_t>( CheckedCast( success ? ovector[2 * i + 1] - ovector[2 * i] : 0 ) );
        if( i < names.size( ) && !names[i].empty( ) )
        {
            outbw.Write( names[i] );
        }
        else
        {
            outbw.Write( std::to_wstring( i ) ); //.
        }
    }

    // add failed groups not included in 'rc'
    {
        uint32_t capturecount;

        if( pcre2_pattern_info(
            re,
            PCRE2_INFO_CAPTURECOUNT,
            &capturecount ) == 0 )
        {
            for( int i = rc; i <= (int)CheckedCast( capturecount ); ++i )
            {
                outbw.WriteT<char>( 'g' );
                outbw.WriteT<char>( 0 );
                outbw.WriteT<int32_t>( 0 );
                outbw.WriteT<int32_t>( 0 );
                if( i < names.size( ) && !names[i].empty( ) )
                {
                    outbw.Write( names[i] );
                }
                else
                {
                    outbw.Write( std::to_wstring( i ) ); //.
                }
            }
        }
    }
}


static std::wstring GetErrorText( int errorNumber )
{
    PCRE2_UCHAR buffer[256] = { 0 };
    pcre2_get_error_message( errorNumber, buffer, _countof( buffer ) );

    return (wchar_t*)buffer;
}


static void DoMatch( BinaryWriterW& outbw, const wstring& pattern, const wstring& text, const wstring& algorithm, int compileOptions, int extraCompileOptions, int matcherOptions, int jitOptions )
{
    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
            {
                pcre2_compile_context* compile_context = pcre2_compile_context_create( NULL );

                if( compile_context == nullptr )
                {
                    throw std::runtime_error( "Failed to create compile context." );
                }

                pcre2_set_compile_extra_options( compile_context, extraCompileOptions );

                int errornumber;
                PCRE2_SIZE erroroffset;

                pcre2_code* re = pcre2_compile(
                    reinterpret_cast<PCRE2_SPTR16>( pattern.c_str( ) ), /* the pattern */
                    PCRE2_ZERO_TERMINATED, /* indicates pattern is zero-terminated */
                    compileOptions,        /* options */
                    &errornumber,          /* for error number */
                    &erroroffset,          /* for error offset */
                    compile_context );     /* compile context */

                if( re == nullptr )
                {
                    throw std::runtime_error( WStringToUtf8( std::format( L"Error {} at {}: {}.", errornumber, erroroffset, GetErrorText( errornumber ) ) ) );
                }

                if( jitOptions > 0 )
                {
                    errornumber = pcre2_jit_compile( re, jitOptions );

                    if( errornumber < 0 )
                    {
                        throw std::runtime_error( WStringToUtf8( std::format( L"Error {}: {}.", errornumber, GetErrorText( errornumber ) ) ) );
                    }
                }

                pcre2_match_context* match_context = pcre2_match_context_create( NULL );

                if( match_context == nullptr )
                {
                    throw std::runtime_error( "Failed to create match context" );
                }

                pcre2_match_data* match_data = nullptr;
                std::vector<int> dfa_workspace;

                int rc;

                if( algorithm == L"Standard" )
                {
                    match_data = pcre2_match_data_create_from_pattern( re, NULL );

                    rc = pcre2_match(
                        re,                         /* the compiled pattern */
                        reinterpret_cast<PCRE2_SPTR16>( text.c_str( ) ),  /* the subject string */
                        PCRE2_ZERO_TERMINATED,      /* the length of the subject */
                        0,                          /* start at offset 0 in the subject */
                        matcherOptions,             /* options */
                        match_data,                 /* block for storing the result */
                        match_context               /* match context */
                    );
                }
                else if( algorithm == L"DFA" )
                {
                    dfa_workspace.resize( 1000 ); // (see 'pcre2test.c')
                    match_data = pcre2_match_data_create( 1000, NULL );

                    rc = pcre2_dfa_match(
                        re,                         /* the compiled pattern */
                        reinterpret_cast<PCRE2_SPTR16>( text.c_str( ) ),  /* the subject string */
                        PCRE2_ZERO_TERMINATED,      /* the length of the subject */
                        0,                          /* start at offset 0 in the subject */
                        matcherOptions,             /* options */
                        match_data,                 /* block for storing the result */
                        match_context,              /* match context */
                        dfa_workspace.data( ),
                        dfa_workspace.size( )
                    );
                }
                else
                {
                    throw std::runtime_error( std::format( "Unsupported algorithm: '{}'", WStringToUtf8( algorithm ) ) );
                }

                if( rc == 0 )
                {
                    throw std::runtime_error( "'ovector' was not big enough for all the captured substrings" );
                }

                if( rc < 0 && rc != PCRE2_ERROR_NOMATCH )
                {
                    throw std::runtime_error( WStringToUtf8( std::format( L"Error {}: {}.", rc, GetErrorText( rc ) ) ) );
                }

                PCRE2_SIZE* const ovector = pcre2_get_ovector_pointer( match_data );

                if( ovector == nullptr )
                {
                    throw std::runtime_error( "Null 'ovector'." );
                }

                //if( ovector[0] > ovector[1] )
                //{
                //    // TODO: show more details; see 'pcre2demo.c'
                //    throw std::runtime_error( "\\K was used in an assertion to set the match start after its end." );
                //}

                outbw.WriteT<char>( 'b' );

                if( rc != PCRE2_ERROR_NOMATCH )
                {
                    WriteMatch( outbw, re, ovector, rc );

                    // find next matches

                    {
                        const wchar_t* subject = text.c_str( );
                        auto subject_length = text.length( );


                        // their tricky stuffs; code and comments are from 'pcre2demo.c'

                        uint32_t option_bits;
                        uint32_t newline;
                        int crlf_is_newline;
                        int utf8;

                        /* Before running the loop, check for UTF-8 and whether CRLF is a valid newline
                        sequence. First, find the options with which the regex was compiled and extract
                        the UTF state. */

                        (void)pcre2_pattern_info( re, PCRE2_INFO_ALLOPTIONS, &option_bits );
                        utf8 = ( option_bits & PCRE2_UTF ) != 0;

                        /* Now find the newline convention and see whether CRLF is a valid newline
                        sequence. */

                        (void)pcre2_pattern_info( re, PCRE2_INFO_NEWLINE, &newline );
                        crlf_is_newline = newline == PCRE2_NEWLINE_ANY ||
                            newline == PCRE2_NEWLINE_CRLF ||
                            newline == PCRE2_NEWLINE_ANYCRLF;

                        /* Loop for second and subsequent matches */

                        for( ;;)
                        {
                            uint32_t options = 0;                   /* Normally no options */
                            PCRE2_SIZE start_offset = ovector[1];   /* Start at end of previous match */

                            /* If the previous match was for an empty string, we are finished if we are
                            at the end of the subject. Otherwise, arrange to run another match at the
                            same point to see if a non-empty match can be found. */

                            if( ovector[0] == ovector[1] )
                            {
                                if( ovector[0] == subject_length ) break;
                                options = PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
                            }

                            /* If the previous match was not an empty string, there is one tricky case to
                            consider. If a pattern contains \K within a lookbehind assertion at the
                            start, the end of the matched string can be at the offset where the match
                            started. Without special action, this leads to a loop that keeps on matching
                            the same substring. We must detect this case and arrange to move the start on
                            by one character. The pcre2_get_startchar() function returns the starting
                            offset that was passed to pcre2_match(). */

                            else
                            {
                                PCRE2_SIZE startchar = pcre2_get_startchar( match_data );
                                if( start_offset <= startchar )
                                {
                                    if( startchar >= subject_length ) break;   /* Reached end of subject.   */
                                    start_offset = startchar + 1;             /* Advance by one character. */
                                    if( utf8 )                                 /* If UTF-8, it may be more  */
                                    {                                       /*   than one code unit.     */
                                        for( ; start_offset < subject_length; start_offset++ )
                                            if( ( subject[start_offset] & 0xc0 ) != 0x80 ) break;
                                    }
                                }
                            }

                            /* Run the next matching operation */

                            rc = pcre2_match(
                                re,                   /* the compiled pattern */
                                reinterpret_cast<PCRE2_SPTR16>( subject ),              /* the subject string */
                                subject_length,       /* the length of the subject */
                                start_offset,         /* starting offset in the subject */
                                options,              /* options */
                                match_data,           /* block for storing the result */
                                NULL );                /* use default match context */

                            /* This time, a result of NOMATCH isn't an error. If the value in "options"
                            is zero, it just means we have found all possible matches, so the loop ends.
                            Otherwise, it means we have failed to find a non-empty-string match at a
                            point where there was a previous empty-string match. In this case, we do what
                            Perl does: advance the matching position by one character, and continue. We
                            do this by setting the "end of previous match" offset, because that is picked
                            up at the top of the loop as the point at which to start again.

                            There are two complications: (a) When CRLF is a valid newline sequence, and
                            the current position is just before it, advance by an extra byte. (b)
                            Otherwise we must ensure that we skip an entire UTF character if we are in
                            UTF mode. */

                            if( rc == PCRE2_ERROR_NOMATCH )
                            {
                                if( options == 0 ) break;                    /* All matches found */
                                ovector[1] = start_offset + 1;              /* Advance one code unit */
                                if( crlf_is_newline &&                      /* If CRLF is a newline & */
                                    start_offset < subject_length - 1 &&    /* we are at CRLF, */
                                    subject[start_offset] == '\r' &&
                                    subject[start_offset + 1] == '\n' )
                                    ovector[1] += 1;                          /* Advance by one more. */
                                else if( utf8 )                              /* Otherwise, ensure we */
                                {                                         /* advance a whole UTF-8 */
                                    while( ovector[1] < subject_length )       /* character. */
                                    {
                                        if( ( subject[ovector[1]] & 0xc0 ) != 0x80 ) break;
                                        ovector[1] += 1;
                                    }
                                }
                                continue;    /* Go round the loop again */
                            }

                            /* Other matching errors are not recoverable. */

                            if( rc < 0 )
                            {
                                throw std::runtime_error( WStringToUtf8( std::format( L"Error {}: {}.", rc, GetErrorText( rc ) ) ) );
                            }

                            /* Match succeeded */


                            /* The match succeeded, but the output vector wasn't big enough. This
                            should not happen. */

                            if( rc == 0 )
                            {
                                throw std::runtime_error( "'ovector' was not big enough for all the captured substrings" );
                            }

                            if( ovector[0] > ovector[1] )
                            {
                                // TODO: show more details; see 'pcre2demo.c'
                                throw std::runtime_error( "\\K was used in an assertion to set the match start after its end." );
                            }

                            WriteMatch( outbw, re, ovector, rc );

                        } /* End of loop to find second and subsequent matches */
                    }
                }

                outbw.WriteT<char>( 'e' );

                pcre2_match_context_free( match_context );
                pcre2_match_data_free( match_data );
                pcre2_code_free( re );
                pcre2_compile_context_free( compile_context );

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

            auto v = TO_STR( PCRE2_MAJOR ) L"." TO_STR( PCRE2_MINOR );

            outbw.Write( v );

            return 0;
        }

        if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );

            std::wstring algorithm = inbr.ReadString( );

            // Compile options

            int compile_options = 0;

            if( inbr.ReadByte( ) ) compile_options |= PCRE2_ANCHORED;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_ALLOW_EMPTY_CLASS;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_ALT_BSUX;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_ALT_CIRCUMFLEX;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_ALT_VERBNAMES;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_CASELESS;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_DOLLAR_ENDONLY;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_DOTALL;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_DUPNAMES;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_ENDANCHORED;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_EXTENDED;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_EXTENDED_MORE;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_FIRSTLINE;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_LITERAL;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_MATCH_UNSET_BACKREF;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_MULTILINE;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NEVER_BACKSLASH_C;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NEVER_UCP;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NEVER_UTF;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NO_AUTO_CAPTURE;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NO_AUTO_POSSESS;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NO_DOTSTAR_ANCHOR;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_NO_START_OPTIMIZE;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_UCP;
            if( inbr.ReadByte( ) ) compile_options |= PCRE2_UNGREEDY;

            // Extra compile options

            int extra_compile_options = 0;

            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ALLOW_SURROGATE_ESCAPES;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ALT_BSUX;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ASCII_BSD;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ASCII_BSS;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ASCII_BSW;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ASCII_DIGIT;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ASCII_POSIX;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_CASELESS_RESTRICT;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_ESCAPED_CR_IS_LF;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_MATCH_LINE;
            if( inbr.ReadByte( ) ) extra_compile_options |= PCRE2_EXTRA_MATCH_WORD;

            // Match options

            int matcher_options = 0;

            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_ANCHORED;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_COPY_MATCHED_SUBJECT;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_ENDANCHORED;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_NOTBOL;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_NOTEOL;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_NOTEMPTY;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_NOTEMPTY_ATSTART;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_NO_JIT;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_PARTIAL_HARD;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_PARTIAL_SOFT;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_DFA_SHORTEST;
            if( inbr.ReadByte( ) ) matcher_options |= PCRE2_DISABLE_RECURSELOOP_CHECK;

            // JIT options

            int jit_options = 0;

            if( inbr.ReadByte( ) ) jit_options |= PCRE2_JIT_COMPLETE;
            if( inbr.ReadByte( ) ) jit_options |= PCRE2_JIT_PARTIAL_SOFT;
            if( inbr.ReadByte( ) ) jit_options |= PCRE2_JIT_PARTIAL_HARD;


            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            DoMatch( outbw, pattern, text, algorithm, compile_options, extra_compile_options, matcher_options, jit_options );

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

