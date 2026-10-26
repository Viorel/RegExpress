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


namespace DotNETPlugin;

internal partial class SubengineFuzzyRegex( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool Posix, bool FullCase, bool Version0, bool Version1), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix( d.Posix, d.FullCase, d.Version0, d.Version1 ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.HasCaptures | RegexEngineCapabilityEnum.OverlappingMatches;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = LazyFeatureMatrix.GetValue( (options.Posix, options.FullCase, options.Version0, options.Version1) ),
        };
    }

    public class Rootobject
    {
        public required string[] Names { get; set; }
        public required int[][][] Matches { get; set; }
    }


    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Class == ClassEnum.FuzzyRegex );

        var data = new
        {
            pattern,
            text,
            options = new
            {
                IgnoreCase = options.IgnoreCase,
                Multiline = options.Multiline,
                Singleline = options.Singleline,
                Unicode = options.UseDotnetUnicode,
                IgnorePatternWhitespace = options.IgnorePatternWhitespace,
                Ascii = options.Ascii,
                RightToLeft = options.RightToLeft,
                Word = options.Word,
                BestMatch = options.BestMatch,
                EnhanceMatch = options.EnhanceMatch,
                Posix = options.Posix,
                FullCase = options.FullCase,
                Version0 = options.Version0,
                Version1 = options.Version1,
                Overlapped = options.Overlapped,
                Partial = options.Partial,

                Timeout = ValidationUtilities.ParseUInt64( "Timeout", options.TimeoutMs ),
                StepTimeout = ValidationUtilities.ParseUInt64( "Step Timeout", options.StepTimeoutMs ),
                MaxCompiledNodes = ValidationUtilities.ParseInt32( "MaxCompiledNodes", options.MaxCompiledNodes ),
            }
        };
        string json = JsonSerializer.Serialize( data );

        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.Write( json );
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

        if( root_object == null ) throw new Exception( "Invalid response" );

        List<SimpleMatch> matches = [];
        SimpleTextGetter stg = new( text );
        IdentityIndexConverter index_converter = IdentityIndexConverter.Instance;

        foreach( int[][] m in root_object.Matches )
        {
            if( cnc.IsCancellationRequested ) break;

            SimpleMatch match;

            {
                int native_start = m[0][0];
                int native_length = m[0][1];
                int native_end = native_start + native_length;

                Debug.Assert( native_start >= 0 && native_end >= native_start );

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );

                match.AddDefaultGroup( );
            }

            for( int i = 1; i < m.Length; ++i ) // skip default group
            {
                int[] g = m[i];

                string name = root_object.Names[i];

                bool success = g.Length > 0;

                if( !success )
                {
                    match.AddFailedGroup( name );
                }
                else
                {
                    int native_start = g[0];
                    int native_length = g[1];
                    int native_end = native_start + native_length;

                    Debug.Assert( native_start >= 0 && native_end >= native_start );

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    var group = match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );

                    for( int j = 2; j < g.Length; j += 2 )
                    {
                        int c_native_start = g[j];
                        int c_native_length = g[j + 1];
                        int c_native_end = c_native_start + c_native_length;

                        Debug.Assert( c_native_start >= 0 && c_native_end >= c_native_start );

                        (int c_char_start, int c_char_length) = index_converter.Convert( c_native_start, c_native_end );

                        group.AddCapture( c_native_start, c_native_length, c_char_start, c_char_length );
                    }
                }
            }

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, "FuzzyRegexWorker", @"FuzzyRegexWorker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( bool Posix, bool FullCase, bool Version0, bool Version1 )
    {
        if( !Version0 && !Version1 ) Version1 = true;

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

            Literal_QE = false,
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
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = false,
            Esc_u4 = true,
            Esc_U8 = true,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = false,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = true,
            GenericEscape = false,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_1_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = false,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = true,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = false,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = true,
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

            InsideSets_Operators = Version1,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = Version1,
            InsideSets_Operator_DoubleVerticalLine = Version1,
            InsideSets_Operator_DoubleMinus = Version1,
            InsideSets_Operator_DoubleTilde = Version1,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Compatible, // 
            Anchor_z = true,
            Anchor_G = true,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = true,
            Anchor_mM = true,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
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
            AtomicGroup = true,
            BranchReset = true,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.Any, // TODO: actually it supports \1, \2, ... \99.
            Backref_kApos = false,
            Backref_kLtGt = false,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.Value,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = true,
            AllowSpacesInBackref = false,

            Recursive_Num = true,
            Recursive_PlusMinusNum = true,
            Recursive_R = true,
            Recursive_Name = true,
            Recursive_PGtName = true,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = true,
            Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = true,
            Quantifier_Lazy = !Posix,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = true,
            Conditional_BackrefByName = true,
            Conditional_Pattern = true,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = false,
            Conditional_BackrefByName_LtGt = false,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = true,
            Conditional_VERSION = false,

            ControlVerbs = true,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = true,
            EmptyConstructX = true,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_wW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = true,
            ßSS = Version1 || (Version0 && FullCase),
        };
    }

    [GeneratedRegex( @" at byte offset (\d+)" )]
    private static partial Regex RegexExtractByteOffset( );
}
