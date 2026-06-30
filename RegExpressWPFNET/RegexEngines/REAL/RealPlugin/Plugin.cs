using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace RealPlugin
{
    public class Plugin : RegexPlugin
    {
        #region RegexPlugin

        public override IReadOnlyList<IRegexEngine> GetEngines( )
        {
            return [new Engine( )];
        }

        #endregion

    }
}
