using RegExpressLibrary;
using System.Collections.Generic;


namespace HtmlAgilityPackPlugin
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
