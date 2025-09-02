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
        static readonly Lazy<string?> LazyVersion = new( GetVersion );
        readonly Lazy<UCOptions> mOptionsControl;
        static readonly LazyData<(ModuleEnum, int), FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );


        public Engine( )
        {
            mOptionsControl = new Lazy<UCOptions>( ( ) =>
            {
                var oc = new UCOptions( );
                oc.Changed += OptionsControl_Changed;

                return oc;
            } );
        }


        #region IRegexEngine

        public string Kind => "Python";

        public string? Version => LazyVersion.Value;

        public string Name => "Python";

        public string Subtitle => $"{Name} ({mOptionsControl.Value.GetSelectedModuleTitle( )})";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.NoCaptures | RegexEngineCapabilityEnum.OverlappingMatches;

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
            Options options = mOptionsControl.Value.GetSelectedOptions( );
            string json = JsonSerializer.Serialize( options, JsonUtilities.JsonOptions );

            return json;
        }


        public void ImportOptions( string? json )
        {
            Options options_obj;

            if( string.IsNullOrWhiteSpace( json ) )
            {
                options_obj = new Options( );
            }
            else
            {
                try
                {
                    options_obj = JsonSerializer.Deserialize<Options>( json, JsonUtilities.JsonOptions )!;
                }
                catch
                {
                    // ignore versioning errors, for example
                    if( Debugger.IsAttached ) Debugger.Break( );

                    options_obj = new Options( );
                }
            }

            mOptionsControl.Value.SetSelectedOptions( options_obj );
        }


        public RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );

            return Matcher.GetMatches( cnc, pattern, text, options );
        }


        public SyntaxOptions GetSyntaxOptions( )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );

            return new SyntaxOptions
            {
                XLevel = options.VERBOSE ? XLevelEnum.x : XLevelEnum.none,
                FeatureMatrix = LazyFeatureMatrix.GetValue( (options.Module, options.Module == ModuleEnum.regex ? options.VERSION1 ? 1 : 0 : 0) )
            };
        }


        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            Engine engine_re = new( );
            engine_re.mOptionsControl.Value.SetSelectedOptions( new Options { Module = ModuleEnum.re, VERBOSE = true, VERSION0 = false, VERSION1 = false } );

            Engine engine_regex_v0 = new( );
            engine_regex_v0.mOptionsControl.Value.SetSelectedOptions( new Options { Module = ModuleEnum.regex, VERBOSE = true, VERSION0 = true, VERSION1 = false } );

            Engine engine_regex_v1 = new( );
            engine_regex_v1.mOptionsControl.Value.SetSelectedOptions( new Options { Module = ModuleEnum.regex, VERBOSE = true, VERSION0 = false, VERSION1 = true } );

            return
                [
                    new FeatureMatrixVariant("re", LazyFeatureMatrix.GetValue((ModuleEnum.re, 0)), engine_re),
                    new FeatureMatrixVariant("regex V0", LazyFeatureMatrix.GetValue((ModuleEnum.regex, 0)), engine_regex_v0),
                    new FeatureMatrixVariant("regex V1", LazyFeatureMatrix.GetValue((ModuleEnum.regex, 1)), engine_regex_v1)
                ];
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


        static FeatureMatrix BuildFeatureMatrix( (ModuleEnum module, int version) key )
        {
            bool is_regex = key.module == ModuleEnum.regex;

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
                GenericEscape = true,

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
                InsideSets_GenericEscape = true,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = true,
                Class_hHhexa = false,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = false,
                Class_O = false,
                Class_R = is_regex,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = true,
                Class_X = is_regex,
                Class_Not = false,
                Class_pP = is_regex,
                Class_pPBrace = is_regex,
                Class_Name = false,

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
                InsideSets_Class_pP = is_regex,
                InsideSets_Class_pPBrace = is_regex,
                InsideSets_Class_Name = is_regex,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = is_regex && key.version == 1,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = is_regex && key.version == 1,
                InsideSets_Operator_DoubleVerticalLine = is_regex && key.version == 1,
                InsideSets_Operator_DoubleMinus = is_regex && key.version == 1,
                InsideSets_Operator_DoubleTilde = is_regex && key.version == 1,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = true,
                Anchor_Z = true,
                Anchor_z = false,
                Anchor_G = is_regex,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = is_regex,
                Anchor_mM = is_regex,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = false,
                NamedGroup_LtGt = is_regex,
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
                BranchReset = is_regex,
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
                Backref_gApos = false,
                Backref_gLtGt = is_regex,
                Backref_gNum = false,
                Backref_gNegNum = false,
                Backref_gBrace = false,
                Backref_PEqName = true,
                AllowSpacesInBackref = false,

                Recursive_Num = is_regex,
                Recursive_PlusMinusNum = false,
                Recursive_R = is_regex,
                Recursive_Name = is_regex,
                Recursive_PGtName = is_regex,

                Quantifier_Asterisk = true,
                Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_FreeForm = is_regex ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
                Quantifier_LowAbbrev = true,

                Conditional_BackrefByNumber = true,
                Conditional_BackrefByName = true,
                Conditional_Pattern = is_regex,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = false,
                Conditional_BackrefByName_LtGt = false,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = is_regex,
                Conditional_VERSION = false,

                ControlVerbs = is_regex,
                ScriptRuns = false,
                Callouts = false,

                EmptyConstruct = is_regex,
                EmptyConstructX = is_regex,
                EmptySet = false,

                SplitSurrogatePairs = true,
                AllowDuplicateGroupName = is_regex,
                FuzzyMatchingParams = false,
            };
        }
    }
}
