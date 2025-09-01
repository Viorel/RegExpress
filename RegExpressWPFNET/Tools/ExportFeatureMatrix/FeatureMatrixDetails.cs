using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

class FeatureMatrixDetails
{
    internal class Rule
    {
        internal string Pattern { get; }
        internal string? TextToMatch { get; }
        internal string? TextToNotMatch { get; }

        internal Rule( string pattern, string? textToMatch, string? textToNotMatch )
        {
            Debug.Assert( textToMatch != null || textToNotMatch != null );

            Pattern = pattern;
            TextToMatch = textToMatch;
            TextToNotMatch = textToNotMatch;
        }
    }

    internal readonly string ShortDesc;
    internal readonly string? Desc;
    internal readonly Func<FeatureMatrix, bool>? Func;
    internal readonly List<Rule> Rules = [];
    internal Func<FeatureMatrix, bool>? DirectCheck { get; init; }

    internal FeatureMatrixDetails( string shortDesc )
    {
        ShortDesc = shortDesc;
        Desc = null;
        Func = null;
    }

    internal FeatureMatrixDetails( string shortDesc, string desc, Func<FeatureMatrix, bool> func )
    {
        ShortDesc = shortDesc;
        Desc = desc;
        Func = func;
    }

    internal FeatureMatrixDetails( string shortDesc, string desc, Func<FeatureMatrix, bool> func,
        [StringSyntax( StringSyntaxAttribute.Regex )] string pattern1, string? text1Match, string? text1NoMatch )
        :
        this( shortDesc, desc, func )
    {
        Rules.Add( new Rule( pattern1, text1Match, text1NoMatch ) );
    }

    internal FeatureMatrixDetails( string shortDesc, string desc, Func<FeatureMatrix, bool> func,
        [StringSyntax( StringSyntaxAttribute.Regex )] string pattern1, string? text1Match, string? text1NoMatch,
        [StringSyntax( StringSyntaxAttribute.Regex )] string pattern2, string? text2Match, string? text2NoMatch )
        :
        this( shortDesc, desc, func )
    {
        Rules.Add( new Rule( pattern1, text1Match, text1NoMatch ) );
        Rules.Add( new Rule( pattern2, text2Match, text2NoMatch ) );
    }

    internal FeatureMatrixDetails( string shortDesc, string desc, Func<FeatureMatrix, bool> func,
        [StringSyntax( StringSyntaxAttribute.Regex )] string pattern1, string? text1Match, string? text1NoMatch,
        [StringSyntax( StringSyntaxAttribute.Regex )] string pattern2, string? text2Match, string? text2NoMatch,
        [StringSyntax( StringSyntaxAttribute.Regex )] string pattern3, string? text3Match, string? text3NoMatch )
        :
        this( shortDesc, desc, func )
    {
        Rules.Add( new Rule( pattern1, text1Match, text1NoMatch ) );
        Rules.Add( new Rule( pattern2, text2Match, text2NoMatch ) );
        Rules.Add( new Rule( pattern3, text3Match, text3NoMatch ) );
    }


