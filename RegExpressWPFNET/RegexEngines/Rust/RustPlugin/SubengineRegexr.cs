using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;


namespace RustPlugin;

partial class SubengineRegexr( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix( ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.Value;

        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public class Rootobject
    {
        public required string[] names { get; set; }
        public required Match[] matches { get; set; }
    }

    public class Match
    {
        public required int[][] g { get; set; }
        public required Ng[] ng { get; set; }
    }

    public class Ng
    {
        public required string n { get; set; }
        public required int[] g { get; set; }
    }


    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.crate == CrateEnum.regexr );

        bool use_builder = options.UseBuilder;

        var obj = new
        {
            use_builder = use_builder,
            pattern = pattern,
            text = text,
            options = new
            {
                options.jit,
                options.optimize_prefixes,
                size_limit = use_builder ? ValidationUtilities.ParseUInt32( "size_limit", options.size_limit ) : null,
                nest_limit = use_builder ? ValidationUtilities.ParseUInt32( "nest_limit", options.nest_limit ) : null,
                backtrack_limit = use_builder ? ValidationUtilities.ParseUInt32( "backtrack_limit", options.backtrack_limit ) : null,
            }
        };

        string json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );

        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.Write( json );
        };

#if DEBUG
        ph.Environment.Add( "RUST_BACKTRACE", "1" );
#endif

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

#if DEBUG
        using StreamReader sr = new( ph.OutputStream );
        string output = sr.ReadToEnd( );
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( output );
#else
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

        if( root_object == null || root_object.matches == null || root_object.names == null ) throw new Exception( "Null response" );

        List<IMatch> matches = [];
        SimpleTextGetter? stg = new( text );
        Utf8IndexConverter index_converter = new( text );

        foreach( var m in root_object.matches )
        {
            SimpleMatch? match = null;

            for( int group_index = 0; group_index < m.g.Length; group_index++ )
            {
                int[] g = m.g[group_index];
                bool success = g.Length == 2;

                int native_start = success ? g[0] : 0;
                int native_end = success ? g[1] : 0;
                int native_length = native_end - native_start;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                if( group_index == 0 )
                {
                    Debug.Assert( match == null );
                    Debug.Assert( success );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                }

                Debug.Assert( match != null );

                string name = group_index.ToString( CultureInfo.InvariantCulture );

                if( !success )
                {
                    match.AddFailedGroup( name );
                }
                else
                {
                    match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                }
            }

            Debug.Assert( match != null );

            foreach( var ng in m.ng )
            {
                bool success = ng.g.Length == 2;

                if( !success )
                {
                    match.AddFailedGroup( ng.n );
                }
                else
                {
                    int native_start = ng.g[0];
                    int native_end = ng.g[1];
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    match.AddSucceededGroup( native_start, native_length, char_start, char_length, ng.n );
                }
            }

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    private static string? AdjustErrorMessage( string error, string pattern )
    {
        // try to show character offset based on byte offset, which appears in error messages

        System.Text.RegularExpressions.Match m = RegexExtractByteOffset( ).Match( error );

        if( m.Success && int.TryParse( m.Groups[1].Value, out int byte_offset ) )
        {
            try
            {
                byte[] utf8_bytes = Encoding.UTF8.GetBytes( pattern );
                int char_offset = Encoding.UTF8.GetCharCount( utf8_bytes, 0, byte_offset );

                if( char_offset != byte_offset )
                {
                    string new_message = $"{error.TrimEnd( )}{Environment.NewLine}at character index {char_offset}";

                    return new_message;
                }
            }
            catch
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                // ignore
            }
        }

        return error;
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"RustRegexrWorker.bin" );

        return worker_exe;
    }


    private static FeatureMatrix BuildFeatureMatrix( )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = true,
            XModeComments = true,
            InsideSets_XModeComments = false,

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
            Esc_v = true,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = true,
            Esc_u4 = true,
            Esc_U8 = false,
            Esc_uBrace = true,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = false,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = true,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = true,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = false,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = false,
            Class_hHhorspace = true,
            Class_lL = false,
            Class_N = true,
            Class_O = false,
            Class_R = true,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = true,
            Class_X = true,
            Class_pP = true,
            Class_pPBrace = true,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = true,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = true,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = true,
            InsideSets_Class_pPBrace = true,
            InsideSets_Class_Name = true,
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
            InsideSets_Operator_DoubleMinus = true,
            InsideSets_Operator_DoubleTilde = true,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Correct,
            Anchor_z = true,
            Anchor_G = false,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = true,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = true,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = true,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NestedLookaround = true,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = true,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
            Backref_kApos = true,
            Backref_kLtGt = true,
            Backref_kBrace = true,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = true,
            AllowSpacesInBackref = false,

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
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.Both,
            Quantifier_LowAbbrev = true,
            Quantifier_Lazy = true,
            Quantifier_Possessive = false,

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

            EmptyConstruct = false,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = true,
            ßSS = false,
        };
    }

    [GeneratedRegex( @" at position (\d+)" )]
    private static partial Regex RegexExtractByteOffset( );
}
