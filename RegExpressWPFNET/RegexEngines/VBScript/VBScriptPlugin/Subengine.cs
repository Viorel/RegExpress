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
using System.Text;
using System.Text.RegularExpressions;


namespace VBScriptPlugin;

partial class Subengine( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new Lazy<FeatureMatrix>( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.NoGroupIndex | RegexEngineCapabilityEnum.NoGroupSuccessFlag;
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
        string options_str = "";
        if( options.IgnoreCase ) options_str += "i";
        if( options.Multiline ) options_str += "m";
        if( options.Global ) options_str += "g";

        using ProcessHelper ph = new ProcessHelper( "cscript.exe" );

        ph.AllEncoding = EncodingEnum.ASCII;
        ph.Arguments = new[] { "/nologo", GetWorkerPath( ), "x" };

        ph.StreamWriter = sw =>
        {
            sw.Write( ToArg( pattern ) );
            sw.Write( "\u001F" );
            sw.Write( ToArg( text ) );
            sw.Write( "\u001F" );
            sw.Write( options_str );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) )
        {
            // Possible:
            //  <path>\VBScriptWorker.vbs(31, 1) Microsoft VBScript runtime error: <text of error>
            //  <path>\VBScriptWorker.vbs(50, 9) Microsoft VBScript compilation error: <text of error>


            var m = ErrorRegex( ).Match( ph.Error );
            if( m.Success )
            {
                throw new Exception( m.Groups["err"].Value );
            }

            throw new Exception( ph.Error );
        }

        List<SimpleMatch> matches = [];
        SimpleTextGetter stg = new( text );
        SimpleMatch? current_match = null;

        string? line;

        while( ( line = ph.StreamReader.ReadLine( ) ) != null )
        {
            var m = MatchRegex( ).Match( line );
            if( m.Success )
            {
                current_match = SimpleMatch.Create( int.Parse( m.Groups[1].Value ), int.Parse( m.Groups[2].Value ), stg );

                current_match.AddDefaultGroup( );

                matches.Add( current_match );
            }
            else
            {
                var sm = SubmatchRegex( ).Match( line );
                if( sm.Success )
                {
                    if( current_match == null ) throw new InvalidOperationException( );

                    string value = sm.Groups[1].Value;

                    Debug.Assert( value.StartsWith( '"' ) );
                    Debug.Assert( value.EndsWith( '"' ) );

                    value = value[1..^1];

                    value = UCodeRegex( ).Replace( value, m => ( (char)Convert.ToUInt16( m.Groups[1].Value, 16 ) ).ToString( ) );

                    current_match.AddSucceededNoDetailsGroup( current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ), value );
                }
                else
                {
#if DEBUG
                    throw new Exception( $"Bad response: '{line}'." );
#else
                    throw new Exception( "Bad response." );
#endif
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }


    public static string? GetVersion( ICancellable cnc )
    {
        using ProcessHelper ph = new( "cscript.exe" );

        ph.AllEncoding = EncodingEnum.ASCII;
        ph.Arguments = new[] { "/nologo", GetWorkerPath( ), "v" };

        if( !ph.Start( cnc ) ) return null;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        string version_s = ph.StreamReader.ReadToEnd( ).Trim( );

        return version_s;
    }


    static string ToArg( string text )
    {
        StringBuilder sb = new( );
        bool is_open_segment = false;

        foreach( char c in text )
        {
            if( char.IsAsciiLetterOrDigit( c ) )
            {
                if( is_open_segment )
                {
                    sb.Append( c );
                }
                else
                {
                    if( sb.Length == 0 )
                    {
                        sb.Append( '\'' ).Append( c );
                    }
                    else
                    {
                        sb.Append( "&'" ).Append( c );
                    }

                    is_open_segment = true;
                }
            }
            else
            {
                if( is_open_segment )
                {
                    sb.Append( '\'' );
                }

                if( sb.Length != 0 )
                {
                    sb.Append( '&' );
                }

                sb.Append( "ChrW(&H" ).Append( ( (uint)c ).ToString( "X" ) ).Append( ')' );

                is_open_segment = false;
            }
        }

        if( is_open_segment )
        {
            sb.Append( '\'' );
        }

        string r = sb.ToString( );

        if( r.Length == 0 ) r = "''";

        return r;
    }


    static string GetWorkerPath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_path = Path.Combine( assembly_dir, @"VBScriptWorker.vbs" );

        return worker_path;
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

            Esc_a = false,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = true,
            Esc_Octal = FeatureMatrix.OctalEnum.Octal_1_3,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = false,
            Esc_u4 = true,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = true,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = true,

            InsideSets_Esc_a = false,
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
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = true,
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
            Anchor_A = false,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = false,
            Anchor_G = false,
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
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.None,
            NestedLookaround = true,
            AtomicGroup = false,
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
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = false,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
            Σσς = false,
            ßSS = false,
        };
    }

    [GeneratedRegex( @"\\VBScriptWorker\.vbs\s*\(\d+,\s*\d+\)\s*(?<err>Microsoft VBScript (.*?)?error:.*)" )]
    private static partial Regex ErrorRegex( );

    [GeneratedRegex( @"^m\s+(\d+)\s+(\d+)" )]
    private static partial Regex MatchRegex( );

    [GeneratedRegex( @"^s\s+("".*"")" )]
    private static partial Regex SubmatchRegex( );

    [GeneratedRegex( @"\\u([0-9A-Fa-f]{4})" )]
    private static partial Regex UCodeRegex( );
}
