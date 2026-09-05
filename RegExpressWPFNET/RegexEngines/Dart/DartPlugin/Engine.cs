using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace DartPlugin;

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

    public override string Kind => "Dart";

    public override string Version => Versions.Dart;

    public override string Name => "Dart";

    public override string Subtitle => $"{Name} ({Options.package switch
    {
        PackageEnum.RegExp => "RegExp",
        PackageEnum.OnigurumaDart => "OnigRegex",
        _ => "unknown"
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
#if true
        List<FeatureMatrixVariant> matrices =
            [
                new FeatureMatrixVariant( "RegExp", new Engine { Options = new Options { package = PackageEnum.RegExp, unicode = false }} ),
                new FeatureMatrixVariant( "RegExp (unicode)", new Engine { Options = new Options { package = PackageEnum.RegExp, unicode = true }} ),
                new FeatureMatrixVariant( "OnigRegex", new Engine { Options = new Options { package = PackageEnum.OnigurumaDart, OnigurumaSyntax = OnigurumaSyntaxEnum.onigSyntaxOniguruma }} ),
            ];
        return matrices;
#else
        // for investigations

        List<FeatureMatrixVariant> matrices = [];

        foreach( OnigurumaSyntaxEnum syntax in Enum.GetValues<OnigurumaSyntaxEnum>( ).Where( s => s != OnigurumaSyntaxEnum.None ) )
        {
            matrices.Add( new FeatureMatrixVariant( $"({Enum.GetName( syntax )!["onigSyntax".Length..]})", new Engine { Options = new Options { package = PackageEnum.OnigurumaDart, OnigurumaSyntax = syntax } } ) );
        }

        return matrices;
#endif
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.caseInsensitive = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.extend = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        Options options = Options;

        return options.package switch
        {
            PackageEnum.RegExp => new SubengineRegExp( options ),
            PackageEnum.OnigurumaDart => new SubengineOnigurumaDart( options ),
            _ => throw new NotImplementedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
