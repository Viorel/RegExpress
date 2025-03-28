﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace HyperscanPlugin
{
    enum ModeEnum
    {
        None,
        HS_MODE_BLOCK,
        HS_MODE_STREAM,
        HS_MODE_VECTORED,
    }


    enum ModeSomEnum
    {
        None,
        HS_MODE_SOM_HORIZON_LARGE,
        HS_MODE_SOM_HORIZON_MEDIUM,
        HS_MODE_SOM_HORIZON_SMALL,
    }


    internal class Options
    {
        public bool HS_FLAG_CASELESS { get; set; }
        public bool HS_FLAG_DOTALL { get; set; }
        public bool HS_FLAG_MULTILINE { get; set; }
        public bool HS_FLAG_SINGLEMATCH { get; set; }
        public bool HS_FLAG_ALLOWEMPTY { get; set; }
        public bool HS_FLAG_UTF8 { get; set; } = true;
        public bool HS_FLAG_UCP { get; set; }
        public bool HS_FLAG_PREFILTER { get; set; }
        public bool HS_FLAG_SOM_LEFTMOST { get; set; } = true;

        //public bool HS_FLAG_COMBINATION { get; set; } // has sense in case of multiple patterns

        public bool HS_FLAG_QUIET { get; set; }


        public string LevenshteinDistance { get; set; } = "";
        public string HammingDistance { get; set; } = "";
        public string MinOffset { get; set; } = "";
        public string MaxOffset { get; set; } = "";
        public string MinLength { get; set; } = "";


        public ModeEnum Mode { get; set; } = ModeEnum.HS_MODE_BLOCK;

        public ModeSomEnum ModeSom { get; set; } = ModeSomEnum.None;


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }


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
