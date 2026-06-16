using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Controls;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;


namespace RustPlugin
{
    class Engine : IRegexEngine
    {
        static readonly LazyData<(StructEnum @struct, bool isOctal, bool isUnicode), FeatureMatrix> LazyFeatureMatrix_Regex =
            new( d => BuildFeatureMatrix_Regex( d.@struct, d.isOctal, d.isUnicode ) );

        static readonly LazyData<StructEnum, FeatureMatrix> LazyFeatureMatrix_RegexLite =
            new( @struct => BuildFeatureMatrix_RegexLiteCrate( @struct ) );

        static readonly LazyData<(StructEnum @struct, bool isUnicode, bool isOniguruma), FeatureMatrix> LazyFeatureMatrix_FancyRegex =
            new( d => BuildFeatureMatrix_FancyRegex( d.@struct, d.isUnicode, d.isOniguruma ) );

        static readonly LazyData<(StructEnum @struct, bool isUnicode, bool isUnicodeSets), FeatureMatrix> LazyFeatureMatrix_Regress =
            new( d => BuildFeatureMatrix_Regress( d.@struct, d.isUnicode, d.isUnicodeSets ) );

        static readonly LazyData<UnicodeModeEnum, FeatureMatrix> LazyFeatureMatrix_Resharp =
            new( unicodeMode => BuildFeatureMatrix_Resharp( unicodeMode ) );

        static readonly LazyData<int /*unused yet*/, FeatureMatrix> LazyFeatureMatrix_Anre =
            new( _ => BuildFeatureMatrix_Anre( ) );

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

        public string Kind => "Rust";

        public string? Version => Versions.Rust;

        public string Name => "Rust";

        public string Subtitle => $"{Name} ({mOptionsControl.Value.GetSelectedCrateTitle( )})";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.NoCaptures | RegexEngineCapabilityEnum.ScrollErrorsToEnd;

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
            return Options.crate switch
            {
                CrateEnum.regex => MatcherRegex.GetMatches( cnc, pattern, text, Options ),
                CrateEnum.regex_lite => MatcherRegexLite.GetMatches( cnc, pattern, text, Options ),
                CrateEnum.fancy_regex => MatcherFancyRegex.GetMatches( cnc, pattern, text, Options ),
                CrateEnum.regress => MatcherRegress.GetMatches( cnc, pattern, text, Options ),
                CrateEnum.resharp => MatcherResharp.GetMatches( cnc, pattern, text, Options ),
                CrateEnum.anre => MatcherAnre.GetMatches( cnc, pattern, text, Options ),
                _ => throw new InvalidOperationException( )
            };
        }

