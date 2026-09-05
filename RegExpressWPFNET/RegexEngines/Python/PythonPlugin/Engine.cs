using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace PythonPlugin;

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

    public override string Kind => "Python";

    public override string Version => Versions.Python;

    public override string Name => "Python";

    public override string Subtitle => $"{Name} ({mOptionsControl.Value.GetSelectedModuleTitle( )})";

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
        Engine engine_re = new( ) { Options = new Options { Module = ModuleEnum.re, VERSION0 = false, VERSION1 = false } };
        //Engine engine_regex_v0 = new( ) { Options = new Options { Module = ModuleEnum.regex, POSIX = false, VERSION0 = true, VERSION1 = false } };
        Engine engine_regex_v1 = new( ) { Options = new Options { Module = ModuleEnum.regex, POSIX = false, VERSION0 = false, VERSION1 = true } };
        Engine engine_regex_v1_posix = new( ) { Options = new Options { Module = ModuleEnum.regex, POSIX = true, VERSION0 = false, VERSION1 = true } };
        //Engine engine_real_regex_ascii = new( ) { Options = new Options { Module = ModuleEnum.real_regex, VERSION0 = false, VERSION1 = false, ASCII = true } };
        Engine engine_real_regex = new( ) { Options = new Options { Module = ModuleEnum.real_regex, VERSION0 = false, VERSION1 = false, ASCII = false } };

        return
            [
                new FeatureMatrixVariant("re", engine_re),
                //new FeatureMatrixVariant("regex V0", engine_regex_v0),
                new FeatureMatrixVariant("regex V1", engine_regex_v1),
                new FeatureMatrixVariant("regex V1 (posix)", engine_regex_v1_posix),
                //new FeatureMatrixVariant("real-regex (ASCII)", engine_real_regex_ascii),
                new FeatureMatrixVariant("real-regex", engine_real_regex),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.IGNORECASE = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.VERBOSE = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.Module switch
        {
            ModuleEnum.re => new SubengineRe( Options ),
            ModuleEnum.regex => new SubengineRegex( Options ),
            ModuleEnum.real_regex => new SubengineRealRegex( Options ),
            _ => throw new NotImplementedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
