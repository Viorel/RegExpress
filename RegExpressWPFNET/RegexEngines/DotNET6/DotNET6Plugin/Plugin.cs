using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace DotNET6Plugin
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
