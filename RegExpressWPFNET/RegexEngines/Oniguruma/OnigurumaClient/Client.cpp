// OnigurumaClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
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


static const char* TryGetErrorSymbol0( int code )
{

#define C(c) if( code == c) return #c;

    C( ONIG_MISMATCH );
    C( ONIG_NO_SUPPORT_CONFIG );
    C( ONIG_ABORT );
    C( ONIGERR_MEMORY );
    C( ONIGERR_TYPE_BUG );
    C( ONIGERR_PARSER_BUG );
    C( ONIGERR_STACK_BUG );
    C( ONIGERR_UNDEFINED_BYTECODE );
    C( ONIGERR_UNEXPECTED_BYTECODE );
    C( ONIGERR_MATCH_STACK_LIMIT_OVER );
    C( ONIGERR_PARSE_DEPTH_LIMIT_OVER );
    C( ONIGERR_RETRY_LIMIT_IN_MATCH_OVER );
    C( ONIGERR_RETRY_LIMIT_IN_SEARCH_OVER );
    C( ONIGERR_DEFAULT_ENCODING_IS_NOT_SETTED );
    C( ONIGERR_SPECIFIED_ENCODING_CANT_CONVERT_TO_WIDE_CHAR );
    C( ONIGERR_FAIL_TO_INITIALIZE );
    C( ONIGERR_INVALID_ARGUMENT );
    C( ONIGERR_END_PATTERN_AT_LEFT_BRACE );
    C( ONIGERR_END_PATTERN_AT_LEFT_BRACKET );
    C( ONIGERR_EMPTY_CHAR_CLASS );
    C( ONIGERR_PREMATURE_END_OF_CHAR_CLASS );
    C( ONIGERR_END_PATTERN_AT_ESCAPE );
    C( ONIGERR_END_PATTERN_AT_META );
    C( ONIGERR_END_PATTERN_AT_CONTROL );
    C( ONIGERR_META_CODE_SYNTAX );
    C( ONIGERR_CONTROL_CODE_SYNTAX );
    C( ONIGERR_CHAR_CLASS_VALUE_AT_END_OF_RANGE );
    C( ONIGERR_CHAR_CLASS_VALUE_AT_START_OF_RANGE );
    C( ONIGERR_UNMATCHED_RANGE_SPECIFIER_IN_CHAR_CLASS );
    C( ONIGERR_TARGET_OF_REPEAT_OPERATOR_NOT_SPECIFIED );
    C( ONIGERR_TARGET_OF_REPEAT_OPERATOR_INVALID );
    C( ONIGERR_NESTED_REPEAT_OPERATOR );
    C( ONIGERR_UNMATCHED_CLOSE_PARENTHESIS );
    C( ONIGERR_END_PATTERN_WITH_UNMATCHED_PARENTHESIS );
    C( ONIGERR_END_PATTERN_IN_GROUP );
    C( ONIGERR_UNDEFINED_GROUP_OPTION );
    C( ONIGERR_INVALID_POSIX_BRACKET_TYPE );
    C( ONIGERR_INVALID_LOOK_BEHIND_PATTERN );
    C( ONIGERR_INVALID_REPEAT_RANGE_PATTERN );
    C( ONIGERR_TOO_BIG_NUMBER );
    C( ONIGERR_TOO_BIG_NUMBER_FOR_REPEAT_RANGE );
    C( ONIGERR_UPPER_SMALLER_THAN_LOWER_IN_REPEAT_RANGE );
    C( ONIGERR_EMPTY_RANGE_IN_CHAR_CLASS );
    C( ONIGERR_MISMATCH_CODE_LENGTH_IN_CLASS_RANGE );
    C( ONIGERR_TOO_MANY_MULTI_BYTE_RANGES );
    C( ONIGERR_TOO_SHORT_MULTI_BYTE_STRING );
    C( ONIGERR_TOO_BIG_BACKREF_NUMBER );
    C( ONIGERR_INVALID_BACKREF );
    C( ONIGERR_NUMBERED_BACKREF_OR_CALL_NOT_ALLOWED );
    C( ONIGERR_TOO_MANY_CAPTURES );
    C( ONIGERR_TOO_LONG_WIDE_CHAR_VALUE );
    C( ONIGERR_EMPTY_GROUP_NAME );
    C( ONIGERR_INVALID_GROUP_NAME );
    C( ONIGERR_INVALID_CHAR_IN_GROUP_NAME );
    C( ONIGERR_UNDEFINED_NAME_REFERENCE );
    C( ONIGERR_UNDEFINED_GROUP_REFERENCE );
    C( ONIGERR_MULTIPLEX_DEFINED_NAME );
    C( ONIGERR_MULTIPLEX_DEFINITION_NAME_CALL );
    C( ONIGERR_NEVER_ENDING_RECURSION );
    C( ONIGERR_GROUP_NUMBER_OVER_FOR_CAPTURE_HISTORY );
    C( ONIGERR_INVALID_CHAR_PROPERTY_NAME );
    C( ONIGERR_INVALID_IF_ELSE_SYNTAX );
    C( ONIGERR_INVALID_ABSENT_GROUP_PATTERN );
    C( ONIGERR_INVALID_ABSENT_GROUP_GENERATOR_PATTERN );
    C( ONIGERR_INVALID_CALLOUT_PATTERN );
    C( ONIGERR_INVALID_CALLOUT_NAME );
    C( ONIGERR_UNDEFINED_CALLOUT_NAME );
    C( ONIGERR_INVALID_CALLOUT_BODY );
    C( ONIGERR_INVALID_CALLOUT_TAG_NAME );
    C( ONIGERR_INVALID_CALLOUT_ARG );
    C( ONIGERR_INVALID_CODE_POINT_VALUE );
    C( ONIGERR_INVALID_WIDE_CHAR_VALUE );
    C( ONIGERR_TOO_BIG_WIDE_CHAR_VALUE );
    C( ONIGERR_NOT_SUPPORTED_ENCODING_COMBINATION );
    C( ONIGERR_INVALID_COMBINATION_OF_OPTIONS );
    C( ONIGERR_TOO_MANY_USER_DEFINED_OBJECTS );
    C( ONIGERR_TOO_LONG_PROPERTY_NAME );
    C( ONIGERR_LIBRARY_IS_NOT_INITIALIZED );

#undef C

    return nullptr;
}


