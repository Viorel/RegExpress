using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text.Json;


namespace DartPlugin;

class SubengineRegExp( Options options ) : RegexSubengine
{
    static readonly LazyData<bool /*isUnicode*/, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( options.unicode );

        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public class RootObject
    {
        public Match[]? Matches { get; set; }
    }

    public class Match
    {
        public int s { get; set; }
        public int e { get; set; }
        public string?[]? g { get; set; }
        public Ng[]? ng { get; set; }
    }

    public class Ng
    {
        public string? n { get; set; }
        public string? v { get; set; }
    }

    public class RelaxedJsonConverter : System.Text.Json.Serialization.JsonConverter<string>
    {
        public override string Read( ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options )
        {
            try
            {
                return reader.GetString( )!;
            }
            catch( InvalidOperationException )
            {
                return System.Text.Encoding.UTF8.GetString( reader.ValueSpan );

                // incomplete surrogate pairs are returned as lowercase hexadecimal codes preceded by "\\u"
            }
        }

        public override void Write( Utf8JsonWriter writer, string value, JsonSerializerOptions options )
        {
            writer.WriteStringValue( value );
        }
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.package == PackageEnum.RegExp );

        var data = new
        {
            pattern,
            text,
            options = new
            {
                options.multiLine,
                options.caseSensitive,
                options.unicode,
                options.dotAll,
            }
        };
        string json = JsonSerializer.Serialize( data );

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.StreamWriter = sw =>
        {
            sw.Write( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        JsonSerializerOptions json_options = new( )
        {
            Converters = { new RelaxedJsonConverter( ) },
        };

#if DEBUG
        using StreamReader sr = new( ph.OutputStream );
        string output = sr.ReadToEnd( );
        RootObject? root_object = JsonSerializer.Deserialize<RootObject>( output, json_options );
#else
        RootObject? root_object = JsonSerializer.Deserialize<RootObject>( ph.OutputStream, json_options );
#endif

        if( root_object == null ) throw new Exception( "Invalid response." );

        List<IMatch> matches = [];

        if( root_object.Matches != null )
        {
            SimpleTextGetter stg = new( text );

            foreach( Match m in root_object.Matches )
            {
                SimpleMatch match;

                {
                    int native_start = m.s;
                    int native_end = m.e;
                    Debug.Assert( native_end >= native_start );
                    int native_length = native_end - native_start;

                    match = SimpleMatch.Create( native_start, native_length, stg );

                    match.AddDefaultGroup( );
                }

                {
                    // unnamed groups

                    for( int i = 0; i < m.g!.Length; ++i )
                    {
                        int group_index = i + 1;
                        string? name = null; // try to determine the name?
                        if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                        var g = m.g[i];
                        bool success = g != null;

                        if( !success )
                        {
                            match.AddFailedGroup( name );
                        }
                        else
                        {
                            // no details

                            match.AddSucceededNoDetailsGroup( name, g! );
                        }
                    }
                }

                {
                    // named groups (also presented in unnamed groups)

                    for( int i = 0; i < m.ng!.Length; ++i )
                    {
                        var ng = m.ng[i];

                        int group_index = i + 1;
                        string name = ng.n!;

                        bool success = ng.v != null;

                        if( !success )
                        {
                            match.AddFailedGroup( name );
                        }
                        else
                        {
                            // no details

                            match.AddSucceededNoDetailsGroup( name, ng.v! );

                        }
                    }
                }

                matches.Add( match );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, "regexpworker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( bool isUnicode )
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
            InsideSets_Literal_qBrace = false,

            Esc_a = false,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = true,
            Esc_Octal = isUnicode ? FeatureMatrix.OctalEnum.None : FeatureMatrix.OctalEnum.Octal_1_3,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = false,
            Esc_u4 = true,
            Esc_U8 = false,
            Esc_uBrace = isUnicode,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = !isUnicode,

            InsideSets_Esc_a = false,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = isUnicode ? FeatureMatrix.OctalEnum.None : FeatureMatrix.OctalEnum.Octal_1_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = false,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = isUnicode,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = !isUnicode,

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
            Class_pPBrace = isUnicode,

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
            InsideSets_Class_pPBrace = isUnicode,
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
            EmptySet = true,
            EmptySetAny = true,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = false,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = isUnicode,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
            Σσς = true,
            ßSS = false,
        };
    }
}
