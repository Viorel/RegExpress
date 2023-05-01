using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace WebView2Plugin
{
    class Options
    {
        public bool i { get; set; }
        public bool m { get; set; }
        public bool s { get; set; }
        public bool u { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
