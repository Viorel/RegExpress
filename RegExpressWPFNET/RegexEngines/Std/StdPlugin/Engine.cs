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


namespace StdPlugin
{
    class Engine : IRegexEngine
    {
        readonly Lazy<UCOptions> mOptionsControl;
        static readonly LazyData<(CompilerEnum, GrammarEnum), FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

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

        public string Kind => "Std";

        public string? Version => ""; // (versions are displayed for each compiler later)

        public string Name => "std::wregex";

        public string Subtitle => $"{Name}{mOptionsControl.Value.GetSelectedOptions( ).Compiler switch { CompilerEnum.MSVC => "", CompilerEnum.GCC => " (GCC)", _ => " (Unknown)" }}";

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

            return options.Compiler switch
            {
                CompilerEnum.MSVC => MatcherMSVC.GetMatches( cnc, pattern, text, options ),
                CompilerEnum.GCC => MatcherGCC.GetMatches( cnc, pattern, text, options ),
                _ => throw new InvalidOperationException( )
            };
        }

        public SyntaxOptions GetSyntaxOptions( )
        {
            var options = mOptionsControl.Value.GetSelectedOptions( );

            return new SyntaxOptions
            {
                XLevel = XLevelEnum.none,
                AllowEmptySets = options.Grammar == GrammarEnum.ECMAScript,
                FeatureMatrix = LazyFeatureMatrix.GetValue( (options.Compiler, options.Grammar) )
            };
        }

        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            List<FeatureMatrixVariant> variants = [];

            foreach( GrammarEnum grammar in Enum.GetValues<GrammarEnum>( ) )
            {
                if( grammar == GrammarEnum.None ) continue;

                Engine engine = new( );
                engine.mOptionsControl.Value.SetSelectedOptions( new Options { Compiler = CompilerEnum.MSVC, Grammar = grammar } );

                variants.Add( new FeatureMatrixVariant( Enum.GetName( grammar ), LazyFeatureMatrix.GetValue( (CompilerEnum.MSVC, grammar) ), engine ) );
            }

            {
                GrammarEnum grammar = GrammarEnum.ECMAScript;

                Engine engine = new( );
                engine.mOptionsControl.Value.SetSelectedOptions( new Options { Compiler = CompilerEnum.GCC, Grammar = grammar } );

                variants.Add( new FeatureMatrixVariant( $"{Enum.GetName( grammar )} (GCC)", LazyFeatureMatrix.GetValue( (CompilerEnum.GCC, grammar) ), engine ) );
            }

