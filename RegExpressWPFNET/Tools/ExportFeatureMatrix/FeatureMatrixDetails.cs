using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
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
    internal readonly string? TestPatternMatch = null;
    internal readonly string? TestTextMatch = null;
    internal readonly string? TestPatternNoMatch = null;
    internal readonly string? TestTextNoMatch = null;

    internal FeatureMatrixDetails( string shortDesc, string? desc, Func<FeatureMatrix, bool>? func )
    {
        ShortDesc = shortDesc;
        Desc = desc;
        Func = func;
    }

    internal FeatureMatrixDetails( string shortDesc, string? desc, Func<FeatureMatrix, bool>? func,
        [StringSyntax( StringSyntaxAttribute.Regex )] string? testPatternMatch, string? testTextMatch,
        [StringSyntax( StringSyntaxAttribute.Regex )] string? testPatternNoMatch = null, string? testTextNoMatch = null )
    {
        if( ( testPatternMatch == null ) != ( testTextMatch == null ) ) throw new ArgumentException( "test match" );
        if( ( testPatternNoMatch == null ) != ( testTextNoMatch == null ) ) throw new ArgumentException( "test no match" );

        ShortDesc = shortDesc;
        Desc = desc;
        Func = func;

        TestPatternMatch = testPatternMatch;
        TestTextMatch = testTextMatch;
        TestPatternNoMatch = testPatternNoMatch;
        TestTextNoMatch = testTextNoMatch;
    }

    internal static readonly FeatureMatrixDetails[] AllFeatureMatrixDetails =
        [
            new ( @"General", null, null ),

            new ( @"(…)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal, @"(x)", "x" ),
            new ( @"\(…\)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed, @"\(x\)", "x" ),

            new ( @"[…]", @"Character group", fm => fm.Brackets, @"[x]", "x" ),
            new ( @"(?[…])", @"Character group", fm => fm.ExtendedBrackets, @"(?[[x]])", "x" ),

            new ( @"|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal, @"x|y", "y" ),
            new ( @"\|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed, @"x\|y", "y" ),

            new ( @"(?#comment)", @"Inline comment", fm => fm.InlineComments ), // TODO
            new ( @"#comment", @"Comment when enabled by options", fm => fm.XModeComments ),
            new ( @"[#comment]", @"Comment inside […] when enabled by options", fm => fm.InsideSets_XModeComments ),

            new ( @"(?flags)", @"Inline options", fm => fm.Flags, @"(?i)x", "X" ),
            new ( @"(?flags:…)", @"Inline scoped options", fm => fm.ScopedFlags, @"(?i:x)", "X" ),
            new ( @"(?^flags)", @"Inline fresh options", fm => fm.CircumflexFlags, @"(?i)(?^)X", "X", @"(?i)(?^)x", "X" ),
            new ( @"(?^flags:…)", @"Inline scoped fresh options", fm => fm.ScopedCircumflexFlags, @"(?i)(?^:X)", "X", @"(?i)(?^:x)", "X" ),
            new ( @"(?x)", @"Allow 'x' flag", fm => fm.XFlag, @"(?x)a b", "ab" ),
            new ( @"(?xx)", @"Allow 'xx' flag", fm => fm.XXFlag, @"(?x)[a b](?xx)[a b]", " b", @"(?xx)[x y]", " " ),

            new ( @"\Q…\E", @"Literal", fm => fm.Literal_QE, @"\Qx\E", "x", @"\Qx\E", "Q" ),
            new ( @"[\Q…\E]", @"Literal inside […]", fm => fm.InsideSets_Literal_QE, @"[\Qx\E]", "x", @"[\Qx\E]", "Q" ),
            new ( @"[\q{…}]", @"Literal inside […]", fm => fm.InsideSets_Literal_qBrace, @"[\q{x}]", "x", @"[\q{x}]", "q" ),

            new ( @"Escapes", null, null ),

            new ( @"\a", @"Bell, \u0007", fm => fm.Esc_a, @"\a", "\u0007" ),
            new ( @"\b", @"Backspace, \u0008", fm => fm.Esc_b, @"\b", "\u0008", @"x\b", "x" ),
            new ( @"\e", @"Escape, \u001B", fm => fm.Esc_e, @"\e", "\u001B" ),
            new ( @"\f", @"Form feed, \u000C", fm => fm.Esc_f, @"\f", "\u000C" ),
            new ( @"\n", @"New line, \u000A", fm => fm.Esc_n, @"\n", "\u000A" ),
            new ( @"\r", @"Carriage return, \u000D", fm => fm.Esc_r, @"\r", "\u000D" ),
            new ( @"\t", @"Tab, \u0009", fm => fm.Esc_t, @"\t", "\u0009" ),
            new ( @"\v", @"Vertical tab, \u000B", fm => fm.Esc_v, @"\v", "\u000B", @"\v", "\f" ),
            new ( @"\1..\7", @"Octal, one digit", fm => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3, @"\3", "\u0003" ),
            new ( @"\nnn", @"Octal, two or three digits", fm => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 || fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3, @"\11\101", "\u0009A" ),
            new ( @"\0nnn", @"Octal, up to three digits after '\0'", fm => fm.Esc_Octal0_1_3, @"\03\011\0011", "\u0003\u0009\u0009" ),
            new ( @"\o{nn…}", @"Octal", fm => fm.Esc_oBrace, @"\o{11}", "\u0009" ),
            new ( @"\xXX", @"Hexadecimal code, two digits", fm => fm.Esc_x2, @"\x09", "\u0009" ),
            new ( @"\x{XX…}", @"Hexadecimal code", fm => fm.Esc_xBrace, @"\x{0009}", "\u0009" ),
            new ( @"\uXXXX", @"Hexadecimal code, four digits", fm => fm.Esc_u4, @"\u0009", "\u0009" ),
            new ( @"\UXXXXXXXX", @"Hexadecimal code, eight digits", fm => fm.Esc_U8, @"\U00000009", "\u0009" ),
            new ( @"\u{XX…}", @"Hexadecimal code", fm => fm.Esc_uBrace, @"\u{0009}", "\u0009" ),
            new ( @"\U{XX…}", @"Hexadecimal code", fm => fm.Esc_UBrace, @"\U{0009}", "\u0009" ),
            new ( @"\cC", @"Control character", fm => fm.Esc_c1, @"\cM", "\r" ),
            new ( @"\CC", @"Control character", fm => fm.Esc_C1, @"\CM", "\r" ),
            new ( @"\C-C", @"Control character", fm => fm.Esc_CMinus, @"\C-M", "\r" ),
            new ( @"\N{…}", @"Unicode name or 'U+code'", fm => fm.Esc_NBrace, @"\N{comma}", "," ), // (some do not understand "COMMA", "LATIN CAPITAL LETTER A")
            new ( @"\any", @"Generic escape", fm => fm.GenericEscape, @"\\", @"\" ),

            new ( @"Escapes inside […]", null, null ),

            new ( @"[\a]", @"Bell, \u0007", fm => fm.InsideSets_Esc_a, @"[\a]", "\u0007" ),
            new ( @"[\b]", @"Backspace, \u0008", fm => fm.InsideSets_Esc_b, @"[\b]", "\u0008" ),
            new ( @"[\e]", @"Escape, \u001B", fm => fm.InsideSets_Esc_e, @"[\e]", "\u001B" ),
            new ( @"[\f]", @"Form feed, \u000C", fm => fm.InsideSets_Esc_f, @"[\f]", "\u000C" ),
            new ( @"[\n]", @"New line, \u000A", fm => fm.InsideSets_Esc_n, @"[\n]", "\u000A" ),
            new ( @"[\r]", @"Carriage return, \u000D", fm => fm.InsideSets_Esc_r, @"[\r]", "\u000D" ),
            new ( @"[\t]", @"Tab, \u0009", fm => fm.InsideSets_Esc_t, @"[\t]", "\u0009" ),
            new ( @"[\v]", @"Vertical tab, \u000B", fm => fm.InsideSets_Esc_v, @"[\v]", "\u000B", @"[\v]", "\f" ),
            new ( @"[\1..\7]", @"Octal, one digit", fm => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3, @"[\3]", "\u0003" ),
            new ( @"[\nnn]", @"Octal, two or three digits", fm => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 || fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3, @"[\11][\101]", "\u0009A" ),
            new ( @"[\0nnn]", @"Octal, up to three digits after '\0'", fm => fm.InsideSets_Esc_Octal0_1_3, @"[\03][\011][\0011]", "\u0003\u0009\u0009" ),
            new ( @"[\o{nn…}]", @"Octal", fm => fm.InsideSets_Esc_oBrace, @"[\o{11}]", "\u0009" ),
            new ( @"[\xXX]", @"Hexadecimal code, two digits", fm => fm.InsideSets_Esc_x2, @"[\x09]", "\u0009" ),
            new ( @"[\x{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_xBrace, @"[\x{0009}]", "\u0009" ),
            new ( @"[\uXXXX]", @"Hexadecimal code, four digits", fm => fm.InsideSets_Esc_u4, @"[\u0009]", "\u0009", @"[\u0009]", "X" ),
            new ( @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", fm => fm.InsideSets_Esc_U8, @"[\U00000009]", "\u0009", @"[\U00000009]", "x" ),
            new ( @"[\u{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_uBrace, @"[\u{0009}]", "\u0009", @"[\u{0009}]", "X" ),
            new ( @"[\U{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_UBrace, @"[\U{0009}]", "\u0009", @"[\U{0009}]", "x" ),
            new ( @"[\cC]", @"Control character", fm => fm.InsideSets_Esc_c1, @"[\cM]", "\r" ),
            new ( @"[\CC]", @"Control character", fm => fm.InsideSets_Esc_C1, @"[\CM]", "\r" ),
            new ( @"[\C-C]", @"Control character ", fm => fm.InsideSets_Esc_CMinus, @"[\C-M]", "\r" ),
            new ( @"[\N{…}]", @"Unicode name or 'U+code'", fm => fm.InsideSets_Esc_NBrace, @"[\N{comma}]", "," ), // (some do not understand "COMMA", "LATIN CAPITAL LETTER A")
            new ( @"[\any]", @"Generic escape", fm => fm.InsideSets_GenericEscape, @"[\-]", "-", @"[\-]", @"\" ),

            new ( @"Classes", null, null ),

            new ( @".", @"Any, including or excepting newline (\n) depending on options", fm => fm.Class_Dot, @".", "x" ),
            new ( @"\C", @"Single byte", fm => fm.Class_Cbyte, @"\C\C", "î" ),
            new ( @"\C", @"Single code point", fm => fm.Class_Ccp, @"\C", "x", @"\C\C", "î" ),
            new ( @"\d, \D", @"Digit", fm => fm.Class_dD, @"\d\D", "9x" ),
            new ( @"\h, \H", @"Hexadecimal character", fm => fm.Class_hHhexa, @"\h\H", "Ax" ),
            new ( @"\h, \H", @"Horizontal space", fm => fm.Class_hHhorspace, @"\h\H", " x" ),
            new ( @"\l, \L", @"Lowercase character", fm => fm.Class_lL, @"\l\L", "xX" ),
            new ( @"\N", @"Any except '\n'", fm => fm.Class_N, @"\N", "a", @"\N", "\n" ),
            new ( @"\O", @"Any", fm => fm.Class_O, @"\O", "a" ),
            new ( @"\R", @"Line break", fm => fm.Class_R, @"a\Rb", "a\r\nb" ),
            new ( @"\s, \S", @"Space", fm => fm.Class_sS, @"\s\S", " x"),
            new ( @"\sx, \Sx", @"Syntax group; 'x' — group", fm => fm.Class_sSx, @"\ss", " " ),
            new ( @"\u, \U", @"Uppercase character", fm => fm.Class_uU, @"\u\U", "Xx" ),
            new ( @"\v, \V", @"Vertical space", fm => fm.Class_vV, @"\v\v", "\r\f" ),
            new ( @"\w, \W", @"Word character", fm => fm.Class_wW, @"\w\w\w", "xyz" ),
            new ( @"\X", @"Extended grapheme cluster", fm => fm.Class_X, @"\X", "a" ),
            new ( @"\!c, \!\c", @"Not; 'c' — character, '\c' — escaped character", fm => fm.Class_Not, @"\!x", "a" ),
            new ( @"\pX, \PX", @"Unicode property, X — short property name", fm => fm.Class_pP, @"\pL\PL", "x9" ),
            new ( @"\p{…}, \P{…}", @"Unicode property", fm => fm.Class_pPBrace, @"\p{L}\P{L}", "x9" ),
            new ( @"[:class:]", @"Character class", fm => fm.Class_Name, @"[:alpha:]", "X" ),

            new ( @"Classes inside […]", null, null ),

            new ( @"[\d], [\D]", @"Digit", fm => fm.InsideSets_Class_dD, @"[\d]", "9" ),
            new ( @"[\h], [\H]", @"Hexadecimal character", fm => fm.InsideSets_Class_hHhexa, @"[\h][\H]", "Ax" ),
            new ( @"[\h], [\H]", @"Horizontal space", fm => fm.InsideSets_Class_hHhorspace, @"[\h][\H]", " x" ),
            new ( @"[\l], [\L]", @"Lowercase character", fm => fm.InsideSets_Class_lL, @"[\l][\L]", "xX" ),
            new ( @"[\R]", @"Line break", fm => fm.InsideSets_Class_R, @"a[\R]b", "a\r\nb" ),
            new ( @"[\s], [\S]", @"Space", fm => fm.InsideSets_Class_sS, @"a[\s][\S]x", "a 9x" ),
            new ( @"[\sx], [\Sx]", @"Syntax group; 'x' — group", fm => fm.InsideSets_Class_sSx, @"[\ss]", " ", @"[\ss]", "s" ),
            new ( @"[\u], [\U]", @"Uppercase character", fm => fm.InsideSets_Class_uU, @"[\u][\U]", "Xx" ),
            new ( @"[\v], [\V]", @"Vertical space", fm => fm.InsideSets_Class_vV, @"[\v][\v]", "\r\f" ),
            new ( @"[\w], [\W]", @"Word character", fm => fm.InsideSets_Class_wW, @"[\w][\w][\w]", "xyz" ),
            new ( @"[\X]", @"Extended grapheme cluster", fm => fm.InsideSets_Class_X, @"[\X]", "a" ),
            new ( @"[\pX], [\PX]", @"Unicode property, X — short property name", fm => fm.InsideSets_Class_pP, @"[\pL][\PL]", "x9" ),
            new ( @"[\p{…}], [\P{…}]", @"Unicode property", fm => fm.InsideSets_Class_pPBrace, @"[\p{L}][\P{L}]", "x9" ),
            new ( @"[[:class:]]", @"Character class", fm => fm.InsideSets_Class_Name, @"[[:alpha:]]", "X" ),
            new ( @"[[=elem=]]", @"Equivalence", fm => fm.InsideSets_Equivalence, @"[[=a=]][[=a=]]", "aA" ), // 'Á' not matched by C++ regex.
            new ( @"[[.elem.]]", @"Collating symbol", fm => fm.InsideSets_Collating, @"a[[.ch.]]x", "achx" ),

            new ( @"Operators inside […]", null, null ),

            new ( @"[[…] op […]]", @"Using operators for nested groups", fm => fm.InsideSets_Operators),
            new ( @"(?[[…] op […]])", @"Using operators for nested groups", fm => fm.InsideSets_OperatorsExtended),
            new ( @"[[…] & […]]", @"Intersection", fm => fm.InsideSets_Operator_Ampersand, @"[[ab]&[bc]]", "b", @"[[ab]&[bc]]", "&" ),
            new ( @"[[…] + […]]", @"Union", fm => fm.InsideSets_Operator_Plus, @"[[a]+[b]][[d]+[b]]", "ab", @"[[a]+[b]]", "+" ),
            new ( @"[[…] | […]]", @"Union", fm => fm.InsideSets_Operator_VerticalLine, @"[[a]+[b]][[d]+[b]]", "ab", @"[[a]|[b]]", "|" ),
            new ( @"[[…] - […]]", @"Subtraction", fm => fm.InsideSets_Operator_Minus, @"[[abc]-[b]][[abc]-[b]]", "ac", @"[[a]-[b]]", "][b-" ),
            new ( @"[[…] ^ […]]", @"Symmetric difference", fm => fm.InsideSets_Operator_Circumflex, @"[[ab]^6[bc]][[ab]^[bc]]", "ac", @"[[ab]^[bc]]", "b][^" ),
            new ( @"[![…]]", @"Complement", fm => fm.InsideSets_Operator_Exclamation, @"[![abc]]", "d" ),
            new ( @"[[…] && […]]", @"Intersection", fm => fm.InsideSets_Operator_DoubleAmpersand, @"[[ab]&&[bc]]", "b" ),
            new ( @"[[…] || […]]", @"Union", fm => fm.InsideSets_Operator_DoubleVerticalLine, @"[[a]||[c]][[d]||[b]]", "ab", @"[[a]||[b]]", "|[" ),
            new ( @"[[…] -- […]]", @"Difference", fm => fm.InsideSets_Operator_DoubleMinus, @"[[ab]--[b]]", "a", @"[[ab]--[b]]", "][-b" ),
            new ( @"[[…] ~~ […]]", @"Symmetric difference", fm => fm.InsideSets_Operator_DoubleTilde, @"[[ab]~~[bc]][[ab]~~[bc]]", "ac", @"[[ab]~~[bc]]", "b][~" ),

            new ( @"Anchors", null, null ),

            new ( @"^", @"Beginning of string or line, depending on options", fm => fm.Anchor_Circumflex, @"^x", "x" ),
            new ( @"$", @"End, or before '\n' at end of string or line, depending on options", fm => fm.Anchor_Dollar, @"x$", "x" ),
            new ( @"\A", @"Start of string", fm => fm.Anchor_A, @"\Ax", "x" ),
            new ( @"\Z", @"End of string, or before '\n' at end of string", fm => fm.Anchor_Z, @"x\Z", "x" ),
            new ( @"\z", @"End of string", fm => fm.Anchor_z, @"x\z", "x" ),
            new ( @"\G", @"start of string or end of previous match", fm => fm.Anchor_G, @"\Gx", "x" ),
            new ( @"\b, \B", @"Boundary between \w and \W", fm => fm.Anchor_bB, @"\bx", "y x" ),
            new ( @"\b{g}", @"Unicode extended grapheme cluster boundary", fm => fm.Anchor_bg, @"\b{g}x", "y x" ),
            new ( @"\b{…}, \B{…}", @"Typed boundary", fm => fm.Anchor_bBBrace, @"\b{wb}x", "y x" ),
            new ( @"\K", @"Keep the stuff left of the \K", fm => fm.Anchor_K, @"a\Kb", "ab" ),
            new ( @"\m, \M", @"Start of word, end of word", fm => fm.Anchor_mM, @"\mword\M", "some word here" ),
            new ( @"\<, \>", @"Start of word, end of word", fm => fm.Anchor_LtGt, @"\<word\>", "some word here" ),
            new ( @"\`, \'", @"Start of string, end of string", fm => fm.Anchor_GraveApos, @"\`x\'", "x" ),
            new ( @"\y, \Y", @"Boundary between graphemes", fm => fm.Anchor_yY, @"a\yb", "ab" ),

            new ( @"Named groups and backreferences", null, null ),

            new ( @"(?'name'…)", @"Named group", fm => fm.NamedGroup_Apos, @"(?'n'x)", "x" ),
            new ( @"(?<name>…)", @"Named group", fm => fm.NamedGroup_LtGt, @"(?<n>x)", "x" ),
            new ( @"(?P<name>…)", @"Named group", fm => fm.NamedGroup_PLtGt, @"(?P<n>x)", "x" ),
            new ( @"(?@…)", @"Capturing group, depending on options", fm => fm.NamedGroup_AtApos || fm.NamedGroup_AtLtGt || fm.CapturingGroup, @"(@<n>x)", "x" ),

            new ( @"\1, \2, …, \9", @"Backreferences", fm => fm.Backref_Num == FeatureMatrix.BackrefEnum.OneDigit || fm.Backref_Num == FeatureMatrix.BackrefEnum.Any , @"(x)\1", "xx" ),
            new ( @"\nnn", @"Backreference, two or more digits", fm => fm.Backref_Num == FeatureMatrix.BackrefEnum.Any, @"(x)(x)(x)(x)(x)(x)(x)(x)(x)(y)\10", "xxxxxxxxxyy" ),
            new ( @"\k'name'", @"Backreference by name", fm => fm.Backref_kApos, @"(?'n'x)\k'n'", "xx" ),
            new ( @"\k<name>", @"Backreference by name", fm => fm.Backref_kLtGt, @"(?<n>x)\k<n>", "xx" ),
            new ( @"\k{name}", @"Backreference by name", fm => fm.Backref_kBrace, @"(?<n>x)\k{n}", "xx" ),
            new ( @"\kn", @"Backreference \k1, \k2, …", fm => fm.Backref_kNum, @"(?<n>x)\k1", "xx" ),
            new ( @"\k-n", @"Relative backreference \k-1, \k-2, …", fm => fm.Backref_kNegNum, @"(?<n>x)\k-1", "xx" ),
            new ( @"\g'…'", @"Subroutine by name or number", fm => fm.Backref_gApos, @"(?'n'x)\g'n'", "xx" ),
            new ( @"\g<…>", @"Subroutine by name or number", fm => fm.Backref_gLtGt, @"(?<n>x)\g<n>", "xx" ),
            new ( @"\gn", @"Subroutine \g1, \g2, …", fm => fm.Backref_gNum, @"(?<n>x)\g1", "xx" ),
            new ( @"\g-n", @"Relative subroutine \g-1, \g-2, …", fm => fm.Backref_gNegNum, @"(?<n>x)\g-1", "xx" ),
            new ( @"\g{…}", @"Subroutine \g{name}, \g{number}, \g{-number}, g{+number}", fm => fm.Backref_gBrace, @"(?<n>x)\g{n}", "xx" ),
            new ( @"(?P=name)", @"Subroutine by name", fm => fm.Backref_PEqName, @"(?P<n>x)(?P=n)", "xx" ),
            new ( @"\k< … >, \g< … >", @"Allow spaces like '\k < name >' when whitespaces are enabled by options", fm => fm.AllowSpacesInBackref ), // TODO

            new ( @"Grouping", null, null ),

            new ( @"(?:…)", @"Noncapturing group", fm => fm.NoncapturingGroup, @"(?:x)", "x" ),
            new ( @"(?=…)", @"Positive lookahead ", fm => fm.PositiveLookahead, @"a(?=x)x", "ax" ),
            new ( @"(?!…)", @"Negative lookahead ", fm => fm.NegativeLookahead, @"a(?!x)y", "ay" ),
            new ( @"(?<=…)", @"Positive lookbehind", fm => fm.PositiveLookbehind, @"(?<=x)a", "xa" ),
            new ( @"(?<!…)", @"Negative lookbehind", fm => fm.NegativeLookbehind, @"(?<!x)a", "ya" ),
            new ( @"(?>…)", @"Atomic group", fm => fm.AtomicGroup, @"(?>x)", "x" ),
            new ( @"(?|…)", @"Branch reset", fm => fm.BranchReset, @"(?|(a)|(b)\1)", "bb" ),
            new ( @"(?*…)", @"Non-atomic positive lookahead", fm => fm.NonatomicPositiveLookahead, @"a(?*x)x", "ax" ),
            new ( @"(?<*…)", @"Non-atomic positive lookbehind ", fm => fm.NonatomicPositiveLookbehind, @"(?<*x)a", "xa" ),
            new ( @"(?~…)", @"Absent operator", fm => fm.AbsentOperator, @"/\*(?~\*\/)\*\/", "/* abc */" ),
            new ( @"( ? … )", @"Allow spaces like '( ? < name >…)' when whitespaces are enabled by options", fm => fm.AllowSpacesInGroups ), // TODO

            new ( @"Recursive patterns", null, null ),

            new ( @"(?n)", @"Recursive subpattern by number", fm => fm.Recursive_Num, @"(x.)(?1)", "xyxz" ),
            new ( @"(?-n), (?+n)", @"Relative recursive subpattern by number", fm => fm.Recursive_PlusMinusNum, @"(x(.))(?-1)", "xyz" ),
            new ( @"(?R)", @"Recursive whole pattern", fm => fm.Recursive_R, @"a(?R)*b", "aabb" ),
            new ( @"(?&name)", @"Recursive subpattern by name", fm => fm.Recursive_Name, @"(?<n>a)(?&n)", "aa" ),
            new ( @"(?P>name)", @"Recursive subpattern by name", fm => fm.Recursive_PGtName, @"(?P<n>a)(?P>n)", "aa" ),

            new ( @"Quantifiers", null, null ),

            new ( @"*", @"Zero or more times", fm => fm.Quantifier_Asterisk, @"xy*", "x" ),
            new ( @"+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal, @"xy+", "xyy" ),
            new ( @"\+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed, @"xy\+", "xyy" ),
            new ( @"?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal, @"xy?", "x" ),
            new ( @"\?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed, @"xy\?", "x" ),
            new ( @"{n,m}", @"Between n and m times: {n}, {n,}, {n,m}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal, @"x{2,3}", "xx" ),
            new ( @"\{n,m\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed, @"x\{2,3\}", "xx" ),
            new ( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.Both ), // TODO
            new ( @"{ n, m } ", @"Allow spaces within {…} or \{…\} when spaces are allowed by options", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.XModeOnly ), // TODO
            new ( @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", fm => fm.Quantifier_LowAbbrev, @"x{,3}", "xxx" ),
            new ( @"{expr}, \{expr\}", @"Approximate matching using given engine-specific expression", fm => fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal || fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed ), // TODO

            new ( @"Conditionals", null, null ),

            new ( @"(?(number)…|…)", @"Conditionals by number, +number and -number", fm => fm.Conditional_BackrefByNumber, @"(x)(?(1)y|z)", "xy" ),
            new ( @"(?(name)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName, @"(?<n>x)(?(n)y|z)", "xy" ),
            new ( @"(?(pattern)…|…)", @"Conditional subpattern", fm => fm.Conditional_Pattern, @"x(?(?=.z)y|z)", "xyz" ),
            new ( @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", fm => fm.Conditional_PatternOrBackrefByName, @"x(?(y).|z)", "xy" ),
            new ( @"(?('name')…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_Apos, @"(?'n'x)(?('n')y|z)", "xy" ),
            new ( @"(?(<name>)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_LtGt, @"(?<n>x)(?(<n>)y|z)", "xy" ),
            new ( @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", fm => fm.Conditional_R, @"(?(R)a+|(?R)b)", "aaaab" ),
            new ( @"(?(R&name)…|…)", @"Recursive conditional by name", fm => fm.Conditional_RName, @"(?<A>(?'B'abc(?(R)(?(R&A)1)(?(R&B)2)X|(?1)(?2)(?R))))", "abcabc1Xabc2XabcXabcabc" ),
            new ( @"(?(DEFINE)…|…)", @"Defining subpatterns", fm => fm.Conditional_DEFINE, @"(?(DEFINE)(?<n>x.z))(?&n)", "xyz" ),
            new ( @"(?(VERSION…)…|…)", @"Checking for version using 'VERSION=decimal' or 'VERSION>=decimal'", fm => fm.Conditional_VERSION, @"(?(VERSION>=1)xyz|abc)", "xyz" ),

            new ( @"Miscellaneous", null, null ),

            new ( @"(*verb)", @"Control verbs: (*verb), (*verb:…), (*:name)", fm => fm.ControlVerbs, @"x|y(*FAIL)", "x" ),
            new ( @"(*…:…)", @"Script runs, such as (*atomic:…)", fm => fm.ScriptRuns, @"(*atomic:x)", "x" ),
            new ( @"(?Cn), (*func)", @"Callouts (custom functions)", fm => fm.Callouts ),

            new ( @"(?)", @"Empty construct", fm => fm.EmptyConstruct, @"x(?)y", "xy" ),
            new ( @"(? )", @"Empty construct when whitespaces are enabled by options", fm => fm.EmptyConstructX, @"(?x)x(? )y", "xy" ),
            new ( @"[]", @"Empty set", fm => fm.EmptySet, @"x[]?", "x" ),

            new ( @"“.” on Surrogate Pairs", @"Split Surrogate Pair characters into components", fm => fm.SplitSurrogatePairs ), // TODO

        ];
}