static std::wstring TryGetErrorSymbol( int code )
{
    const char* s = TryGetErrorSymbol0( code );

    return s == nullptr ? L"" : Utf8ToWString( s );
}


static std::wstring FormatError( int code, const OnigErrorInfo* optionalEinfo )
{
    char text[ONIG_MAX_ERROR_MESSAGE_LEN];
    onig_error_code_to_str( (UChar*)text, code, optionalEinfo ); // (for unknown reason, the result is ASCII) 

    std::wstring symbol = TryGetErrorSymbol( code );

    if( symbol.empty( ) )
    {
        return std::format( L"{}\r\n\r\n({})", Utf8ToWString( text ), code );
    }
    else
    {
        return std::format( L"{}\r\n\r\n({}, {})", Utf8ToWString( text ), symbol, code );
    }
}


static void DoMatch( BinaryWriterW& outbw, const wstring& pattern, const wstring& text,
    /*const*/ OnigSyntaxType& adjustedSyntax, OnigOptionType compileOptions, std::uint32_t searchOptions )
{
    DWORD code;
    char error_text[128] = "";

    __try
    {
        [&]( )
        {
            std::unique_ptr<regex_t, decltype( &onig_free )> regex( nullptr, &onig_free );

            {
                regex_t* native_regex;
                OnigErrorInfo einfo;
                int r;

                r = onig_new(
                    &native_regex,
                    (UChar*)pattern.c_str( ),
                    (UChar*)( pattern.c_str( ) + pattern.length( ) ),
                    compileOptions,
                    ONIG_ENCODING_UTF16_LE,
                    &adjustedSyntax,
                    &einfo );

                if( r ) throw std::runtime_error( WStringToUtf8( FormatError( r, &einfo ) ) );

                regex.reset( native_regex );
            }

            // extract group names

            std::vector<std::wstring> group_names;
            {
                onig_foreach_name( regex.get( ),
                    []( const OnigUChar* name, const OnigUChar* nameEnd, int numberOfGroups, int* groupNumberList, OnigRegex regex, void* lparam ) -> int
                    {
                        std::vector<std::wstring>& group_names = *( std::vector<std::wstring>* )lparam;

                        std::wstring group_name( (wchar_t*)name, (wchar_t*)nameEnd );

                        int* nums;
                        int r = onig_name_to_group_numbers( regex, name, nameEnd, &nums );

                        for( int i = 0; i < r; ++i )
                        {
                            int group_number = nums[i];

                            while( group_names.size( ) <= group_number ) group_names.push_back( L"" );

                            group_names[group_number] = group_name;
                        }

                        return 0;
                    },
                    &group_names );
            }


            std::unique_ptr<OnigMatchParam, decltype( &onig_free_match_param )>     match_param( onig_new_match_param( ), &onig_free_match_param );
            std::unique_ptr<OnigRegion, void( * )( OnigRegion* )>                   region( onig_region_new( ), []( OnigRegion* reg ) { onig_region_free( reg, 1 ); } );

            // currently default parameters are used
            onig_initialize_match_param( match_param.get( ) );

            //onig_set_match_stack_limit_size_of_match_param ( OnigMatchParam * param, unsigned int limit );
            //onig_set_retry_limit_in_match_of_match_param ( OnigMatchParam * param, unsigned long limit );
            //onig_set_subexp_call_max_nest_level(  ); 

            int r;
            const wchar_t* start = text.c_str( );
            const wchar_t* previous_start = start;

            outbw.WriteT<char>( 'b' );

            for( ;;)
            {
                r = onig_search_with_param(
                    regex.get( ),
                    (UChar*)text.c_str( ), (UChar*)( text.c_str( ) + text.length( ) ),
                    (UChar*)start, (UChar*)( text.c_str( ) + text.length( ) ),
                    region.get( ),
                    searchOptions,
                    match_param.get( ) );

                if( r == ONIG_MISMATCH ) break;

                if( r < 0 ) throw std::runtime_error( WStringToUtf8( FormatError( r, nullptr ) ) );

                int match_index = -1;
                int match_length = -1;

                for( int i = 0; i < region->num_regs; ++i )
                {
                    int group_number = i;
                    std::wstring group_name;
                    if( group_number < group_names.size( ) ) group_name = group_names[group_number];
                    if( group_name.empty( ) ) group_name = to_wstring( group_number );

                    if( region->beg[i] >= 0 )
                    {
                        // succeeded group

                        assert( ( region->beg[i] % sizeof( wchar_t ) ) == 0 ); // even positions expected
                        assert( ( region->end[i] % sizeof( wchar_t ) ) == 0 );

                        int begin = region->beg[i] / sizeof( wchar_t );
                        int end = region->end[i] / sizeof( wchar_t );

                        if( i == 0 )
                        {
                            match_index = begin;
                            match_length = end - begin;

                            outbw.WriteT<char>( 'm' );
                            outbw.WriteT<int32_t>( match_index );
                            outbw.WriteT<int32_t>( match_length );
                        }

                        // default group 0

                        outbw.WriteT<char>( 'g' );
                        outbw.WriteT<char>( 1 );
                        outbw.WriteT<int32_t>( begin );
                        outbw.WriteT<int32_t>( end - begin );
                        outbw.Write( group_name );

                        // captures

                        int number_of_captures = onig_number_of_captures( regex.get( ) );
                        int number_of_capture_histories = onig_number_of_capture_histories( regex.get( ) );
                        OnigCaptureTreeNode* capture_tree = onig_get_capture_tree( region.get( ) );

                        {
                            struct TraverseTreeData { int groupNumber; BinaryWriterW* bw; } data{ group_number, &outbw };

                            onig_capture_tree_traverse( region.get( ), ONIG_TRAVERSE_CALLBACK_AT_FIRST,
                                []( int group, int beg, int end, int level, int at, void* arg ) -> int
                                {
                                    const TraverseTreeData* data = (TraverseTreeData*)arg;

                                    if( group == 0 )
                                    {
                                        // skip, not needed
                                    }
                                    else
                                    {
                                        if( group == data->groupNumber )
                                        {
                                            assert( ( beg % 2 ) == 0 ); // event positions expected
                                            assert( ( end % 2 ) == 0 );

                                            data->bw->WriteT<char>( 'c' );
                                            data->bw->WriteT<int32_t>( beg / 2 );
                                            data->bw->WriteT<int32_t>( ( end - beg ) / 2 );
                                        }
                                    }

                                    return 0;
                                },
                                &data );
                        }
                    }
                    else
                    {
                        // failed group

                        if( i == 0 )
                        {
                            assert( false );
                        }
                        else
                        {
                            outbw.WriteT<char>( 'g' );
                            outbw.WriteT<char>( 0 );
                            outbw.WriteT<int32_t>( 0 );
                            outbw.WriteT<int32_t>( 0 );
                            outbw.Write( group_name );
                        }
                    }
                }

                // TODO: check if it should be much more complicated -- see PCRE2

                start = text.c_str( ) + match_index + match_length;

                if( start == previous_start )
                {
                    ++start;
                }

                previous_start = start;
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


void ReadOptions( BinaryReaderW& inbr, decltype( ONIG_SYNTAX_ONIGURUMA )* syntax, OnigOptionType* compile_options, decltype( ONIG_OPTION_NONE )* search_options,
    bool* fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY, bool* fONIG_SYN_STRICT_CHECK_BACKREF )
{
    std::wstring syntax_s = inbr.ReadString( );

    *syntax = ONIG_SYNTAX_ONIGURUMA;
    if( syntax_s == L"ONIG_SYNTAX_ONIGURUMA" ) *syntax = ONIG_SYNTAX_ONIGURUMA;
    else if( syntax_s == L"ONIG_SYNTAX_ASIS" ) *syntax = ONIG_SYNTAX_ASIS;
    else if( syntax_s == L"ONIG_SYNTAX_POSIX_BASIC" ) *syntax = ONIG_SYNTAX_POSIX_BASIC;
    else if( syntax_s == L"ONIG_SYNTAX_POSIX_EXTENDED" ) *syntax = ONIG_SYNTAX_POSIX_EXTENDED;
    else if( syntax_s == L"ONIG_SYNTAX_EMACS" ) *syntax = ONIG_SYNTAX_EMACS;
    else if( syntax_s == L"ONIG_SYNTAX_GREP" ) *syntax = ONIG_SYNTAX_GREP;
    else if( syntax_s == L"ONIG_SYNTAX_GNU_REGEX" ) *syntax = ONIG_SYNTAX_GNU_REGEX;
    else if( syntax_s == L"ONIG_SYNTAX_JAVA" ) *syntax = ONIG_SYNTAX_JAVA;
    else if( syntax_s == L"ONIG_SYNTAX_PERL" ) *syntax = ONIG_SYNTAX_PERL;
    else if( syntax_s == L"ONIG_SYNTAX_PERL_NG" ) *syntax = ONIG_SYNTAX_PERL_NG;
    else if( syntax_s == L"ONIG_SYNTAX_RUBY" ) *syntax = ONIG_SYNTAX_RUBY;
    else if( syntax_s == L"ONIG_SYNTAX_PYTHON" ) *syntax = ONIG_SYNTAX_PYTHON;


    // Compile-time options

    *compile_options = ONIG_OPTION_NONE;

    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_SINGLELINE;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_MULTILINE;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_IGNORECASE;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_EXTEND;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_FIND_LONGEST;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_FIND_NOT_EMPTY;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_NEGATE_SINGLELINE;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_DONT_CAPTURE_GROUP;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_CAPTURE_GROUP;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_IGNORECASE_IS_ASCII;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_WORD_IS_ASCII;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_DIGIT_IS_ASCII;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_SPACE_IS_ASCII;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_POSIX_IS_ASCII;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER;
    if( inbr.ReadByte( ) ) *compile_options |= ONIG_OPTION_TEXT_SEGMENT_WORD;

    // Search-time options

    *search_options = ONIG_OPTION_NONE;

    if( inbr.ReadByte( ) ) *search_options |= ONIG_OPTION_NOTBOL;
    if( inbr.ReadByte( ) ) *search_options |= ONIG_OPTION_NOTEOL;
    if( inbr.ReadByte( ) ) *search_options |= ONIG_OPTION_NOT_BEGIN_STRING;
    if( inbr.ReadByte( ) ) *search_options |= ONIG_OPTION_NOT_END_STRING;
    if( inbr.ReadByte( ) ) *search_options |= ONIG_OPTION_NOT_BEGIN_POSITION;

    // Configuration

    *fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY = inbr.ReadByte( ) != 0;
    *fONIG_SYN_STRICT_CHECK_BACKREF = inbr.ReadByte( ) != 0;
}




#define ALL_DATA_FLAGS \
    DECLARE_OP( ONIG_SYN_OP_VARIABLE_META_CHARACTERS )                   \
    DECLARE_OP( ONIG_SYN_OP_DOT_ANYCHAR )                                \
    DECLARE_OP( ONIG_SYN_OP_ASTERISK_ZERO_INF )                          \
    DECLARE_OP( ONIG_SYN_OP_ESC_ASTERISK_ZERO_INF )                      \
    DECLARE_OP( ONIG_SYN_OP_PLUS_ONE_INF )                               \
    DECLARE_OP( ONIG_SYN_OP_ESC_PLUS_ONE_INF )                           \
    DECLARE_OP( ONIG_SYN_OP_QMARK_ZERO_ONE )                             \
    DECLARE_OP( ONIG_SYN_OP_ESC_QMARK_ZERO_ONE )                         \
    DECLARE_OP( ONIG_SYN_OP_BRACE_INTERVAL )                             \
    DECLARE_OP( ONIG_SYN_OP_ESC_BRACE_INTERVAL )                         \
    DECLARE_OP( ONIG_SYN_OP_VBAR_ALT )                                   \
    DECLARE_OP( ONIG_SYN_OP_ESC_VBAR_ALT )                               \
    DECLARE_OP( ONIG_SYN_OP_LPAREN_SUBEXP )                              \
    DECLARE_OP( ONIG_SYN_OP_ESC_LPAREN_SUBEXP )                          \
    DECLARE_OP( ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR )                          \
    DECLARE_OP( ONIG_SYN_OP_ESC_CAPITAL_G_BEGIN_ANCHOR )                 \
    DECLARE_OP( ONIG_SYN_OP_DECIMAL_BACKREF )                            \
    DECLARE_OP( ONIG_SYN_OP_BRACKET_CC )                                 \
    DECLARE_OP( ONIG_SYN_OP_ESC_W_WORD )                                 \
    DECLARE_OP( ONIG_SYN_OP_ESC_LTGT_WORD_BEGIN_END )                    \
    DECLARE_OP( ONIG_SYN_OP_ESC_B_WORD_BOUND )                           \
    DECLARE_OP( ONIG_SYN_OP_ESC_S_WHITE_SPACE )                          \
    DECLARE_OP( ONIG_SYN_OP_ESC_D_DIGIT )                                \
    DECLARE_OP( ONIG_SYN_OP_LINE_ANCHOR )                                \
    DECLARE_OP( ONIG_SYN_OP_POSIX_BRACKET )                              \
    DECLARE_OP( ONIG_SYN_OP_QMARK_NON_GREEDY )                           \
    DECLARE_OP( ONIG_SYN_OP_ESC_CONTROL_CHARS )                          \
    DECLARE_OP( ONIG_SYN_OP_ESC_C_CONTROL )                              \
    DECLARE_OP( ONIG_SYN_OP_ESC_OCTAL3 )                                 \
    DECLARE_OP( ONIG_SYN_OP_ESC_X_HEX2 )                                 \
    DECLARE_OP( ONIG_SYN_OP_ESC_X_BRACE_HEX8 )                           \
    DECLARE_OP( ONIG_SYN_OP_ESC_O_BRACE_OCTAL )                          \
\
    DECLARE_OP2( ONIG_SYN_OP2_ESC_CAPITAL_Q_QUOTE )                      \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_GROUP_EFFECT )                       \
    DECLARE_OP2( ONIG_SYN_OP2_OPTION_PERL )                              \
    DECLARE_OP2( ONIG_SYN_OP2_OPTION_RUBY )                              \
    DECLARE_OP2( ONIG_SYN_OP2_PLUS_POSSESSIVE_REPEAT )                   \
    DECLARE_OP2( ONIG_SYN_OP2_PLUS_POSSESSIVE_INTERVAL )                 \
    DECLARE_OP2( ONIG_SYN_OP2_CCLASS_SET_OP )                            \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_LT_NAMED_GROUP )                     \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_K_NAMED_BACKREF )                      \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_G_SUBEXP_CALL )                        \
    DECLARE_OP2( ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY )                   \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_CAPITAL_C_BAR_CONTROL )                \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_CAPITAL_M_BAR_META )                   \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_V_VTAB )                               \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_U_HEX4 )                               \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_GNU_BUF_ANCHOR )                       \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_P_BRACE_CHAR_PROPERTY )                \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_P_BRACE_CIRCUMFLEX_NOT )               \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_H_XDIGIT )                             \
    DECLARE_OP2( ONIG_SYN_OP2_INEFFECTIVE_ESCAPE )                       \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE )                     \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_CAPITAL_K_KEEP )                       \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_CAPITAL_R_GENERAL_NEWLINE )            \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_CAPITAL_N_O_SUPER_DOT )                \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_TILDE_ABSENT_GROUP )                 \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_X_Y_GRAPHEME_CLUSTER )                 \
    DECLARE_OP2( ONIG_SYN_OP2_ESC_X_Y_TEXT_SEGMENT )                     \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_PERL_SUBEXP_CALL )                   \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_BRACE_CALLOUT_CONTENTS )             \
    DECLARE_OP2( ONIG_SYN_OP2_ASTERISK_CALLOUT_NAME )                    \
    DECLARE_OP2( ONIG_SYN_OP2_OPTION_ONIGURUMA )                         \
    DECLARE_OP2( ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME )                     \
