using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace VBScriptPlugin
{
    class Options
    {
        public bool IgnoreCase { get; set; }
        public bool Global { get; set; } = true;


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
