using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace FortranPlugin;

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

    public override string Kind => "Fortran";

    public override string Version => Versions.IFX;

    public override string Name => "Fortran (IFX)";

    public override string Subtitle => $"Fortran ({Options.Module switch
    {
        ModuleEnum.Forgex => "Forgex",
        ModuleEnum.RegexPerazz => "Perazz",
        ModuleEnum.RegexJeyemhex => "Jeyemhex",
        ModuleEnum.Fregex => "Fregex",
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
        return
            [
                new FeatureMatrixVariant("Forgex", new Engine( ) { Options = new Options { Module = ModuleEnum.Forgex, MatchAll = true } } ),
                new FeatureMatrixVariant("Regex (Perazz)", new Engine( ) { Options = new Options { Module = ModuleEnum.RegexPerazz, MatchAll = true } }),
                new FeatureMatrixVariant("Regex (Jeyemhex)", new Engine( ) { Options = new Options { Module = ModuleEnum.RegexJeyemhex, MatchAll = true } }),
                new FeatureMatrixVariant("Fregex", new Engine( ) { Options = new Options { Module = ModuleEnum.Fregex } }),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.ignorecase = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( Options );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override void SetStickyMode( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.Module switch
        {
            ModuleEnum.Forgex => new SubengineForgex( Options ),
            ModuleEnum.RegexPerazz => new SubengineRegexPerazz( Options ),
            ModuleEnum.RegexJeyemhex => new SubengineRegexJeyemhex( Options ),
            ModuleEnum.Fregex => new SubengineFregex( Options ),
            _ => throw new NotImplementedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
