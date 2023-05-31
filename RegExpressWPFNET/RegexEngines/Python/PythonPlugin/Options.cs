using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace PythonPlugin
{
    internal class Options
    {
        public bool ASCII { get; set; }
        public bool DOTALL { get; set; }
        public bool IGNORECASE { get; set; }
        public bool LOCALE { get; set; }
        public bool MULTILINE { get; set; }
        public bool VERBOSE { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
