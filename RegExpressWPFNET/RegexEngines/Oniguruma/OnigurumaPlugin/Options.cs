using System;
using System.Collections.Generic;
using System.Linq;
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
    }
}
