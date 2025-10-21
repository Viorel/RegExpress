using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace PCRE2Plugin
{
    enum AlgorithmEnum
    {
        None = 0,
        Standard,
        DFA,
    }


    internal class Options
    {
        public AlgorithmEnum Algorithm { get; set; } = AlgorithmEnum.Standard;
        public string Locale { get; set; } = "C"; // ("C" -- default C-language locale; "" -- use current system locale) 


        // Compile options (from https://pcre2project.github.io/pcre2/doc/pcre2_compile/)

        public bool PCRE2_ANCHORED { get; set; }
        public bool PCRE2_ALLOW_EMPTY_CLASS { get; set; }
        public bool PCRE2_ALT_BSUX { get; set; }
        public bool PCRE2_ALT_CIRCUMFLEX { get; set; }
        public bool PCRE2_ALT_EXTENDED_CLASS { get; set; }
        public bool PCRE2_ALT_VERBNAMES { get; set; }
        //public bool PCRE2_AUTO_CALLOUT {get;set;}
        public bool PCRE2_CASELESS { get; set; }
        public bool PCRE2_DOLLAR_ENDONLY { get; set; }
        public bool PCRE2_DOTALL { get; set; }
        public bool PCRE2_DUPNAMES { get; set; }
        public bool PCRE2_ENDANCHORED { get; set; }
        public bool PCRE2_EXTENDED { get; set; }
        public bool PCRE2_EXTENDED_MORE { get; set; }
        public bool PCRE2_FIRSTLINE { get; set; }
        public bool PCRE2_LITERAL { get; set; }
        //public bool PCRE2_MATCH_INVALID_UTF {get;set;}
        public bool PCRE2_MATCH_UNSET_BACKREF { get; set; }
        public bool PCRE2_MULTILINE { get; set; }
        public bool PCRE2_NEVER_BACKSLASH_C { get; set; }
        public bool PCRE2_NEVER_UCP { get; set; }
        public bool PCRE2_NEVER_UTF { get; set; }
        public bool PCRE2_NO_AUTO_CAPTURE { get; set; }
        public bool PCRE2_NO_AUTO_POSSESS { get; set; }
        public bool PCRE2_NO_DOTSTAR_ANCHOR { get; set; }
        public bool PCRE2_NO_START_OPTIMIZE { get; set; }
        //public bool PCRE2_NO_UTF_CHECK {get;set;}
        public bool PCRE2_UCP { get; set; }
        public bool PCRE2_UNGREEDY { get; set; }
        public bool PCRE2_USE_OFFSET_LIMIT { get; set; }
        //public bool PCRE2_UTF {get;set;}


        // Extra compile options

        public bool PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK { get; set; }
        public bool PCRE2_EXTRA_ALLOW_SURROGATE_ESCAPES { get; set; }
        public bool PCRE2_EXTRA_ALT_BSUX { get; set; }
        public bool PCRE2_EXTRA_ASCII_BSD { get; set; }
        public bool PCRE2_EXTRA_ASCII_BSS { get; set; }
        public bool PCRE2_EXTRA_ASCII_BSW { get; set; }
        public bool PCRE2_EXTRA_ASCII_DIGIT { get; set; }
        public bool PCRE2_EXTRA_ASCII_POSIX { get; set; }
        public bool PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL { get; set; }
        public bool PCRE2_EXTRA_CASELESS_RESTRICT { get; set; }
        public bool PCRE2_EXTRA_ESCAPED_CR_IS_LF { get; set; }
        public bool PCRE2_EXTRA_MATCH_LINE { get; set; }
        public bool PCRE2_EXTRA_MATCH_WORD { get; set; }
        public bool PCRE2_EXTRA_NEVER_CALLOUT { get; set; }
        public bool PCRE2_EXTRA_NO_BS0 { get; set; }
        public bool PCRE2_EXTRA_PYTHON_OCTAL { get; set; }
        public bool PCRE2_EXTRA_TURKISH_CASING { get; set; }


        // Match options

        public bool PCRE2_ANCHORED_mo { get; set; } // ('_mo' added to avoid duplicate names)
        public bool PCRE2_COPY_MATCHED_SUBJECT { get; set; }
        public bool PCRE2_DISABLE_RECURSELOOP_CHECK { get; set; }
        public bool PCRE2_ENDANCHORED_mo { get; set; } // ('_mo' added to avoid duplicate names)
        public bool PCRE2_NOTBOL { get; set; }
        public bool PCRE2_NOTEOL { get; set; }
        public bool PCRE2_NOTEMPTY { get; set; }
        public bool PCRE2_NOTEMPTY_ATSTART { get; set; }
        public bool PCRE2_NO_JIT { get; set; }
        //public bool PCRE2_NO_UTF_CHECK {get;set;}
        public bool PCRE2_PARTIAL_HARD { get; set; }
        public bool PCRE2_PARTIAL_SOFT { get; set; }
        public bool PCRE2_DFA_SHORTEST { get; set; }


        // JIT options

        public bool UseJIT { get; set; }
        public bool PCRE2_JIT_COMPLETE { get; set; }
        public bool PCRE2_JIT_PARTIAL_SOFT { get; set; }
        public bool PCRE2_JIT_PARTIAL_HARD { get; set; }


        // Limits

        public string? DepthLimit { get; set; }
        public string? HeapLimit { get; set; }
        public string? MatchLimit { get; set; }
        public string? MaxPatternCompiledLength { get; set; }
        public string? OffsetLimit { get; set; }
        public string? ParensNestLimit { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
