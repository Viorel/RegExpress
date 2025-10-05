using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace StdPlugin
{
    enum CompilerEnum
    {
        None,
        MSVC,
        GCC,
        SRELL,
    }

    enum GrammarEnum
    {
        None,
        ECMAScript,
        basic,
        extended,
        awk,
        grep,
        egrep,
    }

    class Options
    {
        public CompilerEnum Compiler { get; set; } = CompilerEnum.MSVC;
        public GrammarEnum Grammar { get; set; } = GrammarEnum.ECMAScript;
        public string Locale { get; set; } = "C"; // ("C" -- default C-language locale; "" -- use current system locale; GCC also accepts "POSIX") 


        public bool icase { get; set; }
        public bool nosubs { get; set; }
        public bool optimize { get; set; }
        public bool collate { get; set; }
        public bool multiline { get; set; } // not for MSVC, where it is true by default

        public bool match_not_bol { get; set; }
        public bool match_not_eol { get; set; }
        public bool match_not_bow { get; set; }
        public bool match_not_eow { get; set; }
        public bool match_any { get; set; }
        public bool match_not_null { get; set; }
        public bool match_continuous { get; set; }
        public bool match_prev_avail { get; set; }

        // MSVC specific

        public string? REGEX_MAX_STACK_COUNT { get; set; }
        public string? REGEX_MAX_COMPLEXITY_COUNT { get; set; }

        // GCC specific

        public bool polynomial { get; set; }

        // SRELL specific

        public bool dotall { get; set; }
        public bool unicodesets { get; set; }
        public bool vmode { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
