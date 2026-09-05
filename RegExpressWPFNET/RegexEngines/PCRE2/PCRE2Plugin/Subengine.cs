using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;


namespace PCRE2Plugin;

class Subengine( Options options ) : RegexSubengine
{
    readonly LazyData<(bool PCRE2_ALLOW_EMPTY_CLASS, bool PCRE2_ALT_BSUX, bool PCRE2_EXTRA_ALT_BSUX, bool PCRE2_ALT_EXTENDED_CLASS, bool PCRE2_DUPNAMES, bool PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL), FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        bool is_literal = options.PCRE2_LITERAL;
        bool is_extended = options.PCRE2_EXTENDED;
        bool is_extended_more = options.PCRE2_EXTENDED_MORE;
        bool allow_empty_set = options.PCRE2_ALLOW_EMPTY_CLASS;
        bool bad_escape_is_literal = options.PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL;
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (PCRE2_ALLOW_EMPTY_CLASS: allow_empty_set, PCRE2_ALT_BSUX: options.PCRE2_ALT_BSUX, PCRE2_EXTRA_ALT_BSUX: options.PCRE2_EXTRA_ALT_BSUX, PCRE2_ALT_EXTENDED_CLASS: options.PCRE2_ALT_EXTENDED_CLASS, PCRE2_DUPNAMES: true, PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL: bad_escape_is_literal) );

        return new SyntaxOptions
        {
            Literal = is_literal,
            XLevel = is_extended_more ? XLevelEnum.xx : is_extended ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        UInt32? depth_limit = ValidationUtilities.ParseUInt32( "depth_limit", options.DepthLimit );
        UInt32? heap_limit = ValidationUtilities.ParseUInt32( "heap_limit", options.HeapLimit );
        UInt32? match_limit = ValidationUtilities.ParseUInt32( "match_limit", options.MatchLimit );
        UInt64? max_pattern_compiled_length = ValidationUtilities.ParseUInt64( "max_pattern_compiled_length", options.MaxPatternCompiledLength );
        UInt64? offset_limit = ValidationUtilities.ParseUInt64( "offset_limit", options.OffsetLimit );
        UInt32? parens_nest_limit = ValidationUtilities.ParseUInt32( "parens_nest_limit", options.ParensNestLimit );
        UInt32? max_varlookbehind = ValidationUtilities.ParseUInt32( "max_varlookbehind", options.MaxVarLookbehind );

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "m" );
            bw.Write( (byte)'b' );

            bw.Write( pattern );
            bw.Write( text );

            bw.Write( Enum.GetName( options.Algorithm )! );
            bw.Write( options.Locale );
            bw.Write( Enum.GetName( options.Newline )! );
            bw.Write( Enum.GetName( options.Bsr )! );

            // Compile options

            bw.Write( Convert.ToByte( options.PCRE2_ANCHORED ) );
            bw.Write( Convert.ToByte( options.PCRE2_ALLOW_EMPTY_CLASS ) );
            bw.Write( Convert.ToByte( options.PCRE2_ALT_BSUX ) );
            bw.Write( Convert.ToByte( options.PCRE2_ALT_CIRCUMFLEX ) );
            bw.Write( Convert.ToByte( options.PCRE2_ALT_EXTENDED_CLASS ) );
            bw.Write( Convert.ToByte( options.PCRE2_ALT_VERBNAMES ) );
            bw.Write( Convert.ToByte( options.PCRE2_CASELESS ) );
            bw.Write( Convert.ToByte( options.PCRE2_DOLLAR_ENDONLY ) );
            bw.Write( Convert.ToByte( options.PCRE2_DOTALL ) );
            bw.Write( Convert.ToByte( options.PCRE2_DUPNAMES ) );
            bw.Write( Convert.ToByte( options.PCRE2_ENDANCHORED ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTENDED ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTENDED_MORE ) );
            bw.Write( Convert.ToByte( options.PCRE2_FIRSTLINE ) );
            bw.Write( Convert.ToByte( options.PCRE2_LITERAL ) );
            bw.Write( Convert.ToByte( options.PCRE2_MATCH_UNSET_BACKREF ) );
            bw.Write( Convert.ToByte( options.PCRE2_MULTILINE ) );
            bw.Write( Convert.ToByte( options.PCRE2_NEVER_BACKSLASH_C ) );
            bw.Write( Convert.ToByte( options.PCRE2_NEVER_UCP ) );
            bw.Write( Convert.ToByte( options.PCRE2_NEVER_UTF ) );
            bw.Write( Convert.ToByte( options.PCRE2_NO_AUTO_CAPTURE ) );
            bw.Write( Convert.ToByte( options.PCRE2_NO_AUTO_POSSESS ) );
            bw.Write( Convert.ToByte( options.PCRE2_NO_DOTSTAR_ANCHOR ) );
            bw.Write( Convert.ToByte( options.PCRE2_NO_START_OPTIMIZE ) );
            bw.Write( Convert.ToByte( options.PCRE2_UCP ) );
            bw.Write( Convert.ToByte( options.PCRE2_UNGREEDY ) );
            bw.Write( Convert.ToByte( options.PCRE2_USE_OFFSET_LIMIT ) );

            // Extra compile options

            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ALLOW_SURROGATE_ESCAPES ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ALT_BSUX ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_BSD ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_BSS ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_BSW ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_DIGIT ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_POSIX ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_CASELESS_RESTRICT ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ESCAPED_CR_IS_LF ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_MATCH_LINE ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_MATCH_WORD ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_NEVER_CALLOUT ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_NO_BS0 ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_PYTHON_OCTAL ) );
            bw.Write( Convert.ToByte( options.PCRE2_EXTRA_TURKISH_CASING ) );

            // Match options

            bw.Write( Convert.ToByte( options.PCRE2_ANCHORED_mo ) );
            bw.Write( Convert.ToByte( options.PCRE2_COPY_MATCHED_SUBJECT ) );
            bw.Write( Convert.ToByte( options.PCRE2_DISABLE_RECURSELOOP_CHECK ) );
            bw.Write( Convert.ToByte( options.PCRE2_ENDANCHORED_mo ) );
            bw.Write( Convert.ToByte( options.PCRE2_NOTBOL ) );
            bw.Write( Convert.ToByte( options.PCRE2_NOTEOL ) );
            bw.Write( Convert.ToByte( options.PCRE2_NOTEMPTY ) );
            bw.Write( Convert.ToByte( options.PCRE2_NOTEMPTY_ATSTART ) );
            bw.Write( Convert.ToByte( options.PCRE2_NO_JIT ) );
            bw.Write( Convert.ToByte( options.PCRE2_PARTIAL_HARD ) );
            bw.Write( Convert.ToByte( options.PCRE2_PARTIAL_SOFT ) );
            bw.Write( Convert.ToByte( options.PCRE2_DFA_SHORTEST ) );

            // JIT Options

            bw.Write( Convert.ToByte( options.UseJIT ) );
            if( options.UseJIT )
            {
                bw.Write( Convert.ToByte( options.PCRE2_JIT_COMPLETE ) );
                bw.Write( Convert.ToByte( options.PCRE2_JIT_PARTIAL_SOFT ) );
                bw.Write( Convert.ToByte( options.PCRE2_JIT_PARTIAL_HARD ) );
            }

            // Limits

            bw.WriteOptional( depth_limit );
            bw.WriteOptional( heap_limit );
            bw.WriteOptional( match_limit );
            bw.WriteOptional( max_pattern_compiled_length );
            bw.WriteOptional( offset_limit );
            bw.WriteOptional( parens_nest_limit );
            bw.WriteOptional( max_varlookbehind );

            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        var br = ph.BinaryReader;

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        SimpleMatch? current_match = null;

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
                }
                else
                {
                    current_match.AddSucceededGroup( native_index, native_length, name );
                }
            }
            break;
            case (byte)'e':
                done = true;
                break;
            default:
                throw new Exception( "Invalid response [3]." );
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"PCRE2Worker.bin" );

        return worker_exe;
    }

    static FeatureMatrix BuildFeatureMatrix( (bool PCRE2_ALLOW_EMPTY_CLASS, bool PCRE2_ALT_BSUX, bool PCRE2_EXTRA_ALT_BSUX, bool PCRE2_ALT_EXTENDED_CLASS, bool PCRE2_DUPNAMES, bool PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL) options )
    {
        (bool PCRE2_ALLOW_EMPTY_CLASS, bool PCRE2_ALT_BSUX, bool PCRE2_EXTRA_ALT_BSUX, bool PCRE2_ALT_EXTENDED_CLASS, bool PCRE2_DUPNAMES, bool PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL) = options;

        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = true,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = true,
            XModeComments = true,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = true,
            ScopedCircumflexFlags = true,
            XFlag = true,
            XXFlag = true,

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
            Esc_xBrace = !( PCRE2_ALT_BSUX | PCRE2_EXTRA_ALT_BSUX ),
            Esc_u4 = PCRE2_ALT_BSUX | PCRE2_EXTRA_ALT_BSUX,
            Esc_U8 = false,
            Esc_uBrace = PCRE2_EXTRA_ALT_BSUX,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL,

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
            InsideSets_Esc_xBrace = !( PCRE2_ALT_BSUX | PCRE2_EXTRA_ALT_BSUX ),
            InsideSets_Esc_u4 = PCRE2_ALT_BSUX | PCRE2_EXTRA_ALT_BSUX,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = PCRE2_EXTRA_ALT_BSUX,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL,

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

            InsideSets_Operators = PCRE2_ALT_EXTENDED_CLASS,
            InsideSets_OperatorsExtended = true,
            InsideSets_Operator_Ampersand = true, // extended syntax: (?[[...]&[...]])
            InsideSets_Operator_Plus = true, // extended
            InsideSets_Operator_VerticalLine = true, // extended
            InsideSets_Operator_Minus = true, // extended
            InsideSets_Operator_Circumflex = true, // extended
            InsideSets_Operator_Exclamation = true, // extended
            InsideSets_Operator_DoubleAmpersand = PCRE2_ALT_EXTENDED_CLASS,
            InsideSets_Operator_DoubleVerticalLine = PCRE2_ALT_EXTENDED_CLASS,
            InsideSets_Operator_DoubleMinus = PCRE2_ALT_EXTENDED_CLASS,
            InsideSets_Operator_DoubleTilde = PCRE2_ALT_EXTENDED_CLASS,

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
            DuplicateGroupName = PCRE2_DUPNAMES,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NestedLookaround = true,
            AtomicGroup = true,
            BranchReset = true,
            NonatomicPositiveLookahead = true,
            NonatomicPositiveLookbehind = true,
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
            Recursive_ReturnGroups = true,

            Quantifier_Asterisk = true,
            Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.Both,
            Quantifier_LowAbbrev = true,
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
            Conditional_VERSION = true,

            ControlVerbs = true,
            ScriptRuns = true,
            Callouts = true,

            EmptyConstruct = true,
            EmptyConstructX = false,
            EmptySet = PCRE2_ALLOW_EMPTY_CLASS,
            EmptySetAny = true,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = true,
            ßSS = false,
        };
    }
}
