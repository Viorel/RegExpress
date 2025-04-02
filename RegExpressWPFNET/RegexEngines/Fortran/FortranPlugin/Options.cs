using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace FortranPlugin
{

    enum ModuleEnum
    {
        None,
        Forgex,
        RegexPerazz,
        RegexJeyemhex,
    }

    internal class Options
    {
        public ModuleEnum Module { get; set; } = ModuleEnum.Forgex;

        public bool MatchAll { get; set; } = false;

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