    internal static readonly FeatureMatrixDetails[] AllFeatureMatrixDetails =
        [
            new ( @"General"),

            new ( @"(…)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal, @"(x)", "x", null ),
            new ( @"\(…\)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed, @"\(x\)", "x", null ),

            new ( @"[…]", @"Character group", fm => fm.Brackets, @"[x]", "x", null ),
            new ( @"(?[…])", @"Character group", fm => fm.ExtendedBrackets, @"(?[[x]])", "x", null ),

            new ( @"|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal, @"x|y", "y", null ),
            new ( @"\|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed  , @"x\|y", "y", null ),
            new ( @"new line (\n)", @"Alternatives on separate lines", fm => fm.AlternationOnSeparateLines, "x\ny", "y", null ),

            new ( @"(?#comment)", @"Inline comment", fm => fm.InlineComments ), // TODO
            new ( @"#comment", @"Comment", fm => fm.XModeComments ),
            new ( @"[#comment]", @"Comment inside […]", fm => fm.InsideSets_XModeComments ),

            new ( @"(?flags)", @"Inline options", fm => fm.Flags, @"(?i)x", "X", null ),
            new ( @"(?flags:…)", @"Inline scoped options", fm => fm.ScopedFlags, @"(?i:x)", "X", null ),
            new ( @"(?^flags)", @"Inline fresh options", fm => fm.CircumflexFlags, @"(?i)(?^)X", "X", null, @"(?i)(?^)x", "X", null ),
            new ( @"(?^flags:…)", @"Inline scoped fresh options", fm => fm.ScopedCircumflexFlags, @"(?i)(?^:X)", "X", null, @"(?i)(?^:x)", "X", null ),
            new ( @"(?x)", @"Allow 'x' flag", fm => fm.XFlag, @"(?x)a b", "ab", null ),
            new ( @"(?xx)", @"Allow 'xx' flag", fm => fm.XXFlag, @"(?x)[a b](?xx)[a b]", " a", "a " ),

            new ( @"\Q…\E", @"Literal", fm => fm.Literal_QE, @"a\Qx\E", "ax", "aQxE" ),
            new ( @"[\Q…\E]", @"Literal inside […]", fm => fm.InsideSets_Literal_QE, @"[\Qx\E]", "x", "Q" ),
            new ( @"[\q{…}]", @"Literal inside […]", fm => fm.InsideSets_Literal_qBrace, @"[\q{x}]", "x", "q" ),

            new ( @"Escapes" ),

            new ( @"\a", @"Bell, \u0007", fm => fm.Esc_a, @"\a", "\u0007", null ),
            new ( @"\b", @"Backspace, \u0008", fm => fm.Esc_b, @"x\by", "x\u0008y", null ),
            new ( @"\e", @"Escape, \u001B", fm => fm.Esc_e, @"\e", "\u001B", null ),
            new ( @"\f", @"Form feed, \u000C", fm => fm.Esc_f, @"\f", "\u000C", null ),
            new ( @"\n", @"New line, \u000A", fm => fm.Esc_n, @"\n", "\u000A", null ),
            new ( @"\r", @"Carriage return, \u000D", fm => fm.Esc_r, @"\r", "\u000D", null ),
            new ( @"\t", @"Tab, \u0009", fm => fm.Esc_t, @"\t", "\u0009", null ),
            new ( @"\v", @"Vertical tab, \u000B", fm => fm.Esc_v, @"\v", "\u000B", "\f" ),
            new ( @"\1..\7", @"Octal, one digit", fm => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3, @"\3", "\u0003", null ),
            new ( @"\nn, \nnn", @"Octal, two or three digits", fm => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 || fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3,
                @"\11\101", "\u0009A", null, @"\11\000\101\000", "\u0009A", null ), // ('\000' for Oniguruma)
            new ( @"\0nnn", @"Octal, up to three digits after '\0'", fm => fm.Esc_Octal0_1_3,
                @"\03\011\0011", "\u0003\u0009\u0009", null, @"\03\000\011\000\0011\000", "\u0003\u0009\u0009", null ),
            new ( @"\o{nn…}", @"Octal", fm => fm.Esc_oBrace, @"\o{11}", "\u0009", null ),
            new ( @"\xXX", @"Hexadecimal code, two digits", fm => fm.Esc_x2, @"\x09", "\u0009", null, @"\x09\x00", "\u0009", null ),
            new ( @"\x{XX…}", @"Hexadecimal code", fm => fm.Esc_xBrace, @"\x{0009}", "\u0009", null ),
            new ( @"\uXXXX", @"Hexadecimal code, four digits", fm => fm.Esc_u4, @"\u0009", "\u0009" , null),
            new ( @"\UXXXXXXXX", @"Hexadecimal code, eight digits", fm => fm.Esc_U8, @"\U00000009", "\u0009" , null),
            new ( @"\u{XX…}", @"Hexadecimal code", fm => fm.Esc_uBrace, @"\u{0009}", "\u0009", null ),
            new ( @"\U{XX…}", @"Hexadecimal code", fm => fm.Esc_UBrace, @"\U{0009}", "\u0009", null ),
            new ( @"\cC", @"Control character", fm => fm.Esc_c1, @"\cM", "\r" , null),
            new ( @"\CC", @"Control character", fm => fm.Esc_C1, @"\CM", "\r" , null),
            new ( @"\C-C", @"Control character", fm => fm.Esc_CMinus, @"\C-M", "\r" , null),
            new ( @"\N{…}", @"Unicode name or 'U+code'", fm => fm.Esc_NBrace, @"\N{COMMA}", "," , null, @"\N{comma}", "," , null),
            new ( @"\any", @"Generic escape", fm => fm.GenericEscape, @"\\", @"\", null ),

            new ( @"Escapes inside […]" ),

            new ( @"[\a]", @"Bell, \u0007", fm => fm.InsideSets_Esc_a, @"[\a]", "\u0007", null ),
            new ( @"[\b]", @"Backspace, \u0008", fm => fm.InsideSets_Esc_b, @"[\b]", "\u0008", null ),
            new ( @"[\e]", @"Escape, \u001B", fm => fm.InsideSets_Esc_e, @"[\e]", "\u001B", null ),
            new ( @"[\f]", @"Form feed, \u000C", fm => fm.InsideSets_Esc_f, @"[\f]", "\u000C" , null),
            new ( @"[\n]", @"New line, \u000A", fm => fm.InsideSets_Esc_n, @"[\n]", "\u000A" , null),
            new ( @"[\r]", @"Carriage return, \u000D", fm => fm.InsideSets_Esc_r, @"[\r]", "\u000D" , null),
            new ( @"[\t]", @"Tab, \u0009", fm => fm.InsideSets_Esc_t, @"[\t]", "\u0009" , null),
            new ( @"[\v]", @"Vertical tab, \u000B", fm => fm.InsideSets_Esc_v, @"[\v]", "\u000B", "\f" ),
            new ( @"[\1..\7]", @"Octal, one digit", fm => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3, @"[\3]", "\u0003", null, @"[\3\0]", "\u0003", null ),
            new ( @"[\nn], [\nnn]", @"Octal, two or three digits", fm => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 || fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3,
                @"[\11][\101]", "\u0009A", null, @"[\11\000][\101\000]", "\u0009A", null ), // ('\000' for Oniguruma)
            new ( @"[\0nnn]", @"Octal, up to three digits after '\0'", fm => fm.InsideSets_Esc_Octal0_1_3,
                @"[\03][\011][\0011]", "\u0003\u0009\u0009" , null, @"[\03\000][\011\000][\0011\000]", "\u0003\u0009\u0009" , null),
            new ( @"[\o{nn…}]", @"Octal", fm => fm.InsideSets_Esc_oBrace, @"[\o{11}]", "\u0009" , null),
            new ( @"[\xXX]", @"Hexadecimal code, two digits", fm => fm.InsideSets_Esc_x2, @"[\x09]", "\u0009" , null, @"[\x09\x00]", "\u0009" , null),
            new ( @"[\x{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_xBrace, @"[\x{0009}]", "\u0009" , null),
            new ( @"[\uXXXX]", @"Hexadecimal code, four digits", fm => fm.InsideSets_Esc_u4, @"[\u0009]", "\u0009", null ),
            new ( @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", fm => fm.InsideSets_Esc_U8, @"[\U00000009]", "\u0009", "x" ),
            new ( @"[\u{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_uBrace, @"[\u{0009}]", "\u0009", "X" ),
            new ( @"[\U{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_UBrace, @"[\U{0009}]", "\u0009", "x" ),
            new ( @"[\cC]", @"Control character", fm => fm.InsideSets_Esc_c1, @"[\cM]", "\r", null ),
            new ( @"[\CC]", @"Control character", fm => fm.InsideSets_Esc_C1, @"[\CM]", "\r", null ),
            new ( @"[\C-C]", @"Control character ", fm => fm.InsideSets_Esc_CMinus, @"[\C-M]", "\r", null ),
            new ( @"[\N{…}]", @"Unicode name or 'U+code'", fm => fm.InsideSets_Esc_NBrace, @"[\N{COMMA}]", ",", null, @"[\N{comma}]", ",", null ),
            new ( @"[\any]", @"Generic escape", fm => fm.InsideSets_GenericEscape, @"[\-]", "-", @"\" ),

            new ( @"Classes" ),

            new ( @".", @"Any, including or excepting newline (\n)", fm => fm.Class_Dot, @".", "x", null ),
            new ( @"\C", @"Single byte", fm => fm.Class_Cbyte, @"\C\C", "î", null ),
            new ( @"\C", @"Single code point", fm => fm.Class_Ccp, @"\C\C", "xx", "î" ),
            new ( @"\d, \D", @"Digit", fm => fm.Class_dD, @"\d\D", "9x", null ),
            new ( @"\h, \H", @"Hexadecimal character", fm => fm.Class_hHhexa, @"\h\H", "Ax", null ),
            new ( @"\h, \H", @"Horizontal space", fm => fm.Class_hHhorspace, @"\h\H", " x", null ),
            new ( @"\l, \L", @"Lowercase character", fm => fm.Class_lL, @"\l\L", "xX", null ),
            new ( @"\N", @"Any except '\n'", fm => fm.Class_N, @"\N", "a", null, @"\N", "\n", null ),
            new ( @"\O", @"Any", fm => fm.Class_O, @"\O", "a", null ),
            new ( @"\R", @"Line break", fm => fm.Class_R, @"a\Rb", "a\r\nb", null ),
            new ( @"\s, \S", @"Space", fm => fm.Class_sS, @"\s\S", " x", null),
            new ( @"\sx, \Sx", @"Syntax group; 'x' — group", fm => fm.Class_sSx, @"\ss", " ", null ),
            new ( @"\u, \U", @"Uppercase character", fm => fm.Class_uU, @"\u\U", "Xx", null ),
            new ( @"\v, \V", @"Vertical space", fm => fm.Class_vV, @"\v\v", "\r\f", null ),
            new ( @"\w, \W", @"Word character", fm => fm.Class_wW, @"\w\w\w", "xyz", null ),
            new ( @"\X", @"Extended grapheme cluster", fm => fm.Class_X, @"\X", "a", null ),
            new ( @"\!c, \!\c", @"Not; 'c' — character, '\c' — escaped character", fm => fm.Class_Not, @"\!x", "a", null ),
            new ( @"\pX, \PX", @"Unicode property, X — short property name", fm => fm.Class_pP, @"\pL\PL", "x9", null ),
            new ( @"\p{…}, \P{…}", @"Unicode property", fm => fm.Class_pPBrace, @"\p{L}\P{L}", "x9", null ),
            new ( @"[:class:]", @"Character class", fm => fm.Class_Name, @"[:alpha:]", "X", null ),

            new (@"Classes inside […]"),

            new ( @"[\d], [\D]", @"Digit", fm => fm.InsideSets_Class_dD, @"[\d]", "9", null ),
            new ( @"[\h], [\H]", @"Hexadecimal character", fm => fm.InsideSets_Class_hHhexa, @"[\h][\H]", "Ax", null ),
            new ( @"[\h], [\H]", @"Horizontal space", fm => fm.InsideSets_Class_hHhorspace, @"[\h][\H]", " x", null ),
            new ( @"[\l], [\L]", @"Lowercase character", fm => fm.InsideSets_Class_lL, @"[\l][\L]", "xX", null ),
            new ( @"[\R]", @"Line break", fm => fm.InsideSets_Class_R, @"a[\R]b", "a\r\nb", null ),
            new ( @"[\s], [\S]", @"Space", fm => fm.InsideSets_Class_sS, @"a[\s][\S]x", "a 9x", null ),
            new ( @"[\sx], [\Sx]", @"Syntax group; 'x' — group", fm => fm.InsideSets_Class_sSx, @"[\ss]", " ", "s" ),
            new ( @"[\u], [\U]", @"Uppercase character", fm => fm.InsideSets_Class_uU, @"[\u][\U]", "Xx", null ),
            new ( @"[\v], [\V]", @"Vertical space", fm => fm.InsideSets_Class_vV, @"[\v][\v]", "\r\f", null ),
            new ( @"[\w], [\W]", @"Word character", fm => fm.InsideSets_Class_wW, @"[\w][\w][\w]", "xyz", null ),
            new ( @"[\X]", @"Extended grapheme cluster", fm => fm.InsideSets_Class_X, @"[\X]", "a", null ),
            new ( @"[\pX], [\PX]", @"Unicode property, X — short property name", fm => fm.InsideSets_Class_pP, @"[\pL][\PL]", "x9", null ),
            new ( @"[\p{…}], [\P{…}]", @"Unicode property", fm => fm.InsideSets_Class_pPBrace, @"[\p{L}][\P{L}]", "x9", null ),
            new ( @"[[:class:]]", @"Character class", fm => fm.InsideSets_Class_Name, @"[[:alpha:]]", "X", null ),
            new ( @"[[=elem=]]", @"Equivalence", fm => fm.InsideSets_Equivalence, @"[[=a=]][[=a=]]", "aA", null ), // 'Á' not matched by STL regex.
            new ( @"[[.elem.]]", @"Collating symbol", fm => fm.InsideSets_Collating, @"a[[.ch.]]x", "achx", null ), // STL seems to hav a defect.

            new ( @"Operators inside […]" ),

            new ( @"[[…] op […]]", @"Using operators for nested groups", fm => fm.InsideSets_Operators, @"[[ab]&[bc]]", "b", "&", @"[[ab]&&[bc]]", "b", "&"),
            new ( @"(?[[…] op […]])", @"Using operators for nested groups", fm => fm.InsideSets_OperatorsExtended, @"(?[[ab]&[bc]])", "b", "&"),
            new ( @"[…] & […]", @"Intersection", fm => fm.InsideSets_Operator_Ampersand, @"[[ab]&[bc]]", "b", "&", @"(?[[ab]&[bc]])", "b", "&" ),
            new ( @"[…] + […]", @"Union", fm => fm.InsideSets_Operator_Plus, @"[[a]+[b]]", "a", "+", @"(?[[a]+[b]])", "a", "+" ),
            new ( @"[…] | […]", @"Union", fm => fm.InsideSets_Operator_VerticalLine, @"[[a]|[b]]", "b", "|", @"(?[[a]|[b]])", "b", "|" ),
            new ( @"[…] - […]", @"Subtraction", fm => fm.InsideSets_Operator_Minus, @"[[ab]-[b]]", "a", "b", @"(?[[ab]-[b]])", "a", "b" ),
            new ( @"[…] ^ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_Circumflex, @"[[ab]^[bc]]", "c", "^", @"(?[[ab]^[bc]])", "c", "^" ),
            new ( @"![…]", @"Complement", fm => fm.InsideSets_Operator_Exclamation, @"[![abc]]", "d", null, "(?[![abc]])", "d", null ),
            new ( @"[…] && […]", @"Intersection", fm => fm.InsideSets_Operator_DoubleAmpersand, @"[[ab]&&[bc]]", "b", null ),
            new ( @"[…] || […]", @"Union", fm => fm.InsideSets_Operator_DoubleVerticalLine, @"[[a]||[b]]", "b", "]|[" ),
            new ( @"[…] -- […]", @"Difference", fm => fm.InsideSets_Operator_DoubleMinus, @"[[ab]--[b]]", "a", "][-b" ),
            new ( @"[…] ~~ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_DoubleTilde, @"[[ab]~~[bca]]", "c", "ab][~" ),

            new ( @"Anchors" ),

            new ( @"^", @"Beginning of string or line", fm => fm.Anchor_Circumflex, @"^x", "x", null ),
            new ( @"$", @"End, or before '\n' at end of string or line", fm => fm.Anchor_Dollar, @"x$", "x", null ),
            new ( @"\A", @"Start of string", fm => fm.Anchor_A, @"\Ax", "x", null ),
            new ( @"\Z", @"End of string, or before '\n' at end of string", fm => fm.Anchor_Z, @"x\Z", "x", null ),
            new ( @"\z", @"End of string", fm => fm.Anchor_z, @"x\z", "x", null ),
            new ( @"\G", @"start of string or end of previous match", fm => fm.Anchor_G, @"\Gx", "x", null ),
            new ( @"\b, \B", @"Boundary between \w and \W", fm => fm.Anchor_bB, @"\bx", "y x", null ),
            new ( @"\b{g}", @"Unicode extended grapheme cluster boundary", fm => fm.Anchor_bg, @"\b{g}x", "y x", null ),
            new ( @"\b{…}, \B{…}", @"Typed boundary", fm => fm.Anchor_bBBrace, @"\b{wb}x", "y x", null ),
            new ( @"\K", @"Keep the stuff left of the \K", fm => fm.Anchor_K, @"a\Kb", "ab", null ),
            new ( @"\m, \M", @"Start of word, end of word", fm => fm.Anchor_mM, @"\mword\M", "some word here", null ),
            new ( @"\<, \>", @"Start of word, end of word", fm => fm.Anchor_LtGt, @"\<word\>", "some word here", null ),
            new ( @"\`, \'", @"Start of string, end of string", fm => fm.Anchor_GraveApos, @"\`x\'", "x", null ),
            new ( @"\y, \Y", @"Boundary between graphemes", fm => fm.Anchor_yY, @"a\yb", "ab", null ),

            new ( @"Named groups and backreferences" ),

            new ( @"(?'name'…)", @"Named group", fm => fm.NamedGroup_Apos, @"(?'n'x)", "x", null, @"\(?'n'x\)", "x", null ),
            new ( @"(?<name>…)", @"Named group", fm => fm.NamedGroup_LtGt, @"(?<n>x)", "x", null, @"\(?<n>x\)", "x", null ),
            new ( @"(?P<name>…)", @"Named group", fm => fm.NamedGroup_PLtGt, @"(?P<n>x)", "x", null, @"\(?P<n>x\)", "x", null ),
            new ( @"(?@…)", @"Capturing group", fm => fm.NamedGroup_AtApos || fm.NamedGroup_AtLtGt || fm.CapturingGroup, @"(?@<n>x)", "x", null ),
            new ( @"Duplicate names", @"Allow duplicate group names", fm => fm.AllowDuplicateGroupName, @"(?<a>x)|(?<a>y)", "y", null, @"\(?<a>x\)|\(?<a>y\)", "y", null, @"(?P<a>x)|(?P<a>y)", "y", null ),
            new ( @"\1, \2, …, \9", @"Backreferences", fm => fm.Backref_Num == FeatureMatrix.BackrefEnum.OneDigit || fm.Backref_Num == FeatureMatrix.BackrefEnum.Any , @"(x)\1", "xx", "x", @"\(x\)\1", "xx", "x" ),
            new ( @"\nnn", @"Backreference, two or more digits", fm => fm.Backref_Num == FeatureMatrix.BackrefEnum.Any, @"(x)(x)(x)(x)(x)(x)(x)(x)(x)(y)\10", "xxxxxxxxxyy", "x", @"\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(y\)\10", "xxxxxxxxxyy", null ),
            new ( @"\k'name'", @"Backreference by name", fm => fm.Backref_kApos, @"(?'n'x)\k'n'", "xx", null ),
            new ( @"\k<name>", @"Backreference by name", fm => fm.Backref_kLtGt, @"(?<n>x)\k<n>", "xx", null ),
            new ( @"\k{name}", @"Backreference by name", fm => fm.Backref_kBrace, @"(?<n>x)\k{n}", "xx", null ),
            new ( @"\kn", @"Backreference \k1, \k2, …", fm => fm.Backref_kNum, @"(?<n>x)\k1", "xx", null ),
            new ( @"\k-n", @"Relative backreference \k-1, \k-2, …", fm => fm.Backref_kNegNum, @"(?<n>x)\k-1", "xx", null ),
            new ( @"\g'…'", @"Subroutine by name or number", fm => fm.Backref_gApos, @"(?'n'x)\g'n'", "xx", null ),
            new ( @"\g<…>", @"Subroutine by name or number", fm => fm.Backref_gLtGt, @"(?<n>x)\g<n>", "xx", null ),
            new ( @"\gn", @"Subroutine \g1, \g2, …", fm => fm.Backref_gNum, @"(?<n>x)\g1", "xx", null ),
            new ( @"\g-n", @"Relative subroutine \g-1, \g-2, …", fm => fm.Backref_gNegNum, @"(?<n>x)\g-1", "xx", null ),
            new ( @"\g{…}", @"Subroutine \g{name}, \g{number}, \g{-number}, g{+number}", fm => fm.Backref_gBrace, @"(?<n>x)\g{n}", "xx", null ),
            new ( @"(?P=name)", @"Subroutine by name", fm => fm.Backref_PEqName, @"(?P<n>x)(?P=n)", "xx", null ),
            new ( @"\k< … >, \g< … >", @"Allow spaces like '\k < name >'", fm => fm.AllowSpacesInBackref ), // TODO

            new ( @"Grouping" ),

            new ( @"(?:…)", @"Noncapturing group", fm => fm.NoncapturingGroup, @"(?:x)", "x", null, @"\(?:x\)", "x", null ),
            new ( @"(?=…)", @"Positive lookahead ", fm => fm.PositiveLookahead, @"a(?=x)x", "ax", null, @"\(?=x\)x", "ax", null ),
            new ( @"(?!…)", @"Negative lookahead ", fm => fm.NegativeLookahead, @"a(?!x)y", "ay", null, @"\(?!x\)y", "ay", null ),
            new ( @"(?<=…)", @"Positive lookbehind", fm => fm.PositiveLookbehind, @"(?<=x)a", "xa", null, @"\(?<=x\)a", "xa", null ),
            new ( @"(?<!…)", @"Negative lookbehind", fm => fm.NegativeLookbehind, @"(?<!x)a", "ya", null, @"\(?<!x\)a", "ya", null ),
            new ( @"(?>…)", @"Atomic group", fm => fm.AtomicGroup, @"(?>x)", "x", null, @"\(?>x\)", "x", null ),
            new ( @"(?|…)", @"Branch reset", fm => fm.BranchReset, @"(?|(a)|(b)\1)", "bb", null, @"\(?|\(a\)\|\(b\)\1\)", "bb", "x" ),
            new ( @"(?*…)", @"Non-atomic positive lookahead", fm => fm.NonatomicPositiveLookahead, @"a(?*x)x", "ax", null ),
            new ( @"(?<*…)", @"Non-atomic positive lookbehind ", fm => fm.NonatomicPositiveLookbehind, @"(?<*x)a", "xa", "x" ),
            new ( @"(?~…)", @"Absent operator", fm => fm.AbsentOperator, @"/\*(?~\*\/)\*\/", "/* abc */", null ),
            new ( @"( ? … )", @"Allow spaces like '( ? < name >…)'", fm => fm.AllowSpacesInGroups ), // TODO

            new ( @"Recursive patterns" ),

            new ( @"(?n)", @"Recursive subpattern by number", fm => fm.Recursive_Num, @"(x.)(?1)", "xyxz", "xyZ" ),
            new ( @"(?-n), (?+n)", @"Relative recursive subpattern by number", fm => fm.Recursive_PlusMinusNum, @"(x(.))(?-1)", "xyz", null, @"\(x\(.\)\)\(?-1\)", "xyz", null ),
            new ( @"(?R)", @"Recursive whole pattern", fm => fm.Recursive_R, @"a(?R)*b", "aabb", "b" ),
            new ( @"(?&name)", @"Recursive subpattern by name", fm => fm.Recursive_Name, @"(?<n>a)(?&n)", "aa", null ),
            new ( @"(?P>name)", @"Recursive subpattern by name", fm => fm.Recursive_PGtName, @"(?P<n>a)(?P>n)", "aa", null ),

            new (@"Quantifiers"),

            new ( @"*", @"Zero or more times", fm => fm.Quantifier_Asterisk, @"xy*", "x", null ),
            new ( @"+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal, @"xy+", "xyy", null ),
            new ( @"\+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed, @"xy\+", "xyy", null ),
            new ( @"?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal, @"xy?", "x", null ),
            new ( @"\?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed, @"xy\?", "x", null ),
            new ( @"{n,m}", @"Between n and m times: {n}, {n,}, {n,m}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal, @"x{2,3}", "xx", null ),
            new ( @"\{n,m\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed, @"x\{2,3\}", "xx", null ),
            new ( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.Both ), // TODO
            new ( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.XModeOnly ), // TODO
            new ( @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", fm => fm.Quantifier_LowAbbrev, @"x{,3}", "xxx", null, @"x\{,3\}", "xxx", null ),

            new ( @"Conditionals" ),

            new ( @"(?(number)…|…)", @"Conditionals by number, +number, -number", fm => fm.Conditional_BackrefByNumber, @"(x)(?(1)y|z)", "xy", "bx" ),
            new ( @"(?(name)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName, @"(?<n>x)(?(n)y|z)", "xy", null, @"(?P<n>x)(?(n)y|z)", "xy", null ),
            new ( @"(?(pattern)…|…)", @"Conditional subpattern", fm => fm.Conditional_Pattern, @"x(?(?=.z)y|z)", "xyz", null, @"x(?((?=.z))y|z)", "xyz", null ),
            new ( @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", fm => fm.Conditional_PatternOrBackrefByName, @"x(?(y).|z)", "xy", null ),
            new ( @"(?('name')…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_Apos, @"(?'n'x)(?('n')y|z)", "xy", null ),
            new ( @"(?(<name>)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_LtGt, @"(?<n>x)(?(<n>)y|z)", "xy", null, @"(?P<n>x)(?(<n>)y|z)", "xy", null ),
            new ( @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", fm => fm.Conditional_R, @"(?(R)a+|(?R)b)", "aaaab", null ),
            new ( @"(?(R&name)…|…)", @"Recursive conditional by name", fm => fm.Conditional_RName, @"(?<A>(?'B'abc(?(R)(?(R&A)1)(?(R&B)2)X|(?1)(?2)(?R))))", "abcabc1Xabc2XabcXabcabc", null ),
            new ( @"(?(DEFINE)…|…)", @"Defining subpatterns", fm => fm.Conditional_DEFINE, @"(?(DEFINE)(?<n>x.z))(?&n)", "xyz", null ),
            new ( @"(?(VERSION…)…|…)", @"Checking for version using 'VERSION=decimal' or 'VERSION>=decimal'", fm => fm.Conditional_VERSION, @"(?(VERSION>=1)xyz|abc)", "xyz", null ),

            new ( @"Miscellaneous" ),

            new ( @"(*verb)", @"Control verbs: (*verb), (*verb:…), (*:name)", fm => fm.ControlVerbs, @"x(*ACCEPT)|y(*FAIL)", "x", null, @"(*UCP)a", "a", null, @"x(*SKIP)y", "xy", null ),
            new ( @"(*…:…)", @"Script runs, such as (*atomic:…)", fm => fm.ScriptRuns, @"(*atomic:x)", "x", null ),
            new ( @"(?Cn), (*func)", @"Callouts (custom functions)", fm => fm.Callouts ),

            new ( @"(?)", @"Empty construct", fm => fm.EmptyConstruct, @"x(?)y", "xy", "x" ),
            new ( @"(? )", @"Empty construct", fm => fm.EmptyConstructX, @"(?x)a(? )b", "ab", null ),
            new ( @"[]", @"Empty set", fm => fm.EmptySet, @"x[]?", "x", null ),

            // (all seems to split the surrogate pairs)
            //new ( @"Split surrogates", @"“.” splits Surrogate Pair characters into components", fm => fm.SplitSurrogatePairs, @"a..b", "a❤️b", null ), 

            new ( @"Fuzzy matching", @"Approximate matching using special patterns or parameters", fm => fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal || fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed,
                    @"(test){i}", "teXst", null,
                    @"(test){+1}", "teXst", null,
                    @"\(test\)\{+1\}", "teXst", null
                    ) { DirectCheck = fm=>fm.FuzzyMatchingParams },
        ];
}
