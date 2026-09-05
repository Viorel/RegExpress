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
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;


namespace JavaScriptPlugin;

partial class SubengineWebView2( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool uFlag, bool vFlag), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix_V8( d.uFlag, d.vFlag ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.u, options.v) );

        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public class ResponseVersion
    {
        public string? Version { get; set; }
    }

    public class ResponseMatch
    {
        [JsonPropertyName( "g" )]
        public Dictionary<string, int[]>? Groups { get; set; }

        [JsonPropertyName( "i" )]
        public List<int[]>? Indices { get; set; }
    }

    public class ResponseMatches
    {
        public List<ResponseMatch>? Matches { get; set; }

        public string? Error { get; set; }
    }

    static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Runtime == RuntimeEnum.WebView2 );

        string flags = string.Concat(
            options.i ? "i" : "",
            options.m ? "m" : "",
            options.s ? "s" : "",
            options.u ? "u" : "",
            options.v ? "v" : "",
            options.y ? "y" : "",
            options.g ? "g" : "",
            options.Function == FunctionEnum.Exec ? "E" : ""
            );

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;
        ph.Arguments = ["b"];

        ph.BinaryWriter = bw =>
        {
            bw.Write( (byte)'b' );
            bw.Write( ToJavaScriptString( pattern ) );
            bw.Write( ToJavaScriptString( text ) );
            bw.Write( flags );
            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        using StreamReader sr = new( ph.OutputStream, Encoding.Unicode );
        string output_contents = sr.ReadToEnd( );

        ResponseMatches? client_response = JsonSerializer.Deserialize<ResponseMatches>( output_contents );

        if( client_response == null ) throw new Exception( "JavaScript failed." );
        if( !string.IsNullOrWhiteSpace( client_response.Error ) ) throw new Exception( client_response.Error );

        List<IMatch> matches = new( );
        SimpleTextGetter stg = new( text );

        foreach( var cm in client_response.Matches! )
        {
            if( cm.Indices!.Any( ) )
            {
                int native_start = cm.Indices![0][0];
                int native_end = cm.Indices[0][1];
                int native_length = native_end - native_start;

                SimpleMatch sm = SimpleMatch.Create( native_start, native_length, stg );

                sm.AddDefaultGroup( );

                HashSet<string> used_names = [];

                for( int i = 1; i < cm.Indices.Count; ++i )
                {
                    // figure out the name
                    string? n = cm.Groups?.FirstOrDefault( g => cm.Indices[i] != null && (g.Value[0], g.Value[1]) == (cm.Indices[i][0], cm.Indices[i][1]) && !used_names.Contains( g.Key ) ).Key;
                    n ??= cm.Groups?.FirstOrDefault( g => cm.Indices[i] != null && (g.Value[0], g.Value[1]) == (cm.Indices[i][0], cm.Indices[i][1]) ).Key;

                    string name;

                    if( n != null )
                    {
                        name = n;
                        used_names.Add( n );
                    }
                    else
                    {
                        name = i.ToString( CultureInfo.InvariantCulture );
                    }

                    int[] g = cm.Indices[i];

                    if( g == null )
                    {
                        sm.AddFailedGroup( name );
                    }
                    else
                    {
                        native_start = cm.Indices[i][0];
                        native_end = cm.Indices[i][1];
                        native_length = native_end - native_start;

                        sm.AddSucceededGroup( native_start, native_length, name );
                    }
                }

                matches.Add( sm );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    public static string? GetVersion( ICancellable cnc )
    {
        try
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;
            ph.Arguments = ["v"];

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            using StreamReader sr = new( ph.OutputStream, Encoding.Unicode );
            string output_contents = sr.ReadToEnd( );

            ResponseVersion? v = JsonSerializer.Deserialize<ResponseVersion>( output_contents );

            string? version = v!.Version;

            // keep up to three components

            if( version != null )
            {
                var m = SimplifyVersionRegex( ).Match( version );
                if( m.Success )
                {
                    version = m.Groups["v"].Value;
                }
            }

            return version;
        }
        catch( Exception exc )
        {
            _ = exc;
            if( Debugger.IsAttached ) Debugger.Break( );

            return null;
        }
    }


    static string ToJavaScriptString( string text )
    {
        return Encoding.UTF8.GetString( JsonEncodedText.Encode( text ).EncodedUtf8Bytes );
    }


    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"WebView2Worker.bin" );

        return worker_exe;
    }

    internal static void StartGetVersion( Action<string?> setVersion )
    {
        if( LazyVersion.IsValueCreated )
        {
            setVersion( LazyVersion.Value );

            return;
        }

        Thread t = new( ( ) =>
        {
            setVersion( LazyVersion.Value );
        } )
        {
            IsBackground = true
        };

        t.Start( );
    }


    static FeatureMatrix BuildFeatureMatrix_V8( bool uFlag, bool vFlag )
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
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
            XXFlag = false,

            Literal_QE = false,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = vFlag,

            Esc_a = false,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = true,
            Esc_Octal = !uFlag && !vFlag ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = false,
            Esc_u4 = true,
            Esc_U8 = false,
            Esc_uBrace = uFlag || vFlag,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = !uFlag && !vFlag,

            InsideSets_Esc_a = false,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = !uFlag && !vFlag ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = false,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = uFlag || vFlag,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = !uFlag && !vFlag,

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
            Class_pPBrace = uFlag || vFlag,

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
            InsideSets_Class_pPBrace = uFlag || vFlag,
            InsideSets_Class_Name = false,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = vFlag,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = vFlag,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = vFlag,
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
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = false,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = true,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NestedLookaround = true,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.Any,
            Backref_kApos = false,
            Backref_kLtGt = true,
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
            EmptySet = true,
            EmptySetAny = true,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = false,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = uFlag || vFlag,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
            Σσς = true,
            ßSS = false,
        };
    }

    [GeneratedRegex( @"^(?<v>\d+([.]\d+([.]\d+)?)?)([.]\d+)*$", RegexOptions.ExplicitCapture )]
    private static partial Regex SimplifyVersionRegex( );
}
