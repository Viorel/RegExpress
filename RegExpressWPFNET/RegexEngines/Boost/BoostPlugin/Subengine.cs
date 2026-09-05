using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;


namespace BoostPlugin;

partial class Subengine( Options options ) : RegexSubengine
{
    static readonly LazyData<GrammarEnum, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return options.Grammar switch
        {
            GrammarEnum.normal => RegexEngineCapabilityEnum.HasCaptures,
            GrammarEnum.ECMAScript => RegexEngineCapabilityEnum.HasCaptures,
            GrammarEnum.JavaScript => RegexEngineCapabilityEnum.HasCaptures,
            GrammarEnum.JScript => RegexEngineCapabilityEnum.HasCaptures,
            GrammarEnum.perl => RegexEngineCapabilityEnum.HasCaptures,
            GrammarEnum.extended => RegexEngineCapabilityEnum.None,
            GrammarEnum.egrep => RegexEngineCapabilityEnum.None,
            GrammarEnum.awk => RegexEngineCapabilityEnum.None,
            GrammarEnum.basic => RegexEngineCapabilityEnum.None,
            GrammarEnum.sed => RegexEngineCapabilityEnum.None,
            GrammarEnum.grep => RegexEngineCapabilityEnum.None,
            GrammarEnum.emacs => RegexEngineCapabilityEnum.HasCaptures,
            GrammarEnum.literal => RegexEngineCapabilityEnum.NoGroups | RegexEngineCapabilityEnum.NoGroupIndex | RegexEngineCapabilityEnum.NoGroupSuccessFlag,
            _ => throw new NotImplementedException( ),
        };
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( options.Grammar );

        return new SyntaxOptions
        {
            Literal = options.Grammar == GrammarEnum.literal,
            XLevel = options.mod_x ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        // try identifying group names

        var regex_group_names = FindGroupsRegex( );

        string[] possible_group_names =
            regex_group_names
                .Matches( pattern )
                .Select( m => m.Groups["n"] )
                .Where( g => g.Success )
                .Select( g => g.Value )
                .ToArray( );


        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "m" );
            bw.Write( (byte)'b' );

            bw.Write( pattern );
            bw.Write( text );

            bw.Write( Enum.GetName( options.Grammar )! );

            // Syntax options

            bw.Write( Convert.ToByte( options.icase ) );
            bw.Write( Convert.ToByte( options.nosubs ) );
            bw.Write( Convert.ToByte( options.optimize ) );
            bw.Write( Convert.ToByte( options.collate ) );
            bw.Write( Convert.ToByte( options.no_except ) );
            bw.Write( Convert.ToByte( options.no_mod_m ) );
            bw.Write( Convert.ToByte( options.no_mod_s ) );
            bw.Write( Convert.ToByte( options.mod_s ) );
            bw.Write( Convert.ToByte( options.mod_x ) );
            bw.Write( Convert.ToByte( options.no_empty_expressions ) );

            // Match options

            bw.Write( Convert.ToByte( options.match_not_bob ) );
            bw.Write( Convert.ToByte( options.match_not_eob ) );
            bw.Write( Convert.ToByte( options.match_not_bol ) );
            bw.Write( Convert.ToByte( options.match_not_eol ) );
            bw.Write( Convert.ToByte( options.match_not_bow ) );
            bw.Write( Convert.ToByte( options.match_not_eow ) );
            bw.Write( Convert.ToByte( options.match_any ) );
            bw.Write( Convert.ToByte( options.match_not_null ) );
            bw.Write( Convert.ToByte( options.match_continuous ) );
            bw.Write( Convert.ToByte( options.match_partial ) );
            bw.Write( Convert.ToByte( options.match_extra ) );
            bw.Write( Convert.ToByte( options.match_single_line ) );
            bw.Write( Convert.ToByte( options.match_prev_avail ) );
            bw.Write( Convert.ToByte( options.match_not_dot_newline ) );
            bw.Write( Convert.ToByte( options.match_not_dot_null ) );
            bw.Write( Convert.ToByte( options.match_posix ) );
            bw.Write( Convert.ToByte( options.match_perl ) );
            bw.Write( Convert.ToByte( options.match_nosubs ) );

            bw.Write( Convert.ToInt16( possible_group_names.Length ) );
            foreach( var n in possible_group_names ) bw.Write( n );

            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        var br = ph.BinaryReader;

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        SimpleMatch? current_match = null;
        SimpleGroup? current_group = null;

        if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response [1]." );

        bool done = false;

