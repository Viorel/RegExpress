using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace HyperscanPlugin
{
    public class Plugin : IRegexPlugin
    {
        #region IRegexPlugin

        public IReadOnlyList<IRegexEngine> GetEngines( )
        {
            return new IRegexEngine[] { new Engine( ), new ChimeraEngine( ) };
        }

        #endregion

    }
}
