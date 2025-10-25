using System;
using System.Collections.Generic;


namespace RegExpressLibrary
{
    public abstract class RegexPlugin
    {
        public abstract IReadOnlyList<IRegexEngine> GetEngines(); // returns fresh engines (not cached)
    }
}
