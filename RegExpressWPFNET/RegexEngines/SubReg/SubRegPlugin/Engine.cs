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


namespace SubRegPlugin
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

        public string Kind => "SubReg";

        public Version Version => LazyVersion.Value;

        public string Name => "SubReg";

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
            return new GenericOptions
            {
                XLevel = XLevelEnum.none,
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


        private static FeatureMatrix BuildFeatureMatrix( )
        {
            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = false,
                ExtendedBrackets = false,

                Esc_a = false,
                Esc_b = true,
                Esc_e = false,
                Esc_f = true,
                Esc_n = true,
                Esc_r = true,
                Esc_t = true,
                Esc_v = true,
                Esc_Octal0_1_3 = false,
                Esc_Octal_1_3 = false,
                Esc_Octal_2_3 = false,
                Esc_oBrace = false,
                Esc_x2 = true,
                Esc_xBrace = false,
                Esc_u4 = false,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = false,
                InsideSets_Esc_b = false,
                InsideSets_Esc_e = false,
                InsideSets_Esc_f = false,
                InsideSets_Esc_n = false,
                InsideSets_Esc_r = false,
                InsideSets_Esc_t = false,
                InsideSets_Esc_v = false,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_Octal_1_3 = false,
                InsideSets_Esc_Octal_2_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = false,
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = false,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = false,
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
                Class_N = false,
                Class_O = false,
                Class_R = false,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = true,
                Class_X = false,
                Class_Not = true,
                Class_pP = false,
                Class_pPBrace = false,

                InsideSets_Class_dD = false,
                InsideSets_Class_hHhexa = true,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = false,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = false,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = false,
                InsideSets_Class = false,
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
                Anchor_bB = false,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = false,
                NamedGroup_LtGt = false,
                NamedGroup_PLtGt = false,

                NoncapturingGroup = true,
                PositiveLookahead = true,
                NegativeLookahead = true,
                PositiveLookbehind = false,
                NegativeLookbehind = false,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_1_9 = false,
                Backref_Num = false,
                Backref_kApos = false,
                Backref_kLtGt = false,
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
                Quantifier_Braces = FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsage.None,
                Quantifier_LowAbbrev = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,

                InlineComments = false,
                XModeComments = false,
                InsideSets_XModeComments = false,

                Flags = true,
                ScopedFlags = false,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = false,
                XXFlag = false,

                Literal_QE = false,
                InsideSets_Literal_QE = false,

                Conditional_BackrefByNumber = false,
                Conditional_BackrefByName = false,
                Conditional_BackrefByName_Apos = false,
                Conditional_BackrefByName_LtGt = false,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = false,
                Conditional_VERSION = false,

                ControlVerbs = false,
                ScriptRuns = false,

                EmptyConstruct = false,
                EmptyConstructX = false, // TODO: "a(? )b": with "xabc" no error, with "ab" gives error

                EmptySet = false,
            };
        }
    }
}