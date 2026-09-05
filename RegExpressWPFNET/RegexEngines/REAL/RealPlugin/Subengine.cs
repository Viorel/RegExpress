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


namespace RealPlugin;

partial class Subengine( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool ecma, bool ascii), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix( d.ecma, d.ascii ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.ecma, options.ascii) );

        return new SyntaxOptions
        {
            XLevel = options.verbose ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.InputEncoding = EncodingEnum.Unicode; //
        ph.OutputEncoding = EncodingEnum.Unicode; //
        ph.ErrorEncoding = EncodingEnum.ASCII;

        ph.BinaryWriter = bw =>
        {
            bw.Write( (byte)'b' );

            bw.Write( pattern );
            bw.Write( text );
            bw.Write( options.icase );
            bw.Write( options.multiline );
            bw.Write( options.dotall );
            //bw.Write( options.bytes ); // not supported here
            bw.Write( options.verbose );
            bw.Write( options.ecma );
            bw.Write( options.ascii );
            bw.Write( options.dollar_endonly );
            bw.Write( options.allow_raw_byte ); //
            bw.Write( options.ungreedy );
            bw.Write( options.longest );

            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

        var br = ph.BinaryReader;

        if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response [1]." );

        // read names

        Dictionary<string, int> names = [];

        for(; ; )
        {
            char b = (char)br.ReadByte( );
            if( b == '-' ) break;

            switch( b )
            {
            case 'n':
            {
                string name = br.ReadString( );
                int group_index = checked((int)br.ReadUInt64( ));
                names.Add( name, group_index );
            }
            break;
            default:
                throw new Exception( "Invalid response [2]." );
            }
        }

        // read matches

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        Utf8IndexConverter index_converter = new( text );
        SimpleMatch? current_match = null;

        for(; ; )
        {
            char b = (char)br.ReadByte( );
            if( b == 'e' ) break;

            switch( b )
            {
            case 'm':
            {
                int native_start = checked((int)br.ReadUInt64( )); // (UTF-8 index)
                int native_end = checked((int)br.ReadUInt64( )); // (UTF-8 index)
                int native_length = native_end - native_start;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                current_match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );

                current_match.AddDefaultGroup( );

                matches.Add( current_match );
            }
            break;
            case 'g':
            {
                if( current_match == null ) throw new Exception( "Invalid response [3]." );

                int group_index = current_match.Groups.Count( );

                UInt64 native_start = br.ReadUInt64( ); // (UTF-8 index)
                UInt64 native_end = br.ReadUInt64( ); // (UTF-8 index)
                UInt64 native_length = native_end - native_start;

                bool success = native_start < UInt64.MaxValue;

                string? name = names.Where( p => p.Value == group_index ).Select( p => p.Key ).FirstOrDefault( );
                name ??= group_index.ToString( CultureInfo.InvariantCulture );

                if( !success )
                {
                    current_match.AddFailedGroup( name );
                }
                else
                {
                    (int char_start, int char_length) = index_converter.Convert( (int)native_start, (int)native_end );

                    current_match.AddSucceededGroup( (int)native_start, (int)native_length, char_start, char_length, name );
                }
            }
            break;
            default:
                throw new Exception( "Invalid response [4]." );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    private static string? AdjustErrorMessage( string error, string pattern )
    {
        // try to show character offset based on byte offset, which is used by REAL in error messages;
        // example of error message: "regex_error at 3: ..."

        Match m = RegexExtractByteOffset( ).Match( error );

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
        string worker_exe = Path.Combine( assembly_dir, @"RealWorker.bin" );

        return worker_exe;
    }

    private static FeatureMatrix BuildFeatureMatrix( bool ecma, bool ascii )
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
            Esc_xBrace = true,
            Esc_u4 = true,
            Esc_U8 = true,
            Esc_uBrace = true,
            Esc_UBrace = false,
            Esc_c1 = false,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = true,
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
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = true,
            InsideSets_Esc_uBrace = true,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = false,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = true,
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
            Class_pP = true,
            Class_pPBrace = true,

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
            InsideSets_Class_pP = true,
            InsideSets_Class_pPBrace = true,
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
            Anchor_Z = ecma ? FeatureMatrix.AnchorZModeEnum.None : FeatureMatrix.AnchorZModeEnum.Compatible,
            Anchor_z = true,
            Anchor_G = false,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = true,
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
            PositiveLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NestedLookaround = false,
            AtomicGroup = true,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.None,
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

            EmptyConstruct = false,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = !ascii,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = !ascii,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = !ascii,
            ßSS = false,
        };
    }

    [GeneratedRegex( @"^regex_error at (\d+): " )]
    private static partial Regex RegexExtractByteOffset( );
}
