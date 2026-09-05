using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading;


namespace JavaPlugin;

partial class SubengineRegex( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool UNICODE_CASE, bool UNICODE_CHARACTER_CLASS), FeatureMatrix> LazyFeatureMatrix =
        new( d => BuildFeatureMatrix( d.UNICODE_CASE, d.UNICODE_CHARACTER_CLASS ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.UNICODE_CASE, options.UNICODE_CHARACTER_CLASS) );

        return new SyntaxOptions
        {
            Literal = options.LITERAL,
            XLevel = options.COMMENTS ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public class Rootobject
    {
        public required Match[] matches { get; set; }
    }

    public class Match
    {
        public int s { get; set; }
        public int e { get; set; }
        public required int[][] g { get; set; }
        public required Ng[] ng { get; set; }
    }

    public class Ng
    {
        public int s { get; set; }
        public int e { get; set; }
        public required string n { get; set; }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Package == PackageEnum.regex );

        Int32? region_start = ValidationUtilities.ParseInt32( "start", options.regionStart );
        Int32? region_end = ValidationUtilities.ParseInt32( "end", options.regionEnd );

        if( ( region_start == null ) != ( region_end == null ) )
        {
            throw new ApplicationException( "Both “start” and “end” must be entered or blank." );
        }

        (string? javaExePath, string? workerDir) = GetPaths( );

        if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

        if( string.IsNullOrWhiteSpace( javaExePath ) ) throw new Exception( "Cannot initialize JRE" );
        if( string.IsNullOrWhiteSpace( workerDir ) ) throw new Exception( "Cannot initialize Java worker" );

        using ProcessHelper ph = new( javaExePath );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.Arguments = ["-cp", $"{workerDir};{Path.Combine( workerDir, "json-simple-1.1.1.jar" )}", "JavaWorker"];

        var obj = new
        {
            command = "get-matches",
            pattern = pattern,
            text = text,
            options = new
            {
                options.CANON_EQ,
                options.CASE_INSENSITIVE,
                options.COMMENTS,
                options.DOTALL,
                options.LITERAL,
                options.MULTILINE,
                options.UNICODE_CASE,
                options.UNICODE_CHARACTER_CLASS,
                options.UNIX_LINES,
                options.useAnchoringBounds,
                options.useTransparentBounds,
                region_start,
                region_end,
            },
        };

        ph.StreamWriter = sw =>
        {
            var json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );
            sw.WriteLine( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

#if DEBUG
        using StreamReader sr = new( ph.OutputStream );
        string output = sr.ReadToEnd( );
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( output );
#else
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

        if( root_object == null ) throw new Exception( "Invalid response." );

        List<SimpleMatch> matches = [];
        SimpleTextGetter stg = new( text );

        foreach( Match m in root_object.matches )
        {
            SimpleMatch match;

            {
                int native_start = m.s;
                int native_end = m.e;
                int native_length = native_end - native_start;

                Debug.Assert( native_start >= 0 && native_end >= native_start );

                match = SimpleMatch.Create( native_start, native_length, stg );

                match.AddDefaultGroup( );
            }

            for( int i = 1; i < m.g.Length; ++i ) // skip default group
            {
                int[] g = m.g[i];

                int native_start = g[0];
                int native_end = g[1];

                bool success = native_start >= 0 && native_end >= 0;

                string name = i.ToString( CultureInfo.InvariantCulture );

                if( !success )
                {
                    match.AddFailedGroup( name );
                }
                else
                {
                    int native_length = native_end - native_start;

                    match.AddSucceededGroup( native_start, native_length, name );
                }
            }

            foreach( var ng in m.ng )
            {
                int native_start = ng.s;
                int native_end = ng.e;
                string name = ng.n;

                bool success = native_start >= 0 && native_end >= 0;

                if( !success )
                {
                    try
                    {
                        SimpleGroup? sg = (SimpleGroup?)match.Groups.Skip( 1 ).SingleOrDefault( g => !g.Success );

                        if( sg == null )
                        {
                            Debug.Fail( "Orphan named group" );

                            match.AddFailedGroup( name );
                        }
                        else
                        {
                            sg.SetName( name );
                        }
                    }
                    catch( InvalidOperationException )
                    {
                        // more than one

                        match.AddFailedGroup( name );
                    }
                }
                else
                {
                    int native_length = native_end - native_start;

                    try
                    {
                        SimpleGroup? sg = (SimpleGroup?)match.Groups.Skip( 1 ).SingleOrDefault( g => g.Success && g.NativeIndex == native_start && g.NativeLength == native_length );

                        if( sg == null )
                        {
                            Debug.Fail( "Orphan named group" );

                            match.AddSucceededGroup( native_start, native_length, name );
                        }
                        else
                        {
                            sg.SetName( name );
                        }
                    }
                    catch( InvalidOperationException )
                    {
                        // more than one

                        match.AddSucceededGroup( native_start, native_length, name );
                    }
                }
            }

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    internal static (string? javaPath, string? workerDir) GetPaths( )
    {
        DecompressJre( );

        if( JrePath == null )
        {
            return (null, null);
        }
        else
        {
            // TODO: do once

            return (
                Path.Combine( JrePath, @"JRE-min\bin\java.exe" ),
                Path.Combine( GetPluginDirectory( ) )
                );
        }
    }


    static string GetPluginDirectory( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;

        return assembly_dir;
    }


    static string GetTemporaryDirectory( )
    {
        string temp_path = Path.GetTempPath( );
        string dir = Path.Combine( temp_path, Path.GetRandomFileName( ) ); // TODO: exclude almost impossible collisions

        return dir;
    }


    static readonly Lock Locker = new( );
    static string? JrePath = null;
    static bool IsJreExtractionDone = false;

    static void DecompressJre( )
    {
        if( IsJreExtractionDone ) return;

        lock( Locker )
        {
            if( IsJreExtractionDone ) return;

            try
            {
                string plugin_dir = GetPluginDirectory( );
                string dest_jre_path = GetTemporaryDirectory( );
                string source_zip = Path.Combine( plugin_dir, @"JRE-min.zip" );

                ZipFile.ExtractToDirectory( source_zip, dest_jre_path );

                AppDomain.CurrentDomain.ProcessExit += ( s, a ) =>
                {
                    try
                    {
                        Directory.Delete( dest_jre_path, recursive: true );
                    }
                    catch( Exception exc )
                    {
                        _ = exc;
                        if( Debugger.IsAttached ) Debugger.Break( );

                        // ignore
                    }
                };

                JrePath = dest_jre_path;
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                JrePath = null;
            }

            IsJreExtractionDone = true;
        }
    }

    static FeatureMatrix BuildFeatureMatrix( bool isUnicodeCase, bool isUnicodeCharacterClass )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = false,
            XModeComments = true,
            InsideSets_XModeComments = true,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = true,
            XXFlag = false,

            Literal_QE = true,
            InsideSets_Literal_QE = true,
            InsideSets_Literal_qBrace = false,

            Esc_a = true,
            Esc_b = false,
            Esc_e = true,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = false,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = true,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = true,
            Esc_u4 = true,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = true,
            GenericEscape = false,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = true,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = false,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = true,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = true,
            InsideSets_GenericEscape = false,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = false,
            Class_hHhorspace = true,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = true,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = true,
            Class_wW = true,
            Class_X = true,
            Class_pP = true, // TODO: not documented? // TODO: in some engines it is case-sensitive or case-insensitive
            Class_pPBrace = true,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = true,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = true,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = true,
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = true,
            InsideSets_Class_pPBrace = true,
            InsideSets_Class_Name = false,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = true,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = true,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = false,
            InsideSets_Operator_DoubleTilde = false,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Correct,
            Anchor_z = true,
            Anchor_G = true,
            Anchor_bB = true,
            Anchor_bg = true,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = false,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = false,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NestedLookaround = true,
            AtomicGroup = true,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = true,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
            Backref_kApos = false,
            Backref_kLtGt = true,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = false,
            AllowSpacesInBackref = true,

            Recursive_Num = false,
            Recursive_PlusMinusNum = false,
            Recursive_R = false,
            Recursive_Name = false,
            Recursive_PGtName = false,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = true,
            Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = false,
            Quantifier_Lazy = true,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = false,
            Conditional_BackrefByName = false,
            Conditional_Pattern = false,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = false,
            Conditional_BackrefByName_LtGt = false,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = false,
            Conditional_VERSION = false,

            ControlVerbs = false,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = true,
            EmptyConstructX = true,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = isUnicodeCharacterClass,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = isUnicodeCase,
            ßSS = false,
        };
    }
}
