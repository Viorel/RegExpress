using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace TREPlugin
{
    internal class Options
    {
        public bool REG_EXTENDED { get; set; } = true;
        public bool REG_ICASE { get; set; }
        public bool REG_NOSUB { get; set; }
        public bool REG_NEWLINE { get; set; }
        public bool REG_LITERAL { get; set; }
        public bool REG_RIGHT_ASSOC { get; set; }
        public bool REG_UNGREEDY { get; set; }

        //

        public bool REG_NOTBOL { get; set; }
        public bool REG_NOTEOL { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
