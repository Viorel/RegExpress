﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace FortranPlugin
{

    internal class Options
    {

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
