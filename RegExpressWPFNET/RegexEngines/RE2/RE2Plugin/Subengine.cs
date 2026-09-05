using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;


namespace RE2Plugin;

class Subengine( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool posix_syntax, bool perl_classes, bool word_boundary), FeatureMatrix> LazyFeatureMatrix =
        new( d => BuildFeatureMatrix( d.posix_syntax, d.perl_classes, d.word_boundary ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.posix_syntax, options.perl_classes, options.word_boundary) );

        return new SyntaxOptions
        {
            Literal = options.literal,
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Int64? max_mem = ValidationUtilities.ParseInt64( "max_mem", options.max_mem );

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "m" );
            bw.Write( (byte)'b' );

            bw.Write( pattern );
            bw.Write( text );

            bw.Write( Convert.ToByte( options.posix_syntax ) );
            bw.Write( Convert.ToByte( options.longest_match ) );
            bw.Write( Convert.ToByte( options.literal ) );
            bw.Write( Convert.ToByte( options.never_nl ) );
            bw.Write( Convert.ToByte( options.dot_nl ) );
            bw.Write( Convert.ToByte( options.never_capture ) );
            bw.Write( Convert.ToByte( options.case_sensitive ) );
            bw.Write( Convert.ToByte( options.perl_classes ) );
            bw.Write( Convert.ToByte( options.word_boundary ) );
            bw.Write( Convert.ToByte( options.one_line ) );

            bw.Write( Enum.GetName( options.anchor )! );

            bw.WriteOptional( max_mem );

            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        var br = ph.BinaryReader;

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        SimpleMatch? current_match = null;

        if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response." );

        bool done = false;

        while( !done )
        {
            switch( br.ReadByte( ) )
            {
            case (byte)'m':
            {
                Int64 native_index = br.ReadInt64( );
                Int64 native_length = br.ReadInt64( );
                current_match = SimpleMatch.Create( (int)native_index, (int)native_length, stg );
                matches.Add( current_match );
            }
            break;
            case (byte)'g':
            {
                if( current_match == null ) throw new Exception( "Invalid response." );
                bool success = br.ReadByte( ) != 0;
                Int64 native_index = br.ReadInt64( );
                Int64 native_length = br.ReadInt64( );
                string name = br.ReadString( );

                if( !success )
                {
                    current_match.AddFailedGroup( name );
                }
                else
                {
                    current_match.AddSucceededGroup( (int)native_index, (int)native_length, name );
                }
            }
            break;
            case (byte)'e':
                done = true;
                break;
            default:
                throw new Exception( "Invalid response." );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"RE2Worker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( bool posix_syntax, bool perl_classes, bool word_boundary )
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

            Flags = !posix_syntax,
            ScopedFlags = !posix_syntax,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
            XXFlag = false,

            Literal_QE = !posix_syntax,
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
            Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = true,
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
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
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
            Class_Cbyte = !posix_syntax,
            Class_Ccp = false,
            Class_dD = !posix_syntax || perl_classes,
            Class_hHhexa = false,
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = false,
            Class_sS = !posix_syntax || perl_classes,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = !posix_syntax || perl_classes,
            Class_X = false,
            Class_pP = !posix_syntax,
            Class_pPBrace = !posix_syntax,

            InsideSets_Class_dD = !posix_syntax || perl_classes,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = !posix_syntax || perl_classes,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = !posix_syntax || perl_classes,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = !posix_syntax,
            InsideSets_Class_pPBrace = !posix_syntax,
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
            Anchor_A = !posix_syntax,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = !posix_syntax,
            Anchor_G = false,
            Anchor_bB = !posix_syntax || word_boundary,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
            NamedGroup_LtGt = !posix_syntax,
            NamedGroup_PLtGt = !posix_syntax,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = !posix_syntax,

            NoncapturingGroup = !posix_syntax,
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
            Quantifier_LowAbbrev = false,
            Quantifier_Lazy = !posix_syntax,
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

            EmptyConstruct = !posix_syntax,
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
}