        public SyntaxOptions GetSyntaxOptions( )
        {
            FeatureMatrix fm = Options.crate switch
            {
                CrateEnum.regex => LazyFeatureMatrix_Regex.GetValue( (Options.@struct, Options.octal, Options.unicode) ),
                CrateEnum.regex_lite => LazyFeatureMatrix_RegexLite.GetValue( Options.@struct ),
                CrateEnum.fancy_regex => LazyFeatureMatrix_FancyRegex.GetValue( (Options.@struct, Options.unicode, Options.oniguruma_mode) ),
                CrateEnum.regress => LazyFeatureMatrix_Regress.GetValue( (Options.@struct, Options.unicode, Options.unicode_sets) ),
                CrateEnum.resharp => LazyFeatureMatrix_Resharp.GetValue( Options.UnicodeMode ),
                CrateEnum.anre => LazyFeatureMatrix_Anre.GetValue( 0 ),
                _ => throw new InvalidOperationException( )
            };

            return new SyntaxOptions
            {
                Literal = Options.crate == CrateEnum.anre && Options.anre_syntax, //
                XLevel = ( Options.crate == CrateEnum.regex || Options.crate == CrateEnum.fancy_regex || Options.crate == CrateEnum.regex_lite || Options.crate == CrateEnum.resharp ) && Options.ignore_whitespace ? XLevelEnum.x : XLevelEnum.none,
                FeatureMatrix = fm,
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            Engine engine_regex = new( ) { Options = new Options { crate = CrateEnum.regex, @struct = StructEnum.RegexBuilder, unicode = true, octal = true } };
            Engine engine_lite = new( ) { Options = new Options { crate = CrateEnum.regex_lite, @struct = StructEnum.RegexBuilder } };
            Engine engine_fancy = new( ) { Options = new Options { crate = CrateEnum.fancy_regex, @struct = StructEnum.RegexBuilder, unicode = true } };
            Engine engine_regress_v = new( ) { Options = new Options { crate = CrateEnum.regress, unicode = true, unicode_sets = true } }; // currently 'ignore_whitespace' not supported
            Engine engine_resharp = new( ) { Options = new Options { crate = CrateEnum.resharp, UnicodeMode = UnicodeModeEnum.Full } };
            Engine engine_anre = new( ) { Options = new Options { crate = CrateEnum.anre } };

            return
                [
                    new FeatureMatrixVariant("regex (“u” flag)", LazyFeatureMatrix_Regex.GetValue( (StructEnum.RegexBuilder, isOctal:true, isUnicode:true) ), engine_regex),
                    new FeatureMatrixVariant("regex_lite", LazyFeatureMatrix_RegexLite.GetValue( (StructEnum.RegexBuilder) ), engine_lite),
                    new FeatureMatrixVariant("fancy_regex (“u” flag)", LazyFeatureMatrix_FancyRegex.GetValue( (StructEnum.RegexBuilder, isUnicode:true, isOniguruma:false) ), engine_fancy),
                    new FeatureMatrixVariant("regress (“uv” flags)", LazyFeatureMatrix_Regress.GetValue( (StructEnum.RegexBuilder, isUnicode:true, isUnicodeSets:true) ), engine_regress_v),
                    new FeatureMatrixVariant("resharp (“Full” mode)", LazyFeatureMatrix_Resharp.GetValue( UnicodeModeEnum.Full ), engine_resharp),
                    new FeatureMatrixVariant("anre", LazyFeatureMatrix_Anre.GetValue( 0 ), engine_anre),
                ];
        }

        public void SetIgnoreCase( bool yes )
        {
            Options.case_insensitive = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options.ignore_whitespace = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        #endregion

        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }

        private static FeatureMatrix BuildFeatureMatrix_Regex( StructEnum @struct, bool isOctal, bool isUnicode )
        {
            isOctal &= @struct == StructEnum.RegexBuilder;

            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
                AlternationOnSeparateLines = false,

                InlineComments = false,
                XModeComments = true,
                InsideSets_XModeComments = true,

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
                Esc_Octal = isOctal ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = false,
                Esc_x2 = true,
                Esc_xBrace = true,
                Esc_u4 = true,
                Esc_U8 = true,
                Esc_uBrace = true,
                Esc_UBrace = true,
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
                InsideSets_Esc_Octal = isOctal ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
                InsideSets_Esc_uBrace = true,
                InsideSets_Esc_UBrace = true,
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

                InsideSets_Operators = true,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = true,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = true,
                InsideSets_Operator_DoubleTilde = true,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = true,
                Anchor_Z = false,
                Anchor_z = true,
                Anchor_G = false,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = true,
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
                PositiveLookahead = false,
                NegativeLookahead = false,
                PositiveLookbehind = FeatureMatrix.LookModeEnum.None,
                NegativeLookbehind = FeatureMatrix.LookModeEnum.None,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = true,

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
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.Both,
                Quantifier_LowAbbrev = false,
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

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = isUnicode,
            };
        }

