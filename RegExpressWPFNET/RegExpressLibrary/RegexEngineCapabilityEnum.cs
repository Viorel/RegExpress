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
        Default = 0,
        NoGroupDetails = 1 << 1, // (no index, no success flag)
        NoCaptures = 1 << 2,
        ScrollErrorsToEnd = 1 << 3,
        OverlappingMatches = 1 << 4,
    }
}
