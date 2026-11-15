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
using System.Text.Json;


namespace GoPlugin;

class SubengineTre( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool extended, bool ungreedy), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix( d.extended, d.ungreedy ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.TreExtended, options.Ungreedy) );

        return new SyntaxOptions
        {
            Literal = options.literal,
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public class RootObject
    {
        public required int[][] Matches { get; set; }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Package == PackageEnum.tre );

        var data = new
        {
            pattern,
            text,

            options = new
            {
                Extended = options.TreExtended,
                ICase = options.IgnoreCase,
                Newline = options.Newline,
                NoSub = options.NoSub,
                Literal = options.literal,
                RightAssoc = options.RightAssoc,
                Ungreedy = options.Ungreedy,

                NotBOL = options.NotBOL,
                NotEOL = options.NotEOL,
                BacktrackingMatcher = options.BacktrackingMatcher,

                FindAll = options.FindAll,
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
        RootObject? root_object = JsonSerializer.Deserialize<RootObject>( output );
#else
        RootObject? root_object = JsonSerializer.Deserialize<RootObject>( ph.OutputStream );
#endif

        if( root_object == null ) throw new Exception( "Invalid response." );

        List<IMatch> matches = [];

        if( root_object.Matches != null )
        {
            SimpleTextGetter stg = new( text );
            CodepointIndexConverter index_converter = new( text );

            foreach( int[] m in root_object.Matches )
            {
                if( cnc.IsCancellationRequested ) break;

                if( m.Length < 2 || ( m.Length % 2 ) != 0 ) throw new Exception( $"Invalid length: {m.Length}." );

                if( options.NoSub )
                {
                    throw new Exception( "Match found, but details are not reported due to NoSub option." );
                }

                SimpleMatch match;

                {
                    // main group

                    int native_start = m[0];
                    int native_end = m[1];
                    int native_length = native_end - native_start;

                    Debug.Assert( native_start >= 0 );
                    Debug.Assert( native_end >= native_start );

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                    match.AddDefaultGroup( );
                }

                {
                    // other groups

                    for( int i = 2; i < m.Length; i += 2 )
                    {
                        int group_index = i / 2;

                        string? name = null; // no yet named groups
                        if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                        bool success = m[i] >= 0 && m[i + 1] >= 0;

                        if( !success )
                        {
                            match.AddFailedGroup( name );
                        }
                        else
                        {
                            int native_start = m[i];
                            int native_end = m[i + 1];
                            int native_length = native_end - native_start;

                            Debug.Assert( native_start >= 0 );
                            Debug.Assert( native_end >= native_start );

                            (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                            match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                        }
                    }
                }

                matches.Add( match );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"TreWorker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( bool extended, bool ungreedy )
    {
        return new FeatureMatrix
        {
            Parentheses = extended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = extended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            AlternationOnSeparateLines = false,

            InlineComments = extended,
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = extended,
            ScopedFlags = extended,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
            XXFlag = false,

            Literal_QE = true,
            InsideSets_Literal_QE = false,
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
            GenericEscape = true,

            InsideSets_Esc_a = false,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = false,
            InsideSets_Esc_n = false,
            InsideSets_Esc_r = false,
            InsideSets_Esc_t = false,
            InsideSets_Esc_v = false,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = false,
            InsideSets_Esc_xBrace = false,
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
            Class_pP = false,
            Class_pPBrace = false,

            InsideSets_Class_dD = false,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = false,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = false,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = false,
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
            Anchor_A = false,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = false,
            Anchor_G = false,
            Anchor_G_Option = false,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = true,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
            NamedGroup_LtGt = false,
            NamedGroup_PLtGt = false,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = false,

            NoncapturingGroup = true,
            PositiveLookahead = false,
            NegativeLookahead = false,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.None,
            NestedLookaround = false,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,
            CapturingLookaroundGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.OneDigit,
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
            Quantifier_Plus = extended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Question = extended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces = extended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = !ungreedy,
            Quantifier_Lazy = extended && !ungreedy,
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

            EmptyConstruct = extended,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_wW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = false,
            ßSS = false,

            Ext_Ph1 = extended,
            Ext_Ph2 = !ungreedy,
        };
    }
}
