using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace HyperscanPlugin;

class ChimeraEngine : RegexEngine
{
    ChimeraOptions mOptions = new( );
    readonly Lazy<UCChimeraOptions> mOptionsControl;

    public ChimeraEngine( )
    {
        mOptionsControl = new Lazy<UCChimeraOptions>( ( ) =>
        {
            UCChimeraOptions oc = new( );
            oc.SetOptions( Options );
            oc.Changed += OptionsControl_Changed;

            return oc;
        } );
    }

    public ChimeraOptions Options
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

    public override string Kind => "Chimera";

    public override string Version => Versions.Chimera;

    public override string Name => "Chimera";

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
            Options = new ChimeraOptions( );
        }
        else
        {
            try
            {
                Options = JsonSerializer.Deserialize<ChimeraOptions>( json, JsonUtilities.JsonOptions )!;
            }
            catch
            {
                // ignore versioning errors, for example
                if( Debugger.IsAttached ) Debugger.Break( );

                Options = new ChimeraOptions( );
            }
        }
    }

    public override IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
    {
        return
            [
                new FeatureMatrixVariant( null, new ChimeraEngine{ Options = new ChimeraOptions { CH_FLAG_UCP = true } } )
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.CH_FLAG_CASELESS = yes;
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
        return new ChimeraSubengine( Options );
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
