using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace DotNETFrameworkPlugin
{
    sealed class Options
    {
        public bool Compiled { get; set; }
        public bool CultureInvariant { get; set; }
        public bool ECMAScript { get; set; }
        public bool ExplicitCapture { get; set; }
        public bool IgnoreCase { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool Multiline { get; set; }
        public bool RightToLeft { get; set; }
        public bool Singleline { get; set; }

        public long TimeoutMs { get; set; } = 10_000;

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
