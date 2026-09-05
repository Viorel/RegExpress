using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Windows.Controls;


namespace BoostPlugin;

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

    public override string Kind => "Boost";

    public override string Version => Versions.Boost;

    public override string Name => "Boost.Regex";

    public override string Subtitle => $"{Name}";

    public override string? NoteForCaptures => "requires ‘match_extra’";

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
        List<FeatureMatrixVariant> variants = [];

        foreach( GrammarEnum grammar in Enum.GetValues<GrammarEnum>( ) )
        {
            if( grammar == GrammarEnum.None ) continue;
            if( grammar == GrammarEnum.literal ) continue;
            if( grammar == GrammarEnum.normal ) continue; // not interested; seems similar to 'ECMAScript'
            if( grammar == GrammarEnum.JavaScript ) continue; // not interested; seems similar to 'ECMAScript'
            if( grammar == GrammarEnum.JScript ) continue; // not interested; seems similar to 'ECMAScript'

            Engine engine = new( ) { Options = new Options { Grammar = grammar, match_extra = false, match_posix = false, match_perl = false } }; // ('match_extra' cannot be used for some grammars)

            variants.Add( new FeatureMatrixVariant( Enum.GetName( grammar ), engine ) );
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
        Options.mod_x = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
        //Options.nosubs = !yes;
        //Options.match_nosubs = !yes;

        Options.match_extra = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override RegexSubengine GetSubengine( )
    {
        return new Subengine( Options );
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }
}
