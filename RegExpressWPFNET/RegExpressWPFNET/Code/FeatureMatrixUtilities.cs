using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace RegExpressWPFNET.Code
{
    class FeatureMatrixDetails
    {
        internal readonly string ShortDesc;
        internal readonly string? Desc;
        internal readonly Func<FeatureMatrix, bool>? Func;

        public FeatureMatrixDetails( string shortDesc, string? desc, Func<FeatureMatrix, bool>? func )
        {
            ShortDesc = shortDesc;
            Desc = desc;
            Func = func;
        }
    }


    static class FeatureMatrixUtilities
    {
        static FeatureMatrixDetails[] AllFeatureMatrixDetails =
        {
            new FeatureMatrixDetails(  @"General", null, null ),

            new FeatureMatrixDetails(  @"(…)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal ),
            new FeatureMatrixDetails(  @"\(…\)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed ),

            new FeatureMatrixDetails(  @"[…]", @"Character group", fm => fm.Brackets ),
            new FeatureMatrixDetails(  @"(?[…])", @"Character group", fm => fm.ExtendedBrackets ),

            new FeatureMatrixDetails(  @"|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal ),
            new FeatureMatrixDetails(  @"\|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed ),

            new FeatureMatrixDetails(  @"(?#comment)", @"Inline comment", fm => fm.InlineComments ),
            new FeatureMatrixDetails(  @"#comment", @"Comment when enabled by options", fm => fm.XModeComments ),
            new FeatureMatrixDetails(  @"[#comment]", @"Comment inside […] when enabled by options", fm => fm.InsideSets_XModeComments ),

            new FeatureMatrixDetails(  @"(?flags)", @"Inline options", fm => fm.Flags ),
            new FeatureMatrixDetails(  @"(?flags:…)", @"Inline scoped options", fm => fm.ScopedFlags ),
            new FeatureMatrixDetails(  @"(?^flags)", @"Inline fresh options", fm => fm.CircumflexFlags ),
            new FeatureMatrixDetails(  @"(?^flags:…)", @"Inline scoped fresh options", fm => fm.ScopedCircumflexFlags ),
            new FeatureMatrixDetails(  @"(?x)", @"Allow 'x' flag", fm => fm.XFlag ),
            new FeatureMatrixDetails(  @"(?xx)", @"Allow 'xx' flag", fm => fm.XXFlag ),

            new FeatureMatrixDetails(  @"\Q…\E", @"Literal", fm => fm.Literal_QE ),
            new FeatureMatrixDetails(  @"[\Q…\E]", @"Literal inside […]", fm => fm.InsideSets_Literal_QE ),


            new FeatureMatrixDetails(  @"Escapes", null, null ),

            new FeatureMatrixDetails(  @"\a", @"Bell, \u0007", fm => fm.Esc_a ),
            new FeatureMatrixDetails(  @"\b", @"Backspace, \u0008", fm => fm.Esc_b ),
            new FeatureMatrixDetails(  @"\e", @"Escape, \u001B", fm => fm.Esc_e ),
            new FeatureMatrixDetails(  @"\f", @"Form feed, \u000C", fm => fm.Esc_f ),
            new FeatureMatrixDetails(  @"\n", @"New line, \u000A", fm => fm.Esc_n ),
            new FeatureMatrixDetails(  @"\r", @"Carriage return, \u000D", fm => fm.Esc_r ),
            new FeatureMatrixDetails(  @"\t", @"Tab, \u0009", fm => fm.Esc_t ),
            new FeatureMatrixDetails(  @"\v", @"Vertical tab, \u000B", fm => fm.Esc_v ),
            new FeatureMatrixDetails(  @"\0nnn", @"Octal, up to three digits after '\0'", fm => fm.Esc_Octal0_1_3 ),
            new FeatureMatrixDetails(  @"\nnn", @"Octal, up to three digits", fm => fm.Esc_Octal_1_3 ),
            new FeatureMatrixDetails(  @"\nnn", @"Octal, two or three digits", fm => fm.Esc_Octal_2_3 ),
            new FeatureMatrixDetails(  @"\o{nn…}", @"Octal", fm => fm.Esc_oBrace ),
            new FeatureMatrixDetails(  @"\xXX", @"Hexadecimal code, two digits", fm => fm.Esc_x2 ),
            new FeatureMatrixDetails(  @"\x{XX…}", @"Hexadecimal code", fm => fm.Esc_xBrace ),
            new FeatureMatrixDetails(  @"\uXXXX", @"Hexadecimal code, four digits", fm => fm.Esc_u4 ),
            new FeatureMatrixDetails(  @"\UXXXXXXXX", @"Hexadecimal code, eight digits", fm => fm.Esc_U8 ),
            new FeatureMatrixDetails(  @"\u{XX…}", @"Hexadecimal code", fm => fm.Esc_uBrace ),
            new FeatureMatrixDetails(  @"\U{XX…}", @"Hexadecimal code", fm => fm.Esc_UBrace ),
            new FeatureMatrixDetails(  @"\cC", @"Control character", fm => fm.Esc_c1 ),
            new FeatureMatrixDetails(  @"\CC", @"Control character", fm => fm.Esc_C1 ),
            new FeatureMatrixDetails(  @"\C-C", @"Control character", fm => fm.Esc_CMinus ),
            new FeatureMatrixDetails(  @"\N{…}", @"Unicode name or 'U+code'", fm => fm.Esc_NBrace ),
            new FeatureMatrixDetails(  @"\any", @"Generic escape", fm => fm.GenericEscape ),

            new FeatureMatrixDetails(  @"Escapes inside […]", null, null ),

            new FeatureMatrixDetails(  @"[\a]", @"Bell, \u0007", fm => fm.InsideSets_Esc_a ),
            new FeatureMatrixDetails(  @"[\b]", @"Backspace, \u0008", fm => fm.InsideSets_Esc_b ),
            new FeatureMatrixDetails(  @"[\e]", @"Escape, \u001B", fm => fm.InsideSets_Esc_e ),
            new FeatureMatrixDetails(  @"[\f]", @"Form feed, \u000C", fm => fm.InsideSets_Esc_f ),
            new FeatureMatrixDetails(  @"[\n]", @"New line, \u000A", fm => fm.InsideSets_Esc_n ),
            new FeatureMatrixDetails(  @"[\r]", @"Carriage return, \u000D", fm => fm.InsideSets_Esc_r ),
            new FeatureMatrixDetails(  @"[\t]", @"Tab, \u0009", fm => fm.InsideSets_Esc_t ),
            new FeatureMatrixDetails(  @"[\v]", @"Vertical tab, \u000B", fm => fm.InsideSets_Esc_v ),
            new FeatureMatrixDetails(  @"[\0nnn]", @"Octal, up to three digits after '\0'", fm => fm.InsideSets_Esc_Octal0_1_3 ),
            new FeatureMatrixDetails(  @"[\nnn]", @"Octal, up to three digits", fm => fm.InsideSets_Esc_Octal_1_3 ),
            new FeatureMatrixDetails(  @"[\nnn]", @"Octal, two or three digits", fm => fm.InsideSets_Esc_Octal_2_3 ),
            new FeatureMatrixDetails(  @"[\o{nn…}]", @"Octal", fm => fm.InsideSets_Esc_oBrace ),
            new FeatureMatrixDetails(  @"[\xXX]", @"Hexadecimal code, two digits", fm => fm.InsideSets_Esc_x2 ),
            new FeatureMatrixDetails(  @"[\x{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_xBrace ),
            new FeatureMatrixDetails(  @"[\uXXXX]", @"Hexadecimal code, four digits", fm => fm.InsideSets_Esc_u4 ),
            new FeatureMatrixDetails(  @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", fm => fm.InsideSets_Esc_U8 ),
            new FeatureMatrixDetails(  @"[\u{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_uBrace ),
            new FeatureMatrixDetails(  @"[\U{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_UBrace ),
            new FeatureMatrixDetails(  @"[\cC]", @"Control character", fm => fm.InsideSets_Esc_c1 ),
            new FeatureMatrixDetails(  @"[\CC]", @"Control character", fm => fm.InsideSets_Esc_C1 ),
            new FeatureMatrixDetails(  @"[\C-C]", @"Control character ", fm => fm.InsideSets_Esc_CMinus ),
            new FeatureMatrixDetails(  @"[\N{…}]", @"Unicode name or 'U+code'", fm => fm.InsideSets_Esc_NBrace ),
            new FeatureMatrixDetails(  @"[\any]", @"Generic escape", fm => fm.InsideSets_GenericEscape ),


            new FeatureMatrixDetails(  @"Classes", null, null ),

            new FeatureMatrixDetails(  @".", @"Any, including or excepting newline (\n) depending on options", fm => fm.Class_Dot ),
            new FeatureMatrixDetails(  @"\C", @"Single byte", fm => fm.Class_Cbyte ),
            new FeatureMatrixDetails(  @"\C", @"Single code point", fm => fm.Class_Ccp ),
            new FeatureMatrixDetails(  @"\d, \D", @"Digit", fm => fm.Class_dD ),
            new FeatureMatrixDetails(  @"\h, \H", @"Hexadecimal character", fm => fm.Class_hHhexa ),
            new FeatureMatrixDetails(  @"\h, \H", @"Horizontal space", fm => fm.Class_hHhorspace ),
            new FeatureMatrixDetails(  @"\l, \L", @"Lowercase character", fm => fm.Class_lL ),
            new FeatureMatrixDetails(  @"\N", @"Any except '\n'", fm => fm.Class_N ),
            new FeatureMatrixDetails(  @"\O", @"Any", fm => fm.Class_O ),
            new FeatureMatrixDetails(  @"\R", @"Line break", fm => fm.Class_R ),
            new FeatureMatrixDetails(  @"\s, \S", @"Space", fm => fm.Class_sS ),
            new FeatureMatrixDetails(  @"\sx, \Sx", @"Syntax group; 'x' — group", fm => fm.Class_sSx ),
            new FeatureMatrixDetails(  @"\u, \U", @"Uppercase character", fm => fm.Class_uU ),
            new FeatureMatrixDetails(  @"\v, \V", @"Vertical space", fm => fm.Class_vV ),
            new FeatureMatrixDetails(  @"\w, \W", @"Word character", fm => fm.Class_wW ),
            new FeatureMatrixDetails(  @"\X", @"Extended grapheme cluster", fm => fm.Class_X ),
            new FeatureMatrixDetails(  @"\!c, \!\c", @"Not; 'c' — character, '\c' — escaped character", fm => fm.Class_Not ),

            new FeatureMatrixDetails(  @"\pX, \PX", @"Unicode property, X — short property name", fm => fm.Class_pP ),
            new FeatureMatrixDetails(  @"\p{…}, \P{…}", @"Unicode property", fm => fm.Class_pPBrace ),
            new FeatureMatrixDetails(  @"[:class:]", @"Character class", fm => fm.Class_Name ),


            new FeatureMatrixDetails(  @"Classes inside […]", null, null ),

            new FeatureMatrixDetails(  @"[\d], [\D]", @"Digit", fm => fm.InsideSets_Class_dD ),
            new FeatureMatrixDetails(  @"[\h], [\H]", @"Hexadecimal character", fm => fm.InsideSets_Class_hHhexa ),
            new FeatureMatrixDetails(  @"[\h], [\H]", @"Horizontal space", fm => fm.InsideSets_Class_hHhorspace ),
            new FeatureMatrixDetails(  @"[\l], [\L]", @"Lowercase character", fm => fm.InsideSets_Class_lL ),
            new FeatureMatrixDetails(  @"[\R]", @"Line break", fm => fm.InsideSets_Class_R ),
            new FeatureMatrixDetails(  @"[\s], [\S]", @"Space", fm => fm.InsideSets_Class_sS ),
            new FeatureMatrixDetails(  @"[\sx], [\Sx]", @"Syntax group; 'x' — group", fm => fm.InsideSets_Class_sSx ),
            new FeatureMatrixDetails(  @"[\u], [\U]", @"Uppercase character", fm => fm.InsideSets_Class_uU ),
            new FeatureMatrixDetails(  @"[\v], [\V]", @"Vertical space", fm => fm.InsideSets_Class_vV ),
            new FeatureMatrixDetails(  @"[\w], [\W]", @"Word character", fm => fm.InsideSets_Class_wW ),
            new FeatureMatrixDetails(  @"[\X]", @"Extended grapheme cluster", fm => fm.InsideSets_Class_X ),
            new FeatureMatrixDetails(  @"[\pX], [\PX]", @"Unicode property, X — short property name", fm => fm.InsideSets_Class_pP ),
            new FeatureMatrixDetails(  @"[\p{…}], [\P{…}]", @"Unicode property", fm => fm.InsideSets_Class_pPBrace ),
            new FeatureMatrixDetails(  @"[[:class:]]", @"Character class", fm => fm.InsideSets_Class_Name ),
            new FeatureMatrixDetails(  @"[[=elem=]]", @"Equivalence", fm => fm.InsideSets_Equivalence ),
            new FeatureMatrixDetails(  @"[[.elem.]]", @"Collating symbol", fm => fm.InsideSets_Collating ),


            new FeatureMatrixDetails(  @"Operators inside […]", null, null ),

            new FeatureMatrixDetails(  @"[[…] op […]]", @"Using operators for nested groups", fm => fm.InsideSets_Operators),
            new FeatureMatrixDetails(  @"(?[[…] op […]])", @"Using operators for nested groups", fm => fm.InsideSets_OperatorsExtended),
            new FeatureMatrixDetails(  @"[…] & […]", @"Intersection", fm => fm.InsideSets_Operator_Ampersand),
            new FeatureMatrixDetails(  @"[…] + […]", @"Union", fm => fm.InsideSets_Operator_Plus),
            new FeatureMatrixDetails(  @"[…] | […]", @"Union", fm => fm.InsideSets_Operator_VerticalLine),
            new FeatureMatrixDetails(  @"[…] - […]", @"Subtraction", fm => fm.InsideSets_Operator_Minus),
            new FeatureMatrixDetails(  @"[…] ^ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_Circumflex),
            new FeatureMatrixDetails(  @"![…]", @"Complement", fm => fm.InsideSets_Operator_Exclamation),
            new FeatureMatrixDetails(  @"[…] && […]", @"Intersection", fm => fm.InsideSets_Operator_DoubleAmpersand),
            new FeatureMatrixDetails(  @"[…] || […]", @"Union", fm => fm.InsideSets_Operator_DoubleVerticalLine),
            new FeatureMatrixDetails(  @"[…] -- […]", @"Difference", fm => fm.InsideSets_Operator_DoubleMinus),
            new FeatureMatrixDetails(  @"[…] ~~ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_DoubleTilde),


            new FeatureMatrixDetails(  @"Anchors", null, null ),

            new FeatureMatrixDetails(  @"^", @"Beginning of string or line, depending on options", fm => fm.Anchor_Circumflex),
            new FeatureMatrixDetails(  @"$", @"End, or before '\n' at end of string or line, depending on options", fm => fm.Anchor_Dollar),
            new FeatureMatrixDetails(  @"\A", @"Start of string", fm => fm.Anchor_A),
            new FeatureMatrixDetails(  @"\Z", @"End of string, or before '\n' at end of string", fm => fm.Anchor_Z),
            new FeatureMatrixDetails(  @"\z", @"End of string", fm => fm.Anchor_z),
            new FeatureMatrixDetails(  @"\G", @"start of string or end of previous match", fm => fm.Anchor_G ),
            new FeatureMatrixDetails(  @"\b, \B", @"Boundary between \w and \W", fm => fm.Anchor_bB ),
            new FeatureMatrixDetails(  @"\b{g}", @"Unicode extended grapheme cluster boundary", fm => fm.Anchor_bg ),
            new FeatureMatrixDetails(  @"\b{…}, \B{…}", @"Typed boundary", fm => fm.Anchor_bBBrace ),
            new FeatureMatrixDetails(  @"\K", @"Keep the stuff left of the \K", fm => fm.Anchor_K ),
            new FeatureMatrixDetails(  @"\m, \M", @"Start of word, end of word", fm => fm.Anchor_mM ),
            new FeatureMatrixDetails(  @"\<, \>", @"Start of word, end of word", fm => fm.Anchor_LtGt ),
            new FeatureMatrixDetails(  @"\`, \'", @"Start of string, end of string", fm => fm.Anchor_GraveApos ),
            new FeatureMatrixDetails(  @"\y, \Y", @"Boundary between graphemes", fm => fm.Anchor_yY ),


            new FeatureMatrixDetails(  @"Named groups and backreferences", null, null ),

            new FeatureMatrixDetails(  @"(?'name'…)", @"Named group", fm => fm.NamedGroup_Apos ),
            new FeatureMatrixDetails(  @"(?<name>…)", @"Named group", fm => fm.NamedGroup_LtGt ),
            new FeatureMatrixDetails(  @"(?P<name>…)", @"Named group", fm => fm.NamedGroup_PLtGt ),

            new FeatureMatrixDetails(  @"\1, \2, …, \9", @"Backreferences \1, \2, …, \9", fm => fm.Backref_1_9 ),
            new FeatureMatrixDetails(  @"\nnn", @"Backreference, one or more digits", fm => fm.Backref_Num ),
            new FeatureMatrixDetails(  @"\k'name'", @"Backreference by name", fm => fm.Backref_kApos ),
            new FeatureMatrixDetails(  @"\k<name>", @"Backreference by name", fm => fm.Backref_kLtGt ),
            new FeatureMatrixDetails(  @"\k{name}", @"Backreference by name", fm => fm.Backref_kBrace ),
            new FeatureMatrixDetails(  @"\kn", @"Backreference \k1, \k2, …", fm => fm.Backref_kNum ),
            new FeatureMatrixDetails(  @"\k-n", @"Relative backreference \k-1, \k-2, …", fm => fm.Backref_kNegNum ),
            new FeatureMatrixDetails(  @"\g'…'", @"Backreference by name or number", fm => fm.Backref_gApos ),
            new FeatureMatrixDetails(  @"\g<…>", @"Backreference by name or number", fm => fm.Backref_gLtGt ),
            new FeatureMatrixDetails(  @"\gn", @"Backreference \g1, \g2, …", fm => fm.Backref_gNum ),
            new FeatureMatrixDetails(  @"\g-n", @"Relative backreference \g-1, \g-2, …", fm => fm.Backref_gNegNum ),
            new FeatureMatrixDetails(  @"\g{…}", @"Backreference \g{name}, \g{number}, \g{-number}, g{+number}", fm => fm.Backref_gBrace ),
            new FeatureMatrixDetails(  @"(?P=name)", @"Backreference by name", fm => fm.Backref_PEqName ),
            new FeatureMatrixDetails(  @"\k< … >, \g< … >", @"Allow spaces like '\k < name >' when whitespaces are enabled by options", fm => fm.AllowSpacesInBackref ),


            new FeatureMatrixDetails(  @"Grouping", null, null ),

            new FeatureMatrixDetails(  @"(?:…)", @"Noncapturing group", fm => fm.NoncapturingGroup ),
            new FeatureMatrixDetails(  @"(?=…)", @"Positive lookahead ", fm => fm.PositiveLookahead ),
            new FeatureMatrixDetails(  @"(?!…)", @"Negative lookahead ", fm => fm.NegativeLookahead ),
            new FeatureMatrixDetails(  @"(?<=…)", @"Positive lookbehind", fm => fm.PositiveLookbehind ),
            new FeatureMatrixDetails(  @"(?<!…)", @"Negative lookbehind", fm => fm.NegativeLookbehind ),
            new FeatureMatrixDetails(  @"(?>…)", @"Atomic group", fm => fm.AtomicGroup ),
            new FeatureMatrixDetails(  @"(?|…)", @"Branch reset", fm => fm.BranchReset ),
            new FeatureMatrixDetails(  @"(?*…)", @"Non-atomic positive lookahead", fm => fm.NonatomicPositiveLookahead ),
            new FeatureMatrixDetails(  @"(?<*…)", @"Non-atomic positive lookbehind ", fm => fm.NonatomicPositiveLookbehind ),
            new FeatureMatrixDetails(  @"(?~…)", @"Absent operator", fm => fm.AbsentOperator ),
            new FeatureMatrixDetails(  @"( ? … )", @"Allow spaces like '( ? < name >…)' when whitespaces are enabled by options", fm => fm.AllowSpacesInGroups ),


            new FeatureMatrixDetails(  @"Recursive patterns", null, null ),

            new FeatureMatrixDetails(  @"(?n)", @"Recursive subpattern by number", fm => fm.Recursive_Num ),
            new FeatureMatrixDetails(  @"(?-n), (?+n)", @"Relative recursive subpattern by number", fm => fm.Recursive_PlusMinusNum ),
            new FeatureMatrixDetails(  @"(?R)", @"Recursive whole pattern", fm => fm.Recursive_R ),
            new FeatureMatrixDetails(  @"(?&name)", @"Recursive subpattern by name", fm => fm.Recursive_Name ),
            new FeatureMatrixDetails(  @"(?P>name)", @"Recursive subpattern by name", fm => fm.Recursive_PGtName ),


            new FeatureMatrixDetails(  @"Quantifiers", null, null ),

            new FeatureMatrixDetails(  @"*", @"Zero or more times", fm => fm.Quantifier_Asterisk ),
            new FeatureMatrixDetails(  @"+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal ),
            new FeatureMatrixDetails(  @"\+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed ),
            new FeatureMatrixDetails(  @"?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal),
            new FeatureMatrixDetails(  @"\?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed),
            new FeatureMatrixDetails(  @"{…}", @"Between n and m times: {n}, {n,}, {n,m}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal ),
            new FeatureMatrixDetails(  @"\{…\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed ),
            new FeatureMatrixDetails(  @"{ … } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.Both ),
            new FeatureMatrixDetails(  @"{ … } ", @"Allow spaces within {…} or \{…\} when spaces are allowed by options", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.XModeOnly ),
            new FeatureMatrixDetails(  @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", fm => fm.Quantifier_LowAbbrev ),


            new FeatureMatrixDetails(  @"Conditionals", null, null ),

            new FeatureMatrixDetails(  @"(?(number)…|…)", @"Conditionals by number, +number and -number", fm => fm.Conditional_BackrefByNumber ),
            new FeatureMatrixDetails(  @"(?(name)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName ),
            new FeatureMatrixDetails(  @"(?(pattern)…|…)", @"Conditional supbattern", fm => fm.Conditional_Pattern ),
            new FeatureMatrixDetails(  @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx supbattern, if no such name", fm => fm.Conditional_PatternOrBackrefByName ),
            new FeatureMatrixDetails(  @"(?('name')…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_Apos ),
            new FeatureMatrixDetails(  @"(?(<name>)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_LtGt ),
            new FeatureMatrixDetails(  @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", fm => fm.Conditional_R ),
            new FeatureMatrixDetails(  @"(?(R&name)…|…)", @"Recursive conditional by name", fm => fm.Conditional_RName ),
            new FeatureMatrixDetails(  @"(?(DEFINE)…|…)", @"Defining subpatterns", fm => fm.Conditional_DEFINE ),
            new FeatureMatrixDetails(  @"(?(VERSION…)…|…)", @"Checking for version using 'VERSION=decimal' or 'VERSION>=decimal'", fm => fm.Conditional_VERSION ),


            new FeatureMatrixDetails(  @"Miscellaneous", null, null ),

            new FeatureMatrixDetails(  @"(*verb)", @"Control verbs: (*verb), (*verb:…), (*:name)", fm => fm.ControlVerbs ),
            new FeatureMatrixDetails(  @"(*…:…)", @"Script runs, such as (*atomic:…)", fm => fm.ScriptRuns ),

            new FeatureMatrixDetails(  @"(?)", @"Empty construct", fm => fm.EmptyConstruct ),
            new FeatureMatrixDetails(  @"(? )", @"Empty construct when whitespaces are enabled by options", fm => fm.EmptyConstructX ),
            new FeatureMatrixDetails(  @"[]", @"Empty set", fm => fm.EmptySet ),

        };


        public static void ExportAsHtml( XmlWriter xw, IEnumerable<IRegexEngine> engines )
        {
            var all_matrices = new List<IReadOnlyList<(string variantName, FeatureMatrix fm)>>( );

            foreach( IRegexEngine engine in engines )
            {
                var fms = engine.GetFeatureMatrices( );
                all_matrices.Add( fms! );
            }

            xw.WriteStartElement( "html" );

            xw.WriteStartElement( "head" );

            xw.WriteElementString( "title", "Regex Feature Matrix" );

            xw.WriteRaw( @"
<style>

h1
{
    font-family: sans-serif;
}

table
{
    border-collapse: collapse;
    font-family: 'Helvetica Narrow','Arial Narrow',Tahoma,Arial,Helvetica,sans-serif;
    font-size: 12pt;
}

th, td
{
    padding: 2pt;
    border: 0.5pt solid black;
}

th
{
    white-space: nowrap;
}

tbody > tr:nth-child(2n+2)
{
	background: #F4F4F4;
}

tbody > tr > th[colspan='100%']
{
    text-align: left;
	background: #FFF8DC;
    padding: 0.4ch 1ch 0.5ch 8pt ;
}

tbody > tr > td
{
    text-align: center;
    white-space: nowrap;
}

tbody > tr > td:nth-child(1)
{
    text-align: left;
    font-family: monospace;
    padding-left: 8pt;
}

tbody > tr > td:nth-child(2)
{
    text-align: left;
    white-space: normal;
    min-width: 30ch;
}

</style>
" );
            xw.WriteEndElement( ); // </head>

            xw.WriteStartElement( "body" );

            xw.WriteElementString( "h1", "Regex Feature Matrix" );

            xw.WriteStartElement( "table" );

            // header
            xw.WriteStartElement( "thead" );
            {
                xw.WriteStartElement( "tr" );
                {
                    xw.WriteStartElement( "th" );
                    xw.WriteAttributeString( "rowspan", "2" );
                    xw.WriteString( "Feature" );
                    xw.WriteEndElement( ); // </th>

                    xw.WriteStartElement( "th" );
                    xw.WriteAttributeString( "rowspan", "2" );
                    xw.WriteString( "Description" );
                    xw.WriteEndElement( ); // </th>

                    int i = 0;
                    foreach( IRegexEngine engine in engines )
                    {
                        var fms = all_matrices[i];
                        if( fms == null ) continue;

                        xw.WriteStartElement( "th" );
                        if( fms.Count == 1 )
                        {
                            xw.WriteAttributeString( "rowspan", "2" );
                        }
                        else
                        {
                            xw.WriteAttributeString( "colspan", fms.Count.ToString( CultureInfo.InvariantCulture ) );
                        }
                        xw.WriteString( engine.Name );
                        xw.WriteElementString( "br", null );
                        xw.WriteString( engine.Version );
                        xw.WriteEndElement( ); // </th>

                        ++i;
                    }
                }
                xw.WriteEndElement( ); // </tr>
                xw.WriteStartElement( "tr" );
                {
                    int i = 0;
                    foreach( IRegexEngine engine in engines )
                    {
                        var fms = all_matrices[i];
                        if( fms == null ) continue;

                        foreach( var p in fms )
                        {
                            if( !string.IsNullOrWhiteSpace( p.variantName ) )
                            {
                                xw.WriteStartElement( "th" );
                                xw.WriteString( p.variantName );
                                xw.WriteEndElement( ); // </th>
                            }
                        }

                        ++i;
                    }
                }
                xw.WriteEndElement( ); // </tr>
            }
            xw.WriteEndElement( ); // </thead>

            // body
            xw.WriteStartElement( "tbody" );
            {
                foreach( var d in AllFeatureMatrixDetails )
                {
                    if( d.Func == null )
                    {
                        xw.WriteEndElement( ); // </tbody>
                        xw.WriteStartElement( "tbody" );

                        xw.WriteStartElement( "tr" );
                        {
                            xw.WriteStartElement( "th" );
                            xw.WriteAttributeString( "colspan", "100%" );
                            xw.WriteValue( d.ShortDesc );
                            xw.WriteEndElement( ); // </th>
                        }
                        xw.WriteEndElement( ); // </tr>
                    }
                    else
                    {
                        WriteRow( xw, d.ShortDesc, d.Desc, engines, all_matrices, d.Func );
                    }
                }
            }
            xw.WriteEndElement( ); // </tbody>

            xw.WriteEndElement( ); // </table>

            xw.WriteElementString( "br", null );
            xw.WriteElementString( "br", null );

            xw.WriteEndElement( ); // </body>
            xw.WriteEndElement( ); // </html>
        }


        static void WriteRow( XmlWriter xw, string shortDesc, string? desc,
            IEnumerable<IRegexEngine> engines, List<IReadOnlyList<(string variantName, FeatureMatrix fm)>> allMatrices,
            Func<FeatureMatrix, bool> func )
        {
            xw.WriteStartElement( "tr" );
            {
                xw.WriteElementString( "td", shortDesc );
                xw.WriteElementString( "td", desc );

                int i = 0;
                foreach( IRegexEngine engine in engines )
                {
                    var fms = allMatrices[i];
                    if( fms == null ) continue;

                    foreach( var p in fms )
                    {
                        xw.WriteStartElement( "td" );
                        if( func( p.fm ) )
                        {
                            xw.WriteString( "+" );
                        }
                        else
                        {
                            xw.WriteElementString( "br", null );
                        }
                        xw.WriteEndElement( ); // </td>
                    }

                    ++i;
                }
            }
            xw.WriteEndElement( ); // </tr>
        }
    }
}
