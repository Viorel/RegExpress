using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace PCRE2Plugin
{
    public class Plugin : IRegexPlugin
    {
        #region IRegexPlugin

        public IReadOnlyList<IRegexEngine> GetEngines( )
        {
            return new[] { new Engine( ) };
        }

        #endregion

    }
}
