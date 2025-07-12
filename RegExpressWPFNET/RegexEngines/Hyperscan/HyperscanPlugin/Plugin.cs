using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace HyperscanPlugin
{
    public class Plugin : RegexPlugin
    {
        #region RegexPlugin

        public override IReadOnlyList<IRegexEngine> GetEngines( )
        {
            return new IRegexEngine[] { new Engine( ), new ChimeraEngine( ) };
        }

        #endregion

    }
}
