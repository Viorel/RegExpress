using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace JavaPlugin
{
    internal class Options
    {

        public bool CANON_EQ { get; set; }
        public bool CASE_INSENSITIVE { get; set; }
        public bool COMMENTS { get; set; }
        public bool DOTALL { get; set; }
        public bool LITERAL { get; set; }
        public bool MULTILINE { get; set; }
        public bool UNICODE_CASE { get; set; }
        public bool UNICODE_CHARACTER_CLASS { get; set; }
        public bool UNIX_LINES { get; set; }
        public string? regionStart { get; set; } // (int)
        public string? regionEnd { get; set; } // (int)
        public bool useAnchoringBounds { get; set; } = true;
        public bool useTransparentBounds { get; set; } = false;


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
