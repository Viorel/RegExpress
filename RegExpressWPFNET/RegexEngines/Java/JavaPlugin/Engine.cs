using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace JavaPlugin;

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

    public override string Kind => "Java";

    public override string Version => Versions.Java;

    public override string Name => "Java";

    public override string Subtitle
    {
        get
        {
            string package = mOptionsControl.Value.GetSelectedPackageTitle( );

            return package == "regex" ? "Java" : $"Java ({package})";
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
        Engine engine_regex = new( ) { Options = new Options { Package = PackageEnum.regex, UNICODE_CASE = true, UNICODE_CHARACTER_CLASS = true } };
        Engine engine_re2j = new( ) { Options = new Options { Package = PackageEnum.re2j } };
        Engine engine_safere = new( ) { Options = new Options { Package = PackageEnum.safere, UNICODE_CASE = true, UNICODE_CHARACTER_CLASS = true } };
        Engine engine_reggie = new( ) { Options = new Options { Package = PackageEnum.reggie } };
        Engine engine_joni = new( ) { Options = new Options { Package = PackageEnum.joni, CAPTURE_GROUP = true } };

        return
            [
                new FeatureMatrixVariant("regex", engine_regex),
                new FeatureMatrixVariant("RE2/J", engine_re2j),
                new FeatureMatrixVariant("SafeRE", engine_safere),
                new FeatureMatrixVariant("Reggie", engine_reggie),
                new FeatureMatrixVariant("Joni", engine_joni),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.CASE_INSENSITIVE = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.COMMENTS = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.Package switch
        {
            PackageEnum.regex => new SubengineRegex( Options ),
            PackageEnum.re2j => new SubengineRE2J( Options ),
            PackageEnum.safere => new SubengineSafeRE( Options ),
            PackageEnum.reggie => new SubengineReggie( Options ),
            PackageEnum.joni => new SubengineJoni( Options ),
            _ => throw new NotImplementedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }

}
