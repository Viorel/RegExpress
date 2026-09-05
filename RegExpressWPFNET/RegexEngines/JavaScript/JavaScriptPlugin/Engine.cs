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
        Engine njs_engine_no_uv = new( ) { Options = new Options { Runtime = RuntimeEnum.NodeJs, u = false, v = false } };
        //Engine njs_engine_u = new( ) { Options = new Options { Runtime = RuntimeEnum.NodeJs, u = true, v = false } };
        Engine njs_engine_v = new( ) { Options = new Options { Runtime = RuntimeEnum.NodeJs, u = false, v = true } };
        //Engine qjs_engine_no_u = new( ) { Options = new Options { Runtime = RuntimeEnum.QuickJs, u = false, v = false } };
        Engine qjs_engine_u = new( ) { Options = new Options { Runtime = RuntimeEnum.QuickJs, u = true, v = false } };
        Engine sm_engine_no_u = new( ) { Options = new Options { Runtime = RuntimeEnum.SpiderMonkey, u = false, v = false } };
        Engine sm_engine_u = new( ) { Options = new Options { Runtime = RuntimeEnum.SpiderMonkey, u = true, v = false } };
        Engine bun_engine_no_u = new( ) { Options = new Options { Runtime = RuntimeEnum.Bun, u = false, v = false } };
        Engine bun_engine_u = new( ) { Options = new Options { Runtime = RuntimeEnum.Bun, u = true, v = false } };
        Engine re2js_engine = new( ) { Options = new Options { Runtime = RuntimeEnum.RE2JS, LOOKBEHINDS = true } };
        //Engine regexPlus_engineNoV = new( ) { Options = new Options { Runtime = RuntimeEnum.RegexPlus, v = false, x = true, n = false } }; //...
        Engine regexPlus_engine = new( ) { Options = new Options { Runtime = RuntimeEnum.RegexPlus, v = true, x = true, n = false } };

        return
            [
                new ("V8", njs_engine_no_uv),
                //new ("V8, “u” flag", njs_engine_u),
                new ("V8 (“v” flag)", njs_engine_v),

                //new ("Qjs", qjs_engine_no_u),
                new ("Qjs (“u” flag)", qjs_engine_u),

                //new ("SM", LazyFeatureMatrix_SpiderMonkey.GetValue( false), sm_engine_no_u),
                new ("SM (“u” flag)", sm_engine_u),

                //new ("Bun", LazyFeatureMatrix_Bun.GetValue( false), bun_engine_no_u),
                new ("Bun (“u” flag)", bun_engine_u),

                new ("RE2JS", re2js_engine),

                //new ("Regex+ (“x” flag)", regexPlus_engineNoV), //...
                new ("Regex+ (“vx” flags)", regexPlus_engine),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.i = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
    }

    public override void SetCollectCaptures( bool yes )
    {
        //Options.n = !yes;
        //if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
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
            _ => throw new NotSupportedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
