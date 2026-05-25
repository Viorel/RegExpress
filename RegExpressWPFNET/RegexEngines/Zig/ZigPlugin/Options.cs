using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace ZigPlugin
{
    internal class Options
    {
        public bool case_insensitive { get; set; }
        public bool multiline { get; set; }
        public bool dot_all { get; set; }
        public bool extended { get; set; }
        public bool unicode { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