            return variants;
        }

        #endregion

        private void OptionsControl_Changed( object? sender, RegexEngineOptionsChangedArgs args )
        {
            OptionsChanged?.Invoke( this, args );
        }

        static FeatureMatrix BuildFeatureMatrix( (CompilerEnum compiler, GrammarEnum grammar) data )
        {
            return data.compiler switch
            {
                CompilerEnum.MSVC => BuildFeatureMatrixMSVC( data.grammar ),
                CompilerEnum.GCC => BuildFeatureMatrixGCC( data.grammar ),
                _ => throw new InvalidOperationException( )
            };
        }

        static FeatureMatrix BuildFeatureMatrixMSVC( GrammarEnum grammar )
        {
            return new FeatureMatrix
            {
                Parentheses = grammar == GrammarEnum.extended ||
                                grammar == GrammarEnum.ECMAScript ||
                                grammar == GrammarEnum.egrep ||
                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                :
                                grammar == GrammarEnum.basic ||
                                grammar == GrammarEnum.grep ? FeatureMatrix.PunctuationEnum.Backslashed
                                :
                                FeatureMatrix.PunctuationEnum.None,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = grammar == GrammarEnum.extended ||
                                            grammar == GrammarEnum.ECMAScript ||
                                            grammar == GrammarEnum.egrep ||
                                            grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                            : FeatureMatrix.PunctuationEnum.None,
                AlternationOnSeparateLines = grammar == GrammarEnum.grep || grammar == GrammarEnum.egrep,

                InlineComments = false,
                XModeComments = false,
                InsideSets_XModeComments = false,

                Flags = false,
                ScopedFlags = false,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = false,
                XXFlag = false,

                Literal_QE = false,
                InsideSets_Literal_QE = false,
                InsideSets_Literal_qBrace = false,

                Esc_a = grammar == GrammarEnum.awk,
                Esc_b = grammar == GrammarEnum.awk,
                Esc_e = false,
                Esc_f = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_n = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_r = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_t = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_v = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_Octal = grammar == GrammarEnum.awk ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = false,
                Esc_x2 = grammar == GrammarEnum.ECMAScript,
                Esc_xBrace = false,
                Esc_u4 = grammar == GrammarEnum.ECMAScript,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = grammar == GrammarEnum.ECMAScript,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = grammar == GrammarEnum.awk,
                InsideSets_Esc_b = grammar == GrammarEnum.awk,
                InsideSets_Esc_e = false,
                InsideSets_Esc_f = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_n = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_r = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_t = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_v = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = grammar == GrammarEnum.ECMAScript,
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = grammar == GrammarEnum.ECMAScript,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = grammar == GrammarEnum.ECMAScript, // is seems that '[\cM]' matches 'M', not '\r';
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = grammar == GrammarEnum.ECMAScript,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = grammar == GrammarEnum.ECMAScript,
                Class_hHhexa = false,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = false,
                Class_O = false,
                Class_R = false,
                Class_sS = grammar == GrammarEnum.ECMAScript,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = grammar == GrammarEnum.ECMAScript,
                Class_X = false,
                Class_Not = false,
                Class_pP = false,
                Class_pPBrace = false,
                Class_Name = false,

                InsideSets_Class_dD = grammar == GrammarEnum.ECMAScript,
                InsideSets_Class_hHhexa = false,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = grammar == GrammarEnum.ECMAScript,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = grammar == GrammarEnum.ECMAScript,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = false,
                InsideSets_Class_Name = true,
                InsideSets_Equivalence = true,
                InsideSets_Collating = true, // TODO: it seems to be a defect of STL; it always matches the last (any) character

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
                Anchor_bB = grammar == GrammarEnum.ECMAScript,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = false,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = false,
                NamedGroup_LtGt = false,
                NamedGroup_PLtGt = false,
                NamedGroup_AtApos = false,
                NamedGroup_AtLtGt = false,
                CapturingGroup = false,

                NoncapturingGroup = grammar == GrammarEnum.ECMAScript,
                PositiveLookahead = grammar == GrammarEnum.ECMAScript,
                NegativeLookahead = grammar == GrammarEnum.ECMAScript,
                PositiveLookbehind = false,
                NegativeLookbehind = false,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = grammar == GrammarEnum.basic || grammar == GrammarEnum.grep ? FeatureMatrix.BackrefEnum.OneDigit :
                              grammar == GrammarEnum.ECMAScript ? FeatureMatrix.BackrefEnum.Any : FeatureMatrix.BackrefEnum.None,
                Backref_kApos = false,
                Backref_kLtGt = false,
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

                Quantifier_Asterisk = true,
                Quantifier_Plus = grammar == GrammarEnum.extended ||
                                                grammar == GrammarEnum.ECMAScript ||
                                                grammar == GrammarEnum.egrep ||
                                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Question = grammar == GrammarEnum.extended ||
                                                grammar == GrammarEnum.ECMAScript ||
                                                grammar == GrammarEnum.egrep ||
                                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces = grammar == GrammarEnum.extended ||
                                                grammar == GrammarEnum.ECMAScript ||
                                                grammar == GrammarEnum.egrep ||
                                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                                :
                                                grammar == GrammarEnum.basic ||
                                                grammar == GrammarEnum.grep ? FeatureMatrix.PunctuationEnum.Backslashed
                                                : FeatureMatrix.PunctuationEnum.None,
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

                EmptyConstruct = false,
                EmptyConstructX = false,
                EmptySet = grammar == GrammarEnum.ECMAScript,

                AsciiOnly = false,
                SplitSurrogatePairs = true,
                AllowDuplicateGroupName = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Reject,

            };
        }

        static FeatureMatrix BuildFeatureMatrixGCC( GrammarEnum grammar )
        {
            return new FeatureMatrix
            {
                Parentheses = grammar == GrammarEnum.extended ||
                                grammar == GrammarEnum.ECMAScript ||
                                grammar == GrammarEnum.egrep ||
                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                :
                                grammar == GrammarEnum.basic ||
                                grammar == GrammarEnum.grep ? FeatureMatrix.PunctuationEnum.Backslashed
                                :
                                FeatureMatrix.PunctuationEnum.None,

                Brackets = true,
                ExtendedBrackets = false,

                VerticalLine = grammar == GrammarEnum.extended ||
                                            grammar == GrammarEnum.ECMAScript ||
                                            grammar == GrammarEnum.egrep ||
                                            grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                            : FeatureMatrix.PunctuationEnum.None,
                AlternationOnSeparateLines = grammar == GrammarEnum.grep || grammar == GrammarEnum.egrep,

                InlineComments = false,
                XModeComments = false,
                InsideSets_XModeComments = false,

                Flags = false,
                ScopedFlags = false,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = false,
                XXFlag = false,

                Literal_QE = false,
                InsideSets_Literal_QE = false,
                InsideSets_Literal_qBrace = false,

                Esc_a = grammar == GrammarEnum.awk,
                Esc_b = grammar == GrammarEnum.awk,
                Esc_e = false,
                Esc_f = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_n = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_r = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_t = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_v = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                Esc_Octal = grammar == GrammarEnum.awk ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = false,
                Esc_x2 = grammar == GrammarEnum.ECMAScript,
                Esc_xBrace = false,
                Esc_u4 = grammar == GrammarEnum.ECMAScript,
                Esc_U8 = false,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = false, // is seems that '\cM' matches 'M', not '\r'; '\c.' matches '.'grammar == GrammarEnum.ECMAScript,
                Esc_C1 = false,
                Esc_CMinus = false,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = grammar == GrammarEnum.awk,
                InsideSets_Esc_b = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_e = false,
                InsideSets_Esc_f = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_n = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_r = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_t = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_v = grammar == GrammarEnum.ECMAScript || grammar == GrammarEnum.awk,
                InsideSets_Esc_Octal = grammar == GrammarEnum.awk ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = false,
                InsideSets_Esc_x2 = grammar == GrammarEnum.ECMAScript,
                InsideSets_Esc_xBrace = false,
                InsideSets_Esc_u4 = grammar == GrammarEnum.ECMAScript,
                InsideSets_Esc_U8 = false,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = false, // is seems that '[\cM]' matches 'M', not '\r';
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = false,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = grammar == GrammarEnum.ECMAScript,

                Class_Dot = true,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = grammar == GrammarEnum.ECMAScript,
                Class_hHhexa = false,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = false,
                Class_O = false,
                Class_R = false,
                Class_sS = grammar == GrammarEnum.ECMAScript,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = grammar == GrammarEnum.ECMAScript,
                Class_X = false,
                Class_Not = false,
                Class_pP = false,
                Class_pPBrace = false,
                Class_Name = false,

                InsideSets_Class_dD = grammar == GrammarEnum.ECMAScript,
                InsideSets_Class_hHhexa = false,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = grammar == GrammarEnum.ECMAScript,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = grammar == GrammarEnum.ECMAScript,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = false,
                InsideSets_Class_Name = true,
                InsideSets_Equivalence = true,
                InsideSets_Collating = true, // TODO: it seems to be a defect of STL; it always matches the last (any) character

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
                Anchor_bB = grammar == GrammarEnum.ECMAScript,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = false,
                Anchor_mM = false,
                Anchor_LtGt = false,
                Anchor_GraveApos = false,
                Anchor_yY = false,

                NamedGroup_Apos = false,
                NamedGroup_LtGt = false,
                NamedGroup_PLtGt = false,
                NamedGroup_AtApos = false,
                NamedGroup_AtLtGt = false,
                CapturingGroup = false,

                NoncapturingGroup = grammar == GrammarEnum.ECMAScript,
                PositiveLookahead = grammar == GrammarEnum.ECMAScript,
                NegativeLookahead = grammar == GrammarEnum.ECMAScript,
                PositiveLookbehind = false,
                NegativeLookbehind = false,
                AtomicGroup = false,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = false,
                AllowSpacesInGroups = false,

                Backref_Num = grammar == GrammarEnum.basic || grammar == GrammarEnum.grep ? FeatureMatrix.BackrefEnum.OneDigit :
                              grammar == GrammarEnum.ECMAScript ? FeatureMatrix.BackrefEnum.Any : FeatureMatrix.BackrefEnum.None,
                Backref_kApos = false,
                Backref_kLtGt = false,
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

                Quantifier_Asterisk = true,
                Quantifier_Plus = grammar == GrammarEnum.extended ||
                                                grammar == GrammarEnum.ECMAScript ||
                                                grammar == GrammarEnum.egrep ||
                                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Question = grammar == GrammarEnum.extended ||
                                                grammar == GrammarEnum.ECMAScript ||
                                                grammar == GrammarEnum.egrep ||
                                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces = grammar == GrammarEnum.extended ||
                                                grammar == GrammarEnum.ECMAScript ||
                                                grammar == GrammarEnum.egrep ||
                                                grammar == GrammarEnum.awk ? FeatureMatrix.PunctuationEnum.Normal
                                                :
                                                grammar == GrammarEnum.basic ||
                                                grammar == GrammarEnum.grep ? FeatureMatrix.PunctuationEnum.Backslashed
                                                : FeatureMatrix.PunctuationEnum.None,
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

                EmptyConstruct = false,
                EmptyConstructX = false,
                EmptySet = grammar == GrammarEnum.ECMAScript,

                AsciiOnly = false,
                SplitSurrogatePairs = true,
                AllowDuplicateGroupName = false,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.None,
            };
        }
    }
}
