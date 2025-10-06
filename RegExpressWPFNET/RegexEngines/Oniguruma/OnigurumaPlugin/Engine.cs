using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Controls;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;


namespace OnigurumaPlugin
{
    class Engine : IRegexEngine
    {
        static readonly Lazy<string?> LazyVersion = new( GetVersion );
        readonly Lazy<UCOptions> mOptionsControl;


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

        public string Kind => "Oniguruma";

        public string? Version => LazyVersion.Value;

        public string Name => "Oniguruma";

        public string Subtitle => $"{Name}";

        public RegexEngineCapabilityEnum Capabilities => RegexEngineCapabilityEnum.Default;

        public string? NoteForCaptures => "requires ‘ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY’";

        public event RegexEngineOptionsChanged? OptionsChanged;
        public event EventHandler? FeatureMatrixReady;


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
            var options = mOptionsControl.Value.GetSelectedOptions( );
            bool is_literal = options.Syntax == SyntaxEnum.ONIG_SYNTAX_ASIS;

            return new SyntaxOptions
            {
                Literal = is_literal,
                XLevel = options.ONIG_OPTION_EXTEND ? XLevelEnum.x : XLevelEnum.none,
                FeatureMatrix = is_literal ? mLastFeatureMatrix = default : TryGetFeatureMatrix( new Key( options ) )
            };
        }


        public IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( )
        {
            List<FeatureMatrixVariant> variants = [];

            foreach( SyntaxEnum syntax in Enum.GetValues<SyntaxEnum>( ) )
            {
                if( syntax == SyntaxEnum.None ) continue;
                if( syntax == SyntaxEnum.ONIG_SYNTAX_ASIS ) continue;

                string syntax_name = Enum.GetName( syntax )!;
                string variant = syntax_name.StartsWith( "ONIG_SYNTAX_" ) ? syntax_name["ONIG_SYNTAX_".Length..] : syntax_name;

                Engine engine = new( );
                Options options = new( )
                {
                    Syntax = syntax,
                    ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY = syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA,
                };
                engine.mOptionsControl.Value.SetSelectedOptions( options );

                variants.Add( new FeatureMatrixVariant( variant, MakeFeatureMatrix( options ), engine ) );
            }

            return variants;
        }

