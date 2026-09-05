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

    public override string Subtitle => $"Fortran ({mOptionsControl.Value.GetSelectedModuleTitle( )})";

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
        Engine engine_forgex = new( ) { Options = new Options { Module = ModuleEnum.Forgex } };
        Engine engine_perazz = new( ) { Options = new Options { Module = ModuleEnum.RegexPerazz } };
        Engine engine_jayemhex = new( ) { Options = new Options { Module = ModuleEnum.RegexJeyemhex } };

        return
            [
                new FeatureMatrixVariant("Forgex", engine_forgex),
                new FeatureMatrixVariant("Regex (Perazz)", engine_perazz),
                new FeatureMatrixVariant("Regex (Jeyemhex)", engine_jayemhex),
            ];
    }

    public override void SetIgnoreCase( bool yes )
    {
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
    }

    public override void SetCollectCaptures( bool yes )
    {
    }

    public override RegexSubengine GetSubengine( )
    {
        return Options.Module switch
        {
            ModuleEnum.Forgex => new SubengineForgex( Options ),
            ModuleEnum.RegexPerazz => new SubengineRegexPerazz( Options ),
            ModuleEnum.RegexJeyemhex => new SubengineRegexJeyemhex( Options ),
            _ => throw new NotImplementedException( ),
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
