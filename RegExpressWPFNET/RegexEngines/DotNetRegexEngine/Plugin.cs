using System;
using System.Diagnostics;
using RegExpressLibrary;


namespace DotNetRegexEngine
{
    public class Plugin : IRegexPlugin
    {
        static readonly Lazy<string> LazyVersion = new Lazy<string>( GetVersion );

        #region IRegexPlugin

        public string Id => ".NET";
        public string Name => ".NET, Regex class";
        public string Version => LazyVersion.Value;

        #endregion

        static string GetVersion( )
        {
            try
            {
                return "???";//........DotNetMatcher.GetVersion( );
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }
        }

    }
}
