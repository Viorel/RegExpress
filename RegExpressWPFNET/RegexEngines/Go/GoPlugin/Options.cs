using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace GoPlugin
{

    internal class Options
    {
        public bool posix_syntax { get; set; }
        public bool longest_match { get; set; }
        public bool literal { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
