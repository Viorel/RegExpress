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


namespace StdPlugin;

partial class SubengineGCC( Options options ) : RegexSubengine
{
    static readonly LazyData<GrammarEnum, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( options.Grammar );

        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }


    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Compiler == CompilerEnum.GCC );

        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.WriteLine( "\"m\"" ); // (command)
            sw.WriteLine( JsonSerializer.Serialize( pattern ) );
            sw.WriteLine( JsonSerializer.Serialize( text ) );

            string grammar = Enum.GetName( options.Grammar )!;
            sw.WriteLine( JsonSerializer.Serialize( grammar ) );

            sw.WriteLine( JsonSerializer.Serialize( options.Locale ?? "" ) );

            string flags = "";
            if( options.icase ) flags += "icase ";
            if( options.nosubs ) flags += "nosubs ";
            if( options.optimize ) flags += "optimize ";
            if( options.collate ) flags += "collate ";
            if( options.multiline ) flags += "multiline ";
            if( options.polynomial ) flags += "polynomial ";

            if( options.match_not_bol ) flags += "match_not_bol ";
            if( options.match_not_eol ) flags += "match_not_eol ";
            if( options.match_not_bow ) flags += "match_not_bow ";
            if( options.match_not_eow ) flags += "match_not_eow ";
            if( options.match_any ) flags += "match_any ";
            if( options.match_not_null ) flags += "match_not_null ";
            if( options.match_continuous ) flags += "match_continuous ";
            if( options.match_prev_avail ) flags += "match_prev_avail ";

            sw.WriteLine( JsonSerializer.Serialize( flags ) );
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
                    int native_index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                    Debug.Assert( native_index >= 0 );

                    if( native_index >= 0 )
                    {
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

                        int native_index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture );
                        int native_length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                        bool success = native_index >= 0;

                        string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                        if( !success )
                        {
                            current_match.AddFailedGroup( name );
                        }
                        else
                        {
                            current_match.AddSucceededGroup( native_index, native_length, name );
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
        string worker_exe = Path.Combine( assembly_dir, @"GccWorker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( GrammarEnum grammar )
    {
        return new FeatureMatrix
        {
            Parentheses = grammar == GrammarEnum.extended ||
                            grammar == GrammarEnum.ECMAScript ||
                            grammar == GrammarEnum.egrep ||
                            grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                            :
                            grammar == GrammarEnum.basic ||
                            grammar == GrammarEnum.grep ? FeatureMatrix.PunctuationEnum.Backslashed
                            :
                            FeatureMatrix.PunctuationEnum.None,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = grammar == GrammarEnum.extended ||
                                        grammar == GrammarEnum.ECMAScript ||
                                        grammar == GrammarEnum.egrep ||
                                        grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                        : FeatureMatrix.PunctuationEnum.None,
            AlternationOnSeparateLines = grammar == GrammarEnum.grep || grammar == GrammarEnum.egrep,

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

            Esc_a = grammar == GrammarEnum.awk,
            Esc_b = grammar == GrammarEnum.awk,
            Esc_e = false,
            Esc_f = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            Esc_n = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            Esc_r = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            Esc_t = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            Esc_v = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            Esc_Octal = grammar == GrammarEnum.awk ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = grammar == GrammarEnum.ECMAScript,
            Esc_xBrace = false,
            Esc_u4 = grammar == GrammarEnum.ECMAScript,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = false, // is seems that '\cM' matches 'M', not '\r'; '\c.' matches '.'grammar == GrammarEnum.ECMAScript,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = grammar == GrammarEnum.ECMAScript,

            InsideSets_Esc_a = grammar == GrammarEnum.awk,
            InsideSets_Esc_b = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            InsideSets_Esc_n = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            InsideSets_Esc_r = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            InsideSets_Esc_t = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            InsideSets_Esc_v = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
            InsideSets_Esc_Octal = grammar == GrammarEnum.awk ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = grammar == GrammarEnum.ECMAScript,
            InsideSets_Esc_xBrace = false,
            InsideSets_Esc_u4 = grammar == GrammarEnum.ECMAScript,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = false, // is seems that '[\cM]' matches 'M', not '\r';
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = grammar == GrammarEnum.ECMAScript,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = grammar == GrammarEnum.ECMAScript,
            Class_hHhexa = false,
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = false,
            Class_sS = grammar == GrammarEnum.ECMAScript,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = grammar == GrammarEnum.ECMAScript,
            Class_X = false,
            Class_pP = false,
            Class_pPBrace = false,

            InsideSets_Class_dD = grammar == GrammarEnum.ECMAScript,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = grammar == GrammarEnum.ECMAScript,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = grammar == GrammarEnum.ECMAScript,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = false,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = true,
            InsideSets_Collating = true, // TODO: it seems to be a defect of STL; it always matches the last (any) character

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
            Anchor_bB = grammar == GrammarEnum.ECMAScript,
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

            NoncapturingGroup = grammar == GrammarEnum.ECMAScript,
            PositiveLookahead = grammar == GrammarEnum.ECMAScript,
            NegativeLookahead = grammar == GrammarEnum.ECMAScript,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.None,
            NestedLookaround = true,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = grammar == GrammarEnum.basic || grammar == GrammarEnum.grep ? FeatureMatrix.BackrefEnum.OneDigit :
                          grammar == GrammarEnum.ECMAScript ? FeatureMatrix.BackrefEnum.Any : FeatureMatrix.BackrefEnum.None,
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
            Quantifier_Plus = grammar == GrammarEnum.extended ||
                                            grammar == GrammarEnum.ECMAScript ||
                                            grammar == GrammarEnum.egrep ||
                                            grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Question = grammar == GrammarEnum.extended ||
                                            grammar == GrammarEnum.ECMAScript ||
                                            grammar == GrammarEnum.egrep ||
                                            grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces = grammar == GrammarEnum.extended ||
                                            grammar == GrammarEnum.ECMAScript ||
                                            grammar == GrammarEnum.egrep ||
                                            grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                            :
                                            grammar == GrammarEnum.basic ||
                                            grammar == GrammarEnum.grep ? FeatureMatrix.PunctuationEnum.Backslashed
                                            : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = false,
            Quantifier_Lazy = grammar == GrammarEnum.ECMAScript,
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
            EmptySet = grammar == GrammarEnum.ECMAScript,
            EmptySetAny = grammar == GrammarEnum.ECMAScript,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = false,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None, // 'Accept' when 'polynomial' option is set; otherwise it hangs; however, this option disables back-references
            Σσς = false,
            ßSS = false,
        };
    }

    [GeneratedRegex( @"(?x)^\s* m \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseMatchRegex( );

    [GeneratedRegex( @"(?x)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
    private static partial Regex ParseGroupRegex( );
}
