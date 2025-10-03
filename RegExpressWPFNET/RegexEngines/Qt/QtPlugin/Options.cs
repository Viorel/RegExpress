using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace QtPlugin
{

    class Options
    {
        public bool CaseInsensitiveOption { get; set; }
        public bool DotMatchesEverythingOption { get; set; }
        public bool MultilineOption { get; set; }
        public bool ExtendedPatternSyntaxOption { get; set; }
        public bool InvertedGreedinessOption { get; set; }
        public bool DontCaptureOption { get; set; }
        public bool UseUnicodePropertiesOption { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