        while( !done )
        {
            switch( br.ReadByte( ) )
            {
            case (byte)'m':
            {
                Int32 native_index = br.ReadInt32( );
                Int32 native_length = br.ReadInt32( );
                current_match = SimpleMatch.Create( native_index, native_length, stg );
                matches.Add( current_match );
                current_group = null;
            }
            break;
            case (byte)'g':
            {
                if( current_match == null ) throw new Exception( "Invalid response [2]." );
                bool success = br.ReadByte( ) != 0;
                Int32 native_index = br.ReadInt32( );
                Int32 native_length = br.ReadInt32( );
                string name = br.ReadString( );

                if( !success )
                {
                    current_match.AddFailedGroup( name );
                    current_group = null;
                }
                else
                {
                    current_group = current_match.AddSucceededGroup( native_index, native_length, name );
                }
            }
            break;
            case (byte)'c':
            {
                if( current_group == null ) throw new Exception( "Invalid response [3]." );
                Int32 native_index = br.ReadInt32( );
                Int32 native_length = br.ReadInt32( );
                current_group.AddCapture( native_index, native_length );
            }
            break;
            case (byte)'e':
                done = true;
                break;
            default:
                throw new Exception( "Invalid response [4]." );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"BoostWorker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( GrammarEnum grammar )
    {
        bool is_perl =
            grammar == GrammarEnum.perl ||
            grammar == GrammarEnum.ECMAScript ||
            grammar == GrammarEnum.normal ||
            grammar == GrammarEnum.JavaScript ||
            grammar == GrammarEnum.JScript;

        bool is_POSIX_extended =
            grammar == GrammarEnum.extended ||
            grammar == GrammarEnum.egrep ||
            grammar == GrammarEnum.awk;

        bool is_POSIX_basic =
            grammar == GrammarEnum.basic ||
            grammar == GrammarEnum.sed ||
            grammar == GrammarEnum.grep ||
            grammar == GrammarEnum.emacs;

        bool is_awk =
            grammar == GrammarEnum.awk;

        bool is_emacs =
            grammar == GrammarEnum.emacs;

        return new FeatureMatrix
        {
            Parentheses = is_perl || is_POSIX_extended ? FeatureMatrix.PunctuationEnum.Normal : is_POSIX_basic ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = is_perl || is_POSIX_extended ? FeatureMatrix.PunctuationEnum.Normal :
                            is_emacs ? FeatureMatrix.PunctuationEnum.Backslashed :
                            FeatureMatrix.PunctuationEnum.None,
            AlternationOnSeparateLines = grammar == GrammarEnum.grep || grammar == GrammarEnum.egrep,

            InlineComments = is_perl || is_emacs, // using \(?# \) in emacs
            XModeComments = is_perl,
            InsideSets_XModeComments = false,

            Flags = is_perl,
            ScopedFlags = is_perl,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = is_perl,
            XXFlag = false,

            Literal_QE = is_perl || is_POSIX_extended,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = false,

            Esc_a = is_perl || is_POSIX_extended,
            Esc_b = false,
            Esc_e = is_perl || is_POSIX_extended,
            Esc_f = is_perl || is_POSIX_extended,
            Esc_n = is_perl || is_POSIX_extended,
            Esc_r = is_perl || is_POSIX_extended,
            Esc_t = is_perl || is_POSIX_extended,
            Esc_v = is_POSIX_extended,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = is_perl || is_POSIX_extended || is_POSIX_basic,
            Esc_oBrace = false,
            Esc_x2 = is_perl || is_POSIX_extended,
            Esc_xBrace = is_perl || is_POSIX_extended,
            Esc_u4 = false,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = is_perl || is_POSIX_extended,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = is_perl || is_POSIX_extended,
            GenericEscape = true,

            InsideSets_Esc_a = is_perl || is_awk || is_emacs,
            InsideSets_Esc_b = is_perl || is_awk || is_emacs,
            InsideSets_Esc_e = is_perl || is_awk || is_emacs,
            InsideSets_Esc_f = is_perl || is_awk || is_emacs,
            InsideSets_Esc_n = is_perl || is_awk || is_emacs,
            InsideSets_Esc_r = is_perl || is_awk || is_emacs,
            InsideSets_Esc_t = is_perl || is_awk || is_emacs,
            InsideSets_Esc_v = is_perl || is_awk || is_emacs,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = is_perl || is_awk || is_emacs,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = is_perl || is_awk || is_emacs,
            InsideSets_Esc_xBrace = is_perl || is_awk || is_emacs,
            InsideSets_Esc_u4 = false,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = is_perl || is_awk || is_emacs,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = is_perl || is_awk || is_emacs,
            InsideSets_GenericEscape = is_perl || is_awk || is_emacs,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = is_perl || is_POSIX_extended,
            Class_dD = is_perl || is_POSIX_extended,
            Class_hHhexa = false,
            Class_hHhorspace = is_perl || is_POSIX_extended,
            Class_lL = is_perl || is_POSIX_extended,
            Class_N = false,
            Class_O = false,
            Class_R = is_perl,
            Class_sS = is_perl || is_POSIX_extended,
            Class_sSx = is_emacs,
            Class_uU = is_perl || is_POSIX_extended,
            Class_vV = is_perl,
            Class_wW = is_perl || is_POSIX_extended || is_emacs,
            Class_X = is_perl || is_POSIX_extended,
            Class_pP = is_perl || is_POSIX_extended,
            Class_pPBrace = is_perl || is_POSIX_extended,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = true,
            InsideSets_Class_lL = true,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = true,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = true,
            InsideSets_Class_vV = false, // TODO: it seems that [\V] works as NOT Esc_v? 
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = false,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = true,
            InsideSets_Collating = true,

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
            Anchor_A = is_perl || is_POSIX_extended || is_emacs,
            Anchor_Z = is_perl || is_POSIX_extended ? FeatureMatrix.AnchorZModeEnum.Correct : FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = is_perl || is_POSIX_extended || is_emacs,
            Anchor_G = is_perl || is_POSIX_extended,
            Anchor_bB = is_perl || is_POSIX_extended || is_emacs,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = true,
            Anchor_K = is_perl,
            Anchor_mM = false,
            Anchor_LtGt = is_perl || is_POSIX_extended || is_emacs,
            Anchor_GraveApos = is_perl || is_POSIX_extended || is_emacs,
            Anchor_yY = false,

            NamedGroup_Apos = is_perl || is_emacs,
            NamedGroup_LtGt = is_perl || is_emacs,
            NamedGroup_PLtGt = false,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = is_perl,

            NoncapturingGroup = is_perl || is_emacs,
            PositiveLookahead = is_perl || is_emacs,
            NegativeLookahead = is_perl || is_emacs,
            PositiveLookbehind = is_perl || is_emacs ? FeatureMatrix.LookModeEnum.FixedLength : FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = is_perl || is_emacs ? FeatureMatrix.LookModeEnum.FixedLength : FeatureMatrix.LookModeEnum.None,
            NestedLookaround = true,
            AtomicGroup = is_perl || is_emacs,
            BranchReset = is_perl || is_emacs,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = is_perl || is_POSIX_basic ? FeatureMatrix.BackrefEnum.OneDigit : FeatureMatrix.BackrefEnum.None,
            Backref_kApos = is_perl,
            Backref_kLtGt = is_perl,
            Backref_kBrace = is_perl,
            Backref_kNum = is_perl,
            Backref_kNegNum = is_perl,
            Backref_gApos = is_perl ? FeatureMatrix.BackrefModeEnum.Value : FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = is_perl ? FeatureMatrix.BackrefModeEnum.Value : FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = is_perl ? FeatureMatrix.BackrefModeEnum.Value : FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = is_perl ? FeatureMatrix.BackrefModeEnum.Value : FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = is_perl ? FeatureMatrix.BackrefModeEnum.Value : FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = false,
            AllowSpacesInBackref = false,

            Recursive_Num = is_perl,
            Recursive_PlusMinusNum = is_perl || is_emacs, // TODO: '-1' works, '+1' does not work for emacs
            Recursive_R = is_perl,
            Recursive_Name = is_perl,
            Recursive_PGtName = false,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = true,
            Quantifier_Plus = is_perl || is_POSIX_extended || is_emacs ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Question = is_perl || is_POSIX_extended || is_emacs ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces = is_perl || is_POSIX_extended ? FeatureMatrix.PunctuationEnum.Normal :
                                is_POSIX_basic ? FeatureMatrix.PunctuationEnum.Backslashed :
                                FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.Both,
            Quantifier_LowAbbrev = false,
            Quantifier_Lazy = is_perl || is_emacs,
            Quantifier_Possessive = is_perl,

            Conditional_BackrefByNumber = is_perl,
            Conditional_BackrefByName = false,
            Conditional_Pattern = is_perl,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = is_perl,
            Conditional_BackrefByName_LtGt = is_perl,
            Conditional_R = is_perl,
            Conditional_RName = is_perl,
            Conditional_DEFINE = is_perl,
            Conditional_VERSION = false,

            ControlVerbs = is_perl,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = false,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = is_perl || is_POSIX_extended || is_emacs,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Reject,
            Σσς = false,
            ßSS = false,
        };
    }

    [GeneratedRegex( "\\(\\? ((?'a'')|<) (?'n'.*?) (?(a)'|>)", RegexOptions.ExplicitCapture | RegexOptions.IgnorePatternWhitespace )]
    private static partial Regex FindGroupsRegex( );
}
