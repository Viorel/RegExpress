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


namespace GoPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<string?> LazyVersion = new( GetVersion );
        static readonly LazyData<(PackageEnum package, bool isPoxis, bool isRE2), FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

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

        public string Kind => "Go";

        public string? Version => LazyVersion.Value;

        public string Name => "Go";

        public string Subtitle => $"{Name} ({Enum.GetName<PackageEnum>( Options.Package )})";

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
            FeatureMatrix fm = LazyFeatureMatrix.GetValue( (Options.Package, isPoxis: Options.posix_syntax, isRE2: Options.RE2) );
            bool is_regexp2 = Options.Package == PackageEnum.regexp2;

            return new SyntaxOptions
            {
                Literal = Options.literal,
                XLevel = ( is_regexp2 && Options.IgnorePatternWhitespace ) ? XLevelEnum.x : XLevelEnum.none,
                AllowEmptySets = fm.EmptySet,
                FeatureMatrix = fm,
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            return
                [
                    new FeatureMatrixVariant( "regexp", LazyFeatureMatrix.GetValue( (PackageEnum.regexp, isPoxis: false, isRE2: false) ), new Engine { Options = new Options { Package = PackageEnum.regexp, posix_syntax = false, RE2 = false }} ),
                    new FeatureMatrixVariant( "regexp (posix)", LazyFeatureMatrix.GetValue( (PackageEnum.regexp, isPoxis: true, isRE2: false) ), new Engine { Options = new Options { Package = PackageEnum.regexp, posix_syntax = true, RE2 = false }} ),
                    new FeatureMatrixVariant( "regexp2", LazyFeatureMatrix.GetValue( (PackageEnum.regexp2, isPoxis: false, isRE2: false) ), new Engine { Options = new Options { Package = PackageEnum.regexp2, posix_syntax = false, RE2 = false }} ),
                    new FeatureMatrixVariant( "rexa", LazyFeatureMatrix.GetValue( (PackageEnum.rexa, isPoxis: false, isRE2: false) ), new Engine { Options = new Options { Package = PackageEnum.rexa, posix_syntax = false, RE2 = false }} )
                ];
        }

        public void SetIgnoreCase( bool yes )
        {
            Options.IgnoreCase = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options.IgnorePatternWhitespace = yes;
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


        static FeatureMatrix BuildFeatureMatrix( (PackageEnum package, bool isPoxis, bool isRE2) data )
        {
            bool is_regexp = data.package == PackageEnum.regexp;
            bool is_normal_regexp = is_regexp && !data.isPoxis;
            bool is_regexp2 = data.package == PackageEnum.regexp2;
            bool is_rexa = data.package == PackageEnum.rexa;
            bool is_normal = is_normal_regexp || is_regexp2 || is_rexa;
            bool is_RE2 = data.isRE2;

            return new FeatureMatrix
            {
                Parentheses = FeatureMatrix.PunctuationEnum.Normal,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
                AlternationOnSeparateLines = false,

                InlineComments = is_regexp2,
                XModeComments = is_regexp2,
                InsideSets_XModeComments = false,

                Flags = is_normal,
                ScopedFlags = is_normal,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = is_regexp2,
                XXFlag = false,

                Literal_QE = is_normal_regexp,
                InsideSets_Literal_QE = false,
                InsideSets_Literal_qBrace = false,

                Esc_a = true,
                Esc_b = false,
                Esc_e = is_regexp2 || is_rexa,
                Esc_f = true,
                Esc_n = true,
                Esc_r = true,
                Esc_t = true,
                Esc_v = is_regexp || is_regexp2,
                Esc_Octal = is_regexp || is_regexp2 ? FeatureMatrix.OctalEnum.Octal_2_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = false,
                Esc_x2 = is_regexp || is_regexp2,
                Esc_xBrace = is_regexp || is_regexp2,
                Esc_u4 = is_regexp2,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = is_regexp2,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = true,
                InsideSets_Esc_b = is_regexp2,
                InsideSets_Esc_e = is_regexp2 || is_rexa,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = is_regexp || is_regexp2,
                InsideSets_Esc_Octal = is_regexp ? FeatureMatrix.OctalEnum.Octal_2_3 : is_regexp2 ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = is_regexp || is_regexp2,
                InsideSets_Esc_xBrace = is_regexp || is_regexp2,
                InsideSets_Esc_u4 = is_regexp2,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = is_regexp2,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = true,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = is_normal,
                Class_hHhexa = false,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = false,
                Class_O = false,
                Class_R = false,
                Class_sS = is_normal,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = is_normal,
                Class_X = false,
                Class_pP = is_normal,
                Class_pPBrace = is_normal,

                InsideSets_Class_dD = is_normal,
                InsideSets_Class_hHhexa = false,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = is_normal_regexp || is_regexp2,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = is_normal,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = is_normal_regexp || is_regexp2,
                InsideSets_Class_pPBrace = is_normal_regexp || is_regexp2,
                InsideSets_Class_Name = is_regexp,
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
                Anchor_A = is_normal,
                Anchor_Z = is_regexp2,
                Anchor_z = is_normal,
                Anchor_G = is_regexp2,
                Anchor_bB = is_normal,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = false,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = is_regexp2,
                NamedGroup_LtGt = is_normal,
                NamedGroup_PLtGt = is_normal_regexp || is_rexa || ( is_regexp2 && is_RE2 ),
                BalancingGroup = is_regexp2,
                CapturingGroup = false,

                NoncapturingGroup = is_normal,
                PositiveLookahead = is_regexp2 || is_rexa,
                NegativeLookahead = is_regexp2 || is_rexa,
                PositiveLookbehind = is_regexp2 || is_rexa ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
                NegativeLookbehind = is_regexp2 || is_rexa ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
                AtomicGroup = is_regexp2 || is_rexa,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = is_regexp2 ? FeatureMatrix.BackrefEnum.Any : is_rexa ? FeatureMatrix.BackrefEnum.OneDigit : FeatureMatrix.BackrefEnum.None,
                Backref_kApos = is_regexp2,
                Backref_kLtGt = is_regexp2,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
                Backref_gLtGt = FeatureMatrix.BackrefModeEnum.None,
                Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
                Backref_PEqName = is_regexp2 && is_RE2,
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
                Quantifier_Lazy = is_normal,

                Conditional_BackrefByNumber = is_regexp2,
                Conditional_BackrefByName = is_regexp2,
                Conditional_Pattern = is_regexp2,
                Conditional_PatternOrBackrefByName = is_regexp2,
                Conditional_BackrefByName_Apos = false,
                Conditional_BackrefByName_LtGt = false,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = false,
                Conditional_VERSION = false,

                ControlVerbs = false,
                ScriptRuns = false,
                Callouts = false,

                EmptyConstruct = is_normal_regexp,
                EmptyConstructX = false,
                EmptySet = is_rexa,
                EmptySetAny = is_rexa,

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                AllowDuplicateGroupName = is_normal,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = is_regexp || is_rexa ? FeatureMatrix.CatastrophicBacktrackingEnum.Accept : FeatureMatrix.CatastrophicBacktrackingEnum.None,
                Σσς = false,
            };
        }
    }
}
