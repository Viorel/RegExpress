using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace DotNETPlugin
{
    enum ClassEnum
    {
        None,
        RegexDotNet,
        RegexDotNetFramework,
        GoRegexp,
    }

    sealed class Options
    {
        public ClassEnum Class { get; set; } = ClassEnum.RegexDotNet;

        public bool Compiled { get; set; }
        public bool CultureInvariant { get; set; }
        public bool ECMAScript { get; set; }
        public bool ExplicitCapture { get; set; }
        public bool IgnoreCase { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool Multiline { get; set; }
        public bool NonBacktracking { get; set; } // not in .NET Framework 4.8
        public bool RightToLeft { get; set; }
        public bool Singleline { get; set; }

        public long TimeoutMs { get; set; } = 10_000;

        // go.regexp

        public bool posix { get; set; }
        public bool longest { get; set; }
        public bool literal { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
