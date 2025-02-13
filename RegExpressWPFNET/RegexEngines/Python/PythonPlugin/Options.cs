using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace PythonPlugin
{

    enum ModuleEnum
    {
        None,
        re,
        regex,
    }


    internal class Options
    {
        public ModuleEnum Module { get; set; } = ModuleEnum.re;


        // For "re" and "regex":

        public bool ASCII { get; set; }
        public bool DOTALL { get; set; }
        public bool IGNORECASE { get; set; }
        public bool LOCALE { get; set; }
        public bool MULTILINE { get; set; }
        public bool VERBOSE { get; set; }


        // For "regex":
        public bool BESTMATCH { get; set; }
        public bool ENHANCEMATCH { get; set; }
        public bool FULLCASE { get; set; }
        public bool POSIX { get; set; }
        public bool REVERSE { get; set; }
        public bool UNICODE { get; set; }
        public bool WORD { get; set; }
        public bool VERSION0 { get; set; }
        public bool VERSION1 { get; set; } = true;
        public bool overlapped { get; set; }
        public bool partial { get; set; }
        public string? timeout { get; set; } // seconds, double

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
