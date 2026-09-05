using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace GoPlugin;

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

    public override string Kind => "Go";

    public override string Version => Versions.Go;

    public override string Name => "Go";

    public override string Subtitle => $"{Name} ({Enum.GetName<PackageEnum>( Options.Package )})";

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
                new FeatureMatrixVariant( "regexp", new Engine { Options = new Options { Package = PackageEnum.regexp, posix = false }} ),
                new FeatureMatrixVariant( "regexp (posix)", new Engine { Options = new Options { Package = PackageEnum.regexp, posix = true}} ),
                new FeatureMatrixVariant( "regexp2", new Engine { Options = new Options { Package = PackageEnum.regexp2, ECMAScript= false, RE2 = false }} ),
                new FeatureMatrixVariant( "rexa", new Engine { Options = new Options { Package = PackageEnum.rexa }} ),
                new FeatureMatrixVariant( "coregex", new Engine { Options = new Options { Package = PackageEnum.coregex, posix = false }} ),
                //new FeatureMatrixVariant( "coregex (posix)", new Engine { Options = new Options { Package = PackageEnum.coregex, posix = true }} ),
                new FeatureMatrixVariant( "onigmo", new Engine { Options = new Options { Package = PackageEnum.onigmo }} ),
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
        return Options.Package switch
        {
            PackageEnum.regexp => new SubengineRegexp( Options ),
            PackageEnum.regexp2 => new SubengineRegexp2( Options ),
            PackageEnum.rexa => new SubengineRexa( Options ),
            PackageEnum.coregex => new SubengineCoregex( Options ),
            PackageEnum.onigmo => new SubengineOnigmoGoRegexp( Options ),
            _ => throw new NotImplementedException( )
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
