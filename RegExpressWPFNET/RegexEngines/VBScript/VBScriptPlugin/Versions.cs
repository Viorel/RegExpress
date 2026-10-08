using RegExpressLibrary;
using System;
using System.Diagnostics;

namespace VBScriptPlugin;

class Versions
{
    static Lazy<string> lazyVBScriptVersion = new( ( ) => GetVBScriptVersion( ) ?? "" );

    public static string VBScript { get; } = lazyVBScriptVersion.Value; // the version of VBScript is determined programmatically
    public static string TwinBasic { get; } = "beta-x-1002";

    static string? GetVBScriptVersion( )
    {
        try
        {
            return SubengineVBScript.GetVersion( NonCancellable.Instance );
        }
        catch( Exception exc )
        {
            _ = exc;
            if( Debugger.IsAttached ) Debugger.Break( );

            return null;
        }
    }

}
