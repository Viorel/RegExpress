﻿using System;
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


namespace JavaPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<string?> LazyVersion = new( GetVersion );
        readonly Lazy<UCOptions> mOptionsControl;
        static readonly LazyData<PackageEnum, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );


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

        public string Kind => "Java";

        public string? Version => LazyVersion.Value;

        public string Name => "Java";

        public string Subtitle
        {
            get
            {
                string package = mOptionsControl.Value.GetSelectedPackageTitle( );

                return package == "regex" ? "Java" : $"Java ({package})";
            }
        }

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
            bool is_regex = options.Package == PackageEnum.regex;

            return new SyntaxOptions
            {
                Literal = is_regex && options.LITERAL,
                XLevel = is_regex && options.COMMENTS ? XLevelEnum.x : XLevelEnum.none,
                FeatureMatrix = LazyFeatureMatrix.GetValue( options.Package )
            };
        }


        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            Engine engine_regex = new( );
            engine_regex.mOptionsControl.Value.SetSelectedOptions( new Options { Package = PackageEnum.regex } );

            Engine engine_re2j = new( );
            engine_re2j.mOptionsControl.Value.SetSelectedOptions( new Options { Package = PackageEnum.re2j } );

            return
                [
                    new FeatureMatrixVariant("regex", LazyFeatureMatrix.GetValue(PackageEnum.regex), engine_regex),
                    new FeatureMatrixVariant("re2j", LazyFeatureMatrix.GetValue(PackageEnum.re2j), engine_re2j),
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


        static FeatureMatrix BuildFeatureMatrix( PackageEnum package )
        {
            bool is_regex = package == PackageEnum.regex;
            bool is_re2j = package == PackageEnum.re2j;

            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
                AlternationOnSeparateLines = false,

                InlineComments = false,
                XModeComments = is_regex,
                InsideSets_XModeComments = is_regex,

                Flags = true,
                ScopedFlags = true,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = is_regex,
                XXFlag = false,

                Literal_QE = true,
                InsideSets_Literal_QE = is_regex,
                InsideSets_Literal_qBrace = false,

                Esc_a = true,
                Esc_b = false,
                Esc_e = is_regex,
                Esc_f = true,
                Esc_n = true,
                Esc_r = true,
                Esc_t = true,
                Esc_v = is_re2j,
                Esc_Octal = is_re2j ? FeatureMatrix.OctalEnum.Octal_2_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = is_regex,
                Esc_oBrace = false,
                Esc_x2 = true,
                Esc_xBrace = true,
                Esc_u4 = is_regex,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = is_regex,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = is_regex,
                GenericEscape = true,

                InsideSets_Esc_a = true,
                InsideSets_Esc_b = false,
                InsideSets_Esc_e = is_regex,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = is_re2j,
                InsideSets_Esc_Octal = is_re2j ? FeatureMatrix.OctalEnum.Octal_2_3 : FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = is_regex,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = is_regex,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = is_regex,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = is_regex,
                InsideSets_GenericEscape = true,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = true,
                Class_hHhexa = false,
                Class_hHhorspace = is_regex,
                Class_lL = false,
                Class_N = false,
                Class_O = false,
                Class_R = is_regex,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = is_regex,
                Class_wW = true,
                Class_X = is_regex,
                Class_Not = false,
                Class_pP = true, // TODO: not documented? // TODO: in some engines it is case-sensitive or case-insensitive
                Class_pPBrace = true,
                Class_Name = false,

                InsideSets_Class_dD = true,
                InsideSets_Class_hHhexa = false,
                InsideSets_Class_hHhorspace = is_regex,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = true,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = is_regex,
                InsideSets_Class_wW = true,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = true,
                InsideSets_Class_pPBrace = true,
                InsideSets_Class_Name = is_re2j,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = is_regex,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = is_regex,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = false,
                InsideSets_Operator_DoubleTilde = false,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = true,
                Anchor_Z = is_regex,
                Anchor_z = true,
                Anchor_G = is_regex,
                Anchor_bB = true,
                Anchor_bg = is_regex,
                Anchor_bBBrace = false,
                Anchor_K = false,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = false,
                NamedGroup_LtGt = true,
                NamedGroup_PLtGt = is_re2j,
                NamedGroup_AtApos = false,
                NamedGroup_AtLtGt = false,
                CapturingGroup = false,

                NoncapturingGroup = true,
                PositiveLookahead = is_regex,
                NegativeLookahead = is_regex,
                PositiveLookbehind = is_regex,
                NegativeLookbehind = is_regex,
                AtomicGroup = is_regex,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = is_regex,

                Backref_Num = is_regex ? FeatureMatrix.BackrefEnum.Any : FeatureMatrix.BackrefEnum.None, // (if no group, digits are drop until a group is found)
                Backref_kApos = false,
                Backref_kLtGt = is_regex,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = false,
                Backref_gLtGt = false,
                Backref_gNum = false,
                Backref_gNegNum = false,
                Backref_gBrace = false,
                Backref_PEqName = false,
                AllowSpacesInBackref = is_regex,

                Recursive_Num = false,
                Recursive_PlusMinusNum = false,
                Recursive_R = false,
                Recursive_Name = false,
                Recursive_PGtName = false,

                Quantifier_Asterisk = true,
                Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
                Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
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

                EmptyConstruct = true,
                EmptyConstructX = is_regex,
                EmptySet = false,

                SplitSurrogatePairs = false,
            };
        }
    }
}