\
    DECLARE_BEHAVIOUR( ONIG_SYN_BACKSLASH_ESCAPE_IN_CC )                 \
    DECLARE_BEHAVIOUR( ONIG_SYN_ALLOW_INTERVAL_LOW_ABBREV )              \
\
    DECLARE_COMPILE_OPTION( ONIG_OPTION_EXTEND )                         


#pragma pack(push, 1)
struct Details
{
#define DECLARE_OP(name) bool f##name;
#define DECLARE_OP2(name) bool f##name;
#define DECLARE_BEHAVIOUR(name) bool f##name;
#define DECLARE_COMPILE_OPTION(name) bool f##name;

    ALL_DATA_FLAGS

#undef DECLARE_OP
#undef DECLARE_OP2
#undef DECLARE_BEHAVIOUR
#undef DECLARE_COMPILE_OPTION

        Details( OnigSyntaxType& adjustedSyntax, OnigOptionType compileOptions )
    {
#define DECLARE_OP(name) f##name = (adjustedSyntax.op & name) != 0;
#define DECLARE_OP2(name) f##name = (adjustedSyntax.op2 & name) != 0;
#define DECLARE_BEHAVIOUR(name) f##name = (adjustedSyntax.behavior & name) != 0;
#define DECLARE_COMPILE_OPTION(name) f##name = (compileOptions & name) != 0;

        ALL_DATA_FLAGS

#undef DECLARE_OP
#undef DECLARE_OP2
#undef DECLARE_BEHAVIOUR
#undef DECLARE_COMPILE_OPTION
    }

};
#pragma pack(pop)


