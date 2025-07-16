using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace TinyRegexCPlugin
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
