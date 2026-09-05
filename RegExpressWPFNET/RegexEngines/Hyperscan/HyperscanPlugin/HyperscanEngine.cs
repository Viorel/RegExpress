using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace HyperscanPlugin;

class HyperscanEngine : RegexEngine
{
    HyperscanOptions mOptions = new( );
    readonly Lazy<UCHyperscanOptions> mOptionsControl;

    public HyperscanEngine( )
    {
        mOptionsControl = new Lazy<UCHyperscanOptions>( ( ) =>
        {
            UCHyperscanOptions oc = new( );
            oc.SetOptions( Options );
            oc.Changed += OptionsControl_Changed;

            return oc;
        } );
    }

    public HyperscanOptions Options
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

    public override string Kind => "Hyperscan";

    public override string Version => Versions.Hyperscan;

    public override string Name => "Hyperscan";

    public override string Subtitle => $"{Name}";

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
            Options = new HyperscanOptions( );
        }
        else
        {
            try
            {
                Options = JsonSerializer.Deserialize<HyperscanOptions>( json, JsonUtilities.JsonOptions )!;
            }
            catch
            {
                // ignore versioning errors, for example
                if( Debugger.IsAttached ) Debugger.Break( );

                Options = new HyperscanOptions( );
            }
        }
    }

    public override IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
    {
        HyperscanEngine engine = new( ) { Options = new HyperscanOptions { HS_FLAG_UTF8 = true, HS_FLAG_UCP = true, HS_FLAG_SOM_LEFTMOST = false } };
        // ('HS_FLAG_UTF8=true' allows non-latin letters, 'HS_FLAG_SOM_LEFTMOST=false' reduce the "Pattern is too large" errors)

        return
            [
                new FeatureMatrixVariant( null, engine )
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.HS_FLAG_CASELESS = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        return new HyperscanSubengine( Options );
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