        private static FeatureMatrix BuildFeatureMatrix_RegexLiteCrate( StructEnum @struct )
        {
            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
                AlternationOnSeparateLines = false,

                InlineComments = false,
                XModeComments = true,
                InsideSets_XModeComments = true,

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
                Esc_xBrace = true,
                Esc_u4 = true,
                Esc_U8 = true,
                Esc_uBrace = true,
                Esc_UBrace = true,
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
                InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
                InsideSets_Esc_uBrace = true,
                InsideSets_Esc_UBrace = true,
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
                InsideSets_Class_sS = true,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = true,
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
                Anchor_A = true,
                Anchor_Z = false,
                Anchor_z = true,
                Anchor_G = false,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = true,
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
                PositiveLookahead = false,
                NegativeLookahead = false,
                PositiveLookbehind = FeatureMatrix.LookModeEnum.None,
                NegativeLookbehind = FeatureMatrix.LookModeEnum.None,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = true,

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
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.Both,
                Quantifier_LowAbbrev = false,
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

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = false,
            };
        }

        private static FeatureMatrix BuildFeatureMatrix_FancyRegex( StructEnum @struct, bool isUnicode, bool isOniguruma )
        {
            isOniguruma &= @struct == StructEnum.RegexBuilder;

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

                Literal_QE = false,
                InsideSets_Literal_QE = false,
                InsideSets_Literal_qBrace = false,

                Esc_a = true,
                Esc_b = false,
                Esc_e = true,
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
                Esc_UBrace = true,
                Esc_c1 = false,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = false,

                InsideSets_Esc_a = true,
                InsideSets_Esc_b = true,
                InsideSets_Esc_e = true,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = true,
                InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
                InsideSets_Esc_uBrace = true,
                InsideSets_Esc_UBrace = true,
                InsideSets_Esc_c1 = false,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = false,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = true,
                Class_hHhexa = true,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = true,
                Class_O = true,
                Class_R = true,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = true,
                Class_X = false,
                Class_pP = true,
                Class_pPBrace = true,

                InsideSets_Class_dD = true,
                InsideSets_Class_hHhexa = true,
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

                InsideSets_Operators = true,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = true,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = true,
                InsideSets_Operator_DoubleTilde = true,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = true,
                Anchor_Z = true,
                Anchor_z = true,
                Anchor_G = true,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = true,
                Anchor_K = true,
                Anchor_mM = false,
                Anchor_LtGt = !isOniguruma,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = true,
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
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = true,
                AllowSpacesInGroups = true,

                Backref_Num = FeatureMatrix.BackrefEnum.Any,
                Backref_kApos = true,
                Backref_kLtGt = true,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = FeatureMatrix.BackrefModeEnum.Pattern,
                Backref_gLtGt = FeatureMatrix.BackrefModeEnum.Pattern,
                Backref_gNum = FeatureMatrix.BackrefModeEnum.Pattern,
                Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
                Backref_PEqName = true,
                AllowSpacesInBackref = false,

                Recursive_Num = false,
                Recursive_PlusMinusNum = false,
                Recursive_R = false,
                Recursive_Name = false,
                Recursive_PGtName = true,
                Recursive_ReturnGroups = false,

                Quantifier_Asterisk = true,
                Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.XModeOnly,
                Quantifier_LowAbbrev = true,
                Quantifier_Lazy = true,

                Conditional_BackrefByNumber = true,
                Conditional_BackrefByName = false,
                Conditional_Pattern = true,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = true,
                Conditional_BackrefByName_LtGt = true,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = false,
                Conditional_VERSION = false,

                ControlVerbs = true,
                ScriptRuns = false,
                Callouts = false,

                EmptyConstruct = false,
                EmptyConstructX = true,
                EmptySet = false,
                EmptySetAny = false,

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = isUnicode,
            };
        }

        private static FeatureMatrix BuildFeatureMatrix_Regress( StructEnum @struct, bool isUnicode, bool isUnicodeSets )
        {
            isUnicodeSets &= @struct == StructEnum.RegexBuilder;

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
                ScopedFlags = true,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = false,
                XXFlag = false,

                Literal_QE = false,
                InsideSets_Literal_QE = false,
                InsideSets_Literal_qBrace = isUnicodeSets,

                Esc_a = false,
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
                Esc_U8 = false,
                Esc_uBrace = true,
                Esc_UBrace = false,
                Esc_c1 = true,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = !isUnicode,

                InsideSets_Esc_a = false,
                InsideSets_Esc_b = !isUnicodeSets,
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
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = true,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = true,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = !isUnicode,

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
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = true,
                InsideSets_Class_Name = false,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = isUnicodeSets,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = isUnicodeSets,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = isUnicodeSets,
                InsideSets_Operator_DoubleTilde = false,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = false,
                Anchor_Z = false,
                Anchor_z = false,
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
                NamedGroup_LtGt = true,
                NamedGroup_PLtGt = false,
                BalancingGroup = false,
                CapturingGroup = false,
                DuplicateGroupName = true,

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
                NegativeLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
                AtomicGroup = false,
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
                EmptySet = true,
                EmptySetAny = true,

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
                Σσς = true,
            };
        }

        static FeatureMatrix BuildFeatureMatrix_Resharp( UnicodeModeEnum unicodeMode )
        {
            bool is_unicode = unicodeMode == UnicodeModeEnum.Full || unicodeMode == UnicodeModeEnum.Javascript;

            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
                AlternationOnSeparateLines = false,

                InlineComments = false,
                XModeComments = true,
                InsideSets_XModeComments = true,

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
                Esc_xBrace = true,
                Esc_u4 = true,
                Esc_U8 = true,
                Esc_uBrace = true,
                Esc_UBrace = true,
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
                InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = true,
                InsideSets_Esc_uBrace = true,
                InsideSets_Esc_UBrace = true,
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
                Anchor_Z = false,
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
                NamedGroup_LtGt = true,
                NamedGroup_PLtGt = true,
                BalancingGroup = false,
                CapturingGroup = false,
                DuplicateGroupName = false,

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = FeatureMatrix.LookModeEnum.None,
                NegativeLookbehind = FeatureMatrix.LookModeEnum.None,
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
                Quantifier_Lazy = false,

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

                AsciiOnly = false,
                SplitSurrogatePairs = !is_unicode,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = true,

                Ext_UniversalWildcard = true,
                Ext_Operator_Intersection = true,
                Ext_Operator_Complement = true,
            };
        }

        static FeatureMatrix BuildFeatureMatrix_Anre( )
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
                Esc_uBrace = true,
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
                InsideSets_Esc_uBrace = true,
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
                Anchor_A = false,
                Anchor_Z = false,
                Anchor_z = false,
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
                NamedGroup_LtGt = true,
                NamedGroup_PLtGt = false,
                BalancingGroup = false,
                CapturingGroup = false,
                DuplicateGroupName = true,

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = FeatureMatrix.LookModeEnum.FixedLength,
                NegativeLookbehind = FeatureMatrix.LookModeEnum.FixedLength,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = FeatureMatrix.BackrefEnum.None,
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
                EmptySet = true,
                EmptySetAny = true,

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
                Σσς = false,

                Ext_UniversalWildcard = false,
                Ext_Operator_Intersection = false,
                Ext_Operator_Complement = false,
            };
        }
    }
}
