using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;


namespace OnigurumaPlugin;

class Subengine( Options options, Engine parentEngine ) : RegexSubengine
{
    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return options.Syntax switch
        {
            SyntaxEnum.ONIG_SYNTAX_ONIGURUMA => RegexEngineCapabilityEnum.HasCaptures,
            SyntaxEnum.ONIG_SYNTAX_ASIS => RegexEngineCapabilityEnum.None,
            SyntaxEnum.ONIG_SYNTAX_POSIX_BASIC => RegexEngineCapabilityEnum.None,
            SyntaxEnum.ONIG_SYNTAX_POSIX_EXTENDED => RegexEngineCapabilityEnum.None,
            SyntaxEnum.ONIG_SYNTAX_EMACS => RegexEngineCapabilityEnum.None,
            SyntaxEnum.ONIG_SYNTAX_GREP => RegexEngineCapabilityEnum.None,
            SyntaxEnum.ONIG_SYNTAX_GNU_REGEX => RegexEngineCapabilityEnum.None,
            SyntaxEnum.ONIG_SYNTAX_JAVA => RegexEngineCapabilityEnum.HasCaptures,
            SyntaxEnum.ONIG_SYNTAX_PERL => RegexEngineCapabilityEnum.HasCaptures,
            SyntaxEnum.ONIG_SYNTAX_PERL_NG => RegexEngineCapabilityEnum.HasCaptures,
            SyntaxEnum.ONIG_SYNTAX_RUBY => RegexEngineCapabilityEnum.HasCaptures,
            SyntaxEnum.ONIG_SYNTAX_PYTHON => RegexEngineCapabilityEnum.HasCaptures,
            _ => throw new NotImplementedException( ),
        };
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        bool is_literal = options.Syntax == SyntaxEnum.ONIG_SYNTAX_ASIS;

        return new SyntaxOptions
        {
            Literal = is_literal,
            XLevel = options.ONIG_OPTION_EXTEND ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = is_literal ? default( FeatureMatrix ) : parentEngine.TryGetFeatureMatrix( new Engine.FeatureMatrixKey( options ) )
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

            WriteOptions( bw, options );

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

    static void WriteOptions( BinaryWriter bw, Options options )
    {
        bw.Write( Enum.GetName( options.Syntax )! );

        // Compile-time options

        bw.Write( Convert.ToByte( options.ONIG_OPTION_SINGLELINE ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_MULTILINE ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_IGNORECASE ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_IGNORECASE_IS_ASCII ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_EXTEND ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_FIND_LONGEST ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_FIND_NOT_EMPTY ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_MATCH_WHOLE_STRING ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_NEGATE_SINGLELINE ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_CAPTURE_GROUP ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_DONT_CAPTURE_GROUP ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_WORD_IS_ASCII ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_DIGIT_IS_ASCII ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_SPACE_IS_ASCII ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_POSIX_IS_ASCII ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_TEXT_SEGMENT_WORD ) );

        // Search-time options

        bw.Write( Convert.ToByte( options.ONIG_OPTION_NOTBOL ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_NOTEOL ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_NOT_BEGIN_STRING ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_NOT_END_STRING ) );
        bw.Write( Convert.ToByte( options.ONIG_OPTION_NOT_BEGIN_POSITION ) );

        // Configuration

        bw.Write( Convert.ToByte( options.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY ) );
        bw.Write( Convert.ToByte( options.ONIG_SYN_STRICT_CHECK_BACKREF ) );
    }


    public static Details? GetDetails( ICancellable cnc, Options options )
    {
        using ProcessHelper ph = new( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "d" );
            bw.Write( (byte)'b' );

            WriteOptions( bw, options );

            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return null;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        var br = ph.BinaryReader;

        var sz = Marshal.SizeOf( typeof( Details ) );

        if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response [D1]." );

        byte[] bytes = br.ReadBytes( Marshal.SizeOf( typeof( Details ) ) );

        if( br.ReadByte( ) != 'e' ) throw new Exception( "Invalid response [D2]." );

        GCHandle gch = GCHandle.Alloc( bytes, GCHandleType.Pinned );
        try
        {
            nint addr = gch.AddrOfPinnedObject( );
            Details details = Marshal.PtrToStructure<Details>( addr )!;

            return details;
        }
        finally
        {
            gch.Free( );
        }
    }


    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"OnigurumaWorker.bin" );

