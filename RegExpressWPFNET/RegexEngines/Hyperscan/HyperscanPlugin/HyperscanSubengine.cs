using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;


namespace HyperscanPlugin;

partial class HyperscanSubengine( HyperscanOptions options ) : RegexSubengine
{
    static readonly LazyData<(bool HS_FLAG_UCP, bool HS_FLAG_SOM_LEFTMOST), FeatureMatrix> LazyFeatureMatrix =
        new( d => BuildFeatureMatrix( d.HS_FLAG_UCP, d.HS_FLAG_SOM_LEFTMOST ) );

    static readonly Encoding AsciiEncodingWithExceptionFallback = Encoding.GetEncoding( Encoding.ASCII.WebName, new EncoderExceptionFallback( ), new DecoderExceptionFallback( ) );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.NoGroups | RegexEngineCapabilityEnum.OverlappingMatches;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.HS_FLAG_UCP, options.HS_FLAG_SOM_LEFTMOST) );

        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        uint? LevenshteinDistance = ValidationUtilities.ParseUInt32( "LevenshteinDistance", options.LevenshteinDistance );
        uint? HammingDistance = ValidationUtilities.ParseUInt32( "HammingDistance", options.HammingDistance );
        uint? MinOffset = ValidationUtilities.ParseUInt32( "MinOffset", options.MinOffset );
        uint? MaxOffset = ValidationUtilities.ParseUInt32( "MaxOffsetDistance", options.MaxOffset );
        uint? MinLength = ValidationUtilities.ParseUInt32( "MinLength", options.MinLength );

        if( !options.HS_FLAG_UTF8 )
        {
            bool is_bad_pattern = false;
            try
            {
                AsciiEncodingWithExceptionFallback.GetByteCount( pattern );
            }
            catch( EncoderFallbackException )
            {
                is_bad_pattern = true;
            }

            bool is_bad_text = false;
            try
            {
                AsciiEncodingWithExceptionFallback.GetByteCount( text );
            }
            catch( EncoderFallbackException )
            {
                is_bad_text = true;
            }

            if( is_bad_pattern && is_bad_text )
            {
                throw new Exception( "The pattern and text contain non-ascii characters. (The 'HS_FLAG_UTF8' flag is required)." );
            }
            if( is_bad_pattern || is_bad_text )
            {
                throw new Exception( $"The {( is_bad_pattern ? "pattern" : "text" )} contains non-ascii characters. (The 'HS_FLAG_UTF8' flag is required)." );
            }
        }

        UInt32 flags = 0;

        if( options.HS_FLAG_CASELESS ) flags |= 1 << 0;
        if( options.HS_FLAG_DOTALL ) flags |= 1 << 1;
        if( options.HS_FLAG_MULTILINE ) flags |= 1 << 2;
        if( options.HS_FLAG_SINGLEMATCH ) flags |= 1 << 3;
        if( options.HS_FLAG_ALLOWEMPTY ) flags |= 1 << 4;
        if( options.HS_FLAG_UTF8 ) flags |= 1 << 5;
        if( options.HS_FLAG_UCP ) flags |= 1 << 6;
        if( options.HS_FLAG_PREFILTER ) flags |= 1 << 7;
        if( options.HS_FLAG_SOM_LEFTMOST ) flags |= 1 << 8;
        //if( options.HS_FLAG_COMBINATION ) flags |= 1 << 9;
        if( options.HS_FLAG_QUIET ) flags |= 1 << 10;


        using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;

        ph.BinaryWriter = bw =>
        {
            bw.Write( "m" );
            bw.Write( (byte)'b' );
            bw.Write( pattern );
            bw.Write( text );
            bw.Write( flags );
            bw.Write( LevenshteinDistance ?? UInt32.MaxValue );
            bw.Write( HammingDistance ?? UInt32.MaxValue );
            bw.Write( MinOffset ?? UInt32.MaxValue );
            bw.Write( MaxOffset ?? UInt32.MaxValue );
            bw.Write( MinLength ?? UInt32.MaxValue );
            bw.Write( checked((byte)options.Mode) );
            bw.Write( checked((byte)options.ModeSom) );
            bw.Write( (byte)'e' );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

        var br = ph.BinaryReader;

        string r = br.ReadString( );

        if( r != "r" )
        {
            throw new Exception( "Unknown result" );
        }

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        Utf8IndexConverter index_converter = new( text );

        int count = checked((int)br.ReadUInt64( ));

        for( int i = 0; i < count; ++i )
        {
            int native_index = checked((int)br.ReadUInt64( ));
            int native_length = checked((int)br.ReadUInt64( ));
            int native_end = native_index + native_length;

            (int char_index, int char_length) = index_converter.Convert( native_index, native_end );

            SimpleMatch m = SimpleMatch.Create( native_index, native_length, char_index, char_length, stg );
            m.AddDefaultGroup( );

            matches.Add( m );
        }

        return new RegexMatches( matches.Count, matches );
    }

    private static string? AdjustErrorMessage( string error, string pattern )
    {
        // try to show character offset based on byte offset, which appears in error messages

        System.Text.RegularExpressions.Match m = RegexExtractByteOffset( ).Match( error );

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
        string worker_exe = Path.Combine( assembly_dir, @"HyperscanWorker.bin" );

        return worker_exe;
    }

    private static FeatureMatrix BuildFeatureMatrix( bool isFlagUcp, bool isSomLeftmost )
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
            Esc_xBrace = true,
            Esc_u4 = false,
            Esc_U8 = false,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = true,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = true,

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
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = false,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = true,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = false,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = false,
            Class_hHhorspace = true,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = false,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = true,
            Class_wW = true,
            Class_X = false,
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
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Correct,
            Anchor_z = true,
            Anchor_G = false,
            Anchor_bB = !isFlagUcp && !isSomLeftmost,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = true,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = true,
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
            Quantifier_Lazy = true, // (it seems to be always lazy)
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

            ControlVerbs = true, // (*UTF8), (*UCP), https://intel.github.io/hyperscan/dev-reference/compilation.html 
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = true,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = isFlagUcp,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = true,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = true,
            ßSS = false,
        };
    }

    [GeneratedRegex( @" at index (\d+)" )]
    private static partial Regex RegexExtractByteOffset( );
}
