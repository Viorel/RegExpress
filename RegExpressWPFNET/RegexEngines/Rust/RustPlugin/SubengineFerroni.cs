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
using System.Text.Json;


namespace RustPlugin;

class SubengineFerroni( Options options ) : RegexSubengine
{
    static readonly LazyData<OnigSyntaxTypeEnum, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );


    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( options.OnigSyntaxType );

        return new SyntaxOptions
        {
            Literal = options.OnigSyntaxType == OnigSyntaxTypeEnum.OnigSyntaxASIS,
            XLevel = options.ignore_whitespace ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public class Rootobject
    {
        public required Match[] matches { get; set; }
        public required Name[] names { get; set; }
    }

    public class Match
    {
        public required int[] g { get; set; }
    }

    public class Name
    {
        public required string n { get; set; }
        public required int[] g { get; set; }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.crate == CrateEnum.ferroni );

        bool use_builder = options.UseBuilder;

        var obj = new
        {
            use_builder = use_builder,
            pattern = pattern,
            text = text,
            options = new
            {
                case_insensitive = options.case_insensitive,
                dot_matches_newline = options.dot_matches_new_line,
                multi_line_anchors = options.multi_line,
                extended = options.ignore_whitespace,

                syntax = Enum.GetName( options.OnigSyntaxType ),
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

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

#if DEBUG
        using StreamReader sr = new( ph.OutputStream );
        string output = sr.ReadToEnd( );
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( output );
#else
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

        if( root_object == null || root_object.matches == null ) throw new Exception( "Null response" );

        List<IMatch> matches = [];
        SimpleTextGetter? stg = new( text );
        Utf8IndexConverter index_converter = new( text );

        foreach( var m in root_object.matches )
        {
            if( cnc.IsCancellationRequested ) break;

            if( m.g.Length < 2 || ( m.g.Length % 2 ) != 0 ) throw new Exception( $"Invalid length: {m.g.Length}." );

            SimpleMatch? match = null;

            {
                int native_start = m.g[0];
                int native_end = m.g[1];
                int native_length = native_end - native_start;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                match.AddDefaultGroup( );
            }

            for( int i = 2; i < m.g.Length; i += 2 )
            {
                int group_index = i / 2;

                int native_start = m.g[i];
                int native_end = m.g[i + 1];

                bool success = native_start >= 0 && native_end >= 0;

                string? name = root_object.names.FirstOrDefault( n => n.g.Contains( group_index ) )?.n;
                name ??= group_index.ToString( CultureInfo.InvariantCulture );

                if( !success )
                {
                    match.AddFailedGroup( name );
                }
                else
                {
                    int native_length = native_end - native_start;
                    Debug.Assert( native_length >= 0 );

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    Debug.Assert( match != null );

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
        string worker_exe = Path.Combine( assembly_dir, @"FerroniWorker.bin" );

        return worker_exe;
    }

    private static FeatureMatrix BuildFeatureMatrix( OnigSyntaxTypeEnum syntax )
    {
        bool grp0 =
            syntax == OnigSyntaxTypeEnum.OnigSyntaxOniguruma ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxRuby ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxPerl ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxPerl_NG;

        bool grp1 =
            grp0 ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxJava ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxPython;

        bool grp2 =
            syntax == OnigSyntaxTypeEnum.OnigSyntaxGrep ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxEmacs ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxPosixBasic;

        bool grp3 =
            syntax == OnigSyntaxTypeEnum.OnigSyntaxPosixExtended ||
            syntax == OnigSyntaxTypeEnum.OnigSyntaxGnuRegex;

        bool is_oniguruma = syntax == OnigSyntaxTypeEnum.OnigSyntaxOniguruma;
        bool is_ruby = syntax == OnigSyntaxTypeEnum.OnigSyntaxRuby;
        bool is_perl = syntax == OnigSyntaxTypeEnum.OnigSyntaxPerl;
        bool is_perl_ng = syntax == OnigSyntaxTypeEnum.OnigSyntaxPerl_NG;
        bool is_perls = is_perl || is_perl_ng;
        bool is_java = syntax == OnigSyntaxTypeEnum.OnigSyntaxJava;
        bool is_python = syntax == OnigSyntaxTypeEnum.OnigSyntaxPython;
        bool is_grep = syntax == OnigSyntaxTypeEnum.OnigSyntaxGrep;
        bool is_emacs = syntax == OnigSyntaxTypeEnum.OnigSyntaxEmacs;
        bool is_posix_extended = syntax == OnigSyntaxTypeEnum.OnigSyntaxPosixExtended;
        bool is_gnu = syntax == OnigSyntaxTypeEnum.OnigSyntaxGnuRegex;

        return new FeatureMatrix
        {
            Parentheses = grp1 || grp3 ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = grp1 || grp3 ? FeatureMatrix.PunctuationEnum.Normal : is_grep || is_emacs ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            AlternationOnSeparateLines = false,

            InlineComments = grp1,
            XModeComments = true,
            InsideSets_XModeComments = false,

            Flags = grp1,
            ScopedFlags = grp1,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = grp1,
            XXFlag = false,

            Literal_QE = false,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = false,

            Esc_a = true,
            Esc_b = false,
            Esc_e = true,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = is_oniguruma || is_ruby || is_java || is_python,
            Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = grp0,
            Esc_x2 = grp1,
            Esc_xBrace = grp0,
            Esc_u4 = is_oniguruma || is_ruby || is_java || is_python,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = grp1,
            Esc_C1 = false,
            Esc_CMinus = is_oniguruma || is_ruby,
            Esc_NBrace = false,
            GenericEscape = true,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = true,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_1_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = grp0,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = is_oniguruma || is_ruby || is_java || is_python,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = grp1,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = is_oniguruma || is_ruby,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = true,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = is_oniguruma || is_ruby,
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = grp0,
            Class_O = grp0,
            Class_R = grp0,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = true,
            Class_X = grp0,
            Class_pP = is_oniguruma || is_perls,
            Class_pPBrace = grp1,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = is_oniguruma || is_ruby,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = true,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = is_oniguruma || is_perls,
            InsideSets_Class_pPBrace = grp1,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = is_oniguruma || is_ruby || is_java,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = is_oniguruma || is_ruby || is_java,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = false,
            InsideSets_Operator_DoubleTilde = false,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = grp1 || is_gnu,
            Anchor_Z = grp1 || is_gnu ? FeatureMatrix.AnchorZModeEnum.Correct : FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = grp0 || is_java || is_gnu,
            Anchor_G = grp1 || is_gnu,
            Anchor_bB = grp1 || is_grep || is_gnu,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = grp0 || is_python,
            Anchor_mM = false,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = grp0,

            NamedGroup_Apos = is_oniguruma || is_ruby || is_perl_ng,
            NamedGroup_LtGt = grp1 || is_emacs,
            NamedGroup_PLtGt = false,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = grp1,

            NoncapturingGroup = grp1 || is_emacs,
            PositiveLookahead = grp1 || is_emacs,
            NegativeLookahead = grp1 || is_emacs,
            PositiveLookbehind = grp1 || is_emacs ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = grp1 || is_emacs ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
            NestedLookaround = true,
            AtomicGroup = grp1 || is_emacs,
            BranchReset = is_grep, //?
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = grp0,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
            Backref_kApos = is_oniguruma || is_ruby || is_perl_ng,
            Backref_kLtGt = is_oniguruma || is_ruby || is_perl_ng,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = is_oniguruma || is_ruby || is_perl_ng ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = is_oniguruma || is_ruby || is_perl_ng ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
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
            Quantifier_Plus = grp1 || is_emacs || grp3 ? FeatureMatrix.PunctuationEnum.Normal : is_grep ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Question = grp1 || is_emacs || grp3 ? FeatureMatrix.PunctuationEnum.Normal : is_grep ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces = grp1 || grp3 ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = grp1 || grp3,
            Quantifier_Lazy = grp1,
            Quantifier_Possessive = grp0 || is_java,

            Conditional_BackrefByNumber = grp0 || is_python,
            Conditional_BackrefByName = false,
            Conditional_Pattern = grp0 || is_python,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = is_oniguruma || is_ruby || is_perl_ng,
            Conditional_BackrefByName_LtGt = grp0 || is_python,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = is_oniguruma || is_ruby || is_perl_ng,
            Conditional_VERSION = false,

            ControlVerbs = is_oniguruma || is_perls || is_python,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = grp1,
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
            Σσς = true, // if not 'ignoreCaseIsAscii'
            ßSS = true, // if not 'ignoreCaseIsAscii'
        };
    }
}
