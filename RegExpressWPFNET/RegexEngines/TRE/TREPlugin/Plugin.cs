using RegExpressLibrary;
using System.Collections.Generic;


namespace TREPlugin;

public class Plugin : RegexPlugin
{
    #region RegexPlugin

    public override IReadOnlyList<RegexEngine> GetEngines( )
    {
        return [new Engine( )];
    }

    #endregion
}
