using RegExpressLibrary;
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Windows;

namespace VBScriptPlugin;

static class Versions
{
    static Lazy<string> lazyVBScriptVersion = new( ( ) => GetVBScriptVersion( ) ?? "" );

    public static string VBScript { get; } = lazyVBScriptVersion.Value; // the version of VBScript is determined programmatically
    public static string TwinBasic { get; } = "1.7.0.0";

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
