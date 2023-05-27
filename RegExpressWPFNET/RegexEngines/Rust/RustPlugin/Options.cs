using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RustPlugin
{

    enum StructEnum
    {
        None,
        Regex,
        RegexBuilder,
    }

    internal class Options
    {
        public StructEnum @struct { get; set; } = StructEnum.Regex;

        public bool case_insensitive { get; set; }
        public bool multi_line { get; set; }
        public bool dot_matches_new_line { get; set; }
        public bool swap_greed { get; set; }
        public bool ignore_whitespace { get; set; }
        public bool unicode { get; set; } = true;
        public bool octal { get; set; }

        public string? size_limit { get; set; }
        public string? dfa_size_limit { get; set; }
        public string? nest_limit { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
