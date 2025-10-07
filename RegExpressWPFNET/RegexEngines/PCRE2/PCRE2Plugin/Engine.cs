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


namespace PCRE2Plugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<string?> LazyVersion = new( GetVersion );

        Options mOptions = new( );
        readonly Lazy<UCOptions> mOptionsControl;
        readonly LazyData<(bool PCRE2_ALT_BSUX, bool PCRE2_EXTRA_ALT_BSUX, bool PCRE2_ALT_EXTENDED_CLASS, bool PCRE2_DUPNAMES), FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

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

                if( mOptionsControl.IsValueCreated )
                {
                    mOptionsControl.Value.SetOptions( mOptions );
                }
            }
        }

        #region IRegexEngine

        public string Kind => "PCRE2";

        public string? Version => LazyVersion.Value;

        public string Name => "PCRE2";

        public string Subtitle => $"{Name}";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.NoCaptures;

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
            return Matcher.GetMatches( cnc, pattern, text, Options );
        }

        public SyntaxOptions GetSyntaxOptions( )
        {
            bool is_literal = Options.PCRE2_LITERAL;
            bool is_extended = Options.PCRE2_EXTENDED;
            bool is_extended_more = Options.PCRE2_EXTENDED_MORE;
            bool allow_empty_set = Options.PCRE2_ALLOW_EMPTY_CLASS;

            return new SyntaxOptions
            {
                Literal = is_literal,
                XLevel = is_extended_more ? XLevelEnum.xx : is_extended ? XLevelEnum.x : XLevelEnum.none,
                AllowEmptySets = allow_empty_set,
                FeatureMatrix = LazyFeatureMatrix.GetValue( (PCRE2_ALT_BSUX: Options.PCRE2_ALT_BSUX, PCRE2_EXTRA_ALT_BSUX: Options.PCRE2_EXTRA_ALT_BSUX, PCRE2_ALT_EXTENDED_CLASS: Options.PCRE2_ALT_EXTENDED_CLASS, PCRE2_DUPNAMES: true) ),
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            Engine engine = new( )
            {
                Options = new Options
                {
                    PCRE2_ALT_BSUX = true,
                    PCRE2_EXTRA_ALT_BSUX = true,
                    PCRE2_ALT_EXTENDED_CLASS = true,
                    PCRE2_ALLOW_EMPTY_CLASS = true,
                    PCRE2_DUPNAMES = true,
                }
            };

            return
                [
                    new FeatureMatrixVariant( null, LazyFeatureMatrix.GetValue((PCRE2_ALT_BSUX:true, PCRE2_EXTRA_ALT_BSUX: true, PCRE2_ALT_EXTENDED_CLASS: true, PCRE2_DUPNAMES: true) ), engine)
                ];
        }
        public void SetIgnoreCase( bool yes )
        {
            Options.PCRE2_CASELESS = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options.PCRE2_EXTENDED = yes;
            Options.PCRE2_EXTENDED_MORE = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        #endregion


        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }


        static string? GetVersion( )
        {
            try
            {
                return Matcher.GetVersion( NonCancellable.Instance );
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }
        }

        static FeatureMatrix BuildFeatureMatrix( (bool PCRE2_ALT_BSUX, bool PCRE2_EXTRA_ALT_BSUX, bool PCRE2_ALT_EXTENDED_CLASS, bool PCRE2_DUPNAMES) options )
        {
            (bool PCRE2_ALT_BSUX, bool PCRE2_EXTRA_ALT_BSUX, bool PCRE2_ALT_EXTENDED_CLASS, bool PCRE2_DUPNAMES) = options;

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
                InsideSets_Esc_xBrace = !( PCRE2_ALT_BSUX | PCRE2_EXTRA_ALT_BSUX ),
                InsideSets_Esc_u4 = PCRE2_ALT_BSUX | PCRE2_EXTRA_ALT_BSUX,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = PCRE2_EXTRA_ALT_BSUX,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = true,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = true,

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
                Class_Not = false,
                Class_pP = true,
                Class_pPBrace = true,
                Class_Name = false,

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
                Anchor_Z = true,
                Anchor_z = true,
                Anchor_G = true,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = true,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = true,
                NamedGroup_LtGt = true,
                NamedGroup_PLtGt = true,
                NamedGroup_AtApos = false,
                NamedGroup_AtLtGt = false,
                CapturingGroup = false,

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = true,
                NegativeLookbehind = true,
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

                Quantifier_Asterisk = true,
                Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.Both,
                Quantifier_LowAbbrev = true,

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
                EmptySet = true,

                AsciiOnly = false,
                SplitSurrogatePairs = true,
                AllowDuplicateGroupName = PCRE2_DUPNAMES,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = false,
            };
        }
    }
}
