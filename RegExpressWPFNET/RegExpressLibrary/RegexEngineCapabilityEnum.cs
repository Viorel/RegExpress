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
        NoGroups = 1 << 1,
        NoCaptures = 1 << 2,
        CombineSurrogatePairs = 1 << 3,
        ScrollErrorsToEnd = 1 << 4,
        OverlappingMatches = 1 << 5,
    }
}
