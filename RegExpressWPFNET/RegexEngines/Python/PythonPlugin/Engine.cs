using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Controls;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;


namespace PythonPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrix_Re = new( BuildFeatureMatrix_Re );
        static readonly LazyData<(bool isPosix, bool isVersion1), FeatureMatrix> LazyFeatureMatrix_Regex = new( d => BuildFeatureMatrix_Regex( d.isPosix, d.isVersion1 ) );
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrix_RealRegex = new( BuildFeatureMatrix_RealRegex );

        Options mOptions = new( );
        readonly Lazy<UCOptions> mOptionsControl;

        public Engine( )
        {
            mOptionsControl = new Lazy<UCOptions>( ( ) =>
            {
                UCOptions oc = new( );
                oc.SetOptions( Options );
                oc.Changed += OptionsControl_Changed;

                return oc;
            } );
        }

        public Options Options
        {
            get
            {
                return mOptions;
            }
            set
            {
                mOptions = value;

                if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
            }
        }

        #region IRegexEngine

        public string Kind => "Python";

        public string? Version => Versions.Python;

        public string Name => "Python";

        public string Subtitle => $"{Name} ({mOptionsControl.Value.GetSelectedModuleTitle( )})";

        public RegexEngineCapabilityEnum Capabilities => ( mOptions.Module != ModuleEnum.regex ? RegexEngineCapabilityEnum.NoCaptures : RegexEngineCapabilityEnum.None ) | RegexEngineCapabilityEnum.OverlappingMatches;

        public string? NoteForCaptures => null;

        public event RegexEngineOptionsChanged? OptionsChanged;
#pragma warning disable 0067
        public event EventHandler? FeatureMatrixReady;
