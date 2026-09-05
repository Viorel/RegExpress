using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace RustPlugin;

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

    public override string Kind => "Rust";

    public override string Version => Versions.Rust;

    public override string Name => "Rust";

    public override string Subtitle => $"{Name} ({mOptionsControl.Value.GetSelectedCrateTitle( )})";

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

            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.UpdateUI( );
        }
    }

    public override IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
    {
        Engine engine_regex = new( ) { Options = new Options { crate = CrateEnum.regex, UseBuilder = true, unicode = true, octal = true } };
        Engine engine_lite = new( ) { Options = new Options { crate = CrateEnum.regex_lite, UseBuilder = true } };
        Engine engine_fancy = new( ) { Options = new Options { crate = CrateEnum.fancy_regex, UseBuilder = true, unicode = true } };
        Engine engine_regress_uv = new( ) { Options = new Options { crate = CrateEnum.regress, unicode = true, unicode_sets = true } }; // currently 'ignore_whitespace' not supported
        Engine engine_resharp = new( ) { Options = new Options { crate = CrateEnum.resharp, UnicodeMode = UnicodeModeEnum.Full } };
        Engine engine_anre = new( ) { Options = new Options { crate = CrateEnum.anre } };
        //Engine engine_real = new( ) { Options = new Options { crate = CrateEnum.real_regex, UseRegexBuilder = true, unicode = false } };
        Engine engine_real_u = new( ) { Options = new Options { crate = CrateEnum.real_regex, UseBuilder = true, unicode = true } };
        Engine engine_java_regex_uU = new( ) { Options = new Options { crate = CrateEnum.java_regex, unicode = true, unicode_sets = true, d = false, l = false } };
        Engine engine_regexr = new( ) { Options = new Options { crate = CrateEnum.regexr, UseBuilder = true } };
        Engine engine_rexile = new( ) { Options = new Options { crate = CrateEnum.rexile } };

        return
            [
                new FeatureMatrixVariant("regex (“u” flag)", engine_regex),
                new FeatureMatrixVariant("regex-lite", engine_lite),
                new FeatureMatrixVariant("fancy-regex (“u” flag)", engine_fancy),
                new FeatureMatrixVariant("regress (“uv” flags)", engine_regress_uv),
                new FeatureMatrixVariant("resharp (“Full” mode)", engine_resharp),
                new FeatureMatrixVariant("anre", engine_anre),
                //new FeatureMatrixVariant("real-regex", engine_real),
                new FeatureMatrixVariant("real-regex (“u” flag)", engine_real_u),
                new FeatureMatrixVariant("java_regex (“uU” flags)", engine_java_regex_uU),
                new FeatureMatrixVariant("regexr", engine_regexr),
                new FeatureMatrixVariant("rexile", engine_rexile),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.case_insensitive = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.ignore_whitespace = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.crate switch
        {
            CrateEnum.regex => new SubengineRegex( Options ),
            CrateEnum.regex_lite => new SubengineRegexLite( Options ),
            CrateEnum.fancy_regex => new SubengineFancyRegex( Options ),
            CrateEnum.regress => new SubengineRegress( Options ),
            CrateEnum.resharp => new SubengineResharp( Options ),
            CrateEnum.anre => new SubengineAnre( Options ),
            CrateEnum.real_regex => new SubengineRealRegex( Options ),
            CrateEnum.java_regex => new SubengineJavaRegex( Options ),
            CrateEnum.regexr => new SubengineRegexr( Options ),
            CrateEnum.rexile => new SubengineReXile( Options ),
            _ => throw new InvalidOperationException( )
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
