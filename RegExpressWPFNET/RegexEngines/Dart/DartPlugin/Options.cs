using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace DartPlugin
{
    internal class Options
    {
        public bool multiLine { get; set; }
        public bool caseSensitive { get; set; }
        public bool unicode { get; set; }
        public bool dotAll { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
