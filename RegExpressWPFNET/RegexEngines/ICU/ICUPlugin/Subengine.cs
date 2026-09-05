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


namespace ICUPlugin;

class Subengine( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new Lazy<FeatureMatrix>( BuildFeatureMatrix );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.Value;

        return new SyntaxOptions
        {
            Literal = options.UREGEX_LITERAL,
            XLevel = options.UREGEX_COMMENTS ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Int32? limit = ValidationUtilities.ParseInt32( "limit", options.limit );
        Int64? region_start = ValidationUtilities.ParseInt64( "start", options.regionStart );
        Int64? region_end = ValidationUtilities.ParseInt64( "end", options.regionEnd );

        if( ( region_start == null ) != ( region_end == null ) )
        {
            throw new ApplicationException( "Both “start” and “end” must be entered or blank." );
        }

        uint flags = 0;
        //if(options.UREGEX_CANON_EQ) flags |= 1 << 0; // not implemented by ICU
        if( options.UREGEX_CASE_INSENSITIVE ) flags |= 1 << 1;
        if( options.UREGEX_COMMENTS ) flags |= 1 << 2;
        if( options.UREGEX_DOTALL ) flags |= 1 << 3;
        if( options.UREGEX_LITERAL ) flags |= 1 << 4;
        if( options.UREGEX_MULTILINE ) flags |= 1 << 5;
        if( options.UREGEX_UNIX_LINES ) flags |= 1 << 6;
        if( options.UREGEX_UWORD ) flags |= 1 << 7;
        if( options.UREGEX_ERROR_ON_UNKNOWN_ESCAPES ) flags |= 1 << 8;
        if( options.useAnchoringBounds ) flags |= 1 << 9;
        if( options.useTransparentBounds ) flags |= 1 << 10;

        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.Unicode;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "m" );
            bw.Write( (byte)'b' );

            bw.Write( pattern );
            bw.Write( text );
            bw.Write( flags );
            bw.WriteOptional( limit );
            bw.WriteOptional( region_start );
            bw.WriteOptional( region_end );

            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        var br = ph.BinaryReader;

        if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response B." );

        // read group names

        var group_names = new Dictionary<int, string>( );

        for(; ; )
        {
            int i = br.ReadInt32( );
            if( i <= 0 ) break;

            string name = br.ReadString( );

            group_names.Add( i, name );
        }

        // read matches

        List<IMatch> matches = [];
        SimpleTextGetter? stg = new( text );

        for(; ; )
        {
            int group_count = br.ReadInt32( );
            if( group_count < 0 ) break;

            SimpleMatch? match = null; ;

            for( int i = 0; i <= group_count; ++i )
            {
                int native_start = br.ReadInt32( );
                bool success = native_start >= 0;
                int native_end;
                int native_length;

                if( !success )
                {
                    native_end = 0;
                    native_length = 0;
                }
                else
                {
                    native_end = br.ReadInt32( );
                    native_length = native_end - native_start;
                }

                if( i == 0 )
                {
                    Debug.Assert( success );
                    Debug.Assert( match == null );

                    match = SimpleMatch.Create( native_start, native_length, stg );
                    match.AddDefaultGroup( );
                }
                else
                {
                    if( !group_names.TryGetValue( i, out string? name ) )
                    {
                        name = i.ToString( CultureInfo.InvariantCulture );
                    }

                    Debug.Assert( match != null );

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        match.AddSucceededGroup( native_start, native_length, name );
                    }
                }
            }

            Debug.Assert( match != null );

            matches.Add( match );
        }

        if( br.ReadByte( ) != 'e' ) throw new Exception( "Invalid response E." );

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, @"ICUWorker.bin" );

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

            InlineComments = true,
            XModeComments = true,
            InsideSets_XModeComments = true,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = true,
            XXFlag = false,

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
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = true,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = true,
            Esc_u4 = true,
            Esc_U8 = true,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = true,
            GenericEscape = true,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = true,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = false,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = true,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = true,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = true,
            InsideSets_GenericEscape = true,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = false,
            Class_hHhorspace = true,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = true,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = true,
            Class_wW = true,
            Class_X = true,
            Class_pP = false,
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
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = true,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = true,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = true, // TODO: not documented
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = true, // TODO: not documented
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = true,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = true,
            InsideSets_Operator_DoubleTilde = false,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Correct,
            Anchor_z = true,
            Anchor_G = true,
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
            DuplicateGroupName = false,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.BoundedLength,
            NestedLookaround = true,
            AtomicGroup = true,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = true,

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
            AllowSpacesInBackref = true,

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
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.XModeOnly,
            Quantifier_LowAbbrev = false,
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
            Unicode_Class_vW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
            Σσς = true,
            ßSS = true,

            Ext_Class_Name = true,
        };
    }
}
