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


namespace PCRE2Plugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<Version> LazyVersion = new( GetVersion );
        readonly Lazy<UCOptions> mOptionsControl;
        static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new Lazy<FeatureMatrix>( BuildFeatureMatrix );
        static readonly JsonSerializerOptions JsonOptions = new( ) { AllowTrailingCommas = true, IncludeFields = true, ReadCommentHandling = JsonCommentHandling.Skip, WriteIndented = true };


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

        public string Kind => "PCRE2";

        public Version Version => LazyVersion.Value;

        public string Name => "PCRE2";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.NoCaptures;

        public string? NoteForCaptures => null;

        public event RegexEngineOptionsChanged? OptionsChanged;


        public Control GetOptionsControl( )
        {
            return mOptionsControl.Value;
        }


        public string? ExportOptions( )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );
            string json = JsonSerializer.Serialize( options, JsonOptions );

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
                    options_obj = JsonSerializer.Deserialize<Options>( json, JsonOptions )!;
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


        public IMatcher ParsePattern( string pattern )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );

            return new Matcher( pattern, options );

        }

        public FeatureMatrix GetFeatureMatrix( )
        {
            return LazyFeatureMatrix.Value;
        }


        public GenericOptions GetGenericOptions( )
        {
            var options = mOptionsControl.Value.GetSelectedOptions( );

            bool is_literal = options.PCRE2_LITERAL;
            bool is_extended = options.PCRE2_EXTENDED;
            bool is_extended_more = options.PCRE2_EXTENDED_MORE;
            bool allow_empty_set = options.PCRE2_ALLOW_EMPTY_CLASS;

            return new GenericOptions
            {
                Literal = is_literal,
                XLevel = is_extended_more ? XLevelEnum.xx : is_extended ? XLevelEnum.x : XLevelEnum.none,
                AllowEmptySets = allow_empty_set,
            };
        }


        public IReadOnlyList<(string variantName, FeatureMatrix fm)> GetFeatureMatrices( )
        {
            return new List<(string, FeatureMatrix)> { (null, GetFeatureMatrix( )) };
        }

        #endregion


        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }


        static Version? GetVersion( )
        {
            return Matcher.GetVersion( NonCancellable.Instance );
        }


        static FeatureMatrix BuildFeatureMatrix( )
        {
            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,

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

                Esc_a = true,
                Esc_b = false,
                Esc_e = true,
                Esc_f = true,
                Esc_n = true,
                Esc_r = true,
                Esc_t = true,
                Esc_v = false,
                Esc_Octal0_1_3 = false,
                Esc_Octal_1_3 = false,
                Esc_Octal_2_3 = true,
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
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_Octal_1_3 = true,
                InsideSets_Esc_Octal_2_3 = false,
                InsideSets_Esc_oBrace = true,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = false,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = true,
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
                InsideSets_Class = true,
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
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = true,
                NamedGroup_LtGt = true,
                NamedGroup_PLtGt = true,

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

                Backref_1_9 = true,
                Backref_Num = false,
                Backref_kApos = true,
                Backref_kLtGt = true,
                Backref_kBrace = true,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = true,
                Backref_gLtGt = true,
                Backref_gNum = true,
                Backref_gNegNum = true,
                Backref_gBrace = true,
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
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsage.None,
                Quantifier_LowAbbrev = false,

                Conditional_BackrefByNumber = true,
                Conditional_BackrefByName = true,
                Conditional_Pattern = false,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = true,
                Conditional_BackrefByName_LtGt = true,
                Conditional_R = true,
                Conditional_RName = true,
                Conditional_DEFINE = true,
                Conditional_VERSION = true,

                ControlVerbs = true,
                ScriptRuns = true,

                EmptyConstruct = true,
                EmptyConstructX = false,
                EmptySet = true,
            };
        }
    }
}