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


namespace DartPlugin
{
    class Engine : IRegexEngine
    {
        static readonly LazyData<bool /*isUnicode*/, FeatureMatrix> LazyFeatureMatrix_RegExp = new( BuildFeatureMatrix_RegExp );
        static readonly LazyData<OnigurumaSyntaxEnum /*syntax*/, FeatureMatrix> LazyFeatureMatrix_OnigurumaDart = new( BuildFeatureMatrix_OnigurumaDart );


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

        public string Kind => "Dart";

        public string? Version => Versions.Dart;

        public string Name => "Dart";

        public string Subtitle => $"{Name} ({Options.package switch { PackageEnum.RegExp => "RegExp", PackageEnum.OnigurumaDart => "OnigRegex", _ => "unknown" }})";

        public RegexEngineCapabilityEnum Capabilities =>
            Options.package switch
            {
                PackageEnum.RegExp => RegexEngineCapabilityEnum.NoCaptures | RegexEngineCapabilityEnum.NoGroupIndex,
                PackageEnum.OnigurumaDart => RegexEngineCapabilityEnum.NoCaptures,
                _ => RegexEngineCapabilityEnum.NoCaptures | RegexEngineCapabilityEnum.NoGroupIndex
            };

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
            return Options.package switch
            {
                PackageEnum.RegExp => MatcherRegExp.GetMatches( cnc, pattern, text, Options ),
                PackageEnum.OnigurumaDart => MatcherOnigurumaDart.GetMatches( cnc, pattern, text, Options ),
                _ => throw new NotImplementedException( ),
            };
        }

        public SyntaxOptions GetSyntaxOptions( )
        {
            Options options = Options;
            FeatureMatrix fm = options.package switch
            {
                PackageEnum.RegExp => LazyFeatureMatrix_RegExp.GetValue( options.unicode ),
                PackageEnum.OnigurumaDart => LazyFeatureMatrix_OnigurumaDart.GetValue( options.OnigurumaSyntax ),
                _ => throw new NotImplementedException( ),
            };

            return new SyntaxOptions
            {
                Literal = false,
                XLevel = fm.XModeComments && options.extend ? XLevelEnum.x : XLevelEnum.none,
                FeatureMatrix = fm,
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
#if true
            List<FeatureMatrixVariant> matrices =
                [
                    new FeatureMatrixVariant( "RegExp", new Engine { Options = new Options { package = PackageEnum.RegExp, unicode = false }} ),
                    new FeatureMatrixVariant( "RegExp (unicode)", new Engine { Options = new Options { package = PackageEnum.RegExp, unicode = true }} ),
                    new FeatureMatrixVariant( "OnigRegex", new Engine { Options = new Options { package = PackageEnum.OnigurumaDart, OnigurumaSyntax = OnigurumaSyntaxEnum.onigSyntaxOniguruma }} ),
                ];
            return matrices;
#else
            // for investigations

            List<FeatureMatrixVariant> matrices = [];

            foreach( OnigurumaSyntaxEnum syntax in Enum.GetValues<OnigurumaSyntaxEnum>( ).Where( s => s != OnigurumaSyntaxEnum.None ) )
            {
                matrices.Add( new FeatureMatrixVariant( $"({Enum.GetName( syntax )!["onigSyntax".Length..]})", new Engine { Options = new Options { package = PackageEnum.OnigurumaDart, OnigurumaSyntax = syntax } } ) );
            }

            return matrices;
#endif
        }

        public void SetIgnoreCase( bool yes )
        {
            Options.caseInsensitive = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options.extend = yes;
            if( mOptionsControl.IsValueCreated ) mOptionsControl.Value.SetOptions( mOptions );
        }

        public void SetCollectCaptures( bool yes )
        {
        }

        #endregion


        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }

        static FeatureMatrix BuildFeatureMatrix_RegExp( bool isUnicode )
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
                ScopedFlags = true,
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
                Esc_f = true,
                Esc_n = true,
                Esc_r = true,
                Esc_t = true,
                Esc_v = true,
                Esc_Octal = isUnicode ? FeatureMatrix.OctalEnum.None : FeatureMatrix.OctalEnum.Octal_1_3,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = false,
                Esc_x2 = true,
                Esc_xBrace = false,
                Esc_u4 = true,
                Esc_U8 = false,
                Esc_uBrace = isUnicode,
                Esc_UBrace = false,
                Esc_c1 = true,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = !isUnicode,

