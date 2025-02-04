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

        public string? backtrack_limit { get; set; }
        public string? delegate_size_limit { get; set; }
        public string? delegate_dfa_size_limit { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