        return worker_exe;
    }

    internal static FeatureMatrix BuildFeatureMatrix( SyntaxEnum syntax, Details details )
    {
        Debug.Assert( !details.ONIG_SYN_OP_ESC_ASTERISK_ZERO_INF );

        return new FeatureMatrix
        {
            Parentheses = details.ONIG_SYN_OP_LPAREN_SUBEXP ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_LPAREN_SUBEXP ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,

            Brackets = details.ONIG_SYN_OP_BRACKET_CC,
            ExtendedBrackets = false,

            VerticalLine = details.ONIG_SYN_OP_VBAR_ALT ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_VBAR_ALT ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            AlternationOnSeparateLines = false,

            InlineComments = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT, //........
            XModeComments = true,
            InsideSets_XModeComments = false,

            Flags = details.ONIG_SYN_OP2_OPTION_PERL || details.ONIG_SYN_OP2_OPTION_RUBY || details.ONIG_SYN_OP2_OPTION_ONIGURUMA,
            ScopedFlags = details.ONIG_SYN_OP2_OPTION_PERL || details.ONIG_SYN_OP2_OPTION_RUBY || details.ONIG_SYN_OP2_OPTION_ONIGURUMA,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = details.ONIG_SYN_OP2_OPTION_ONIGURUMA || details.ONIG_SYN_OP2_OPTION_PERL || details.ONIG_SYN_OP2_OPTION_RUBY,
            XXFlag = false,

            Literal_QE = details.ONIG_SYN_OP2_ESC_CAPITAL_Q_QUOTE,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = false,

            Esc_a = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
            Esc_b = false, // helper.ONIG_SYN_OP_ESC_CONTROL_CHARS, // TODO: does not seems to correspond to documentation; in some cases '\b' is 'b'
            Esc_e = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
            Esc_f = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
            Esc_n = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
            Esc_r = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
            Esc_t = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
            Esc_v = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_OP2_ESC_V_VTAB,
            Esc_Octal = details.ONIG_SYN_OP_ESC_OCTAL3 ? FeatureMatrix.OctalEnum.Octal_2_3 : FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = details.ONIG_SYN_OP_ESC_O_BRACE_OCTAL,
            Esc_x2 = details.ONIG_SYN_OP_ESC_X_HEX2,
            Esc_xBrace = details.ONIG_SYN_OP_ESC_X_BRACE_HEX8,
            Esc_u4 = details.ONIG_SYN_OP2_ESC_U_HEX4,
            Esc_U8 = syntax == SyntaxEnum.ONIG_SYNTAX_PYTHON,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = details.ONIG_SYN_OP_ESC_C_CONTROL,
            Esc_C1 = false,
            Esc_CMinus = details.ONIG_SYN_OP2_ESC_CAPITAL_C_BAR_CONTROL,
            Esc_NBrace = false,
            GenericEscape = true,

            InsideSets_Esc_a = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_b = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_e = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_f = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_n = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_r = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_t = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_v = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_OP2_ESC_V_VTAB && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Esc_Octal = details.ONIG_SYN_OP_ESC_OCTAL3 ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = details.ONIG_SYN_OP_ESC_O_BRACE_OCTAL,
            InsideSets_Esc_x2 = details.ONIG_SYN_OP_ESC_X_HEX2,
            InsideSets_Esc_xBrace = details.ONIG_SYN_OP_ESC_X_BRACE_HEX8,
            InsideSets_Esc_u4 = details.ONIG_SYN_OP2_ESC_U_HEX4,
            InsideSets_Esc_U8 = syntax == SyntaxEnum.ONIG_SYNTAX_PYTHON,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = details.ONIG_SYN_OP_ESC_C_CONTROL,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = details.ONIG_SYN_OP2_ESC_CAPITAL_C_BAR_CONTROL,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,

            Class_Dot = details.ONIG_SYN_OP_DOT_ANYCHAR,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = details.ONIG_SYN_OP_ESC_D_DIGIT,
            Class_hHhexa = details.ONIG_SYN_OP2_ESC_H_XDIGIT,
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = details.ONIG_SYN_OP2_ESC_CAPITAL_N_O_SUPER_DOT,
            Class_O = details.ONIG_SYN_OP2_ESC_CAPITAL_N_O_SUPER_DOT,
            Class_R = details.ONIG_SYN_OP2_ESC_CAPITAL_R_GENERAL_NEWLINE,
            Class_sS = details.ONIG_SYN_OP_ESC_S_WHITE_SPACE,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = details.ONIG_SYN_OP_ESC_W_WORD,
            Class_X = details.ONIG_SYN_OP2_ESC_X_Y_TEXT_SEGMENT,
            Class_pP = false,
            Class_pPBrace = details.ONIG_SYN_OP2_ESC_P_BRACE_CHAR_PROPERTY || details.ONIG_SYN_OP2_ESC_P_BRACE_CIRCUMFLEX_NOT,

            InsideSets_Class_dD = details.ONIG_SYN_OP_ESC_D_DIGIT && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Class_hHhexa = details.ONIG_SYN_OP2_ESC_H_XDIGIT && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = details.ONIG_SYN_OP_ESC_S_WHITE_SPACE && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = details.ONIG_SYN_OP_ESC_W_WORD && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = details.ONIG_SYN_OP2_ESC_P_BRACE_CHAR_PROPERTY || details.ONIG_SYN_OP2_ESC_P_BRACE_CIRCUMFLEX_NOT,
            InsideSets_Class_Name = details.ONIG_SYN_OP_POSIX_BRACKET,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = details.ONIG_SYN_OP2_CCLASS_SET_OP,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = details.ONIG_SYN_OP2_CCLASS_SET_OP,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = false, // TODO: clarify
            InsideSets_Operator_DoubleTilde = false,

            Anchor_Circumflex = details.ONIG_SYN_OP_LINE_ANCHOR,
            Anchor_Dollar = details.ONIG_SYN_OP_LINE_ANCHOR,
            Anchor_A = details.ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR,
            Anchor_Z = details.ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR ? FeatureMatrix.AnchorZModeEnum.Correct : FeatureMatrix.AnchorZModeEnum.None,
            Anchor_z = details.ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR, // TODO: in Python syntax, it gives undefined operator (-213)
            Anchor_G = details.ONIG_SYN_OP_ESC_CAPITAL_G_BEGIN_ANCHOR,
            Anchor_bB = details.ONIG_SYN_OP_ESC_B_WORD_BOUND,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = details.ONIG_SYN_OP2_ESC_CAPITAL_K_KEEP,
            Anchor_mM = false,
            Anchor_LtGt = details.ONIG_SYN_OP_ESC_LTGT_WORD_BEGIN_END,
            Anchor_GraveApos = details.ONIG_SYN_OP2_ESC_GNU_BUF_ANCHOR,
            Anchor_yY = details.ONIG_SYN_OP2_ESC_X_Y_TEXT_SEGMENT, // TODO: seems to work for some other cases too

            NamedGroup_Apos = details.ONIG_SYN_OP2_QMARK_LT_NAMED_GROUP,
            NamedGroup_LtGt = details.ONIG_SYN_OP2_QMARK_LT_NAMED_GROUP,
            NamedGroup_PLtGt = details.ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME,
            BalancingGroup = false,
            CapturingGroup = details.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY,
            DuplicateGroupName = syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA || syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG || syntax == SyntaxEnum.ONIG_SYNTAX_RUBY,

            NoncapturingGroup = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
            PositiveLookahead = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
            NegativeLookahead = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
            PositiveLookbehind = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT ? syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA || syntax == SyntaxEnum.ONIG_SYNTAX_JAVA ? FeatureMatrix.LookModeEnum.AnyLength : syntax == SyntaxEnum.ONIG_SYNTAX_RUBY ? FeatureMatrix.LookModeEnum.BoundedLength : FeatureMatrix.LookModeEnum.FixedLength : FeatureMatrix.LookModeEnum.None,
            NegativeLookbehind = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT ? syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA || syntax == SyntaxEnum.ONIG_SYNTAX_JAVA ? FeatureMatrix.LookModeEnum.AnyLength : syntax == SyntaxEnum.ONIG_SYNTAX_RUBY ? FeatureMatrix.LookModeEnum.BoundedLength : FeatureMatrix.LookModeEnum.FixedLength : FeatureMatrix.LookModeEnum.None,
            NestedLookaround = true,
            AtomicGroup = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = details.ONIG_SYN_OP2_QMARK_TILDE_ABSENT_GROUP,
            AllowSpacesInGroups = false,

            Backref_Num = details.ONIG_SYN_OP_DECIMAL_BACKREF ? FeatureMatrix.BackrefEnum.Any : FeatureMatrix.BackrefEnum.None,
            Backref_kApos = details.ONIG_SYN_OP2_ESC_K_NAMED_BACKREF,
            Backref_kLtGt = details.ONIG_SYN_OP2_ESC_K_NAMED_BACKREF,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = details.ONIG_SYN_OP2_ESC_G_SUBEXP_CALL ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = details.ONIG_SYN_OP2_ESC_G_SUBEXP_CALL ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = details.ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME,
            AllowSpacesInBackref = false,

            Recursive_Num = syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG,
            Recursive_PlusMinusNum = syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG,
            Recursive_R = false, //details.ONIG_SYN_OP2_QMARK_PERL_SUBEXP_CALL, // TODO: does not seem to work
            Recursive_Name = details.ONIG_SYN_OP2_QMARK_PERL_SUBEXP_CALL,
            Recursive_PGtName = details.ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = details.ONIG_SYN_OP_ASTERISK_ZERO_INF,
            Quantifier_Plus = details.ONIG_SYN_OP_PLUS_ONE_INF ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_PLUS_ONE_INF ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Question = details.ONIG_SYN_OP_QMARK_ZERO_ONE ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_QMARK_ZERO_ONE ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces = details.ONIG_SYN_OP_BRACE_INTERVAL ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_BRACE_INTERVAL ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = details.ONIG_SYN_ALLOW_INTERVAL_LOW_ABBREV,
            Quantifier_Lazy = details.ONIG_SYN_OP_QMARK_NON_GREEDY,
            Quantifier_Possessive = details.ONIG_SYN_OP2_PLUS_POSSESSIVE_REPEAT,

            Conditional_BackrefByNumber = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE,
            Conditional_BackrefByName = false,
            Conditional_Pattern = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE && syntax != SyntaxEnum.ONIG_SYNTAX_PERL && syntax != SyntaxEnum.ONIG_SYNTAX_PYTHON,
            Conditional_BackrefByName_LtGt = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE && syntax != SyntaxEnum.ONIG_SYNTAX_PERL,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA || syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG || syntax == SyntaxEnum.ONIG_SYNTAX_RUBY,
            Conditional_VERSION = false,

            ControlVerbs = details.ONIG_SYN_OP2_ASTERISK_CALLOUT_NAME, // several built-in callouts: https://github.com/kkos/oniguruma/blob/master/doc/CALLOUTS.BUILTIN
            ScriptRuns = false,
            Callouts = details.ONIG_SYN_OP2_ASTERISK_CALLOUT_NAME,

            EmptyConstruct = false,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = details.ONIG_SYN_OP_ESC_W_WORD,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = true,
            ßSS = true,

            Ext_NamedGroup_AtApos = details.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY,
            Ext_NamedGroup_AtLtGt = details.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY,
        };

        // TODO: "\M-x"
        // TODO: "(?Rnumber)"
        // TODO: ONIG_SYN_OP2_QMARK_BRACE_CALLOUT_CONTENTS
    }
}