                InsideSets_Esc_a = false,
                InsideSets_Esc_b = true,
                InsideSets_Esc_e = false,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = true,
                InsideSets_Esc_Octal = isUnicode ? FeatureMatrix.OctalEnum.None : FeatureMatrix.OctalEnum.Octal_1_3,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = true,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = isUnicode,
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
                Class_pPBrace = isUnicode,

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
                InsideSets_Class_pPBrace = isUnicode,
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
                Anchor_Z = FeatureMatrix.AnchorZModeEnum.None,
                Anchor_z = false,
                Anchor_G = false,
                Anchor_bB = true,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_PosixWB = false,
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
                NestedLookaround = true,
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
                Quantifier_Possessive = false,

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

                Unicode_Class_Dot = true,
                Unicode_Class_vW = false,
                InsideSets_Unicode = true,
                UnicodeCaseFolding = true,
                KeepSurrogatePairs = isUnicode,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
                Σσς = true,
                ßSS = false,
            };
        }

        static FeatureMatrix BuildFeatureMatrix_OnigurumaDart( OnigurumaSyntaxEnum syntax )
        {
            bool grp0 =
                syntax == OnigurumaSyntaxEnum.onigSyntaxOniguruma ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxRuby ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxPerl ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxPerlNg;

            bool grp1 =
                grp0 ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxJava ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxPython;

            bool grp2 =
                syntax == OnigurumaSyntaxEnum.onigSyntaxGrep ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxEmacs ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxPosixBasic;

            bool grp3 =
                syntax == OnigurumaSyntaxEnum.onigSyntaxPosixExtended ||
                syntax == OnigurumaSyntaxEnum.onigSyntaxGnuRegex;

            bool is_oniguruma = syntax == OnigurumaSyntaxEnum.onigSyntaxOniguruma;
            bool is_ruby = syntax == OnigurumaSyntaxEnum.onigSyntaxRuby;
            bool is_perl = syntax == OnigurumaSyntaxEnum.onigSyntaxPerl;
            bool is_perl_ng = syntax == OnigurumaSyntaxEnum.onigSyntaxPerlNg;
            bool is_perls = is_perl || is_perl_ng;
            bool is_java = syntax == OnigurumaSyntaxEnum.onigSyntaxJava;
            bool is_python = syntax == OnigurumaSyntaxEnum.onigSyntaxPython;
            bool is_grep = syntax == OnigurumaSyntaxEnum.onigSyntaxGrep;
            bool is_emacs = syntax == OnigurumaSyntaxEnum.onigSyntaxEmacs;
            bool is_posix_extended = syntax == OnigurumaSyntaxEnum.onigSyntaxPosixExtended;
            bool is_gnu = syntax == OnigurumaSyntaxEnum.onigSyntaxGnuRegex;

            return new FeatureMatrix
            {
                Parentheses = grp1 || grp3 ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.Backslashed,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = grp1 || grp3 ? FeatureMatrix.PunctuationEnum.Normal : is_grep || is_emacs ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                AlternationOnSeparateLines = false,

                InlineComments = grp1,
                XModeComments = true,
                InsideSets_XModeComments = false,

                Flags = grp1,
                ScopedFlags = grp1,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = grp1,
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
                Esc_v = is_oniguruma || is_ruby || is_java || is_python,
                Esc_Octal = FeatureMatrix.OctalEnum.Octal_2_3,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = grp0,
                Esc_x2 = grp1,
                Esc_xBrace = grp0,
                Esc_u4 = is_oniguruma || is_ruby || is_java || is_python,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = grp1,
                Esc_C1 = false,
                Esc_CMinus = is_oniguruma || is_ruby,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = true,
                InsideSets_Esc_b = true,
                InsideSets_Esc_e = true,
                InsideSets_Esc_f = true,
                InsideSets_Esc_n = true,
                InsideSets_Esc_r = true,
                InsideSets_Esc_t = true,
                InsideSets_Esc_v = true,
                InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_1_3,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = grp0,
                InsideSets_Esc_x2 = true,
                InsideSets_Esc_xBrace = true,
                InsideSets_Esc_u4 = is_oniguruma || is_ruby || is_java || is_python,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = grp1,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = is_oniguruma || is_ruby,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = true,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = true,
                Class_hHhexa = is_oniguruma || is_ruby,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = grp0,
                Class_O = grp0,
                Class_R = grp0,
                Class_sS = true,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = true,
                Class_X = grp0,
                Class_pP = is_oniguruma || is_perls,
                Class_pPBrace = grp1,

                InsideSets_Class_dD = true,
                InsideSets_Class_hHhexa = is_oniguruma || is_ruby,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = true,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = true,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = is_oniguruma || is_perls,
                InsideSets_Class_pPBrace = grp1,
                InsideSets_Class_Name = true,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = is_oniguruma || is_ruby || is_java,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = is_oniguruma || is_ruby || is_java,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = false,
                InsideSets_Operator_DoubleTilde = false,

                Anchor_Circumflex = true,
                Anchor_Dollar = true,
                Anchor_A = grp1 || is_gnu,
                Anchor_Z = grp1 || is_gnu ? FeatureMatrix.AnchorZModeEnum.Correct : FeatureMatrix.AnchorZModeEnum.None,
                Anchor_z = grp0 || is_java || is_gnu,
                Anchor_G = grp1 || is_gnu,
                Anchor_bB = grp1 || is_grep || is_gnu,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_PosixWB = false,
                Anchor_K = grp0 || is_python,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = grp0,

                NamedGroup_Apos = is_oniguruma || is_ruby || is_perl_ng,
                NamedGroup_LtGt = grp1 || is_emacs,
                NamedGroup_PLtGt = false,
                BalancingGroup = false,
                CapturingGroup = false,
                DuplicateGroupName = grp1,

                NoncapturingGroup = grp1 || is_emacs,
                PositiveLookahead = grp1 || is_emacs,
                NegativeLookahead = grp1 || is_emacs,
                PositiveLookbehind = grp1 || is_emacs ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
                NegativeLookbehind = grp1 || is_emacs ? FeatureMatrix.LookModeEnum.AnyLength : FeatureMatrix.LookModeEnum.None,
                NestedLookaround = true,
                AtomicGroup = grp1 || is_emacs,
                BranchReset = is_grep, //?
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = grp0,
                AllowSpacesInGroups = false,

                Backref_Num = FeatureMatrix.BackrefEnum.Any,
                Backref_kApos = is_oniguruma || is_ruby || is_perl_ng,
                Backref_kLtGt = is_oniguruma || is_ruby || is_perl_ng,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = is_oniguruma || is_ruby || is_perl_ng ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
                Backref_gLtGt = is_oniguruma || is_ruby || is_perl_ng ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
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
                Quantifier_Plus = grp1 || is_emacs || grp3 ? FeatureMatrix.PunctuationEnum.Normal : is_grep ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Question = grp1 || is_emacs || grp3 ? FeatureMatrix.PunctuationEnum.Normal : is_grep ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces = grp1 || grp3 ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
                Quantifier_LowAbbrev = grp1 || grp3,
                Quantifier_Lazy = grp1,
                Quantifier_Possessive = grp0 || is_java,

                Conditional_BackrefByNumber = grp0 || is_python,
                Conditional_BackrefByName = false,
                Conditional_Pattern = grp0 || is_python,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = is_oniguruma || is_ruby || is_perl_ng,
                Conditional_BackrefByName_LtGt = grp0 || is_python,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = is_oniguruma || is_ruby || is_perl_ng,
                Conditional_VERSION = false,

                ControlVerbs = is_oniguruma || is_perls || is_python,
                ScriptRuns = false,
                Callouts = false,

                EmptyConstruct = grp1,
                EmptyConstructX = false,
                EmptySet = false,
                EmptySetAny = false,

                Unicode_Class_Dot = true,
                Unicode_Class_vW = true,
                InsideSets_Unicode = true,
                UnicodeCaseFolding = true,
                KeepSurrogatePairs = true,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = true, // if not 'ignoreCaseIsAscii'
                ßSS = true, // if not 'ignoreCaseIsAscii'
            };
        }
    }
}
