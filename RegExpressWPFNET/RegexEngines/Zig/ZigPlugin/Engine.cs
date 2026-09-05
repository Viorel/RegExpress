using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace ZigPlugin;

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

    public override string Kind => "Zig";

    public override string Version => Versions.Zig;

    public override string Name => "Zig";

    public override string Subtitle => $"Zig ({Options.Library switch
    {
        RegexLibraryEnum.ZigRegex => "zig-regex",
        RegexLibraryEnum.Mvzr => "mvzr",
        RegexLibraryEnum.Pzre => "PZRE",
        RegexLibraryEnum.EziGex => "ezi-gex",
        _ => "Unknown"
    }})";

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
        List<FeatureMatrixVariant> variants = [];

        Engine engine;

        engine = new( ) { Options = new Options { Library = RegexLibraryEnum.ZigRegex } };
        variants.Add( new FeatureMatrixVariant( "zig-regex", engine ) );

        engine = new( ) { Options = new Options { Library = RegexLibraryEnum.Mvzr } };
        variants.Add( new FeatureMatrixVariant( "mvzr", engine ) );

        engine = new( ) { Options = new Options { Library = RegexLibraryEnum.Pzre } };
        variants.Add( new FeatureMatrixVariant( "PZRE", engine ) );

        engine = new( ) { Options = new Options { Library = RegexLibraryEnum.EziGex, unicode = true, case_fold = CaseFoldEnum.full } };
        variants.Add( new FeatureMatrixVariant( "ezi-gex", engine ) );

        return variants;
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.case_insensitive = yes;
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
        return Options.Library switch
        {
            RegexLibraryEnum.ZigRegex => new SubengineZigRegex( Options ),
            RegexLibraryEnum.Mvzr => new SubengineMvzr( Options ),
            RegexLibraryEnum.Pzre => new SubenginePzre( Options ),
            RegexLibraryEnum.EziGex => new SubengineEziGex( Options ),
            _ => throw new InvalidOperationException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
