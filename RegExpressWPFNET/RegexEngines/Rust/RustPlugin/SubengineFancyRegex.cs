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

internal partial class SubengineFancyRegex( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool useBuilder, bool isUnicode, bool isOniguruma), FeatureMatrix> LazyFeatureMatrix =
        new( d => BuildFeatureMatrix( d.useBuilder, d.isUnicode, d.isOniguruma ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.UseBuilder, options.unicode, options.oniguruma_mode) );

        return new SyntaxOptions
        {
            XLevel = options.ignore_whitespace ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    sealed class MatchesResponse
    {
        public string[]? names { get; set; }
        public int[][][]? matches { get; set; }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.crate == CrateEnum.fancy_regex );

        bool use_builder = options.UseBuilder;

        var obj = new
        {
            use_builder = use_builder,
            pattern = pattern,
            text = text,
            options = new
            {
                options.case_insensitive,
                options.multi_line,
                options.ignore_whitespace,
                options.dot_matches_new_line,
                options.crlf,
                options.unicode,
                options.oniguruma_mode,
                options.find_not_empty,
                options.ignore_numbered_groups_when_named_groups_exist,
                options.seek,
                options.disallow_empty_match_at_eof_after_newline,
                options.allow_input_assertion_overrides,
                options.start_text,
                options.end_text,
                bytes_mode = options.bytes_mode.ToString( ),
                backtrack_limit = use_builder ? ValidationUtilities.ParseUInt32( "backtrack_limit", options.backtrack_limit ) : null,
                delegate_size_limit = use_builder ? ValidationUtilities.ParseUInt32( "delegate_size_limit", options.delegate_size_limit ) : null,
                delegate_dfa_size_limit = use_builder ? ValidationUtilities.ParseUInt32( "delegate_dfa_size_limit", options.delegate_dfa_size_limit ) : null,
            }
        };

        string json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

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
        MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( output );
#else
        MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );
#endif

        if( response == null || response.matches == null || response.names == null ) throw new Exception( "Null response" );

        List<IMatch> matches = [];
        SimpleTextGetter? stg = new( text );
        Utf8IndexConverter index_converter = new( text );

        foreach( var m in response.matches )
        {
            SimpleMatch? match = null;

            for( int group_index = 0; group_index < m.Length; group_index++ )
            {
                int[] g = m[group_index];
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

                string name = response.names[group_index];
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
        string worker_exe = Path.Combine( assembly_dir, @"RustFancyWorker.bin" );

        return worker_exe;
    }

    private static FeatureMatrix BuildFeatureMatrix( bool useBuilder, bool isUnicode, bool isOniguruma )
    {
        isUnicode |= !useBuilder;
        isOniguruma &= useBuilder;

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
            Esc_U8 = true,
            Esc_uBrace = true,
            Esc_UBrace = true,
            Esc_c1 = false,
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
            InsideSets_Esc_U8 = true,
            InsideSets_Esc_uBrace = true,
            InsideSets_Esc_UBrace = true,
            InsideSets_Esc_c1 = false,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = false,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = true,
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = true,
            Class_O = true,
            Class_R = true,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = true,
            Class_X = false,
            Class_pP = true,
            Class_pPBrace = true,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = true,
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
            Anchor_G = true,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = true,
            Anchor_PosixWB = false,
            Anchor_K = true,
            Anchor_mM = false,
            Anchor_LtGt = !isOniguruma,
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
            AtomicGroup = true,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = true,
            AllowSpacesInGroups = true,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
            Backref_kApos = true,
            Backref_kLtGt = true,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.Pattern,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.Pattern,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.Pattern,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = true,
            AllowSpacesInBackref = false,

            Recursive_Num = false,
            Recursive_PlusMinusNum = false,
            Recursive_R = false,
            Recursive_Name = false,
            Recursive_PGtName = true,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = true,
            Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.XModeOnly,
            Quantifier_LowAbbrev = true,
            Quantifier_Lazy = true,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = true,
            Conditional_BackrefByName = false,
            Conditional_Pattern = true,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = true,
            Conditional_BackrefByName_LtGt = true,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = true,
            Conditional_VERSION = false,

            ControlVerbs = true,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = false,
            EmptyConstructX = true,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = isUnicode,
            ßSS = false,
        };
    }

    [GeneratedRegex( @" at position (\d+)" )]
    private static partial Regex RegexExtractByteOffset( );
}
