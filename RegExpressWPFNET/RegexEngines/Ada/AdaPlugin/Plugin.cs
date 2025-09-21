using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace AdaPlugin
{
    public class Plugin : RegexPlugin
    {
        #region RegexPlugin

        public override IReadOnlyList<IRegexEngine> GetEngines( )
        {
            return new[] { new Engine( ) };
        }

        #endregion
    }
}
