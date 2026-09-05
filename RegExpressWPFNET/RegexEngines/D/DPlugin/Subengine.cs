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
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;


namespace DPlugin;

class Subengine( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.NoGroupSuccessFlag;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        return new SyntaxOptions
        {
            XLevel = options.x ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = LazyFeatureMatrix.Value
        };
    }

    class VersionResponse
    {
        public string? version { get; set; }
    }


    public class MatchesResponse
    {
        public string[]? names { get; set; }
        public OneMatchResponse[]? matches { get; set; }
    }


    public class OneMatchResponse
    {
        [JsonPropertyName( "i" )]
        public int index { get; set; } // byte-index of the whole match

        [JsonPropertyName( "g" )]
        public int[][]? groups { get; set; } // [byte-index, byte-length], or [-1, 0] if failed

        [JsonPropertyName( "n" )]
        public int[][]? named_groups { get; set; } // [byte-index, byte-length], or [-1, 0] if failed
    }


    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        StringBuilder flags = new( );

        //if( Options.g ) flags.Append( 'g' );
        if( options.i ) flags.Append( 'i' );
        if( options.m ) flags.Append( 'm' );
        if( options.s ) flags.Append( 's' );
        if( options.x ) flags.Append( 'x' );

        var obj = new
        {
            p = pattern,
            t = text,
            f = flags.ToString( ),
        };

        string json = JsonSerializer.Serialize( obj );

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.Write( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

        if( response == null ) throw new Exception( "Null response" );

        List<IMatch> matches = [];
        Utf8IndexConverter index_converter = new( text );
        SimpleTextGetter stg = new( text );

        foreach( var m in response.matches! )
        {
            SimpleMatch? match = null;

            for( int group_index = 0; group_index < m.groups!.Length; group_index++ )
            {
                int[] g = m.groups[group_index];
                bool success = g.Length == 2;

                if( group_index == 0 && !success )
                {
                    // if pattern is "()", which matches any position, 'std.regex' does not return captures, 
                    // even the main one (all are null); however the match object contains the valid index;
                    // this is a workaround:

                    success = true;
                    g = [m.index, 0];
                }

                int native_start = success ? g[0] : 0; // utf-8
                int native_length = success ? g[1] : 0;
                int native_end = native_start + native_length;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                if( group_index == 0 )
                {
                    Debug.Assert( match == null );
                    Debug.Assert( success );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                }

                Debug.Assert( match != null );

                // try to identify the named group by index and length;
                // cannot be done univocally in situations like "(?P<name1>(?P<name2>(.))", because index and length are the same

                string? name;

                var np = m.named_groups!
                    .Where( _ => group_index != 0 )
                    .Select( ( ng, j ) => new { ng, j } )
                    .Where( p => p.ng[0] >= 0 )
                    .FirstOrDefault( z => z.ng[0] == native_start && z.ng[1] == native_length && !match.Groups.Any( q => q.Name == response.names![z.j] ) );

                if( np == null )
                {
                    name = null;
                }
                else
                {
                    name = response.names![np.j];
                }

                if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

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

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"DWorker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = true,
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = false,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = true,
            XXFlag = false,

            Literal_QE = false,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = false,

            Esc_a = false,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = true,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = false,
            Esc_u4 = true,
            Esc_U8 = true,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = true,

            InsideSets_Esc_a = false,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = false,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = true,
            InsideSets_Esc_uBrace = false,
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
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = false,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = true,
            Class_X = false,
            Class_pP = true,
            Class_pPBrace = true,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = false,
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
            InsideSets_Operator_DoubleVerticalLine = true,
            InsideSets_Operator_DoubleMinus = true,
            InsideSets_Operator_DoubleTilde = true,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = false,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = false,
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

            NamedGroup_Apos = false,
            NamedGroup_LtGt = false,
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

            Backref_Num = FeatureMatrix.BackrefEnum.Any, // TODO: it seems that a reference like '\12' works even when there is a single group
            Backref_kApos = false,
            Backref_kLtGt = false,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = false,
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
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.XModeOnly,
            Quantifier_LowAbbrev = false,
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
}
