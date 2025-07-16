using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

class FeatureMatrixDetails
{
    internal readonly string ShortDesc;
    internal readonly string? Desc;
    internal readonly Func<FeatureMatrix, bool>? Func;

    internal FeatureMatrixDetails( string shortDesc, string? desc, Func<FeatureMatrix, bool>? func )
    {
        ShortDesc = shortDesc;
        Desc = desc;
        Func = func;
    }

    internal static readonly FeatureMatrixDetails[] AllFeatureMatrixDetails =
        [
            new (  @"General", null, null ),

            new (  @"(…)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal ),
            new (  @"\(…\)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed ),

            new (  @"[…]", @"Character group", fm => fm.Brackets ),
            new (  @"(?[…])", @"Character group", fm => fm.ExtendedBrackets ),

            new (  @"|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal ),
            new (  @"\|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed ),

            new (  @"(?#comment)", @"Inline comment", fm => fm.InlineComments ),
            new (  @"#comment", @"Comment when enabled by options", fm => fm.XModeComments ),
            new (  @"[#comment]", @"Comment inside […] when enabled by options", fm => fm.InsideSets_XModeComments ),

            new (  @"(?flags)", @"Inline options", fm => fm.Flags ),
            new (  @"(?flags:…)", @"Inline scoped options", fm => fm.ScopedFlags ),
            new (  @"(?^flags)", @"Inline fresh options", fm => fm.CircumflexFlags ),
            new (  @"(?^flags:…)", @"Inline scoped fresh options", fm => fm.ScopedCircumflexFlags ),
            new (  @"(?x)", @"Allow 'x' flag", fm => fm.XFlag ),
            new (  @"(?xx)", @"Allow 'xx' flag", fm => fm.XXFlag ),

            new (  @"\Q…\E", @"Literal", fm => fm.Literal_QE ),
            new (  @"[\Q…\E]", @"Literal inside […]", fm => fm.InsideSets_Literal_QE ),
            new (  @"[\q{…}]", @"Literal inside […]", fm => fm.InsideSets_Literal_qBrace ),


            new (  @"Escapes", null, null ),

            new (  @"\a", @"Bell, \u0007", fm => fm.Esc_a ),
            new (  @"\b", @"Backspace, \u0008", fm => fm.Esc_b ),
            new (  @"\e", @"Escape, \u001B", fm => fm.Esc_e ),
            new (  @"\f", @"Form feed, \u000C", fm => fm.Esc_f ),
            new (  @"\n", @"New line, \u000A", fm => fm.Esc_n ),
            new (  @"\r", @"Carriage return, \u000D", fm => fm.Esc_r ),
            new (  @"\t", @"Tab, \u0009", fm => fm.Esc_t ),
            new (  @"\v", @"Vertical tab, \u000B", fm => fm.Esc_v ),
            new (  @"\0nnn", @"Octal, up to three digits after '\0'", fm => fm.Esc_Octal0_1_3 ),
            new (  @"\nnn", @"Octal, up to three digits", fm => fm.Esc_Octal_1_3 ),
            new (  @"\nnn", @"Octal, two or three digits", fm => fm.Esc_Octal_2_3 ),
            new (  @"\o{nn…}", @"Octal", fm => fm.Esc_oBrace ),
            new (  @"\xXX", @"Hexadecimal code, two digits", fm => fm.Esc_x2 ),
            new (  @"\x{XX…}", @"Hexadecimal code", fm => fm.Esc_xBrace ),
            new (  @"\uXXXX", @"Hexadecimal code, four digits", fm => fm.Esc_u4 ),
            new (  @"\UXXXXXXXX", @"Hexadecimal code, eight digits", fm => fm.Esc_U8 ),
            new (  @"\u{XX…}", @"Hexadecimal code", fm => fm.Esc_uBrace ),
            new (  @"\U{XX…}", @"Hexadecimal code", fm => fm.Esc_UBrace ),
            new (  @"\cC", @"Control character", fm => fm.Esc_c1 ),
            new (  @"\CC", @"Control character", fm => fm.Esc_C1 ),
            new (  @"\C-C", @"Control character", fm => fm.Esc_CMinus ),
            new (  @"\N{…}", @"Unicode name or 'U+code'", fm => fm.Esc_NBrace ),
            new (  @"\any", @"Generic escape", fm => fm.GenericEscape ),

            new (  @"Escapes inside […]", null, null ),

            new (  @"[\a]", @"Bell, \u0007", fm => fm.InsideSets_Esc_a ),
            new (  @"[\b]", @"Backspace, \u0008", fm => fm.InsideSets_Esc_b ),
            new (  @"[\e]", @"Escape, \u001B", fm => fm.InsideSets_Esc_e ),
            new (  @"[\f]", @"Form feed, \u000C", fm => fm.InsideSets_Esc_f ),
            new (  @"[\n]", @"New line, \u000A", fm => fm.InsideSets_Esc_n ),
            new (  @"[\r]", @"Carriage return, \u000D", fm => fm.InsideSets_Esc_r ),
            new (  @"[\t]", @"Tab, \u0009", fm => fm.InsideSets_Esc_t ),
            new (  @"[\v]", @"Vertical tab, \u000B", fm => fm.InsideSets_Esc_v ),
            new (  @"[\0nnn]", @"Octal, up to three digits after '\0'", fm => fm.InsideSets_Esc_Octal0_1_3 ),
            new (  @"[\nnn]", @"Octal, up to three digits", fm => fm.InsideSets_Esc_Octal_1_3 ),
            new (  @"[\nnn]", @"Octal, two or three digits", fm => fm.InsideSets_Esc_Octal_2_3 ),
            new (  @"[\o{nn…}]", @"Octal", fm => fm.InsideSets_Esc_oBrace ),
            new (  @"[\xXX]", @"Hexadecimal code, two digits", fm => fm.InsideSets_Esc_x2 ),
            new (  @"[\x{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_xBrace ),
            new (  @"[\uXXXX]", @"Hexadecimal code, four digits", fm => fm.InsideSets_Esc_u4 ),
            new (  @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", fm => fm.InsideSets_Esc_U8 ),
            new (  @"[\u{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_uBrace ),
            new (  @"[\U{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_UBrace ),
            new (  @"[\cC]", @"Control character", fm => fm.InsideSets_Esc_c1 ),
            new (  @"[\CC]", @"Control character", fm => fm.InsideSets_Esc_C1 ),
            new (  @"[\C-C]", @"Control character ", fm => fm.InsideSets_Esc_CMinus ),
            new (  @"[\N{…}]", @"Unicode name or 'U+code'", fm => fm.InsideSets_Esc_NBrace ),
            new (  @"[\any]", @"Generic escape", fm => fm.InsideSets_GenericEscape ),


            new (  @"Classes", null, null ),

            new (  @".", @"Any, including or excepting newline (\n) depending on options", fm => fm.Class_Dot ),
            new (  @"\C", @"Single byte", fm => fm.Class_Cbyte ),
            new (  @"\C", @"Single code point", fm => fm.Class_Ccp ),
            new (  @"\d, \D", @"Digit", fm => fm.Class_dD ),
            new (  @"\h, \H", @"Hexadecimal character", fm => fm.Class_hHhexa ),
            new (  @"\h, \H", @"Horizontal space", fm => fm.Class_hHhorspace ),
            new (  @"\l, \L", @"Lowercase character", fm => fm.Class_lL ),
            new (  @"\N", @"Any except '\n'", fm => fm.Class_N ),
            new (  @"\O", @"Any", fm => fm.Class_O ),
            new (  @"\R", @"Line break", fm => fm.Class_R ),
            new (  @"\s, \S", @"Space", fm => fm.Class_sS ),
            new (  @"\sx, \Sx", @"Syntax group; 'x' — group", fm => fm.Class_sSx ),
            new (  @"\u, \U", @"Uppercase character", fm => fm.Class_uU ),
            new (  @"\v, \V", @"Vertical space", fm => fm.Class_vV ),
            new (  @"\w, \W", @"Word character", fm => fm.Class_wW ),
            new (  @"\X", @"Extended grapheme cluster", fm => fm.Class_X ),
            new (  @"\!c, \!\c", @"Not; 'c' — character, '\c' — escaped character", fm => fm.Class_Not ),

            new (  @"\pX, \PX", @"Unicode property, X — short property name", fm => fm.Class_pP ),
            new (  @"\p{…}, \P{…}", @"Unicode property", fm => fm.Class_pPBrace ),
            new (  @"[:class:]", @"Character class", fm => fm.Class_Name ),


            new (  @"Classes inside […]", null, null ),

            new (  @"[\d], [\D]", @"Digit", fm => fm.InsideSets_Class_dD ),
            new (  @"[\h], [\H]", @"Hexadecimal character", fm => fm.InsideSets_Class_hHhexa ),
            new (  @"[\h], [\H]", @"Horizontal space", fm => fm.InsideSets_Class_hHhorspace ),
            new (  @"[\l], [\L]", @"Lowercase character", fm => fm.InsideSets_Class_lL ),
            new (  @"[\R]", @"Line break", fm => fm.InsideSets_Class_R ),
            new (  @"[\s], [\S]", @"Space", fm => fm.InsideSets_Class_sS ),
            new (  @"[\sx], [\Sx]", @"Syntax group; 'x' — group", fm => fm.InsideSets_Class_sSx ),
            new (  @"[\u], [\U]", @"Uppercase character", fm => fm.InsideSets_Class_uU ),
            new (  @"[\v], [\V]", @"Vertical space", fm => fm.InsideSets_Class_vV ),
            new (  @"[\w], [\W]", @"Word character", fm => fm.InsideSets_Class_wW ),
            new (  @"[\X]", @"Extended grapheme cluster", fm => fm.InsideSets_Class_X ),
            new (  @"[\pX], [\PX]", @"Unicode property, X — short property name", fm => fm.InsideSets_Class_pP ),
            new (  @"[\p{…}], [\P{…}]", @"Unicode property", fm => fm.InsideSets_Class_pPBrace ),
            new (  @"[[:class:]]", @"Character class", fm => fm.InsideSets_Class_Name ),
            new (  @"[[=elem=]]", @"Equivalence", fm => fm.InsideSets_Equivalence ),
            new (  @"[[.elem.]]", @"Collating symbol", fm => fm.InsideSets_Collating ),


            new (  @"Operators inside […]", null, null ),

            new (  @"[[…] op […]]", @"Using operators for nested groups", fm => fm.InsideSets_Operators),
            new (  @"(?[[…] op […]])", @"Using operators for nested groups", fm => fm.InsideSets_OperatorsExtended),
            new (  @"[…] & […]", @"Intersection", fm => fm.InsideSets_Operator_Ampersand),
            new (  @"[…] + […]", @"Union", fm => fm.InsideSets_Operator_Plus),
            new (  @"[…] | […]", @"Union", fm => fm.InsideSets_Operator_VerticalLine),
            new (  @"[…] - […]", @"Subtraction", fm => fm.InsideSets_Operator_Minus),
            new (  @"[…] ^ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_Circumflex),
            new (  @"![…]", @"Complement", fm => fm.InsideSets_Operator_Exclamation),
            new (  @"[…] && […]", @"Intersection", fm => fm.InsideSets_Operator_DoubleAmpersand),
            new (  @"[…] || […]", @"Union", fm => fm.InsideSets_Operator_DoubleVerticalLine),
            new (  @"[…] -- […]", @"Difference", fm => fm.InsideSets_Operator_DoubleMinus),
            new (  @"[…] ~~ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_DoubleTilde),


            new (  @"Anchors", null, null ),

            new (  @"^", @"Beginning of string or line, depending on options", fm => fm.Anchor_Circumflex),
            new (  @"$", @"End, or before '\n' at end of string or line, depending on options", fm => fm.Anchor_Dollar),
            new (  @"\A", @"Start of string", fm => fm.Anchor_A),
            new (  @"\Z", @"End of string, or before '\n' at end of string", fm => fm.Anchor_Z),
            new (  @"\z", @"End of string", fm => fm.Anchor_z),
            new (  @"\G", @"start of string or end of previous match", fm => fm.Anchor_G ),
            new (  @"\b, \B", @"Boundary between \w and \W", fm => fm.Anchor_bB ),
            new (  @"\b{g}", @"Unicode extended grapheme cluster boundary", fm => fm.Anchor_bg ),
            new (  @"\b{…}, \B{…}", @"Typed boundary", fm => fm.Anchor_bBBrace ),
            new (  @"\K", @"Keep the stuff left of the \K", fm => fm.Anchor_K ),
            new (  @"\m, \M", @"Start of word, end of word", fm => fm.Anchor_mM ),
            new (  @"\<, \>", @"Start of word, end of word", fm => fm.Anchor_LtGt ),
            new (  @"\`, \'", @"Start of string, end of string", fm => fm.Anchor_GraveApos ),
            new (  @"\y, \Y", @"Boundary between graphemes", fm => fm.Anchor_yY ),


            new (  @"Named groups and backreferences", null, null ),

            new (  @"(?'name'…)", @"Named group", fm => fm.NamedGroup_Apos ),
            new (  @"(?<name>…)", @"Named group", fm => fm.NamedGroup_LtGt ),
            new (  @"(?P<name>…)", @"Named group", fm => fm.NamedGroup_PLtGt ),
            new (  @"(?@…)", @"Capturing group, depending on options", fm => fm.NamedGroup_AtApos || fm.NamedGroup_AtLtGt || fm.CapturingGroup ),

            new (  @"\1, \2, …, \9", @"Backreferences", fm => fm.Backref_1_9 ),
            new (  @"\nnn", @"Backreference, one or more digits", fm => fm.Backref_Num ),
            new (  @"\k'name'", @"Backreference by name", fm => fm.Backref_kApos ),
            new (  @"\k<name>", @"Backreference by name", fm => fm.Backref_kLtGt ),
            new (  @"\k{name}", @"Backreference by name", fm => fm.Backref_kBrace ),
            new (  @"\kn", @"Backreference \k1, \k2, …", fm => fm.Backref_kNum ),
            new (  @"\k-n", @"Relative backreference \k-1, \k-2, …", fm => fm.Backref_kNegNum ),
            new (  @"\g'…'", @"Backreference by name or number", fm => fm.Backref_gApos ),
            new (  @"\g<…>", @"Backreference by name or number", fm => fm.Backref_gLtGt ),
            new (  @"\gn", @"Backreference \g1, \g2, …", fm => fm.Backref_gNum ),
            new (  @"\g-n", @"Relative backreference \g-1, \g-2, …", fm => fm.Backref_gNegNum ),
            new (  @"\g{…}", @"Backreference \g{name}, \g{number}, \g{-number}, g{+number}", fm => fm.Backref_gBrace ),
            new (  @"(?P=name)", @"Backreference by name", fm => fm.Backref_PEqName ),
            new (  @"\k< … >, \g< … >", @"Allow spaces like '\k < name >' when whitespaces are enabled by options", fm => fm.AllowSpacesInBackref ),


            new (  @"Grouping", null, null ),

            new (  @"(?:…)", @"Noncapturing group", fm => fm.NoncapturingGroup ),
            new (  @"(?=…)", @"Positive lookahead ", fm => fm.PositiveLookahead ),
            new (  @"(?!…)", @"Negative lookahead ", fm => fm.NegativeLookahead ),
            new (  @"(?<=…)", @"Positive lookbehind", fm => fm.PositiveLookbehind ),
            new (  @"(?<!…)", @"Negative lookbehind", fm => fm.NegativeLookbehind ),
            new (  @"(?>…)", @"Atomic group", fm => fm.AtomicGroup ),
            new (  @"(?|…)", @"Branch reset", fm => fm.BranchReset ),
            new (  @"(?*…)", @"Non-atomic positive lookahead", fm => fm.NonatomicPositiveLookahead ),
            new (  @"(?<*…)", @"Non-atomic positive lookbehind ", fm => fm.NonatomicPositiveLookbehind ),
            new (  @"(?~…)", @"Absent operator", fm => fm.AbsentOperator ),
            new (  @"( ? … )", @"Allow spaces like '( ? < name >…)' when whitespaces are enabled by options", fm => fm.AllowSpacesInGroups ),


            new (  @"Recursive patterns", null, null ),

            new (  @"(?n)", @"Recursive subpattern by number", fm => fm.Recursive_Num ),
            new (  @"(?-n), (?+n)", @"Relative recursive subpattern by number", fm => fm.Recursive_PlusMinusNum ),
            new (  @"(?R)", @"Recursive whole pattern", fm => fm.Recursive_R ),
            new (  @"(?&name)", @"Recursive subpattern by name", fm => fm.Recursive_Name ),
            new (  @"(?P>name)", @"Recursive subpattern by name", fm => fm.Recursive_PGtName ),


            new (  @"Quantifiers", null, null ),

            new (  @"*", @"Zero or more times", fm => fm.Quantifier_Asterisk ),
            new (  @"+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal ),
            new (  @"\+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed ),
            new (  @"?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal),
            new (  @"\?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed),
            new (  @"{n,m}", @"Between n and m times: {n}, {n,}, {n,m}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal ),
            new (  @"\{n,m\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed ),
            new (  @"{ n, m } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.Both ),
            new (  @"{ n, m } ", @"Allow spaces within {…} or \{…\} when spaces are allowed by options", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.XModeOnly ),
            new (  @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", fm => fm.Quantifier_LowAbbrev ),
            new (  @"{expr}, \{expr\}", @"Approximate matching using given engine-specific expression", fm => fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal ||fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed ),


            new (  @"Conditionals", null, null ),

            new (  @"(?(number)…|…)", @"Conditionals by number, +number and -number", fm => fm.Conditional_BackrefByNumber ),
            new (  @"(?(name)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName ),
            new (  @"(?(pattern)…|…)", @"Conditional subpattern", fm => fm.Conditional_Pattern ),
            new (  @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", fm => fm.Conditional_PatternOrBackrefByName ),
            new (  @"(?('name')…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_Apos ),
            new (  @"(?(<name>)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_LtGt ),
            new (  @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", fm => fm.Conditional_R ),
            new (  @"(?(R&name)…|…)", @"Recursive conditional by name", fm => fm.Conditional_RName ),
            new (  @"(?(DEFINE)…|…)", @"Defining subpatterns", fm => fm.Conditional_DEFINE ),
            new (  @"(?(VERSION…)…|…)", @"Checking for version using 'VERSION=decimal' or 'VERSION>=decimal'", fm => fm.Conditional_VERSION ),


            new (  @"Miscellaneous", null, null ),

            new (  @"(*verb)", @"Control verbs: (*verb), (*verb:…), (*:name)", fm => fm.ControlVerbs ),
            new (  @"(*…:…)", @"Script runs, such as (*atomic:…)", fm => fm.ScriptRuns ),

            new (  @"(?)", @"Empty construct", fm => fm.EmptyConstruct ),
            new (  @"(? )", @"Empty construct when whitespaces are enabled by options", fm => fm.EmptyConstructX ),
            new (  @"[]", @"Empty set", fm => fm.EmptySet ),

            new (  @"“.” on Surrogate Pairs", @"Split Surrogate Pair characters into two components", fm => fm.SplitSurrogatePairs ),
        ];
}
