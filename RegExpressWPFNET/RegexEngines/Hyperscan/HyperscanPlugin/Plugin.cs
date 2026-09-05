using RegExpressLibrary;
using System.Collections.Generic;


namespace HyperscanPlugin;

public class Plugin : RegexPlugin
{
    #region RegexPlugin

    public override IReadOnlyList<RegexEngine> GetEngines( )
    {
        return [new HyperscanEngine( ), new ChimeraEngine( )];
    }

    #endregion
}
