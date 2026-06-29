using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary
{
    [Flags]
    public enum RegexEngineCapabilityEnum
    {
        None = 0,
        NoGroups = 1 << 1, // (no groups and captures) 
        NoGroupIndex = 1 << 2, // (no index information for groups; only value is available)
        NoGroupSuccessFlag = 1 << 3, // (no success flag for groups; failed groups are empty and cannot be distinguished from succeeded empty groups)
        NoCaptures = 1 << 4,
        ScrollErrorsToEnd = 1 << 5,
        OverlappingMatches = 1 << 6,
    }
}
