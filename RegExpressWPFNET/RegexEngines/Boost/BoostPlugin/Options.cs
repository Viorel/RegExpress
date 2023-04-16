using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace BoostPlugin
{
    enum GrammarEnum
    {
        None = 0,
        normal,
        ECMAScript,
        JavaScript,
        JScript,
        perl,
        extended,
        egrep,
        awk,
        basic,
        sed,
        grep,
        emacs,
        literal,
    }

    internal class Options
    {
        public GrammarEnum Grammar { get; set; } = GrammarEnum.ECMAScript;


        // Syntax options

        public bool icase { get; set; }
        public bool nosubs { get; set; }
        public bool optimize { get; set; }
        public bool collate { get; set; }
        //public bool  newline_alt {get;set;}
        public bool no_except { get; set; }
        public bool no_mod_m { get; set; }
        public bool no_mod_s { get; set; }
        public bool mod_s { get; set; }
        public bool mod_x { get; set; }
        public bool no_empty_expressions { get; set; }
        //public bool save_subexpression_location{get;set;}

        // TODO: define extra-options too
        //...


        // Match options

        public bool match_not_bob { get; set; }
        public bool match_not_eob { get; set; }
        public bool match_not_bol { get; set; }
        public bool match_not_eol { get; set; }
        public bool match_not_bow { get; set; }
        public bool match_not_eow { get; set; }
        public bool match_any { get; set; }
        public bool match_not_null { get; set; }
        public bool match_continuous { get; set; }
        public bool match_partial { get; set; }
        public bool match_extra { get; set; }
        public bool match_single_line { get; set; }
        public bool match_prev_avail { get; set; }
        public bool match_not_dot_newline { get; set; }
        public bool match_not_dot_null { get; set; }
        public bool match_posix { get; set; }
        public bool match_perl { get; set; }
        public bool match_nosubs { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
