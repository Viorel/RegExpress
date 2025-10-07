using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HyperscanPlugin
{
    enum ChimeraModeEnum
    {
        None,
        CH_MODE_NOGROUPS,
        CH_MODE_GROUPS,
    }


    internal sealed class ChimeraOptions
    {
        public bool CH_FLAG_CASELESS { get; set; }
        public bool CH_FLAG_DOTALL { get; set; }
        public bool CH_FLAG_MULTILINE { get; set; }
        public bool CH_FLAG_SINGLEMATCH { get; set; }
        public bool CH_FLAG_UTF8 { get; set; } = true;
        public bool CH_FLAG_UCP { get; set; }

        public ChimeraModeEnum Mode { get; set; } = ChimeraModeEnum.CH_MODE_GROUPS;

        public string MatchLimit { get; set; } = "";
        public string MatchLimitRecursion { get; set; } = "";

        public ChimeraOptions Clone( )
        {
            return (ChimeraOptions)MemberwiseClone( );
        }
    }
}
