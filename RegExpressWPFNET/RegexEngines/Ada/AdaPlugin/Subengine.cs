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
using System.Text.RegularExpressions;


namespace AdaPlugin;

partial class Subengine( Options options ) : RegexSubengine
{
    internal static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );
    static readonly Encoding StrictAsciiEncoding = Encoding.GetEncoding( "ASCII", EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback );

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

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        try
        {
            _ = StrictAsciiEncoding.GetBytes( pattern );
        }
        catch( EncoderFallbackException exc )
        {
            throw new Exception( string.Format( "Ada engine only supports the ASCII character encoding.\r\nThe pattern contains an invalid character at position {0}.", exc.Index ) );
        }

        try
        {
            _ = StrictAsciiEncoding.GetBytes( text );
        }
        catch( EncoderFallbackException exc )
        {
            throw new Exception( string.Format( "Ada engine only supports the ASCII character encoding.\r\nThe text contains an invalid character at position {0}.", exc.Index ) );
        }

        var obj = new
        {
            pattern = pattern,
            text = text,
            options = new
            {
                options.Case_Insensitive,
                options.Single_Line,
                options.Multiple_Lines,
            }
        };

        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.ASCII;

        ph.StreamWriter = sw =>
        {
            sw.WriteLine( JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions ) );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        List<SimpleMatch> matches = [];
        SimpleTextGetter text_getter = new( text );
        Utf8IndexConverter index_converter = new( text ); // (actually only ASCII is supported, therefore it works like an identity converter)
        SimpleMatch? current_match = null;
        string? line;

        while( ( line = ph.StreamReader.ReadLine( ) ) != null )
        {
            line = line.Trim( );

            if( line.Length == 0 ) continue;
            if( line.StartsWith( "d " ) ) continue; // (for debugging)

            {
                Match m = ParseMatchRegex( ).Match( line );
                if( m.Success )
                {
                    int native_start = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)
                    int native_end = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                    if( native_start <= 0 )
                    {
                        throw new Exception( $"Invalid output: {line}" );
                    }

                    // to 0... and exclusive end; 
                    if( native_end < native_start ) // 'end < start' in case of empty matches
                    {
                        --native_start;
                        native_end = native_start;
                    }
                    else
                    {
                        --native_start;
                    }

                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    current_match = SimpleMatch.Create( native_start, native_length, char_start, char_length, text_getter );
                    current_match.AddDefaultGroup( );
                    matches.Add( current_match );

                    continue;
                }
            }

            {
                Match g = ParseGroupRegex( ).Match( line );
                if( g.Success )
                {
                    if( current_match == null ) throw new ApplicationException( );

                    int native_start = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)
                    int native_end = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                    bool success = native_start > 0;

                    string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        current_match.AddFailedGroup( name );
                    }
                    else
                    {
                        // to 0... and exclusive end

                        --native_start;

                        if( native_end == 0 ) // empty match
                        {
                            native_end = native_start;
                        }

                        Debug.Assert( native_end >= native_start );

                        int native_length = native_end - native_start;

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        SimpleGroup group = current_match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                    }

                    continue;
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"adaworker.bin" );

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

            InlineComments = false,
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = false,
            ScopedFlags = false,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
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
            Esc_v = false,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = false,
            Esc_xBrace = false,
            Esc_u4 = false,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = false,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = true,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = true,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
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
            InsideSets_GenericEscape = true,

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
            Anchor_z = false,
            Anchor_G = false, // (abnormal usage of '\G'; here it is 'the end of the string'; other engines use '\z' and 'Z')
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

            Backref_Num = FeatureMatrix.BackrefEnum.Any, // ("Although the depth of parenthesis is not limited in the regexp, only the first 9 substrings can be retrieved")
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
            EmptyConstructX = false, // TODO: "a(? )b": with "xabc" no error, with "ab" gives error
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = false,
            Unicode_Class_vW = false,
            InsideSets_Unicode = false,
            UnicodeCaseFolding = false,
            KeepSurrogatePairs = true, // (not applicable)
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
            Σσς = false,
            ßSS = false,
        };
    }

    [GeneratedRegex( @"(?x)^\s* m \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseMatchRegex( );

    [GeneratedRegex( @"(?x)^\s* g \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseGroupRegex( );
}
