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
using System.Text.RegularExpressions;


namespace PerlPlugin;

partial class Subengine( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.Value;

        return new SyntaxOptions
        {
            XLevel = options.xx ? XLevelEnum.xx : options.x ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        string modifiers = "";
        if( options.m ) modifiers += "m";
        if( options.s ) modifiers += "s";
        if( options.i ) modifiers += "i";
        if( options.x && !options.xx ) modifiers += "x";
        if( options.xx ) modifiers += "xx";
        if( options.n ) modifiers += "n";
        if( options.a && !options.aa ) modifiers += "a";
        if( options.aa ) modifiers += "aa";
        if( options.d ) modifiers += "d";
        if( options.u ) modifiers += "u";
        if( options.l ) modifiers += "l";
        if( options.g ) modifiers += "g";
        //if( options.c ) modifiers += "c";

        using ProcessHelper ph = new ProcessHelper( GetPerlExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;
        ph.Arguments = new[] { "-CS", GetWorkerPath( ) };

        ph.StreamWriter = sw =>
        {
            var json_obj = new { p = pattern, t = text, m = modifiers };
            string json = JsonSerializer.Serialize( json_obj, JsonUtilities.JsonOptions );

            sw.Write( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        // (The error stream is used for debugging details too)
        if( !string.IsNullOrWhiteSpace( ph.Error ) )
        {
            string error_text = GetErrorRegex( ).Match( ph.Error ).Groups[1].Value.Trim( );

            if( !string.IsNullOrWhiteSpace( error_text ) )
            {
                // remove unneeded details about PerlWorker.pl
                string error_message = RemoveUnneededErrorDetailsRegex( ).Replace( error_text, "" );

                throw new Exception( error_message );
            }
        }

        // collect group names from Perl debugging details

        string debug_text = ph.Error == null ? "" : GetDebugDetailsRegex( ).Match( ph.Error ).Groups[1].Value.Trim( );

        List<string?> numbered_names = new( );

        foreach( Match m in GetGroupRegex( ).Matches( debug_text ) )
        {
            string name = m.Groups[2].Value;
            int number = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );

            // fill gap, reserve
            for( int i = numbered_names.Count; i <= number; ++i ) numbered_names.Add( null );

            Debug.Assert( numbered_names[number] == null || numbered_names[number] == name );

            numbered_names[number] = name;
        }

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        CodepointIndexConverter index_converter = new( text );
        SimpleMatch? match = null;
        string? line;

        while( ( line = ph.StreamReader.ReadLine( ) ) != null )
        {
            if( line == "\x1FM" )
            {
                match = null;
            }
            else if( string.IsNullOrWhiteSpace( line ) )
            {
                continue;
            }
            else
            {
                Match m = ParseGroupResultRegex( ).Match( line );

                if( m.Success )
                {
                    int native_index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                    int native_length = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture );
                    int native_end = native_index + native_length;

                    int group_index = match == null ? 0 : match.Groups.Count( );
                    string? group_name = group_index < numbered_names.Count ? numbered_names[group_index] : null;
                    group_name ??= group_index.ToString( CultureInfo.InvariantCulture );

                    bool success = native_index >= 0;

                    if( !success )
                    {
                        if( match == null ) throw new InvalidOperationException( );

                        Debug.Assert( group_index > 0 );

                        match.AddFailedGroup( group_name );
                    }
                    else
                    {
                        (int char_index, int char_length) = index_converter.Convert( native_index, native_end );

                        if( match == null )
                        {
                            match = SimpleMatch.Create( native_index, native_length, char_index, char_length, stg );
                            matches.Add( match );
                        }

                        match.AddSucceededGroup( native_index, native_length, char_index, char_length, group_name );
                    }
                }
                else
                {
                    if( Debugger.IsAttached ) Debugger.Break( );
                    // ignore
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetPerlExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string? assembly_dir = Path.GetDirectoryName( assembly_location );
        string perl_dir = Path.Combine( assembly_dir!, @"Perl-min\perl" );
        string perl_exe = Path.Combine( perl_dir, @"bin\perl.exe" );

        return perl_exe;
    }


    static string GetWorkerPath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_path = Path.Combine( assembly_dir, @"PerlWorker.pl" );

        return worker_path;
    }
    static FeatureMatrix BuildFeatureMatrix( )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = true,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = true,
            XModeComments = true,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = true,
            ScopedCircumflexFlags = true,
            XFlag = true,
            XXFlag = true,

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
            Esc_v = false,
            Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = true,
            Esc_x2 = true,
            Esc_xBrace = true,
            Esc_u4 = false,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_CMinus = false,
            Esc_NBrace = true,
            GenericEscape = true,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = true,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = false,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_1_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = true,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = false,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false, //...
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = true,
            InsideSets_GenericEscape = true,

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
            Class_vV = true,
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
            InsideSets_Class_vV = true,
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = true,
            InsideSets_Class_pPBrace = true,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = false,
            InsideSets_OperatorsExtended = true,
            InsideSets_Operator_Ampersand = true,
            InsideSets_Operator_Plus = true,
            InsideSets_Operator_VerticalLine = true,
            InsideSets_Operator_Minus = true,
            InsideSets_Operator_Circumflex = true,
            InsideSets_Operator_Exclamation = true,
            InsideSets_Operator_DoubleAmpersand = false,
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
            Anchor_bBBrace = true,
            Anchor_PosixWB = false,
            Anchor_K = true,
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
            PositiveLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NestedLookaround = true,
            AtomicGroup = true,
            BranchReset = true,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
            Backref_kApos = true,
            Backref_kLtGt = true,
            Backref_kBrace = true,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.Value,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.Value,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.Value,
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
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = true,
            Quantifier_Lazy = true,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = true,
            Conditional_BackrefByName = false,
            Conditional_Pattern = true,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = true,
            Conditional_BackrefByName_LtGt = true,
            Conditional_R = true,
            Conditional_RName = true,
            Conditional_DEFINE = true,
            Conditional_VERSION = false,

            ControlVerbs = true,
            ScriptRuns = true,
            Callouts = false,

            EmptyConstruct = true,
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
            ßSS = true,
        };
    }

    [GeneratedRegex( @"\u001FERR>(.*?)<\u001FERR", RegexOptions.Singleline )]
    private static partial Regex GetErrorRegex( );

    [GeneratedRegex( @"\s+at\s+.+\\PerlWorker.pl\s+line\s+\d+,\s+<STDIN>\s+line\s+\d+(?=\.\s*$)", RegexOptions.Singleline )]
    private static partial Regex RemoveUnneededErrorDetailsRegex( );

    [GeneratedRegex( @"\u001FDEBUG>(.*?)<\u001FDEBUG", RegexOptions.Singleline )]
    private static partial Regex GetDebugDetailsRegex( );

    [GeneratedRegex( @"^ \s* \d+: \s* CLOSE(\d+) \s+ '(.*?)' \s+ \(\d+\) \s* $", RegexOptions.Multiline | RegexOptions.IgnorePatternWhitespace )]
    private static partial Regex GetGroupRegex( );

    [GeneratedRegex( @"^\u001FG,(-1|\d+),(\d+)$" )]
    private static partial Regex ParseGroupResultRegex( );
}
