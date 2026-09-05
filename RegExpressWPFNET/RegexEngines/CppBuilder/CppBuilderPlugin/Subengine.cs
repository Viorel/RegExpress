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
using System.Text.RegularExpressions;


namespace CppBuilderPlugin;

partial class Subengine( Options options ) : RegexSubengine
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
            XLevel = options.roIgnorePatternSpace ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = LazyFeatureMatrix.Value,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.ASCII;

        ph.StreamWriter = sw =>
        {

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = new
                {
                    options.roIgnoreCase,
                    options.roMultiLine,
                    options.roExplicitCapture,
                    options.roCompiled,
                    options.roSingleLine,
                    options.roIgnorePatternSpace,
                    options.roNotEmpty,
                }
            };

            sw.WriteLine( JsonSerializer.Serialize( obj ) );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
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
                    int native_index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (starting at 1)
                    Debug.Assert( native_index > 0 );

                    if( native_index > 0 )
                    {
                        --native_index; // make it starting at 0
                        int native_length = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture );

                        current_match = SimpleMatch.Create( native_index, native_length, stg );
                        current_match.AddDefaultGroup( );

                        matches.Add( current_match );
                    }

                    continue;
                }
                else
                {
                    Match g = ParseGroupRegex( ).Match( line );
                    if( g.Success )
                    {
                        if( current_match == null ) throw new Exception( "Invalid response." );

                        int native_index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture ); // (starting at 1)
                        int native_length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                        bool success = native_index > 0;

                        string? name = null;
                        Group gn = g.Groups["n"];
                        if( gn.Success )
                        {
                            string name_js = gn.Value;

                            try
                            {
                                name = JsonSerializer.Deserialize<string>( name_js );
                            }
                            catch
                            {
                                name = null;
                                // ignore?
                            }
                        }

                        name ??= current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                        if( !success )
                        {
                            current_match.AddFailedGroup( name );
                        }
                        else
                        {
                            current_match.AddSucceededGroup( native_index - 1, native_length, name );
                        }

                        continue;
                    }
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"CppBuilderWorker.bin" );

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
            XModeComments = true,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = true,
            XXFlag = false,

            Literal_QE = true,
            InsideSets_Literal_QE = true,
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
            Esc_NBrace = false,
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
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = true,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = true,
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
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Correct,
            Anchor_z = true,
            Anchor_G = true,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = true,
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
            DuplicateGroupName = false,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.FixedLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.FixedLength,
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
            Backref_gApos = FeatureMatrix.BackrefModeEnum.Pattern,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.Pattern,
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
            Quantifier_LowAbbrev = false,
            Quantifier_Lazy = true,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = true,
            Conditional_BackrefByName = true,
            Conditional_Pattern = true,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = true,
            Conditional_BackrefByName_LtGt = true,
            Conditional_R = true,
            Conditional_RName = true,
            Conditional_DEFINE = true,
            Conditional_VERSION = false,

            ControlVerbs = true,
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

    [GeneratedRegex( @"(?ix)^\s* M \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseMatchRegex( );

    [GeneratedRegex( @"(?ix)^\s* g \s+ (-?\d+) \s+ (-?\d+) (\s+(?<n>"".*?""))?" )]
    private static partial Regex ParseGroupRegex( );
}
