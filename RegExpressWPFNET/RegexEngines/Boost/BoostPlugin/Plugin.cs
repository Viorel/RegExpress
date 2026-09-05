using RegExpressLibrary;
using System.Collections.Generic;


namespace BoostPlugin
{
    public class Plugin : RegexPlugin
    {
        #region RegexPlugin

        public override IReadOnlyList<RegexEngine> GetEngines( )
        {
            return [new Engine( )];
        }

        #endregion
    }
}
