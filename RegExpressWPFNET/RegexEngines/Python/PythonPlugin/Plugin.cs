using RegExpressLibrary;
using System.Collections.Generic;


namespace PythonPlugin;

public class Plugin : RegexPlugin
{
    #region RegexPlugin

    public override IReadOnlyList<RegexEngine> GetEngines( )
    {
        return [new Engine( )];
    }

    #endregion
}