        public void SetIgnoreCase( bool yes )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );
            options.ONIG_OPTION_IGNORECASE = yes;
            mOptionsControl.Value.SetSelectedOptions( options );
        }

        public void SetIgnorePatternWhitespace( bool yes )
        {
            Options options = mOptionsControl.Value.GetSelectedOptions( );
            options.ONIG_OPTION_EXTEND = yes;
            mOptionsControl.Value.SetSelectedOptions( options );
        }

        #endregion


        class Key
        {
            public Options Options { get; init; }

            public Key( Options options )
            {
                Options = options;
            }

            public override bool Equals( object? obj )
            {
                return obj is Key key &&
                        EqualityComparer<Options>.Default.Equals( Options, key.Options );
            }

            public override int GetHashCode( )
            {
                return HashCode.Combine( Options );
            }
        }


        static readonly Dictionary<Key, Task<FeatureMatrix>> smFeatureMatrices = new( );
        FeatureMatrix mLastFeatureMatrix = default;

        FeatureMatrix TryGetFeatureMatrix( Key key )
        {
            lock( smFeatureMatrices )
            {
                bool is_failed = false;

                if( smFeatureMatrices.TryGetValue( key, out Task<FeatureMatrix>? task ) )
                {
                    if( task.IsCompleted ) return mLastFeatureMatrix = task.Result;

                    if( task.IsCanceled || task.IsFaulted )
                    {
                        smFeatureMatrices.Remove( key );

                        is_failed = true;
                    }
                    else
                    {
                        // running

                        // to minimise flickering, return the previous feature matrix
                        return mLastFeatureMatrix;
                    }
                }

                Options copy_of_options = key.Options.Clone( ); // detach (?) 

                Task<FeatureMatrix> new_task = Task.Run( ( ) =>
                {
                    if( is_failed ) Task.Delay( 111 );

                    return MakeFeatureMatrix( copy_of_options );
                } );

                // "This API supports the product infrastructure and is not intended to be used directly from your code"
                //new_task.GetAwaiter( ).OnCompleted( ( ) => FeatureMatrixReady?.Invoke( null, null! ) );

                new_task.ContinueWith( fm => FeatureMatrixReady?.Invoke( this, null! ) );

                smFeatureMatrices.Add( key, new_task );

                // to minimise flickering, return the previous feature matrix
                return mLastFeatureMatrix;
            }
        }


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


        private static FeatureMatrix MakeFeatureMatrix( Options options )
        {
            try
            {
                Details? details = Matcher.GetDetails( NonCancellable.Instance, options );

                return BuildFeatureMatrix( options.Syntax, details! );
            }
            catch( Exception exc )
            {
                _ = exc;

                if( Debugger.IsAttached ) Debugger.Break( );

                return default;
            }
        }


        static FeatureMatrix BuildFeatureMatrix( SyntaxEnum syntax, Details details )
        {
            Debug.Assert( !details.ONIG_SYN_OP_ESC_ASTERISK_ZERO_INF );

            return new FeatureMatrix
            {
                Parentheses = details.ONIG_SYN_OP_LPAREN_SUBEXP ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_LPAREN_SUBEXP ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,

                Brackets = details.ONIG_SYN_OP_BRACKET_CC,
                ExtendedBrackets = false,

                VerticalLine = details.ONIG_SYN_OP_VBAR_ALT ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_VBAR_ALT ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                AlternationOnSeparateLines = false,

                InlineComments = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT, //........
                XModeComments = true,
                InsideSets_XModeComments = false,

                Flags = details.ONIG_SYN_OP2_OPTION_PERL || details.ONIG_SYN_OP2_OPTION_RUBY || details.ONIG_SYN_OP2_OPTION_ONIGURUMA,
                ScopedFlags = details.ONIG_SYN_OP2_OPTION_PERL || details.ONIG_SYN_OP2_OPTION_RUBY || details.ONIG_SYN_OP2_OPTION_ONIGURUMA,
                CircumflexFlags = false,
                ScopedCircumflexFlags = false,
                XFlag = details.ONIG_SYN_OP2_OPTION_ONIGURUMA || details.ONIG_SYN_OP2_OPTION_PERL || details.ONIG_SYN_OP2_OPTION_RUBY,
                XXFlag = false,

                Literal_QE = details.ONIG_SYN_OP2_ESC_CAPITAL_Q_QUOTE,
                InsideSets_Literal_QE = false,
                InsideSets_Literal_qBrace = false,

                Esc_a = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
                Esc_b = false, // helper.ONIG_SYN_OP_ESC_CONTROL_CHARS, // TODO: does not seems to correspond to documentation; in some cases '\b' is 'b'
                Esc_e = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
                Esc_f = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
                Esc_n = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
                Esc_r = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
                Esc_t = details.ONIG_SYN_OP_ESC_CONTROL_CHARS,
                Esc_v = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_OP2_ESC_V_VTAB,
                Esc_Octal = details.ONIG_SYN_OP_ESC_OCTAL3 ? FeatureMatrix.OctalEnum.Octal_2_3 : FeatureMatrix.OctalEnum.None,
                Esc_Octal0_1_3 = false,
                Esc_oBrace = details.ONIG_SYN_OP_ESC_O_BRACE_OCTAL,
                Esc_x2 = details.ONIG_SYN_OP_ESC_X_HEX2,
                Esc_xBrace = details.ONIG_SYN_OP_ESC_X_BRACE_HEX8,
                Esc_u4 = details.ONIG_SYN_OP2_ESC_U_HEX4,
                Esc_U8 = syntax == SyntaxEnum.ONIG_SYNTAX_PYTHON,
                Esc_uBrace = false,
                Esc_UBrace = false,
                Esc_c1 = details.ONIG_SYN_OP_ESC_C_CONTROL,
                Esc_C1 = false,
                Esc_CMinus = details.ONIG_SYN_OP2_ESC_CAPITAL_C_BAR_CONTROL,
                Esc_NBrace = false,
                GenericEscape = true,

                InsideSets_Esc_a = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_b = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_e = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_f = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_n = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_r = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_t = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_v = details.ONIG_SYN_OP_ESC_CONTROL_CHARS && details.ONIG_SYN_OP2_ESC_V_VTAB && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Esc_Octal = details.ONIG_SYN_OP_ESC_OCTAL3 ? FeatureMatrix.OctalEnum.Octal_1_3 : FeatureMatrix.OctalEnum.None,
                InsideSets_Esc_Octal0_1_3 = false,
                InsideSets_Esc_oBrace = details.ONIG_SYN_OP_ESC_O_BRACE_OCTAL,
                InsideSets_Esc_x2 = details.ONIG_SYN_OP_ESC_X_HEX2,
                InsideSets_Esc_xBrace = details.ONIG_SYN_OP_ESC_X_BRACE_HEX8,
                InsideSets_Esc_u4 = details.ONIG_SYN_OP2_ESC_U_HEX4,
                InsideSets_Esc_U8 = syntax == SyntaxEnum.ONIG_SYNTAX_PYTHON,
                InsideSets_Esc_uBrace = false,
                InsideSets_Esc_UBrace = false,
                InsideSets_Esc_c1 = details.ONIG_SYN_OP_ESC_C_CONTROL,
                InsideSets_Esc_C1 = false,
                InsideSets_Esc_CMinus = details.ONIG_SYN_OP2_ESC_CAPITAL_C_BAR_CONTROL,
                InsideSets_Esc_NBrace = false,
                InsideSets_GenericEscape = details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,

                Class_Dot = details.ONIG_SYN_OP_DOT_ANYCHAR,
                Class_Cbyte = false,
                Class_Ccp = false,
                Class_dD = details.ONIG_SYN_OP_ESC_D_DIGIT,
                Class_hHhexa = details.ONIG_SYN_OP2_ESC_H_XDIGIT,
                Class_hHhorspace = false,
                Class_lL = false,
                Class_N = details.ONIG_SYN_OP2_ESC_CAPITAL_N_O_SUPER_DOT,
                Class_O = details.ONIG_SYN_OP2_ESC_CAPITAL_N_O_SUPER_DOT,
                Class_R = details.ONIG_SYN_OP2_ESC_CAPITAL_R_GENERAL_NEWLINE,
                Class_sS = details.ONIG_SYN_OP_ESC_S_WHITE_SPACE,
                Class_sSx = false,
                Class_uU = false,
                Class_vV = false,
                Class_wW = details.ONIG_SYN_OP_ESC_W_WORD,
                Class_X = details.ONIG_SYN_OP2_ESC_X_Y_TEXT_SEGMENT,
                Class_Not = false,
                Class_pP = false,
                Class_pPBrace = details.ONIG_SYN_OP2_ESC_P_BRACE_CHAR_PROPERTY || details.ONIG_SYN_OP2_ESC_P_BRACE_CIRCUMFLEX_NOT,
                Class_Name = false,

                InsideSets_Class_dD = details.ONIG_SYN_OP_ESC_D_DIGIT && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Class_hHhexa = details.ONIG_SYN_OP2_ESC_H_XDIGIT && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Class_hHhorspace = false,
                InsideSets_Class_lL = false,
                InsideSets_Class_R = false,
                InsideSets_Class_sS = details.ONIG_SYN_OP_ESC_S_WHITE_SPACE && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Class_sSx = false,
                InsideSets_Class_uU = false,
                InsideSets_Class_vV = false,
                InsideSets_Class_wW = details.ONIG_SYN_OP_ESC_W_WORD && details.ONIG_SYN_BACKSLASH_ESCAPE_IN_CC,
                InsideSets_Class_X = false,
                InsideSets_Class_pP = false,
                InsideSets_Class_pPBrace = details.ONIG_SYN_OP2_ESC_P_BRACE_CHAR_PROPERTY || details.ONIG_SYN_OP2_ESC_P_BRACE_CIRCUMFLEX_NOT,
                InsideSets_Class_Name = details.ONIG_SYN_OP_POSIX_BRACKET,
                InsideSets_Equivalence = false,
                InsideSets_Collating = false,

                InsideSets_Operators = details.ONIG_SYN_OP2_CCLASS_SET_OP,
                InsideSets_OperatorsExtended = false,
                InsideSets_Operator_Ampersand = false,
                InsideSets_Operator_Plus = false,
                InsideSets_Operator_VerticalLine = false,
                InsideSets_Operator_Minus = false,
                InsideSets_Operator_Circumflex = false,
                InsideSets_Operator_Exclamation = false,
                InsideSets_Operator_DoubleAmpersand = details.ONIG_SYN_OP2_CCLASS_SET_OP,
                InsideSets_Operator_DoubleVerticalLine = false,
                InsideSets_Operator_DoubleMinus = false, // TODO: clarify
                InsideSets_Operator_DoubleTilde = false,

                Anchor_Circumflex = details.ONIG_SYN_OP_LINE_ANCHOR,
                Anchor_Dollar = details.ONIG_SYN_OP_LINE_ANCHOR,
                Anchor_A = details.ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR,
                Anchor_Z = details.ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR,
                Anchor_z = details.ONIG_SYN_OP_ESC_AZ_BUF_ANCHOR, // TODO: in Python syntax, it gives undefined operator (-213)
                Anchor_G = details.ONIG_SYN_OP_ESC_CAPITAL_G_BEGIN_ANCHOR,
                Anchor_bB = details.ONIG_SYN_OP_ESC_B_WORD_BOUND,
                Anchor_bg = false,
                Anchor_bBBrace = false,
                Anchor_K = details.ONIG_SYN_OP2_ESC_CAPITAL_K_KEEP,
                Anchor_mM = false,
                Anchor_LtGt = details.ONIG_SYN_OP_ESC_LTGT_WORD_BEGIN_END,
                Anchor_GraveApos = details.ONIG_SYN_OP2_ESC_GNU_BUF_ANCHOR,
                Anchor_yY = details.ONIG_SYN_OP2_ESC_X_Y_TEXT_SEGMENT, // TODO: seems to work for some other cases too

                NamedGroup_Apos = details.ONIG_SYN_OP2_QMARK_LT_NAMED_GROUP,
                NamedGroup_LtGt = details.ONIG_SYN_OP2_QMARK_LT_NAMED_GROUP,
                NamedGroup_PLtGt = details.ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME,
                NamedGroup_AtApos = details.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY,
                NamedGroup_AtLtGt = details.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY,
                CapturingGroup = details.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY,

                NoncapturingGroup = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
                PositiveLookahead = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
                NegativeLookahead = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
                PositiveLookbehind = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
                NegativeLookbehind = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
                AtomicGroup = details.ONIG_SYN_OP2_QMARK_GROUP_EFFECT,
                BranchReset = false,
                NonatomicPositiveLookahead = false,
                NonatomicPositiveLookbehind = false,
                AbsentOperator = details.ONIG_SYN_OP2_QMARK_TILDE_ABSENT_GROUP,
                AllowSpacesInGroups = false,

                Backref_Num = details.ONIG_SYN_OP_DECIMAL_BACKREF ? FeatureMatrix.BackrefEnum.Any : FeatureMatrix.BackrefEnum.None,
                Backref_kApos = details.ONIG_SYN_OP2_ESC_K_NAMED_BACKREF,
                Backref_kLtGt = details.ONIG_SYN_OP2_ESC_K_NAMED_BACKREF,
                Backref_kBrace = false,
                Backref_kNum = false,
                Backref_kNegNum = false,
                Backref_gApos = details.ONIG_SYN_OP2_ESC_G_SUBEXP_CALL ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
                Backref_gLtGt = details.ONIG_SYN_OP2_ESC_G_SUBEXP_CALL ? FeatureMatrix.BackrefModeEnum.Pattern : FeatureMatrix.BackrefModeEnum.None,
                Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
                Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
                Backref_PEqName = details.ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME,
                AllowSpacesInBackref = false,

                Recursive_Num = syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG,
                Recursive_PlusMinusNum = syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG,
                Recursive_R = false, //details.ONIG_SYN_OP2_QMARK_PERL_SUBEXP_CALL, // TODO: does not seem to work
                Recursive_Name = details.ONIG_SYN_OP2_QMARK_PERL_SUBEXP_CALL,
                Recursive_PGtName = details.ONIG_SYN_OP2_QMARK_CAPITAL_P_NAME,

                Quantifier_Asterisk = details.ONIG_SYN_OP_ASTERISK_ZERO_INF,
                Quantifier_Plus = details.ONIG_SYN_OP_PLUS_ONE_INF ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_PLUS_ONE_INF ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Question = details.ONIG_SYN_OP_QMARK_ZERO_ONE ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_QMARK_ZERO_ONE ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces = details.ONIG_SYN_OP_BRACE_INTERVAL ? FeatureMatrix.PunctuationEnum.Normal : details.ONIG_SYN_OP_ESC_BRACE_INTERVAL ? FeatureMatrix.PunctuationEnum.Backslashed : FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
                Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
                Quantifier_LowAbbrev = details.ONIG_SYN_ALLOW_INTERVAL_LOW_ABBREV,

                Conditional_BackrefByNumber = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE,
                Conditional_BackrefByName = false,
                Conditional_Pattern = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE,
                Conditional_PatternOrBackrefByName = false,
                Conditional_BackrefByName_Apos = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE && syntax != SyntaxEnum.ONIG_SYNTAX_PERL && syntax != SyntaxEnum.ONIG_SYNTAX_PYTHON,
                Conditional_BackrefByName_LtGt = details.ONIG_SYN_OP2_QMARK_LPAREN_IF_ELSE && syntax != SyntaxEnum.ONIG_SYNTAX_PERL,
                Conditional_R = false,
                Conditional_RName = false,
                Conditional_DEFINE = syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG,
                Conditional_VERSION = false,

                ControlVerbs = details.ONIG_SYN_OP2_ASTERISK_CALLOUT_NAME, // several built-in callouts: https://github.com/kkos/oniguruma/blob/master/doc/CALLOUTS.BUILTIN
                ScriptRuns = false,
                Callouts = details.ONIG_SYN_OP2_ASTERISK_CALLOUT_NAME,

                EmptyConstruct = false,
                EmptyConstructX = false,
                EmptySet = false,

                AsciiOnly = false,
                SplitSurrogatePairs = false,
                AllowDuplicateGroupName = syntax == SyntaxEnum.ONIG_SYNTAX_ONIGURUMA || syntax == SyntaxEnum.ONIG_SYNTAX_PERL_NG || syntax == SyntaxEnum.ONIG_SYNTAX_RUBY,
                FuzzyMatchingParams = false,
                TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
                Σσς = true,
            };

            // TODO: "\M-x"
            // TODO: "(?Rnumber)"
            // TODO: ONIG_SYN_OP2_QMARK_BRACE_CALLOUT_CONTENTS

        }
    }
}
