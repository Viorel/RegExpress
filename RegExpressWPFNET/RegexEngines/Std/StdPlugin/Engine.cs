using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace StdPlugin;

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

    public override string Kind => "Std";

    public override string Version => ""; // (versions are displayed for each compiler)

    public override string Name => "wregex";

    public override string Subtitle => $"{Options.Compiler switch { CompilerEnum.MSVC => "std::wregex", CompilerEnum.GCC => "std::wregex (GCC)", CompilerEnum.SRELL => "srell::wregex", CompilerEnum.SRELL_LINEAR => "srel3::wregex", _ => " (Unknown)" }}";

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

        foreach( GrammarEnum grammar in Enum.GetValues<GrammarEnum>( ) )
        {
            if( grammar == GrammarEnum.None ) continue;

            Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.MSVC, Grammar = grammar } };

            variants.Add( new FeatureMatrixVariant( Enum.GetName( grammar ), engine ) );
        }

        {
            GrammarEnum grammar = GrammarEnum.ECMAScript;

            Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.GCC, Grammar = grammar } };

            variants.Add( new FeatureMatrixVariant( $"GCC ({Enum.GetName( grammar )})", engine ) );
        }

        {
#if true
            GrammarEnum grammar = GrammarEnum.ECMAScript;

            {
                Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.SRELL, Grammar = grammar, unicodesets = false, vmode = false } };

                variants.Add( new FeatureMatrixVariant( $"SRELL", engine ) );
            }
            {
                Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.SRELL, Grammar = grammar, unicodesets = true, vmode = true } };

                variants.Add( new FeatureMatrixVariant( $"SRELL (“uv” flags)", engine ) );
            }

            {
                Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.SRELL_LINEAR, Grammar = grammar, unicodesets = true, vmode = true } };

                variants.Add( new FeatureMatrixVariant( $"SRELL linear (“uv” flags)", engine ) );
            }

#else
            // for investigations

            foreach( GrammarEnum grammar in Enum.GetValues<GrammarEnum>( ) )
            {
                if( grammar == GrammarEnum.None ) continue;

                {
                    Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.SRELL, Grammar = grammar, unicodesets = false, vmode = false } };

                    variants.Add( new FeatureMatrixVariant( $"SRELL ({Enum.GetName( grammar )})", engine ) );
                }
                {
                    Engine engine = new( ) { Options = new Options { Compiler = CompilerEnum.SRELL, Grammar = grammar, unicodesets = true, vmode = true } };

                    variants.Add( new FeatureMatrixVariant( $"SRELL ({Enum.GetName( grammar )}, “uv” flags)", engine ) );
                }
            }
#endif
        }

        return variants;
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.icase = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
    }

    public override void SetCollectCaptures( bool yes )
    {
        //Options.nosubs = !yes;
        //if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }


    public override RegexSubengine GetSubengine( )
    {
        return Options.Compiler switch
        {
            CompilerEnum.MSVC => new SubengineMSVC( Options ),
            CompilerEnum.GCC => new SubengineGCC( Options ),
            CompilerEnum.SRELL => new SubengineSRELL( Options ),
            CompilerEnum.SRELL_LINEAR => new SubengineSRELL_LINEAR( Options ),
            _ => throw new InvalidOperationException( )
        };
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
