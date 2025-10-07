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


namespace CppBuilderPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<string?> LazyVersion = new( GetVersion );
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

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

                if( mOptionsControl.IsValueCreated )
                {
                    mOptionsControl.Value.SetOptions( mOptions );
                }
            }
        }

        #region IRegexEngine

        public string Kind => "TRegEx";

        public string? Version => LazyVersion.Value;

        public string Name => "TRegEx (C++Builder, Delphi)";

        public string Subtitle => "TRegEx";

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
            return new SyntaxOptions
            {
                XLevel = Options.roIgnorePatternSpace ? XLevelEnum.x : XLevelEnum.none,
                AllowEmptySets = false,
                FeatureMatrix = LazyFeatureMatrix.Value,
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            return 
                [
                    new FeatureMatrixVariant( $"TRegEx (C++Builder, Delphi)", LazyFeatureMatrix.Value, new Engine() )
                ];
        }

        public void SetIgnoreCase( bool yes )
        {
            Options.roIgnoreCase = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options.roIgnorePatternSpace = yes;
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

        static FeatureMatrix BuildFeatureMatrix( )
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
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
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
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
                Quantifier_LowAbbrev = false,

                Conditional_BackrefByNumber = true,
                Conditional_BackrefByName = true,
                Conditional_Pattern = true,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = true,
                Conditional_BackrefByName_LtGt = true,
                Conditional_R = true,
                Conditional_RName = true,
                Conditional_DEFINE = true,
                Conditional_VERSION = false,

                ControlVerbs = true,
                ScriptRuns = false,
                Callouts = false,

                EmptyConstruct = true,
                EmptyConstructX = false,
                EmptySet = false,

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                AllowDuplicateGroupName = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = true,
            };
        }
    }
}
