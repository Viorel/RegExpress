using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace CompileTimeRegexPlugin
{
    internal class Options
    {
        public bool case_insensitive { get; set; }
        public bool multiline { get; set; }
        public bool singleline { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
