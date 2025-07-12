using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace DotNETFrameworkPlugin
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
