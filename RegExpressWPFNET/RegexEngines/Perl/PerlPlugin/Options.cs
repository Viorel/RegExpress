using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace PerlPlugin
{
    class Options
    {
#pragma warning disable IDE1006 // Naming Styles

        public bool m { get; set; }
        public bool s { get; set; }
        public bool i { get; set; }
        public bool x { get; set; }
        public bool xx { get; set; }
        public bool n { get; set; }
        public bool a { get; set; }
        public bool aa { get; set; }
        public bool d { get; set; }
        public bool u { get; set; }
        public bool l { get; set; }
        public bool g { get; set; } = true;
        //public bool c { get; set; } // (has sense in case of a sequence of matches)

#pragma warning restore IDE1006 // Naming Styles

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
