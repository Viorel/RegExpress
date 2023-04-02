using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace DotStdPlugin
{
    enum GrammarEnum
    {
        None,
        ECMAScript,
        basic,
        extended,
        awk,
        grep,
        egrep,
    }


    class Options
    {
        public GrammarEnum Grammar { get; set; } = GrammarEnum.ECMAScript;


        public bool icase { get; set; }
        public bool nosubs { get; set; }
        public bool optimize { get; set; }
        public bool collate { get; set; }


        public bool match_not_bol { get; set; }
        public bool match_not_eol { get; set; }
        public bool match_not_bow { get; set; }
        public bool match_not_eow { get; set; }
        public bool match_any { get; set; }
        public bool match_not_null { get; set; }
        public bool match_continuous { get; set; }
        public bool match_prev_avail { get; set; }


        public string REGEX_MAX_STACK_COUNT { get; set; }
        public string REGEX_MAX_COMPLEXITY_COUNT { get; set; }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
