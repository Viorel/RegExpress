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


namespace ZigPlugin;

/*

Example of input:

{ "pattern": "(?<first>\\d)(\\d*)(?<last>QQQ)?", "text": "a1b23c456", "flags": { } }

Example of result:

{
"names": [
"first",
null,
"last"
],
"matches": [
{
  "start": 1,
  "length": 1,
  "groups": [
    {
      "value": "1"
    },
    {
      "value": ""
    },
    {
      "value": null
    }
  ]
},
{
  "start": 3,
  "length": 2,
  "groups": [
    {
      "value": "2"
    },
    {
      "value": "3"
    },
    {
      "value": null
    }
  ]
}
]
}

*/

class RelaxedJsonConverter : System.Text.Json.Serialization.JsonConverter<string>
{
    public class RootObject
    {
        public string[]? names { get; set; }
        public Match[]? matches { get; set; }
    }

    public class Match
    {
        public int start { get; set; }
        public int length { get; set; }
        public Group[]? groups { get; set; }
    }

    public class Group
    {
        public string? value { get; set; }
    }

    public override string? Read( ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options )
    {
        switch( reader.TokenType )
        {
        case JsonTokenType.String:
            try
            {
                return reader.GetString( )!;
            }
            catch( InvalidOperationException )
            {
                return Encoding.UTF8.GetString( reader.ValueSpan );

                // incomplete surrogate pairs are returned as lowercase hexadecimal codes preceded by "\\u"
            }

        case JsonTokenType.Null:
            return null;

        case JsonTokenType.StartArray:
        {
            StringBuilder sb = new( );

            for( reader.Read( ); reader.TokenType != JsonTokenType.EndArray; reader.Read( ) )
            {
                if( !reader.TryGetUInt16( out UInt16 value ) )
                {
                    throw new JsonException( $"Unexpected token: {reader.TokenType}. Number expected." );
                }

                sb.Append( (char)value );
            }

            return sb.ToString( );
        }

        default:
            throw new JsonException( $"Unexpected token: {reader.TokenType}." );
        }
    }

    public override void Write( Utf8JsonWriter writer, string value, JsonSerializerOptions options )
    {
        writer.WriteStringValue( value );
    }
}

class SubengineZigRegex( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.NoGroupIndex;
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

    class RootObject
    {
        public string[]? names { get; set; }
        public Match[]? matches { get; set; }
    }

    public class Match
    {
        public int start { get; set; }
        public int length { get; set; }
        public Group[]? groups { get; set; }
    }

    public class Group
    {
        public string? value { get; set; }
    }


    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Library == RegexLibraryEnum.ZigRegex );

        var json_object = new
        {
            pattern = pattern,
            text = text,
            flags = new
            {
                options.case_insensitive,
                options.multiline,
                options.dot_all,
                options.extended,
                options.unicode,
            }
        };

        string json = JsonSerializer.Serialize( json_object );

        using ProcessHelper ph = new( GetWorkerExePath( ) );

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
        RootObject? response = JsonSerializer.Deserialize<RootObject>( output, json_options );
#else
        RootObject? response = JsonSerializer.Deserialize<RootObject>( ph.OutputStream, json_options );
#endif

        if( response == null ) throw new Exception( "Null response" );

        List<IMatch> matches = [];
        SimpleTextGetter? stg = new( text );
        Utf8IndexConverter index_converter = new( text );

        foreach( var m in response.matches! )
        {
            SimpleMatch? match = null;

            int native_start = m.start;
            int native_length = m.length;
            int native_end = m.start + native_length;

            (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

            Debug.Assert( match == null );

            match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
            match.AddDefaultGroup( );

            for( int group_index = 0; group_index < m.groups!.Length; group_index++ )
            {
                string? value = m.groups[group_index].value;
                bool success = value != null;

                string? name = response.names?[group_index];
                name ??= ( group_index + 1 ).ToString( CultureInfo.InvariantCulture );

                if( !success )
                {
                    match.AddFailedGroup( name );
                }
                else
                {
                    match.AddSucceededNoDetailsGroup( name, value! );
                }
            }

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"ZigRegexWorker.bin" );

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

            Esc_a = false,
            Esc_b = false,
            Esc_e = false,
            Esc_f = false,
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
            GenericEscape = false,

            InsideSets_Esc_a = false,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = false,
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

            InsideSets_Class_dD = false,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = false,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = false,
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
            Anchor_z = true,
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
            NamedGroup_PLtGt = true,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = true,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.BoundedLength, // (has defects)
            NestedLookaround = true,
            AtomicGroup = false,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.OneDigit,
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
            Quantifier_LowAbbrev = true,
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

            Unicode_Class_Dot = false, // ('unicode' flag not yet implemented)
            Unicode_Class_vW = false,
            InsideSets_Unicode = false,
            UnicodeCaseFolding = false,
            KeepSurrogatePairs = false, // ('unicode' flag not yet implemented)
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Reject,
            Σσς = false,
            ßSS = false,
        };
    }
}
