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
using System.Text.RegularExpressions;


namespace FortranPlugin;

partial class SubengineFregex( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = LazyFeatureMatrix.Value,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Module == ModuleEnum.Fregex );

        string adjusted_pattern = pattern.Replace( "\x1B", " " ).Replace( "\r", "\x1Br" ).Replace( "\n", "\x1Bn" );
        string adjusted_text = text.Replace( "\x1B", " " ).Replace( "\r", "\x1Br" ).Replace( "\n", "\x1Bn" );

        string flags = "";
        if( options.ignorecase ) flags += "i";
        if( options.multiline ) flags += "m";
        if( options.dotall ) flags += "s";

        Int64? step_limit = ValidationUtilities.ParseInt64( "step_limit", options.step_limit );
        string step_limit_s = step_limit == null ? "" : step_limit.Value.ToString( CultureInfo.InvariantCulture );

        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.WriteLine( adjusted_pattern );
            sw.WriteLine( adjusted_text );
            sw.WriteLine( flags );
            sw.WriteLine( step_limit_s );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

        List<SimpleMatch> matches = [];
        SimpleTextGetter text_getter = new( text );
        Utf8IndexConverter? index_converter_for_names = null;
        Utf8IndexConverter index_converter = new( text );
        Dictionary<int, string> names = [];
        string? line;

        while( ( line = ph.StreamReader.ReadLine( ) ) != null )
        {
            if( cnc.IsCancellationRequested ) break;

            line = line.Trim( );

            if( line.Length == 0 ) continue;
            if( line.StartsWith( "d " ) ) continue; // (for debugging)

            {
                Match m = ParseNameRegex( ).Match( line );
                if( m.Success )
                {
                    int group_index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                    int native_start = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture ) - 1;
                    int native_end = int.Parse( m.Groups[3].Value, CultureInfo.InvariantCulture );
                    index_converter_for_names ??= new( pattern );
                    (int char_start, int char_length) = index_converter_for_names.Convert( native_start, native_end );
                    string name = pattern.Substring( char_start, char_length );
                    names[group_index] = name;

                    continue;
                }
            }

            {
                Match m = ParseMatchRegex( ).Match( line );
                if( m.Success )
                {
                    int native_start = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..), UTF-8
                    int native_end = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)
                    if( native_end < native_start ) native_end = native_start - 1; // empty match

                    --native_start; // (but keep 'native_end')

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    SimpleMatch match = SimpleMatch.Create( native_start, native_end - native_start, char_start, char_length, text_getter );
                    match.AddDefaultGroup( );

                    matches.Add( match );

                    continue;
                }
            }

            {
                Match m = ParseGroupRegex( ).Match( line );
                if( m.Success )
                {
                    int native_start = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..), UTF-8
                    int native_end = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                    bool success = native_start > 0 && native_end > 0;

                    SimpleMatch match = matches.Last( );
                    int group_index = match.Groups.Count( );

                    string? name = null;
                    if( names.TryGetValue( group_index, out string? name0 ) ) name = name0;
                    name ??= group_index.ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        if( native_end < native_start ) native_end = native_start - 1; // empty group

                        --native_start; // (but keep 'native_end')

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_end - native_start, char_start, char_length, name );
                    }

                    continue;
                }
            }

#if DEBUG
            if( !string.IsNullOrWhiteSpace( line ) )
            {
                // invalid line
                if( Debugger.IsAttached ) Debugger.Break( );
            }
#endif
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string? AdjustErrorMessage( string error, string pattern )
    {
        // try to show character offset based on byte offset, which appears in error message

        System.Text.RegularExpressions.Match m = ExtractNativeOffsetRegex( ).Match( error );

        if( m.Success && int.TryParse( m.Groups[1].Value, out int byte_offset ) )
        {
            try
            {
                byte[] utf8_bytes = Encoding.UTF8.GetBytes( pattern );
                int char_offset = Encoding.UTF8.GetCharCount( utf8_bytes, 0, byte_offset - 1 ); // it was 1-based

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
        string worker_exe = Path.Combine( assembly_dir, @"FregexWorker.bin" );

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
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
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
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = false,
            InsideSets_Class_Name = false,
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
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Compatible,
            Anchor_z = false,
            Anchor_G = false,
            Anchor_G_Option = false,
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
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
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
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = false,
            Unicode_Class_wW = false,
            InsideSets_Unicode = false,
            UnicodeCaseFolding = false,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Reject,
            Σσς = false,
            ßSS = false,

        };
    }

    [GeneratedRegex( @" at position (\d+)\s*$" )]
    private static partial Regex ExtractNativeOffsetRegex( );

    [GeneratedRegex( @"(?x)^\s* n \s+ (\d+) \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseNameRegex( );

    [GeneratedRegex( @"(?x)^\s* m \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseMatchRegex( );

    [GeneratedRegex( @"(?x)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
    private static partial Regex ParseGroupRegex( );
}
