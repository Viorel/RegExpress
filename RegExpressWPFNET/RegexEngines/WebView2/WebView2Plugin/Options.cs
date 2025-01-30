using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace WebView2Plugin
{
    enum FunctionEnum
    {
        None,
        MatchAll,
        Exec
    }

    class Options
    {
        public FunctionEnum Function { get; set; } = FunctionEnum.MatchAll;

        public bool i { get; set; }
        public bool m { get; set; }
        public bool s { get; set; }
        public bool u { get; set; }
        public bool v { get; set; }
        public bool y { get; set; }
        public bool g { get; set; } = true;

        [JsonIgnore]
        public bool d { get; set; } = true; // to avoid binding errors and to show the checkbox in checked state


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
