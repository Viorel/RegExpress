using System.Collections.Generic;


namespace RegExpressLibrary
{
    public abstract class RegexPlugin
    {
        public abstract IReadOnlyList<RegexEngine> GetEngines( ); // returns fresh engines (not cached)
    }
}
