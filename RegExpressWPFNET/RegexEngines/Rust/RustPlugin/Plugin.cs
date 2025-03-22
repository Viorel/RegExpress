using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace RustPlugin
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
