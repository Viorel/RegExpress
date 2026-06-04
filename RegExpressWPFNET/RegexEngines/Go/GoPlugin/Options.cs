using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace GoPlugin
{
    enum PackageEnum
    {
        None,
        regexp,
        regexp2,
        rexa,
    }

    internal class Options
    {
        public PackageEnum Package { get; set; } = PackageEnum.regexp;

        public bool posix_syntax { get; set; }
        public bool longest_match { get; set; }
        public bool literal { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