void WriteDetails( BinaryWriterW& outbw, /*const*/ OnigSyntaxType& adjustedSyntax, OnigOptionType compileOptions )
{
    Details details( adjustedSyntax, compileOptions );

    outbw.WriteT<char>( 'b' );

    outbw.WriteT( details );

    outbw.WriteT<char>( 'e' );
}


int main( )
{
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

            outbw.Write( Utf8ToWString( onig_version( ) ) );

            return 0;
        }
        else if( command == L"m" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            std::wstring pattern = inbr.ReadString( );
            std::wstring text = inbr.ReadString( );

            auto syntax = ONIG_SYNTAX_ONIGURUMA;
            OnigOptionType compile_options = ONIG_OPTION_NONE;
            auto search_options = ONIG_OPTION_NONE;
            bool fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY = false;
            bool fONIG_SYN_STRICT_CHECK_BACKREF = false;

            ReadOptions( inbr, &syntax, &compile_options, &search_options, &fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY, &fONIG_SYN_STRICT_CHECK_BACKREF );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            OnigSyntaxType adjusted_syntax{};
            onig_copy_syntax( &adjusted_syntax, syntax );

            if( fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY ) adjusted_syntax.op2 |= ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY;
            if( fONIG_SYN_STRICT_CHECK_BACKREF ) adjusted_syntax.behavior |= ONIG_SYN_STRICT_CHECK_BACKREF;

            DoMatch( outbw, pattern, text, adjusted_syntax, compile_options, search_options );

            return 0;
        }
        else if( command == L"d" )
        {
            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [D1]." );

            auto syntax = ONIG_SYNTAX_ONIGURUMA;
            OnigOptionType compile_options = ONIG_OPTION_NONE;
            auto search_options = ONIG_OPTION_NONE;
            bool fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY = false;
            bool fONIG_SYN_STRICT_CHECK_BACKREF = false;

            ReadOptions( inbr, &syntax, &compile_options, &search_options, &fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY, &fONIG_SYN_STRICT_CHECK_BACKREF );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [D2]." );

            OnigSyntaxType adjusted_syntax{};
            onig_copy_syntax( &adjusted_syntax, syntax );

            if( fONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY ) adjusted_syntax.op2 |= ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY;
            if( fONIG_SYN_STRICT_CHECK_BACKREF ) adjusted_syntax.behavior |= ONIG_SYN_STRICT_CHECK_BACKREF;

            WriteDetails( outbw, adjusted_syntax, compile_options );

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

