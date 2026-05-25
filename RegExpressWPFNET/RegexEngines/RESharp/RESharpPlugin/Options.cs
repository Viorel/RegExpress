using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace RESharpPlugin
{
    sealed class Options
    {
        public bool IgnoreCase { get; set; }
        public bool UseDotnetUnicode { get; set; }
        public bool MinimizePattern { get; set; }
        public bool FindLookaroundPrefix { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
