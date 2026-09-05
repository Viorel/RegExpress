using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;


namespace JavaScriptPlugin;

partial class SubengineRE2JS( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool uFlag, bool enableLookbehind), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix( d.uFlag, d.enableLookbehind ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.u, options.LOOKBEHINDS) );

        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }


    public class Response
    {
        public Match[]? Matches { get; set; }
        public string? Error { get; set; }
    }

    public class Match
    {
        public int[][]? ag { get; set; }
        public Ng[]? ng { get; set; }
    }

    public class Ng
    {
        public string? n { get; set; }
        public int? s { get; set; }
        public int? e { get; set; }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Runtime == RuntimeEnum.RE2JS );

        string flags = string.Concat(
            options.i ? "i" : "",
            options.m ? "m" : "",
            options.s ? "s" : "",
            options.DISABLE_UNICODE_GROUPS ? "U" : "",
            options.LONGEST_MATCH ? "l" : "",
            options.LOOKBEHINDS ? "B" : ""
            );

        var data = new { pattern, text, flags };
        string json = JsonSerializer.Serialize( data );

        using ProcessHelper ph = new( GetQuickJsExePath( ) );

        ph.AllEncoding = EncodingEnum.ASCII;
        ph.Arguments = [GetRE2JSWorkerPath( )];
        ph.WorkingDirectory = GetRE2JSWorkerDirectory( );

        ph.StreamWriter = sw =>
        {
            sw.Write( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        Response? response = JsonSerializer.Deserialize<Response>( ph.OutputStream );

        if( response == null ) throw new Exception( "JavaScript failed." );
        if( !string.IsNullOrWhiteSpace( response.Error ) ) throw new Exception( response.Error );
        if( response.Matches == null ) throw new Exception( "Invalid null response." );

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        SimpleMatch? current_match = null;

        foreach( Match response_match in response.Matches )
        {
            /*
             * Example:
             * {"Matches":[{"ag":[[0,3],[1,2],[2,3],[-1,-1]],"ng":[["n",2,3],["x",-1,-1]]},{"ag":[[4,7],[5,6],[6,7],[-1,-1]],"ng":[["n",6,7],["x",-1,-1]]}]}
             */

            HashSet<string> used_names = [];

            for( int i = 0; i < response_match.ag!.Length; i++ )
            {
                int[] g = response_match.ag[i];

                int native_start = g[0];
                int native_end = g[1];
                int native_length = native_end - native_start;

                if( i == 0 )
                {
                    Debug.Assert( native_start >= 0 );
                    Debug.Assert( native_start <= native_end );

                    SimpleMatch sm = SimpleMatch.Create( native_start, native_length, stg );

                    sm.AddDefaultGroup( );

                    matches.Add( sm );

                    current_match = sm;
                }
                else
                {
                    if( current_match == null ) throw new ApplicationException( );

                    if( native_start < 0 )
                    {
                        string name = i.ToString( CultureInfo.InvariantCulture );

                        current_match.AddFailedGroup( name );
                    }
                    else
                    {
                        // find name
                        string? name;
                        name = response_match.ng!.FirstOrDefault( g => g.s >= 0 && g.s == native_start && g.e == native_end && !used_names.Contains( g.n! ) )?.n;
                        if( name != null ) used_names.Add( name );
                        name ??= response_match.ng!.FirstOrDefault( g => g.s >= 0 && g.s == native_start && g.e == native_end )?.n;
                        name ??= i.ToString( CultureInfo.InvariantCulture );

                        current_match.AddSucceededGroup( native_start, native_length, name );
                    }
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetPluginDirectory( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = System.IO.Path.GetDirectoryName( assembly_location )!;

        return assembly_dir;
    }

    static string GetQuickJsWorkerDirectory( )
    {
        return Path.Combine( GetPluginDirectory( ), "QuickJsWorker" );
    }

    static string GetRE2JSWorkerDirectory( )
    {
        return Path.Combine( GetPluginDirectory( ), "RE2JSWorker" );
    }

    static string GetQuickJsExePath( )
    {
        return Path.Combine( GetQuickJsWorkerDirectory( ), "qjs.exe" );
    }

    static string GetRE2JSWorkerPath( )
    {
        return Path.Combine( GetRE2JSWorkerDirectory( ), "RE2JSWorker.js" );
    }

    static FeatureMatrix BuildFeatureMatrix( bool uFlag, bool enableLookbehind )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = false,
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
            XXFlag = false,

            Literal_QE = true,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = false,

            Esc_a = true,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = true,
            Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = true,
            Esc_u4 = false,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = false,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = false,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = false,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = false,
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
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = false,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = false,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = false,
            InsideSets_Operator_DoubleTilde = false,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
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

            NamedGroup_Apos = false,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = true,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = false,

            NoncapturingGroup = true,
            PositiveLookahead = false,
            NegativeLookahead = false,
            PositiveLookbehind = enableLookbehind ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = enableLookbehind ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
            NestedLookaround = false,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.None,
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
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
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

            EmptyConstruct = true,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = false,
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
