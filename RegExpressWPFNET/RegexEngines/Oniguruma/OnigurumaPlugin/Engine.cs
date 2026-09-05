using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows.Controls;


namespace OnigurumaPlugin;

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

    public override string Kind => "Oniguruma";

    public override string Version => Versions.Oniguruma;

    public override string Name => "Oniguruma";

    public override string Subtitle => $"{Name}";

    public override string? NoteForCaptures => "requires ‘ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY’";

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

        foreach( SyntaxEnum syntax in Enum.GetValues<SyntaxEnum>( ) )
        {
            if( syntax == SyntaxEnum.None ) continue;
            if( syntax == SyntaxEnum.ONIG_SYNTAX_ASIS ) continue;

            string syntax_name = Enum.GetName( syntax )!;
            string variant = syntax_name.StartsWith( "ONIG_SYNTAX_" ) ? syntax_name["ONIG_SYNTAX_".Length..] : syntax_name;

            Options options = new( )
            {
                Syntax = syntax,
                ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY = false,// syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA,
            };
            Engine engine = new( ) { Options = options };

            variants.Add( new FeatureMatrixVariant( variant, MakeFeatureMatrix( options ), engine ) );
        }

        return variants;
    }

    public override void SetIgnoreCase( bool yes )
    {
        Options.ONIG_OPTION_IGNORECASE = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetIgnorePatternWhitespace( bool yes )
    {
        Options.ONIG_OPTION_EXTEND = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override void SetCollectCaptures( bool yes )
    {
        //Options.ONIG_OPTION_DONT_CAPTURE_GROUP = !yes;
        //Options.ONIG_OPTION_CAPTURE_GROUP = yes;

        Options.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY = yes;
        if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
    }

    public override RegexSubengine GetSubengine( )
    {
        return new Subengine( Options, this );
    }

    #endregion

    private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
    {
        InvokeOptionsChanged( args );
    }

    internal class FeatureMatrixKey
    {
        public Options Options { get; init; }

        public FeatureMatrixKey( Options options )
        {
            Options = options;
        }

        public override bool Equals( object? obj )
        {
            return obj is FeatureMatrixKey key &&
                    EqualityComparer<Options>.Default.Equals( Options, key.Options );
        }

        public override int GetHashCode( )
        {
            return HashCode.Combine( Options );
        }
    }

    static readonly Dictionary<FeatureMatrixKey, Task<FeatureMatrix>> smFeatureMatrices = [];
    FeatureMatrix mLastFeatureMatrix = default;

    internal FeatureMatrix TryGetFeatureMatrix( FeatureMatrixKey key )
    {
        lock( smFeatureMatrices )
        {
            bool is_failed = false;

            if( smFeatureMatrices.TryGetValue( key, out Task<FeatureMatrix>? task ) )
            {
                if( task.IsCompleted ) return mLastFeatureMatrix = task.Result;

                if( task.IsCanceled || task.IsFaulted )
                {
                    smFeatureMatrices.Remove( key );

                    is_failed = true;
                }
                else
                {
                    // running

                    // to minimise flickering, return the previous feature matrix
                    return mLastFeatureMatrix;
                }
            }

            Options copy_of_options = key.Options.Clone( ); // detach (?) 

            Task<FeatureMatrix> new_task = Task.Run( ( ) =>
            {
                if( is_failed ) Task.Delay( 111 );

                return MakeFeatureMatrix( copy_of_options );
            } );

            // "This API supports the product infrastructure and is not intended to be used directly from your code"
            //new_task.GetAwaiter( ).OnCompleted( ( ) => FeatureMatrixReady?.Invoke( null, null! ) );

            new_task.ContinueWith( fm => InvokeFeatureMatrixReady( ) );

            smFeatureMatrices.Add( key, new_task );

            // to minimise flickering, return the previous feature matrix
            return mLastFeatureMatrix;
        }
    }

    private static FeatureMatrix MakeFeatureMatrix( Options options )
    {
        try
        {
            Details? details = Subengine.GetDetails( NonCancellable.Instance, options );

            return Subengine.BuildFeatureMatrix( options.Syntax, details! );
        }
        catch( Exception exc )
        {
            _ = exc;

            if( Debugger.IsAttached ) Debugger.Break( );

            return default;
        }
    }
}
