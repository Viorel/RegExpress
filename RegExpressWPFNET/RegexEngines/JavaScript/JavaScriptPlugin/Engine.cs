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


namespace JavaScriptPlugin
{
    class Engine : IRegexEngine
    {
        readonly Lazy<UCOptions> mOptionsControl;
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrixUnicodeUnaware = new( ( ) => BuildFeatureMatrix( uFlag: false, vFlag: false ) );
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrixWithUFlag = new( ( ) => BuildFeatureMatrix( uFlag: true, vFlag: false ) );
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrixWithVFlag = new( ( ) => BuildFeatureMatrix( uFlag: false, vFlag: true ) );


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

        public string Kind => "JavaScript";

        public string? Version => ""; // (versions are supplied by Runtimes, displayed in combobox)

        public string Name => "JavaScript";

        public string Subtitle
        {
            get
            {
                Options opt = mOptionsControl.Value.GetSelectedOptions( );

                return opt.Runtime switch
                {
                    RuntimeEnum.WebView2 => "JavaScript (WebView2)",
                    RuntimeEnum.NodeJs => "JavaScript (Node.js)",
                    _ => "Unknown"
                };
            }
        }

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

            return options.Runtime switch
            {
                RuntimeEnum.WebView2 => MatcherWebView2.GetMatches( cnc, pattern, text, options ),
                RuntimeEnum.NodeJs => MatcherNodeJs.GetMatches( cnc, pattern, text, options ),
                _ => throw new NotSupportedException( ),
            };
        }

        public SyntaxOptions GetSyntaxOptions( )
        {
            var options = mOptionsControl.Value.GetSelectedOptions( );

            return new SyntaxOptions
            {
                XLevel = XLevelEnum.none,
                AllowEmptySets = true,
                FeatureMatrix = ( options.v ? LazyFeatureMatrixWithVFlag : options.u ? LazyFeatureMatrixWithUFlag : LazyFeatureMatrixUnicodeUnaware ).Value
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            Engine wv_engine_no_uv = new( );
            wv_engine_no_uv.mOptionsControl.Value.SetSelectedOptions( new Options { Runtime = RuntimeEnum.WebView2, u = false, v = false } );

            Engine wv_engine_u = new( );
            wv_engine_u.mOptionsControl.Value.SetSelectedOptions( new Options { Runtime = RuntimeEnum.WebView2, u = true, v = false } );

            Engine wv_engine_v = new( );
            wv_engine_v.mOptionsControl.Value.SetSelectedOptions( new Options { Runtime = RuntimeEnum.WebView2, u = false, v = true } );

            //Engine nd_engine_no_uv = new( );
            //nd_engine_no_uv.mOptionsControl.Value.SetSelectedOptions( new Options { Runtime = RuntimeEnum.NodeJs, u = false, v = false } );

            //Engine nd_engine_u = new( );
            //nd_engine_u.mOptionsControl.Value.SetSelectedOptions( new Options { Runtime = RuntimeEnum.NodeJs, u = true, v = false } );

            //Engine nd_engine_v = new( );
            //nd_engine_v.mOptionsControl.Value.SetSelectedOptions( new Options { Runtime = RuntimeEnum.NodeJs, u = false, v = true } );

            return
                [
                    new ("no “u”, “v” flags", LazyFeatureMatrixUnicodeUnaware.Value, wv_engine_no_uv),
                    new ("“u” flag", LazyFeatureMatrixWithUFlag.Value, wv_engine_u),
                    new ("“v” flag", LazyFeatureMatrixWithVFlag.Value, wv_engine_v),

                    //new ("no “u”, “v” flags", LazyFeatureMatrixUnicodeUnaware.Value, nd_engine_no_uv),
                    //new ("“u” flag", LazyFeatureMatrixWithUFlag.Value, nd_engine_u),
                    //new ("“v” flag", LazyFeatureMatrixWithVFlag.Value, nd_engine_v),
                ];
        }

        #endregion

        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }

        static FeatureMatrix BuildFeatureMatrix( bool uFlag, bool vFlag )
        {
            Debug.Assert( !( uFlag && vFlag ) );

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
                InsideSets_Literal_qBrace = vFlag,

                Esc_a = false,
                Esc_b = false,
                Esc_e = false,
                Esc_f = true,
                Esc_n = true,
                Esc_r = true,
                Esc_t = true,
                Esc_v = true,
                Esc_Octal = !uFlag && !vFlag ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = false,
                Esc_x2 = true,
                Esc_xBrace = false,
                Esc_u4 = true,
                Esc_U8 = false,
                Esc_uBrace = uFlag || vFlag,
                Esc_UBrace = false,
                Esc_c1 = true,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = false,
                InsideSets_Esc_b = true,
                InsideSets_Esc_e = false,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = true,
                InsideSets_Esc_Octal = !uFlag && !vFlag ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = uFlag || vFlag,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = true,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
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
                Class_R = false,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = true,
                Class_X = false,
                Class_Not = false,
                Class_pP = false,
                Class_pPBrace = uFlag || vFlag,
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
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = uFlag || vFlag,
                InsideSets_Class_Name = false,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = vFlag,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = vFlag,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = vFlag,
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

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = true,
                NegativeLookbehind = true,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = FeatureMatrix.BackrefEnum.Any,
                Backref_kApos = false,
                Backref_kLtGt = true,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = false,
                Backref_gLtGt = false,
                Backref_gNum = false,
                Backref_gNegNum = false,
                Backref_gBrace = false,
                Backref_PEqName = false,
                AllowSpacesInBackref = false,

                Recursive_Num = false,
                Recursive_PlusMinusNum = false,
                Recursive_R = false,
                Recursive_Name = false,
                Recursive_PGtName = false,

                Quantifier_Asterisk = true,
                Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
                Quantifier_LowAbbrev = false,

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

                SplitSurrogatePairs = true,
            };
        }
    }
}
