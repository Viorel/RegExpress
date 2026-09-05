using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace DotNETPlugin;

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

    public override string Kind => ".NET";

    public override string Version => ""; // (versions are displayed for each class)

    public override string Name => ".NET";

    public override string Subtitle => $"{Options.Class switch
    {
        ClassEnum.RegexDotNet => ".NET",
        ClassEnum.RegexDotNetFramework => ".NET Framework",
        ClassEnum.ReSharp => "RE#",
        ClassEnum.Scout => "Scout",
        ClassEnum.LokadUtf8Regex => "Lokad",
        _ => "unknown"
    }}";

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
                // ".NET Framework" not included
                new FeatureMatrixVariant( "Regex", new Engine{ Options = new Options { Class = ClassEnum.RegexDotNet } } ),
                new FeatureMatrixVariant( "RE#", new Engine{ Options = new Options { Class = ClassEnum.ReSharp } } ),
                new FeatureMatrixVariant( "Scout (Unicode)", new Engine{ Options = new Options { Class = ClassEnum.Scout, UnicodeClasses = true } } ),
                new FeatureMatrixVariant( "Lokad Utf8Regex", new Engine{ Options = new Options { Class = ClassEnum.LokadUtf8Regex, CultureInvariant = true } } ),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.IgnoreCase = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.IgnorePatternWhitespace = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
        //Options.ExplicitCapture = !yes;
        //if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.Class switch
        {
            ClassEnum.RegexDotNet => new SubengineDotNet( Options ),
            ClassEnum.RegexDotNetFramework => new SubengineDotNetFramework( Options ),
            ClassEnum.ReSharp => new SubengineReSharp( Options ),
            ClassEnum.Scout => new SubengineScout( Options ),
            ClassEnum.LokadUtf8Regex => new SubengineLokadUtf8Regex( Options ),
            _ => throw new InvalidOperationException( )
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
