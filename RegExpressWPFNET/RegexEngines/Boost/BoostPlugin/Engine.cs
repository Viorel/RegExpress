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


namespace BoostPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<Version> LazyVersion = new( GetVersion );
        readonly Lazy<UCOptions> mOptionsControl;
        static readonly LazyData<GrammarEnum, FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );
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

        public string Kind => "Boost";

        public Version Version => LazyVersion.Value;

        public string Name => "Boost.Regex";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.Default;

        public string? NoteForCaptures => "requires ‘match_extra’";

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
            var options = mOptionsControl.Value.GetSelectedOptions( );

            return LazyFeatureMatrix.GetValue( options.Grammar );
        }


        public GenericOptions GetGenericOptions( )
        {
            var options = mOptionsControl.Value.GetSelectedOptions( );

            return new GenericOptions
            {
                Literal = options.Grammar == GrammarEnum.literal,
                XLevel = options.mod_x ? XLevelEnum.x : XLevelEnum.none,
            };
        }


        public IReadOnlyList<(string variantName, FeatureMatrix fm)> GetFeatureMatrices( )
        {
            var list = new List<(string, FeatureMatrix)>( );

            foreach( GrammarEnum grammar in Enum.GetValues( typeof( GrammarEnum ) ) )
            {
                if( grammar == GrammarEnum.None ) continue;
                if( grammar == GrammarEnum.literal ) continue;

                var fm = LazyFeatureMatrix.GetValue( grammar );

                list.Add( (Enum.GetName( typeof( GrammarEnum ), grammar ), fm) );
            }

            return list;
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


        static FeatureMatrix BuildFeatureMatrix( GrammarEnum grammar )
        {
            bool is_perl =
                grammar == GrammarEnum.perl ||
                grammar == GrammarEnum.ECMAScript ||
                grammar == GrammarEnum.normal ||
                grammar == GrammarEnum.JavaScript ||
                grammar == GrammarEnum.JScript;

            bool is_POSIX_extended =
                grammar == GrammarEnum.extended ||
                grammar == GrammarEnum.egrep ||
                grammar == GrammarEnum.awk;

            bool is_POSIX_basic =
                grammar == GrammarEnum.basic ||
                grammar == GrammarEnum.sed ||
                grammar == GrammarEnum.grep ||
                grammar == GrammarEnum.emacs;

            bool is_emacs =
                grammar == GrammarEnum.emacs;

            return new FeatureMatrix
            {
                Parentheses = is_perl || is_POSIX_extended ? FeatureMatrix.PunctuationEnum.Normal : is_POSIX_basic ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = is_perl || is_POSIX_extended ? FeatureMatrix.PunctuationEnum.Normal :
                                is_emacs ? FeatureMatrix.PunctuationEnum.Backslashed :
                                FeatureMatrix.PunctuationEnum.None,

                InlineComments = is_perl || is_emacs, // using \(?# \) in emacs
                XModeComments = is_perl,
                InsideSets_XModeComments = false,

                Flags = is_perl,
                ScopedFlags = is_perl || is_emacs,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = false,
                XXFlag = false,

                Literal_QE = is_perl || is_POSIX_extended,
                InsideSets_Literal_QE = false,

                Esc_a = is_perl || is_POSIX_extended,
                Esc_b = false,
                Esc_e = is_perl || is_POSIX_extended,
                Esc_f = is_perl || is_POSIX_extended,
                Esc_n = is_perl || is_POSIX_extended,
                Esc_r = is_perl || is_POSIX_extended,
                Esc_t = is_perl || is_POSIX_extended,
                Esc_v = is_POSIX_extended,
                Esc_Octal0_1_3 = is_perl || is_POSIX_extended || is_POSIX_basic,
                Esc_Octal_1_3 = false,
                Esc_Octal_2_3 = false,
                Esc_oBrace = false,
                Esc_x2 = is_perl || is_POSIX_extended,
                Esc_xBrace = is_perl || is_POSIX_extended,
                Esc_u4 = false,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = is_perl || is_POSIX_extended,
                Esc_CMinus = false,
                Esc_NBrace = is_perl || is_POSIX_extended,
                GenericEscape = true,

                InsideSets_Esc_a = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_b = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_e = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_f = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_n = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_r = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_t = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_v = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_Octal0_1_3 = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_Octal_1_3 = false,
                InsideSets_Esc_Octal_2_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_xBrace = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_u4 = false,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = is_perl || grammar == GrammarEnum.awk || is_emacs,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = is_perl || grammar == GrammarEnum.awk || is_emacs,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = is_perl || is_POSIX_extended,
                Class_dD = is_perl || is_POSIX_extended,
                Class_hHhexa = false,
                Class_hHhorspace = is_perl || is_POSIX_extended,
                Class_lL = is_perl || is_POSIX_extended,
                Class_N = false,
                Class_O = false,
                Class_R = is_perl,
                Class_sS = is_perl || is_POSIX_extended,
                Class_sSx = is_emacs,
                Class_uU = is_perl || is_POSIX_extended,
                Class_vV = is_perl,
                Class_wW = is_perl || is_POSIX_extended || is_emacs,
                Class_X = is_perl || is_POSIX_extended,
                Class_Not = false,
                Class_pP = is_perl || is_POSIX_extended,
                Class_pPBrace = is_perl || is_POSIX_extended,

                InsideSets_Class_dD = true,
                InsideSets_Class_hHhexa = false,
                InsideSets_Class_hHhorspace = true,
                InsideSets_Class_lL = true,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = true,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = true,
                InsideSets_Class_vV = false, // TODO: it seems that [\V] works as NOT Esc_v? 
                InsideSets_Class_wW = true,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = false,
                InsideSets_Class = true,
                InsideSets_Equivalence = true,
                InsideSets_Collating = true,

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
                Anchor_A = is_perl || is_POSIX_extended || is_emacs,
                Anchor_Z = is_perl || is_POSIX_extended,
                Anchor_z = is_perl || is_POSIX_extended || is_emacs,
                Anchor_G = is_perl || is_POSIX_extended,
                Anchor_bB = is_perl || is_POSIX_extended || is_emacs,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = is_perl,
                Anchor_LtGt = is_perl || is_POSIX_extended || is_emacs,
                Anchor_GraveApos = is_perl || is_POSIX_extended || is_emacs,
                Anchor_yY = false,

                NamedGroup_Apos = is_perl || is_emacs,
                NamedGroup_LtGt = is_perl || is_emacs,
                NamedGroup_PLtGt = false,

                NoncapturingGroup = is_perl || is_emacs,
                PositiveLookahead = is_perl || is_emacs,
                NegativeLookahead = is_perl || is_emacs,
                PositiveLookbehind = is_perl || is_emacs,
                NegativeLookbehind = is_perl || is_emacs,
                AtomicGroup = is_perl,
                BranchReset = is_perl,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_1_9 = is_perl || is_POSIX_basic,
                Backref_Num = false,
                Backref_kApos = is_perl,
                Backref_kLtGt = is_perl,
                Backref_kBrace = is_perl,
                Backref_kNum = is_perl,
                Backref_kNegNum = is_perl,
                Backref_gApos = is_perl,
                Backref_gLtGt = is_perl,
                Backref_gNum = is_perl,
                Backref_gNegNum = is_perl,
                Backref_gBrace = is_perl,
                Backref_PEqName = false,
                AllowSpacesInBackref = false,

                Recursive_Num = is_perl,
                Recursive_PlusMinusNum = is_perl || is_emacs, // TODO: '-1' works, '+1' does not work for emacs
                Recursive_R = is_perl,
                Recursive_Name = is_perl,
                Recursive_PGtName = false,

                Quantifier_Asterisk = true,
                Quantifier_Plus = is_perl || is_POSIX_extended || is_emacs ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Question = is_perl || is_POSIX_extended || is_emacs ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces = is_perl || is_POSIX_extended ? FeatureMatrix.PunctuationEnum.Normal :
                                    is_POSIX_basic ? FeatureMatrix.PunctuationEnum.Backslashed :
                                    FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsage.Both,
                Quantifier_LowAbbrev = false,

                Conditional_BackrefByNumber = is_perl,
                Conditional_BackrefByName = false,
                Conditional_Pattern = false,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = is_perl,
                Conditional_BackrefByName_LtGt = is_perl,
                Conditional_R = is_perl,
                Conditional_RName = is_perl,
                Conditional_DEFINE = is_perl,
                Conditional_VERSION = false,

                ControlVerbs = is_perl,
                ScriptRuns = false,

                EmptyConstruct = false,
                EmptyConstructX = false,
                EmptySet = false,
            };
        }
    }
}
