using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RustPlugin
{
    enum CrateEnum
    {
        None,
        regex,
        regex_lite,
        fancy_regex,
        regress,
    }

    enum StructEnum
    {
        None,
        Regex,
        RegexBuilder,
    }

    internal class Options
    {
        public CrateEnum @crate { get; set; } = CrateEnum.regex;
        public StructEnum @struct { get; set; } = StructEnum.Regex;

        public bool case_insensitive { get; set; }
        public bool multi_line { get; set; }
        public bool dot_matches_new_line { get; set; }
        public bool swap_greed { get; set; }
        public bool ignore_whitespace { get; set; }
        public bool unicode { get; set; } = true;
        public bool octal { get; set; }
        public bool crlf { get; set; } // ('regex_lite' specific)
        public bool no_opt { get; set; } // ('regress' specific)
        public bool unicode_sets { get; set; } // ('regress' specific)

        // Regex and Regex_lite crates

        public string? size_limit { get; set; }
        public string? dfa_size_limit { get; set; } // (not in 'regex_lite')
        public string? nest_limit { get; set; }

        // Fancy_regex crate

        public string? backtrack_limit { get; set; }
        public string? delegate_size_limit { get; set; }
        public string? delegate_dfa_size_limit { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
