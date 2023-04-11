using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;


namespace OnigurumaPlugin
{
    enum SyntaxEnum
    {
        None = 0,
        ONIG_SYNTAX_ONIGURUMA,
        ONIG_SYNTAX_ASIS,
        ONIG_SYNTAX_POSIX_BASIC,
        ONIG_SYNTAX_POSIX_EXTENDED,
        ONIG_SYNTAX_EMACS,
        ONIG_SYNTAX_GREP,
        ONIG_SYNTAX_GNU_REGEX,
        ONIG_SYNTAX_JAVA,
        ONIG_SYNTAX_PERL,
        ONIG_SYNTAX_PERL_NG,
        ONIG_SYNTAX_RUBY,
        ONIG_SYNTAX_PYTHON,
    }


    //..........[StructLayout( LayoutKind.Sequential )]
    internal class Options
    {
        public SyntaxEnum Syntax { get; set; } = SyntaxEnum.ONIG_SYNTAX_ONIGURUMA;


        // Compile-time options

        public bool ONIG_OPTION_SINGLELINE { get; set; }
        public bool ONIG_OPTION_MULTILINE { get; set; }
        public bool ONIG_OPTION_IGNORECASE { get; set; }
        public bool ONIG_OPTION_EXTEND { get; set; }
        public bool ONIG_OPTION_FIND_LONGEST { get; set; }
        public bool ONIG_OPTION_FIND_NOT_EMPTY { get; set; }
        public bool ONIG_OPTION_NEGATE_SINGLELINE { get; set; }
        public bool ONIG_OPTION_DONT_CAPTURE_GROUP { get; set; }
        public bool ONIG_OPTION_CAPTURE_GROUP { get; set; }
        public bool ONIG_OPTION_IGNORECASE_IS_ASCII { get; set; }
        public bool ONIG_OPTION_WORD_IS_ASCII { get; set; }
        public bool ONIG_OPTION_DIGIT_IS_ASCII { get; set; }
        public bool ONIG_OPTION_SPACE_IS_ASCII { get; set; }
        public bool ONIG_OPTION_POSIX_IS_ASCII { get; set; }
        public bool ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER { get; set; }
        public bool ONIG_OPTION_TEXT_SEGMENT_WORD { get; set; }


        // Search-time options

        public bool ONIG_OPTION_NOTBOL { get; set; }
        public bool ONIG_OPTION_NOTEOL { get; set; }
        public bool ONIG_OPTION_NOT_BEGIN_STRING { get; set; }
        public bool ONIG_OPTION_NOT_END_STRING { get; set; }
        public bool ONIG_OPTION_NOT_BEGIN_POSITION { get; set; }
        //public bool ONIG_OPTION_POSIX_REGION { get; set;}


        // Configuration

        //C( ONIG_SYN_OP_ESC_ASTERISK_ZERO_INF, "enable \\*" );
        //C( ONIG_SYN_OP_ESC_PLUS_ONE_INF, "enable \\+" );
        //C( ONIG_SYN_OP_ESC_QMARK_ZERO_ONE, "enable \\?" );
        //C( ONIG_SYN_OP_ESC_BRACE_INTERVAL, "enable \\{ and \\}" );
        //C( ONIG_SYN_OP_ESC_VBAR_ALT, "enable \\|" );
        //C( ONIG_SYN_OP_ESC_LPAREN_SUBEXP, "enable \\( and \\)" );
        //C( ONIG_SYN_OP_ESC_LTGT_WORD_BEGIN_END, "enable \\< and \\>" );
        //C( ONIG_SYN_OP_ESC_C_CONTROL, "enable \\cx" );
        //C( ONIG_SYN_OP_ESC_OCTAL3, "enable \\000" );
        //C( ONIG_SYN_OP_ESC_X_HEX2, "enable \\xHH" );
        //C( ONIG_SYN_OP_ESC_X_BRACE_HEX8, "enable \\x{HHH…}" );
        //C( ONIG_SYN_OP_ESC_O_BRACE_OCTAL, "enable \\o{OOO…}" );

        //C( ONIG_SYN_OP2_ESC_CAPITAL_Q_QUOTE, "enable \\Q...\\E" );

        public bool ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY { get; set; }
        public bool ONIG_SYN_STRICT_CHECK_BACKREF { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }


        public override bool Equals( object? obj )
        {
            return obj is Options options &&
                     Syntax == options.Syntax &&
                     ONIG_OPTION_SINGLELINE == options.ONIG_OPTION_SINGLELINE &&
                     ONIG_OPTION_MULTILINE == options.ONIG_OPTION_MULTILINE &&
                     ONIG_OPTION_IGNORECASE == options.ONIG_OPTION_IGNORECASE &&
                     ONIG_OPTION_EXTEND == options.ONIG_OPTION_EXTEND &&
                     ONIG_OPTION_FIND_LONGEST == options.ONIG_OPTION_FIND_LONGEST &&
                     ONIG_OPTION_FIND_NOT_EMPTY == options.ONIG_OPTION_FIND_NOT_EMPTY &&
                     ONIG_OPTION_NEGATE_SINGLELINE == options.ONIG_OPTION_NEGATE_SINGLELINE &&
                     ONIG_OPTION_DONT_CAPTURE_GROUP == options.ONIG_OPTION_DONT_CAPTURE_GROUP &&
                     ONIG_OPTION_CAPTURE_GROUP == options.ONIG_OPTION_CAPTURE_GROUP &&
                     ONIG_OPTION_IGNORECASE_IS_ASCII == options.ONIG_OPTION_IGNORECASE_IS_ASCII &&
                     ONIG_OPTION_WORD_IS_ASCII == options.ONIG_OPTION_WORD_IS_ASCII &&
                     ONIG_OPTION_DIGIT_IS_ASCII == options.ONIG_OPTION_DIGIT_IS_ASCII &&
                     ONIG_OPTION_SPACE_IS_ASCII == options.ONIG_OPTION_SPACE_IS_ASCII &&
                     ONIG_OPTION_POSIX_IS_ASCII == options.ONIG_OPTION_POSIX_IS_ASCII &&
                     ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER == options.ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER &&
                     ONIG_OPTION_TEXT_SEGMENT_WORD == options.ONIG_OPTION_TEXT_SEGMENT_WORD &&
                     ONIG_OPTION_NOTBOL == options.ONIG_OPTION_NOTBOL &&
                     ONIG_OPTION_NOTEOL == options.ONIG_OPTION_NOTEOL &&
                     ONIG_OPTION_NOT_BEGIN_STRING == options.ONIG_OPTION_NOT_BEGIN_STRING &&
                     ONIG_OPTION_NOT_END_STRING == options.ONIG_OPTION_NOT_END_STRING &&
                     ONIG_OPTION_NOT_BEGIN_POSITION == options.ONIG_OPTION_NOT_BEGIN_POSITION &&
                     ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY == options.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY &&
                     ONIG_SYN_STRICT_CHECK_BACKREF == options.ONIG_SYN_STRICT_CHECK_BACKREF;
        }

