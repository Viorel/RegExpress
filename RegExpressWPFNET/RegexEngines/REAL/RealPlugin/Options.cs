using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RealPlugin
{
    internal class Options
    {
        public bool icase { get; set; }
        public bool multiline { get; set; }
        public bool dotall { get; set; }
        //public bool bytes { get; set; } // not supported here
        public bool verbose { get; set; }
        public bool ecma { get; set; }
        public bool ascii { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
