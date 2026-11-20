using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace JavaScriptPlugin;

class Engine : RegexEngine
{
    Options mOptions = new( );
    readonly Lazy<UCOptions> mOptionsControl;

    public Engine( )
    {
        mOptionsControl = new Lazy<UCOptions>( ( ) =>
        {
            UCOptions oc = new( );
            oc.SetOptions( Options );
            oc.Changed += OptionsControl_Changed;

            return oc;
        } );
    }

    public Options Options
    {
        get
        {
            return mOptions;
        }
        set
        {
            mOptions = value;

            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }
    }

    #region RegexEngine

    public override string Kind => "JavaScript";

    public override string Version => ""; // (versions are supplied by Runtimes, displayed in combobox)

    public override string Name => "JavaScript";

    public override string Subtitle
    {
        get
        {
            return Options.Runtime switch
            {
                RuntimeEnum.WebView2 => "JavaScript (WebView2)",
                RuntimeEnum.NodeJs => "JavaScript (Node.js)",
                RuntimeEnum.QuickJs => "JavaScript (QuickJs)",
                RuntimeEnum.SpiderMonkey => "JavaScript (SpiderMonkey)",
                RuntimeEnum.Bun => "JavaScriptCore (Bun)",
                RuntimeEnum.RE2JS => "JavaScript (RE2JS)",
                RuntimeEnum.RegexPlus => "JavaScript (Regex+)",
                RuntimeEnum.Jint => "JavaScript (Jint)",
                _ => "Unknown"
            };
        }
    }

    public override string? NoteForCaptures => null;

    public override Control GetOptionsControl( )
    {
        return mOptionsControl.Value;
    }

    public override string? ExportOptions( )
    {
        string json = JsonSerializer.Serialize( Options, JsonUtilities.JsonOptions );

        return json;
    }

    public override void ImportOptions( string? json )
    {
        if( string.IsNullOrWhiteSpace( json ) )
        {
            Options = new Options( );
        }
        else
        {
            try
            {
                Options = JsonSerializer.Deserialize<Options>( json, JsonUtilities.JsonOptions )!;
            }
            catch
            {
                // ignore versioning errors, for example
                if( Debugger.IsAttached ) Debugger.Break( );

                Options = new Options( );
            }
        }
    }

    public override IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
    {
        return
            [
                new ("V8", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.NodeJs, u = false, v = false } }),
                //new ("V8, “u”", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.NodeJs, u = true, v = false } }),
                new ("V8 (“v”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.NodeJs, u = false, v = true } }),

                //new ("Qjs", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.QuickJs, u = false, v = false } }),
                new ("Qjs (“u”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.QuickJs, u = true, v = false } }),

                //new ("SM", LazyFeatureMatrix_SpiderMonkey.GetValue( false), new Engine( ) { Options = new Options { Runtime = RuntimeEnum.SpiderMonkey, u = false, v = false } }),
                new ("SM (“u”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.SpiderMonkey, u = true, v = false } }),

                //new ("Bun", LazyFeatureMatrix_Bun.GetValue( false), new Engine( ) { Options = new Options { Runtime = RuntimeEnum.Bun, u = false, v = false } }),
                new ("Bun (“u”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.Bun, u = true, v = false } }),

                new ("RE2JS", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.RE2JS, LOOKBEHINDS = true } }),

                //new ("Regex+ (“x”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.RegexPlus, v = false, x = true, n = false } }), //...
                new ("Regex+ (“vx”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.RegexPlus, v = true, x = true, n = false } }),

                //new ("Jint", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.Jint, u = false, v = false} }),
                //new ("Jint (“u”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.Jint, u = true, v = false} }),
                new ("Jint (“v”)", new Engine( ) { Options = new Options { Runtime = RuntimeEnum.Jint, u = false, v = true } }),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.i = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.x = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
        //Options.n = !yes;
        //if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetStickyMode( bool yes )
    {
        Options.y = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.Runtime switch
        {
            RuntimeEnum.WebView2 => new SubengineWebView2( Options ),
            RuntimeEnum.NodeJs => new SubengineNodeJs( Options ),
            RuntimeEnum.QuickJs => new SubengineQuickJs( Options ),
            RuntimeEnum.SpiderMonkey => new SubengineSpiderMonkey( Options ),
            RuntimeEnum.Bun => new SubengineBun( Options ),
            RuntimeEnum.RE2JS => new SubengineRE2JS( Options ),
            RuntimeEnum.RegexPlus => new SubengineRegexPlus( Options ),
            RuntimeEnum.Jint => new SubengineJint( Options ),
            _ => throw new NotSupportedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
