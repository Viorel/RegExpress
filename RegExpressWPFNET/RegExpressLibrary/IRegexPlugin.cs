using System;
using System.Collections.Generic;

namespace RegExpressLibrary
{
    public interface IRegexPlugin
    {
        IReadOnlyList<IRegexEngine> GetEngines();
    }
}
