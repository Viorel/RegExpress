using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace DotNETFrameworkPlugin
{
    public class Plugin : IRegexPlugin
    {
        #region IRegexPlugin

        public IReadOnlyList<IRegexEngine> GetEngines( )
        {
            return [new Engine( )];
        }

        #endregion
    }
}
