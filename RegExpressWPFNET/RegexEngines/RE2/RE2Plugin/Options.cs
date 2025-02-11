using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RE2Plugin
{
    enum AnchorEnum
    {
        None,
        UNANCHORED,
        ANCHOR_START,
        ANCHOR_BOTH,
    }

    internal class Options
    {
        public bool posix_syntax { get; set; }
        public bool longest_match { get; set; }
        public bool literal { get; set; }
        public bool never_nl { get; set; }
        public bool dot_nl { get; set; }
        public bool never_capture { get; set; }
        public bool case_sensitive { get; set; }
        public bool perl_classes { get; set; }
        public bool word_boundary { get; set; }
        public bool one_line { get; set; }


        public AnchorEnum anchor { get; set; } = AnchorEnum.UNANCHORED;

        public string? max_mem { get; set; } // (int64_t)

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
