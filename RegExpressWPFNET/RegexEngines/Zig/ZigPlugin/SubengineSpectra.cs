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
using System.Windows.Markup.Localizer;


namespace ZigPlugin;

partial class SubengineSpectra( Options options ) : RegexSubengine
{
    static readonly LazyData<(DialectEnum dialect, bool unicode, bool unicode_sets), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix( d.dialect, d.unicode, d.unicode_sets ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.Dialect, options.unicode, options.unicode_sets) );

        return new SyntaxOptions
        {
            XLevel = options.Dialect == DialectEnum.rust && options.extended ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    class RootObject
    {
        public required string[]? names { get; set; }
        public required int[][]? matches { get; set; }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Library == RegexLibraryEnum.Spectra );

        var json_object = new
        {
            pattern = pattern,
            text = text,
            options = new
            {
                unicode = options.unicode,
                case_insensitive = options.case_insensitive,
                multi_line = options.multiline,
                dot_matches_new_line = options.dot_all,
                swap_greed = options.ungreedy,
                ignore_whitespace = options.extended,
                unicode_sets = options.unicode_sets,
                utf8 = options.utf8,

                dialect = Enum.GetName( options.Dialect ),
            },
        };

        string json = JsonSerializer.Serialize( json_object );

        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.Write( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

#if DEBUG
        using StreamReader sr = new( ph.OutputStream );
        string output = sr.ReadToEnd( );
        RootObject? response = JsonSerializer.Deserialize<RootObject>( output );
#else
        RootObject? response = JsonSerializer.Deserialize<RootObject>( ph.OutputStream );
#endif

        if( response == null || response.matches == null ) throw new Exception( "Null response" );

        List<IMatch> matches = [];
        SimpleTextGetter? stg = new( text );
        Utf8IndexConverter index_converter = new( text );

        foreach( var m in response.matches )
        {
            if( cnc.IsCancellationRequested ) break;

            if( m.Length < 2 || ( m.Length % 2 ) != 0 ) throw new Exception( $"Invalid response array length: {m.Length}" );

            SimpleMatch match;

            {
                int native_start = m[0];
                int native_end = m[1];
                int native_length = native_end - native_start;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                match.AddDefaultGroup( );
            }

            for( int i = 2; i < m.Length; i += 2 )
            {
                int group_index = i / 2;

                string? name = null;
                if( response.names != null && group_index < response.names.Length ) name = response.names[group_index];
                name ??= group_index.ToString( CultureInfo.InvariantCulture );

                int native_start = m[i];
                int native_end = m[i + 1];

                if( native_start < 0 || native_end < 0 )
                {
                    match.AddFailedGroup( name );
                }
                else
                {
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                }
            }

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    private static string? AdjustErrorMessage( string error, string pattern )
    {
        // try to show character offset based on byte offset, which appears in error message

        System.Text.RegularExpressions.Match m = RegexExtractNativeOffset( ).Match( error );

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
        string worker_exe = Path.Combine( assembly_dir, @"SpectraWorker.bin" );

        return worker_exe;
    }

    private static FeatureMatrix BuildFeatureMatrix( DialectEnum dialect, bool unicode, bool unicode_sets )
    {
        bool is_rust = dialect == DialectEnum.rust;
        bool is_ecma = dialect == DialectEnum.ecmascript;

        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = false,
            XModeComments = is_rust,
            InsideSets_XModeComments = is_rust,

            Flags = is_rust,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = is_rust,
            XXFlag = false,

            Literal_QE = false,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = is_ecma && unicode_sets,

            Esc_a = is_rust,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = true,
            Esc_Octal = is_ecma && !unicode && !unicode_sets ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = is_rust,
            Esc_u4 = true,
            Esc_U8 = is_rust,
            Esc_uBrace = is_rust || ( is_ecma && ( unicode || unicode_sets ) ),
            Esc_UBrace = is_rust,
            Esc_c1 = is_ecma,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = is_ecma && ( !unicode && !unicode_sets ),

            InsideSets_Esc_a = is_rust,
            InsideSets_Esc_b = is_ecma,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = is_ecma && !unicode && !unicode_sets ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = is_rust,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = is_rust,
            InsideSets_Esc_uBrace = is_rust || ( is_ecma && ( unicode || unicode_sets ) ),
            InsideSets_Esc_UBrace = is_rust,
            InsideSets_Esc_c1 = is_ecma,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = is_ecma && !unicode && !unicode_sets,

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
            Class_pP = is_rust && unicode,
            Class_pPBrace = ( is_rust && unicode ) || ( is_ecma && ( unicode || unicode_sets ) ),

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
            InsideSets_Class_pP = ( is_rust && unicode ),
            InsideSets_Class_pPBrace = ( is_rust && unicode ) || ( is_ecma && ( unicode || unicode_sets ) ),
            InsideSets_Class_Name = is_rust,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = is_rust || ( is_ecma && unicode_sets ),
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = is_rust || ( is_ecma && unicode_sets ),
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = is_rust || ( is_ecma && unicode_sets ),
            InsideSets_Operator_DoubleTilde = is_rust,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = is_rust,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = is_rust,
            Anchor_G = false,
            Anchor_G_Option = false,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = is_rust,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = is_rust,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = is_rust,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = is_ecma,

            NoncapturingGroup = true,
            PositiveLookahead = is_ecma,
            NegativeLookahead = is_ecma,
            PositiveLookbehind = is_ecma ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = is_ecma ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
            NestedLookaround = is_ecma,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = true,

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

            EmptyConstruct = false,
            EmptyConstructX = false,
            EmptySet = is_ecma,
            EmptySetAny = is_ecma,

            Unicode_Class_Dot = ( is_rust && unicode ) || is_ecma,
            Unicode_Class_wW = is_rust,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = ( is_rust && unicode ) || is_ecma,
            KeepSurrogatePairs = ( is_rust && unicode ) || is_ecma,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = ( is_rust && unicode ) || is_ecma,
            ßSS = false,

            Ext_Ph2 = true,
            Ext_Ph3 = is_ecma,
        };
    }

    [GeneratedRegex( @" at (\d+)\s*$" )]
    private static partial Regex RegexExtractNativeOffset( );
}