#pragma warning restore 0067


        public Control GetOptionsControl( )
        {
            return mOptionsControl.Value;
        }

        public string? ExportOptions( )
        {
            string json = JsonSerializer.Serialize( Options, JsonUtilities.JsonOptions );

            return json;
        }

        public void ImportOptions( string? json )
        {
            if( string.IsNullOrWhiteSpace( json ) )
            {
                Options = new Options( );
            }
            else
            {
                try
                {
                    Options = JsonSerializer.Deserialize<Options>( json, JsonUtilities.JsonOptions )!;
                }
                catch
                {
                    // ignore versioning errors, for example
                    if( Debugger.IsAttached ) Debugger.Break( );

                    Options = new Options( );
                }
            }
        }

        public RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
        {
            return Options.Module switch
            {
                ModuleEnum.re => MatcherRe.GetMatches( cnc, pattern, text, Options ),
                ModuleEnum.regex => MatcherRegex.GetMatches( cnc, pattern, text, Options ),
                ModuleEnum.real_regex => MatcherRealRegex.GetMatches( cnc, pattern, text, Options ),
                _ => throw new NotImplementedException( )
            };
        }


        public SyntaxOptions GetSyntaxOptions( )
        {
            FeatureMatrix fm = Options.Module switch
            {
                ModuleEnum.re => LazyFeatureMatrix_Re.Value,
                ModuleEnum.regex => LazyFeatureMatrix_Regex.GetValue( (Options.POSIX, Options.VERSION1) ),
                ModuleEnum.real_regex => LazyFeatureMatrix_RealRegex.Value,
                _ => throw new InvalidOperationException( ),
            };

            return new SyntaxOptions
            {
                XLevel = Options.VERBOSE ? XLevelEnum.x : XLevelEnum.none,
                FeatureMatrix = fm,
            };
        }


        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            Engine engine_re = new( ) { Options = new Options { Module = ModuleEnum.re, VERSION0 = false, VERSION1 = false } };
            //Engine engine_regex_v0 = new( ) { Options = new Options { Module = ModuleEnum.regex, POSIX = false, VERSION0 = true, VERSION1 = false } };
            Engine engine_regex_v1 = new( ) { Options = new Options { Module = ModuleEnum.regex, POSIX = false, VERSION0 = false, VERSION1 = true } };
            Engine engine_regex_v1_posix = new( ) { Options = new Options { Module = ModuleEnum.regex, POSIX = true, VERSION0 = false, VERSION1 = true } };
            Engine engine_real_regex = new( ) { Options = new Options { Module = ModuleEnum.real_regex, VERSION0 = false, VERSION1 = false } };

            return
                [
                    new FeatureMatrixVariant("re", LazyFeatureMatrix_Re.Value, engine_re),
                    //new FeatureMatrixVariant("regex V0", LazyFeatureMatrix_Regex.GetValue((isPosix: false, isVersion1: false)), engine_regex_v0),
                    new FeatureMatrixVariant("regex V1", LazyFeatureMatrix_Regex.GetValue((isPosix: false, isVersion1: true)), engine_regex_v1),
                    new FeatureMatrixVariant("regex V1 (posix)", LazyFeatureMatrix_Regex.GetValue((isPosix: true, isVersion1: true)), engine_regex_v1_posix),
                    new FeatureMatrixVariant("real-regex", LazyFeatureMatrix_RealRegex.Value, engine_real_regex),
                ];
        }

        public void SetIgnoreCase( bool yes )
        {
            Options.IGNORECASE = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options.VERBOSE = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetCollectCaptures( bool yes )
        {
        }

        #endregion


        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }

        static FeatureMatrix BuildFeatureMatrix_Re( )
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

                Literal_QE = false,
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
                Esc_xBrace = false,
                Esc_u4 = true,
                Esc_U8 = true,
                Esc_uBrace = false,
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
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
                InsideSets_Esc_uBrace = false,
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
                Class_pP = false,
                Class_pPBrace = false,

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
                Anchor_Z = true,
                Anchor_z = true,
                Anchor_G = false,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = false,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = false,
                NamedGroup_LtGt = false,
                NamedGroup_PLtGt = true,
                BalancingGroup = false,
                CapturingGroup = false,
                DuplicateGroupName = false,

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = FeatureMatrix.LookModeEnum.FixedLength,
                NegativeLookbehind = FeatureMatrix.LookModeEnum.FixedLength,
                AtomicGroup = true,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = FeatureMatrix.BackrefEnum.Any, // TODO: actually it supports \1, \2, ... \99.
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
                Backref_PEqName = true,
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

                Conditional_BackrefByNumber = true,
                Conditional_BackrefByName = true,
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

                SupportsUnicode = true,
                InsideSets_SupportsUnicode = true,
                SupportsUnicodeCaseFolding = true,
                KeepSurrogatePairs = true,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
                Σσς = true,
            };
        }

        static FeatureMatrix BuildFeatureMatrix_Regex( bool isPosix, bool isVersion1 )
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

                Literal_QE = false,
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
                Esc_xBrace = false,
                Esc_u4 = true,
                Esc_U8 = true,
                Esc_uBrace = false,
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
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
                InsideSets_Esc_uBrace = false,
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
                Class_R = true,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = true,
                Class_X = true,
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
                InsideSets_Class_Name = true,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = isVersion1,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = isVersion1,
                InsideSets_Operator_DoubleVerticalLine = isVersion1,
                InsideSets_Operator_DoubleMinus = isVersion1,
                InsideSets_Operator_DoubleTilde = isVersion1,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = true,
                Anchor_Z = true,
                Anchor_z = true,
                Anchor_G = true,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = true,
                Anchor_mM = true,
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
                NegativeLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
                AtomicGroup = true,
                BranchReset = true,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = FeatureMatrix.BackrefEnum.Any, // TODO: actually it supports \1, \2, ... \99.
                Backref_kApos = false,
                Backref_kLtGt = false,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
                Backref_gLtGt = FeatureMatrix.BackrefModeEnum.Value,
                Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
                Backref_PEqName = true,
                AllowSpacesInBackref = false,

                Recursive_Num = true,
                Recursive_PlusMinusNum = true,
                Recursive_R = true,
                Recursive_Name = true,
                Recursive_PGtName = true,
                Recursive_ReturnGroups = false,

                Quantifier_Asterisk = true,
                Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
                Quantifier_LowAbbrev = true,
                Quantifier_Lazy = !isPosix,

                Conditional_BackrefByNumber = true,
                Conditional_BackrefByName = true,
                Conditional_Pattern = true,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = false,
                Conditional_BackrefByName_LtGt = false,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = true,
                Conditional_VERSION = false,

                ControlVerbs = true,
                ScriptRuns = false,
                Callouts = false,

                EmptyConstruct = true,
                EmptyConstructX = true,
                EmptySet = false,
                EmptySetAny = false,

                SupportsUnicode = true,
                InsideSets_SupportsUnicode = true,
                SupportsUnicodeCaseFolding = true,
                KeepSurrogatePairs = true,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = true,
            };
        }

        static FeatureMatrix BuildFeatureMatrix_RealRegex( )
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
                ScopedFlags = false,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = true,
                XXFlag = false,

                Literal_QE = false,
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
                Esc_xBrace = false,
                Esc_u4 = true,
                Esc_U8 = true,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = false,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = false,

                InsideSets_Esc_a = true,
                InsideSets_Esc_b = true,
                InsideSets_Esc_e = false,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = true,
                InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
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

                InsideSets_Class_dD = true,
                InsideSets_Class_hHhexa = false,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = false,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = true,
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
                Anchor_Z = true,
                Anchor_z = false,
                Anchor_G = false,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = false,
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
                Quantifier_LowAbbrev = true,
                Quantifier_Lazy = true,

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

                SupportsUnicode = true,
                InsideSets_SupportsUnicode = true,
                SupportsUnicodeCaseFolding = false,
                KeepSurrogatePairs = true,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = false,
            };
        }
    }
}