        public override int GetHashCode( )
        {
            HashCode hash = new HashCode( );
            hash.Add( Syntax );
            hash.Add( ONIG_OPTION_SINGLELINE );
            hash.Add( ONIG_OPTION_MULTILINE );
            hash.Add( ONIG_OPTION_IGNORECASE );
            hash.Add( ONIG_OPTION_EXTEND );
            hash.Add( ONIG_OPTION_FIND_LONGEST );
            hash.Add( ONIG_OPTION_FIND_NOT_EMPTY );
            hash.Add( ONIG_OPTION_NEGATE_SINGLELINE );
            hash.Add( ONIG_OPTION_DONT_CAPTURE_GROUP );
            hash.Add( ONIG_OPTION_CAPTURE_GROUP );
            hash.Add( ONIG_OPTION_IGNORECASE_IS_ASCII );
            hash.Add( ONIG_OPTION_WORD_IS_ASCII );
            hash.Add( ONIG_OPTION_DIGIT_IS_ASCII );
            hash.Add( ONIG_OPTION_SPACE_IS_ASCII );
            hash.Add( ONIG_OPTION_POSIX_IS_ASCII );
            hash.Add( ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER );
            hash.Add( ONIG_OPTION_TEXT_SEGMENT_WORD );
            hash.Add( ONIG_OPTION_NOTBOL );
            hash.Add( ONIG_OPTION_NOTEOL );
            hash.Add( ONIG_OPTION_NOT_BEGIN_STRING );
            hash.Add( ONIG_OPTION_NOT_END_STRING );
            hash.Add( ONIG_OPTION_NOT_BEGIN_POSITION );
            hash.Add( ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY );
            hash.Add( ONIG_SYN_STRICT_CHECK_BACKREF );
            return hash.ToHashCode( );
        }
    }


    [StructLayout( LayoutKind.Sequential, Pack = 1 )]
    class Details
    {
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_VARIABLE_META_CHARACTERS;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_DOT_ANYCHAR;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ASTERISK_ZERO_INF;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_ASTERISK_ZERO_INF;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_PLUS_ONE_INF;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_PLUS_ONE_INF;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_QMARK_ZERO_ONE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_QMARK_ZERO_ONE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_BRACE_INTERVAL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_BRACE_INTERVAL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_VBAR_ALT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_VBAR_ALT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_LPAREN_SUBEXP;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_LPAREN_SUBEXP;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_CAPITAL_G_BEGIN_ANCHOR;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_DECIMAL_BACKREF;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_BRACKET_CC;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_W_WORD;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_LTGT_WORD_BEGIN_END;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_B_WORD_BOUND;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_S_WHITE_SPACE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_D_DIGIT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_LINE_ANCHOR;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_POSIX_BRACKET;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_QMARK_NON_GREEDY;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_CONTROL_CHARS;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_C_CONTROL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_OCTAL3;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_X_HEX2;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_X_BRACE_HEX8;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP_ESC_O_BRACE_OCTAL;

        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_CAPITAL_Q_QUOTE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_GROUP_EFFECT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_OPTION_PERL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_OPTION_RUBY;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_PLUS_POSSESSIVE_REPEAT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_PLUS_POSSESSIVE_INTERVAL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_CCLASS_SET_OP;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_LT_NAMED_GROUP;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_K_NAMED_BACKREF;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_G_SUBEXP_CALL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_CAPITAL_C_BAR_CONTROL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_CAPITAL_M_BAR_META;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_V_VTAB;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_U_HEX4;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_GNU_BUF_ANCHOR;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_P_BRACE_CHAR_PROPERTY;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_P_BRACE_CIRCUMFLEX_NOT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_H_XDIGIT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_INEFFECTIVE_ESCAPE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_CAPITAL_K_KEEP;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_CAPITAL_R_GENERAL_NEWLINE;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_CAPITAL_N_O_SUPER_DOT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_TILDE_ABSENT_GROUP;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_X_Y_GRAPHEME_CLUSTER;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ESC_X_Y_TEXT_SEGMENT;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_PERL_SUBEXP_CALL;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_BRACE_CALLOUT_CONTENTS;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_ASTERISK_CALLOUT_NAME;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_OPTION_ONIGURUMA;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME;

        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_BACKSLASH_ESCAPE_IN_CC;
        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_SYN_ALLOW_INTERVAL_LOW_ABBREV;

        [MarshalAs( UnmanagedType.U1 )] public bool ONIG_OPTION_EXTEND;
    }
}
