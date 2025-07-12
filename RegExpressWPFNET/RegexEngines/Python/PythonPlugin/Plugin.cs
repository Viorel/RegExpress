using System;
using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace PythonPlugin
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
