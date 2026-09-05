using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace VBScriptPlugin;

class Engine : RegexEngine
{
    static readonly Lazy<string?> LazyVersion = new( GetVersion );

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

    public override string Kind => "VBScript";

    public override string Version => LazyVersion.Value ?? "(unknown)";

    public override string Name => "VBScript";

    public override string Subtitle => $"VBScript";

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
                new FeatureMatrixVariant( null, new Engine() )
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.IgnoreCase = yes;
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
        return new Subengine( Options );
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }


    static string? GetVersion( )
    {
        try
        {
            return Subengine.GetVersion( NonCancellable.Instance );
        }
        catch( Exception exc )
        {
            _ = exc;
            if( Debugger.IsAttached ) Debugger.Break( );

            return null;
        }
    }
}
