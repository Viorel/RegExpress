using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;


namespace TREPlugin;

class Subengine( Options options ) : RegexSubengine
{
    static readonly LazyData<bool /* isExtended */, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( options.REG_EXTENDED );

        return new SyntaxOptions
        {
            Literal = options.REG_LITERAL,
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "m" );
            bw.Write( (byte)'b' );

            bw.Write( pattern );
            bw.Write( text );

            bw.Write( Convert.ToByte( options.REG_EXTENDED ) );
            bw.Write( Convert.ToByte( options.REG_ICASE ) );
            bw.Write( Convert.ToByte( options.REG_NOSUB ) );
            bw.Write( Convert.ToByte( options.REG_NEWLINE ) );
            bw.Write( Convert.ToByte( options.REG_LITERAL ) );
            bw.Write( Convert.ToByte( options.REG_RIGHT_ASSOC ) );
            bw.Write( Convert.ToByte( options.REG_UNGREEDY ) );

            bw.Write( Convert.ToByte( options.REG_NOTBOL ) );
            bw.Write( Convert.ToByte( options.REG_NOTEOL ) );

            bw.Write( Convert.ToByte( options.MatchAll ) );

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
                Int32 native_start = br.ReadInt32( );
                Int32 native_end = br.ReadInt32( );
                int native_length = native_end - native_start;
                current_match = SimpleMatch.Create( (int)native_start, (int)native_length, stg );
                current_match.AddDefaultGroup( );
                matches.Add( current_match );
            }
            break;
            case (byte)'g':
            {
                if( current_match == null ) throw new Exception( "Invalid response." );
                Int32 native_start = br.ReadInt32( );
                Int32 native_end = br.ReadInt32( );
                int native_length = native_end - native_start;
                bool success = native_start >= 0;
                string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );
                if( !success )
                {
                    current_match.AddFailedGroup( name );
                }
                else
                {
                    current_match.AddSucceededGroup( native_start, native_length, name );
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
        string worker_exe = Path.Combine( assembly_dir, @"TREWorker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( bool isExtended )
    {
        return new FeatureMatrix
        {
            Parentheses = isExtended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = isExtended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            AlternationOnSeparateLines = false,

            InlineComments = isExtended,
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = isExtended,
            ScopedFlags = isExtended,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
            XXFlag = false,

            Literal_QE = true,
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
            GenericEscape = true,

            InsideSets_Esc_a = false,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = false,
            InsideSets_Esc_n = false,
            InsideSets_Esc_r = false,
            InsideSets_Esc_t = false,
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
            Anchor_LtGt = true,
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
            Quantifier_Plus = isExtended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Question = isExtended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces = isExtended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,
            Quantifier_Braces_FreeForm = isExtended ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = true,
            Quantifier_Lazy = true, // (see also Options.REG_UNGREEDY)
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

            EmptyConstruct = isExtended,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = false,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = false,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = false,
            ßSS = false,
        };
    }
}
