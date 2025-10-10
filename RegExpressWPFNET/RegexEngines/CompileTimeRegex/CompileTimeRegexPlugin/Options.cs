using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace CompileTimeRegexPlugin
{
    internal class Options
    {
        public string? max_captures { get; set; } = "10";
        public string? max_depth { get; set; } = "4";

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
