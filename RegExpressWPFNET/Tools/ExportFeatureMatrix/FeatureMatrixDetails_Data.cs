using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

partial class FeatureMatrixDetails
{
    internal static readonly FeatureMatrixGroup[] AllFeatureMatrixDetails =
        [

            new ( @"General",
            [
                new FeatureMatrixDetails( @"(…)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"(x)", "x", null ),
                new FeatureMatrixDetails( @"\(…\)", @"Grouping constructs", fm => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"\(x\)", "x", null ),

                new FeatureMatrixDetails( @"[…]", @"Character group", fm => fm.Brackets)
                    .Test( @"[x]", "x", null ),
                new FeatureMatrixDetails( @"(?[…])", @"Character group", fm => fm.ExtendedBrackets)
                    .Test( @"(?[[x]])", "x", null ),

                new FeatureMatrixDetails( @"|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"x|y", "y", null ),
                new FeatureMatrixDetails( @"\|", @"Alternation", fm => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"x\|y", "y", null ),
                new FeatureMatrixDetails( @"new line (\n)", @"Alternatives on separate lines", fm => fm.AlternationOnSeparateLines)
                    .Test("x\ny", "y", null ),

                new FeatureMatrixDetails( @"#comment", @"Comment", fm => fm.XModeComments)
                    .IgnorePatternWhitespace()
                    .Test( "a#comment", "a", null )
                    .Test( "(?x)a#comment", "a", null )
                    .Test( "a#comment\nb", "ab", "b" ) // ('\n' is required by Hyperscan)
                    .Test( "(?x)a#comment\nb", "ab", "b" ), // ('\n' is required by Hyperscan)
                new FeatureMatrixDetails( @"(?#comment)", @"Inline comment", fm => fm.InlineComments)
                    .Test( @"a(?#comment)b", "ab", null )
                    .Test( @"a\(?#comment\)b", "ab", "a" ),
                new FeatureMatrixDetails( @"[#comment]", @"Comment inside […]", fm => fm.InsideSets_XModeComments)
                    .IgnorePatternWhitespace()
                    .Test( "a[b#comment\nz]y", "azy", "acy")
                    .Test( "a(?x)[b#comment\nz]y", "azy", "acy")
                    .Test( "a(?xx)[b#comment\nz]y", "azy", "acy"),

                new FeatureMatrixDetails( @"(?flags)", @"Inline options", fm => fm.Flags).IgnoreCase( false )
                    .Test( @"(?i)x", "X", null ),
                new FeatureMatrixDetails( @"(?flags:…)", @"Inline scoped options", fm => fm.ScopedFlags)
                    .IgnoreCase( false )
                    .Test( @"(?i:x)", "X", null ),
                new FeatureMatrixDetails( @"(?^flags)", @"Inline fresh options", fm => fm.CircumflexFlags)
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^)x", "x", "X" ),
                new FeatureMatrixDetails( @"(?^flags:…)", @"Inline scoped fresh options", fm => fm.ScopedCircumflexFlags)
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^:x)", "x", "X" ),
                new FeatureMatrixDetails( @"(?x)", @"Allow 'x' flag", fm => fm.XFlag)
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)a b", "ab", null ),
                new FeatureMatrixDetails( @"(?xx)", @"Allow 'xx' flag", fm => fm.XXFlag)
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)[a b](?xx)[a b]", " a", "a "),

                new FeatureMatrixDetails( @"\Q…\E", @"Literal", fm => fm.Literal_QE)
                    .Test( @"a\Qx\E", "ax", "aQxE"),
                new FeatureMatrixDetails( @"[\Q…\E]", @"Literal inside […]", fm => fm.InsideSets_Literal_QE)
                    .Test( @"[\Qx\E]", "x", "Q"),
                new FeatureMatrixDetails( @"[\q{…}]", @"Literal inside […]", fm => fm.InsideSets_Literal_qBrace)
                    .Test( @"[\q{x}]", "x", "q"),
            ] ),

            new ( @"Escapes",
            [
                new FeatureMatrixDetails( @"\a", @"Bell, \u0007", fm => fm.Esc_a)
                    .Test( @"\a", "\u0007", null ),
                new FeatureMatrixDetails( @"\b", @"Backspace, \u0008", fm => fm.Esc_b)
                    .Test( @"x\by", "x\u0008y", null ),
                new FeatureMatrixDetails( @"\e", @"Escape, \u001B", fm => fm.Esc_e)
                    .Test( @"\e", "\u001B", null ),
                new FeatureMatrixDetails( @"\f", @"Form feed, \u000C", fm => fm.Esc_f)
                    .Test( @"\f", "\u000C", null ),
                new FeatureMatrixDetails( @"\n", @"New line, \u000A", fm => fm.Esc_n)
                    .Test( @"\n", "\u000A", null ),
                new FeatureMatrixDetails( @"\r", @"Carriage return, \u000D", fm => fm.Esc_r)
                    .Test( @"\r", "\u000D", null ),
                new FeatureMatrixDetails( @"\t", @"Tab, \u0009", fm => fm.Esc_t)
                    .Test( @"\t", "\u0009", null ),
                new FeatureMatrixDetails( @"\v", @"Vertical tab, \u000B", fm => fm.Esc_v)
                    .Test( @"\v", "\u000B", "\f"),
                new FeatureMatrixDetails( @"\1..\7", @"Octal, one digit", fm => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3)
                    .Test( @"\3", "\u0003", null ),
                new FeatureMatrixDetails( @"\nn, \nnn", @"Octal, two or three digits", fm => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 || fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3)
                    .Test( @"\11\101", "\u0009A", null )
                    .Test( @"\11\000\101\000", "\u0009A", null ), // ('\000' for Oniguruma)
                new FeatureMatrixDetails( @"\0nnn", @"Octal, up to three digits after '\0'", fm => fm.Esc_Octal0_1_3)
                    .Test( @"\03\011\0011", "\u0003\u0009\u0009", null )
                    .Test( @"\03\000\011\000\0011\000", "\u0003\u0009\u0009", null ),
                new FeatureMatrixDetails( @"\o{nn…}", @"Octal", fm => fm.Esc_oBrace)
                    .Test( @"\o{11}", "\u0009", null ),
                new FeatureMatrixDetails( @"\xXX", @"Hexadecimal code, two digits", fm => fm.Esc_x2)
                    .Test( @"\x09", "\u0009", null )
                    .Test( @"\x09\x00", "\u0009", null ),
                new FeatureMatrixDetails( @"\x{XX…}", @"Hexadecimal code", fm => fm.Esc_xBrace)
                    .Test( @"\x{0009}", "\u0009", null ),
                new FeatureMatrixDetails( @"\uXXXX", @"Hexadecimal code, four digits", fm => fm.Esc_u4)
                    .Test( @"\u0009", "\u0009", null ),
                new FeatureMatrixDetails( @"\UXXXXXXXX", @"Hexadecimal code, eight digits", fm => fm.Esc_U8)
                    .Test( @"\U00000009", "\u0009", null ),
                new FeatureMatrixDetails( @"\u{XX…}", @"Hexadecimal code", fm => fm.Esc_uBrace)
                    .Test( @"\u{0009}", "\u0009", null ),
                new FeatureMatrixDetails( @"\U{XX…}", @"Hexadecimal code", fm => fm.Esc_UBrace)
                    .Test( @"\U{0009}", "\u0009", null ),
                new FeatureMatrixDetails( @"\cC", @"Control character", fm => fm.Esc_c1)
                    .Test( @"\cM", "\r", null ),
                new FeatureMatrixDetails( @"\CC", @"Control character", fm => fm.Esc_C1)
                    .Test( @"\CM", "\r", null ),
                new FeatureMatrixDetails( @"\C-C", @"Control character", fm => fm.Esc_CMinus)
                    .Test( @"\C-M", "\r", null ),
                new FeatureMatrixDetails( @"\N{…}", @"Unicode name or 'U+code'", fm => fm.Esc_NBrace)
                    .Test( @"\N{COMMA}", ",", null )
                    .Test( @"\N{comma}", ",", null ),
                new FeatureMatrixDetails( @"\any", @"Generic escape", fm => fm.GenericEscape)
                    .Test( @"\\", @"\", null ),
            ] ),

            new ( @"Escapes inside […] sets",
            [
                new FeatureMatrixDetails( @"[\a]", @"Bell, \u0007", fm => fm.InsideSets_Esc_a)
                    .Test( @"[\a]", "\u0007", null ),
                new FeatureMatrixDetails( @"[\b]", @"Backspace, \u0008", fm => fm.InsideSets_Esc_b)
                    .Test( @"[\b]", "\u0008", null ),
                new FeatureMatrixDetails( @"[\e]", @"Escape, \u001B", fm => fm.InsideSets_Esc_e)
                    .Test( @"[\e]", "\u001B", null ),
                new FeatureMatrixDetails( @"[\f]", @"Form feed, \u000C", fm => fm.InsideSets_Esc_f)
                    .Test( @"[\f]", "\u000C", null ),
                new FeatureMatrixDetails( @"[\n]", @"New line, \u000A", fm => fm.InsideSets_Esc_n)
                    .Test( @"[\n]", "\u000A", null ),
                new FeatureMatrixDetails( @"[\r]", @"Carriage return, \u000D", fm => fm.InsideSets_Esc_r)
                    .Test( @"[\r]", "\u000D", null ),
                new FeatureMatrixDetails( @"[\t]", @"Tab, \u0009", fm => fm.InsideSets_Esc_t)
                    .Test( @"[\t]", "\u0009", null ),
                new FeatureMatrixDetails( @"[\v]", @"Vertical tab, \u000B", fm => fm.InsideSets_Esc_v)
                    .Test( @"[\v]", "\u000B", "\f"),
                new FeatureMatrixDetails( @"[\1..\7]", @"Octal, one digit", fm => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3)
                    .Test( @"[\3]", "\u0003", null )
                    .Test( @"[\3\0]", "\u0003", null ),
                new FeatureMatrixDetails( @"[\nn], [\nnn]", @"Octal, two or three digits", fm => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 || fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3)
                    .Test( @"[\11][\101]", "\u0009A", null )
                    .Test( @"[\11\000][\101\000]", "\u0009A", null ), // ('\000' for Oniguruma)
                new FeatureMatrixDetails( @"[\0nnn]", @"Octal, up to three digits after '\0'", fm => fm.InsideSets_Esc_Octal0_1_3)
                    .Test( @"[\03][\011][\0011]", "\u0003\u0009\u0009", null )
                    .Test( @"[\03\000][\011\000][\0011\000]", "\u0003\u0009\u0009" , null ),
                new FeatureMatrixDetails( @"[\o{nn…}]", @"Octal", fm => fm.InsideSets_Esc_oBrace)
                    .Test( @"[\o{11}]", "\u0009", null ),
                new FeatureMatrixDetails( @"[\xXX]", @"Hexadecimal code, two digits", fm => fm.InsideSets_Esc_x2)
                    .Test( @"[\x09]", "\u0009", null )
                    .Test( @"[\x09\x00]", "\u0009", null ),
                new FeatureMatrixDetails( @"[\x{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_xBrace)
                    .Test( @"[\x{0009}]", "\u0009", null ),
                new FeatureMatrixDetails( @"[\uXXXX]", @"Hexadecimal code, four digits", fm => fm.InsideSets_Esc_u4)
                    .Test( @"[\u0009]", "\u0009", null ),
                new FeatureMatrixDetails( @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", fm => fm.InsideSets_Esc_U8)
                    .Test( @"[\U00000009]", "\u0009", "x"),
                new FeatureMatrixDetails( @"[\u{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_uBrace)
                    .Test( @"[\u{0009}]", "\u0009", "X"),
                new FeatureMatrixDetails( @"[\U{XX…}]", @"Hexadecimal code", fm => fm.InsideSets_Esc_UBrace)
                    .Test( @"[\U{0009}]", "\u0009", "x"),
                new FeatureMatrixDetails( @"[\cC]", @"Control character", fm => fm.InsideSets_Esc_c1)
                    .Test( @"[\cM]", "\r", null ),
                new FeatureMatrixDetails( @"[\CC]", @"Control character", fm => fm.InsideSets_Esc_C1)
                    .Test( @"[\CM]", "\r", null ),
                new FeatureMatrixDetails( @"[\C-C]", @"Control character ", fm => fm.InsideSets_Esc_CMinus)
                    .Test( @"[\C-M]", "\r", null ),
                new FeatureMatrixDetails( @"[\N{…}]", @"Unicode name or 'U+code'", fm => fm.InsideSets_Esc_NBrace)
                    .Test( @"[\N{COMMA}]", ",", "M" ) // (see also '\N' -- any except '\n')
                    .Test( @"[\N{comma}]", ",", "m" ),
                new FeatureMatrixDetails( @"[\any]", @"Generic escape", fm => fm.InsideSets_GenericEscape)
                    .Test( @"[\-]", "-", @"\"),
            ] ),

            new ( @"Classes",
            [
                new FeatureMatrixDetails( @".", @"Any, including or excepting newline (\n)", fm => fm.Class_Dot)
                    .Test( @".", "x", null ),
                new FeatureMatrixDetails( @"\C", @"Single byte", fm => fm.Class_Cbyte)
                    .Test( @"\C\C", "î", null ),
                new FeatureMatrixDetails( @"\C", @"Single code point", fm => fm.Class_Ccp)
                    .Test( @"\C\C", "îî", "î"),
                new FeatureMatrixDetails( @"\d, \D", @"Digit", fm => fm.Class_dD)
                    .Test( @"\d\D", "9x", null ),
                new FeatureMatrixDetails( @"\h, \H", @"Hexadecimal character", fm => fm.Class_hHhexa)
                    .Test( @"\h\H", "Ax", null ),
                new FeatureMatrixDetails( @"\h, \H", @"Horizontal space", fm => fm.Class_hHhorspace)
                    .Test( @"\h\H", " x", null ),
                new FeatureMatrixDetails( @"\l, \L", @"Lowercase character", fm => fm.Class_lL)
                    .Test( @"\l\L", "xX", null ),
                new FeatureMatrixDetails( @"\N", @"Any except '\n'", fm => fm.Class_N)
                    .Test( @"\N", "a", "\n" ),
                new FeatureMatrixDetails( @"\O", @"Any", fm => fm.Class_O)
                    .Test( @"\O", "a", null ),
                new FeatureMatrixDetails( @"\R", @"Line break", fm => fm.Class_R)
                    .Test( @"a\Rb", "a\r\nb", null ),
                new FeatureMatrixDetails( @"\s, \S", @"Space", fm => fm.Class_sS)
                    .Test( @"\s\S", " x", null ),
                new FeatureMatrixDetails( @"\sx, \Sx", @"Syntax group; 'x' — group", fm => fm.Class_sSx)
                    .Test( @"\ss", " ", null ),
                new FeatureMatrixDetails( @"\u, \U", @"Uppercase character", fm => fm.Class_uU)
                    .Test( @"\u\U", "Xx", null ),
                new FeatureMatrixDetails( @"\v, \V", @"Vertical space", fm => fm.Class_vV)
                    .Test( @"\v\v", "\r\f", null ),
                new FeatureMatrixDetails( @"\w, \W", @"Word character", fm => fm.Class_wW)
                    .Test( @"\w\w\w", "xyz", null ),
                new FeatureMatrixDetails( @"\X", @"Extended grapheme cluster", fm => fm.Class_X)
                    .Test( @"\X", "a", null ),
                new FeatureMatrixDetails( @"\!c, \!\c", @"Not; 'c' — character, '\c' — escaped character", fm => fm.Class_Not)
                    .Test( @"\!x", "a", null ),
                new FeatureMatrixDetails( @"\pX, \PX", @"Unicode property, X — short property name", fm => fm.Class_pP)
                    .Test( @"\pL\PL", "x9", null ),
                new FeatureMatrixDetails( @"\p{…}, \P{…}", @"Unicode property", fm => fm.Class_pPBrace)
                    .Test( @"\p{L}\P{L}", "x9", null ),
                new FeatureMatrixDetails( @"[:class:]", @"Character class outside sets", fm => fm.Class_Name)
                    .Test( @"[:alpha:]", "X", null ),
            ] ),

            new (@"Classes inside […] sets",
            [
                new FeatureMatrixDetails( @"[\d], [\D]", @"Digit", fm => fm.InsideSets_Class_dD)
                    .Test( @"[\d]", "9", null ),
                new FeatureMatrixDetails( @"[\h], [\H]", @"Hexadecimal character", fm => fm.InsideSets_Class_hHhexa)
                    .Test( @"[\h][\H]", "Ax", null ),
                new FeatureMatrixDetails( @"[\h], [\H]", @"Horizontal space", fm => fm.InsideSets_Class_hHhorspace)
                    .Test( @"[\h][\H]", " x", null ),
                new FeatureMatrixDetails( @"[\l], [\L]", @"Lowercase character", fm => fm.InsideSets_Class_lL)
                    .Test( @"[\l][\L]", "xX", null ),
                new FeatureMatrixDetails( @"[\R]", @"Line break", fm => fm.InsideSets_Class_R)
                    .Test( @"a[\R]b", "a\r\nb", null ),
                new FeatureMatrixDetails( @"[\s], [\S]", @"Space", fm => fm.InsideSets_Class_sS)
                    .Test( @"a[\s][\S]x", "a 9x", null ),
                new FeatureMatrixDetails( @"[\sx], [\Sx]", @"Syntax group; 'x' — group", fm => fm.InsideSets_Class_sSx)
                    .Test( @"[\ss]", " ", "s"),
                new FeatureMatrixDetails( @"[\u], [\U]", @"Uppercase character", fm => fm.InsideSets_Class_uU)
                    .Test( @"[\u][\U]", "Xx", null ),
                new FeatureMatrixDetails( @"[\v], [\V]", @"Vertical space", fm => fm.InsideSets_Class_vV)
                    .Test( @"[\v][\v]", "\r\f", null ),
                new FeatureMatrixDetails( @"[\w], [\W]", @"Word character", fm => fm.InsideSets_Class_wW)
                    .Test( @"[\w][\w][\w]", "xyz", null ),
                new FeatureMatrixDetails( @"[\X]", @"Extended grapheme cluster", fm => fm.InsideSets_Class_X)
                    .Test( @"[\X]", "a", null ),
                new FeatureMatrixDetails( @"[\pX], [\PX]", @"Unicode property, X — short property name", fm => fm.InsideSets_Class_pP)
                    .Test( @"[\pL][\PL]", "x9", null ),
                new FeatureMatrixDetails( @"[\p{…}], [\P{…}]", @"Unicode property", fm => fm.InsideSets_Class_pPBrace)
                    .Test( @"[\p{L}][\P{L}]", "x9", null ),
                new FeatureMatrixDetails( @"[[:class:]]", @"Character class", fm => fm.InsideSets_Class_Name)
                    .Test( @"[[:alpha:]]", "X", null ),
                new FeatureMatrixDetails( @"[[=elem=]]", @"Equivalence", fm => fm.InsideSets_Equivalence)
                    .Test( @"[[=a=]][[=a=]]", "aA", null ), // 'Á' not matched by STL regex.
                new FeatureMatrixDetails( @"[[.elem.]]", @"Collating symbol", fm => fm.InsideSets_Collating)
                    .Test( @"a[[.ch.]]x", "achx", null )
                    .Test( @"a[[.comma.]]b", "a,b", null ), // STL seems to have a defect.
            ] ),

            new ( @"Operators inside […] sets",
            [
                new FeatureMatrixDetails( @"[[…] op […]]", @"Using operators for nested groups", fm => fm.InsideSets_Operators)
                    .Test( @"[[ab]&[bc]]", "b", "&")
                    .Test( @"[[ab]&&[bc]]", "b", "&"),
                new FeatureMatrixDetails( @"(?[[…] op […]])", @"Using operators for nested groups", fm => fm.InsideSets_OperatorsExtended)
                    .Test( @"(?[[ab]&[bc]])", "b", "&"),
                new FeatureMatrixDetails( @"[…] & […]", @"Intersection", fm => fm.InsideSets_Operator_Ampersand)
                    .Test( @"[[ab]&[bc]]", "b", "&")
                    .Test( @"(?[[ab]&[bc]])", "b", "&"),
                new FeatureMatrixDetails( @"[…] + […]", @"Union", fm => fm.InsideSets_Operator_Plus)
                    .Test( @"[[a]+[b]]", "a", "+")
                    .Test( @"(?[[a]+[b]])", "a", "+" ),
                new FeatureMatrixDetails( @"[…] | […]", @"Union", fm => fm.InsideSets_Operator_VerticalLine)
                    .Test( @"[[a]|[b]]", "b", "|")
                    .Test( @"(?[[a]|[b]])", "b", "|"),
                new FeatureMatrixDetails( @"[…] - […]", @"Subtraction", fm => fm.InsideSets_Operator_Minus)
                    .Test( @"[[ab]-[b]]", "a", "b")
                    .Test( @"(?[[ab]-[b]])", "a", "b"),
                new FeatureMatrixDetails( @"[…] ^ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_Circumflex)
                    .Test( @"[[ab]^[bc]]", "c", "^")
                    .Test( @"(?[[ab]^[bc]])", "c", "^"),
                new FeatureMatrixDetails( @"![…]", @"Complement", fm => fm.InsideSets_Operator_Exclamation)
                    .Test( @"[![abc]]", "d", null )
                    .Test("(?[![abc]])", "d", null ),
                new FeatureMatrixDetails( @"[…] && […]", @"Intersection", fm => fm.InsideSets_Operator_DoubleAmpersand)
                    .Test( @"[[ab]&&[bc]]", "b", null ),
                new FeatureMatrixDetails( @"[…] || […]", @"Union", fm => fm.InsideSets_Operator_DoubleVerticalLine)
                    .Test( @"[[a]||[b]]", "b", "]|["),
                new FeatureMatrixDetails( @"[…] -- […]", @"Difference", fm => fm.InsideSets_Operator_DoubleMinus)
                    .Test( @"[[ab]--[b]]", "a", "][-b"),
                new FeatureMatrixDetails( @"[…] ~~ […]", @"Symmetric difference", fm => fm.InsideSets_Operator_DoubleTilde)
                    .Test( @"[[ab]~~[bca]]", "c", "ab][~"),
            ] ),

            new ( @"Anchors",
            [
                new FeatureMatrixDetails( @"^", @"Beginning of string or line", fm => fm.Anchor_Circumflex)
                    .Test( @"^x", "x", null ),
                new FeatureMatrixDetails( @"$", @"End, or before '\n' at end of string or line", fm => fm.Anchor_Dollar)
                    .Test( @"x$", "x", null ),
                new FeatureMatrixDetails( @"\A", @"Start of string", fm => fm.Anchor_A)
                    .Test( @"\Ax", "x", null ),
                new FeatureMatrixDetails( @"\Z", @"End of string, or before '\n' at end of string", fm => fm.Anchor_Z)
                    .Test( @"x\Z", "x", null ),
                new FeatureMatrixDetails( @"\z", @"End of string", fm => fm.Anchor_z)
                    .Test( @"x\z", "x", null ),
                new FeatureMatrixDetails( @"\G", @"start of string or end of previous match", fm => fm.Anchor_G)
                    .Test( @"\Gx", "x", null ),
                new FeatureMatrixDetails( @"\b, \B", @"Boundary between \w and \W", fm => fm.Anchor_bB)
                    .Test( @"\bx", "y x", null ),
                new FeatureMatrixDetails( @"\b{g}", @"Unicode extended grapheme cluster boundary", fm => fm.Anchor_bg)
                    .Test( @"\b{g}x", "y x", null ),
                new FeatureMatrixDetails( @"\b{…}, \B{…}", @"Typed boundary", fm => fm.Anchor_bBBrace)
                    .Test( @"\b{wb}x", "y x", null ),
                new FeatureMatrixDetails( @"\K", @"Keep the stuff left of the \K", fm => fm.Anchor_K)
                    .Test( @"a\Kb", "ab", null ),
                new FeatureMatrixDetails( @"\m, \M", @"Start of word, end of word", fm => fm.Anchor_mM)
                    .Test( @"\mword\M", "some word here", null ),
                new FeatureMatrixDetails( @"\<, \>", @"Start of word, end of word", fm => fm.Anchor_LtGt)
                    .Test( @"\<word\>", "some word here", null ),
                new FeatureMatrixDetails( @"\`, \'", @"Start of string, end of string", fm => fm.Anchor_GraveApos)
                    .Test( @"\`x\'", "x", null ),
                new FeatureMatrixDetails( @"\y, \Y", @"Boundary between graphemes", fm => fm.Anchor_yY)
                    .Test( @"a\yb", "ab", null ),
            ] ),

            new ( @"Named groups, subroutines and backreferences",
            [
                new FeatureMatrixDetails( @"(?'name'…)", @"Named group", fm => fm.NamedGroup_Apos)
                    .Test( @"(?'n'x)", "x", null )
                    .Test( @"\(?'n'x\)", "x", null ),
                new FeatureMatrixDetails( @"(?<name>…)", @"Named group", fm => fm.NamedGroup_LtGt)
                    .Test( @"(?<n>x)", "x", null )
                    .Test( @"\(?<n>x\)", "x", null ),
                new FeatureMatrixDetails( @"(?P<name>…)", @"Named group", fm => fm.NamedGroup_PLtGt)
                    .Test( @"(?P<n>x)", "x", null )
                    .Test( @"\(?P<n>x\)", "x", null ),
                new FeatureMatrixDetails( @"(?@…)", @"Capturing group", fm => fm.NamedGroup_AtApos || fm.NamedGroup_AtLtGt || fm.CapturingGroup)
                    .Test( @"(?@<n>x)", "x", null ),
                new FeatureMatrixDetails( @"Duplicate names", @"Allow duplicate group names", fm => fm.AllowDuplicateGroupName)
                    .Test( @"(?<a>x)|(?<a>y)", "y", null )
                    .Test( @"\(?<a>x\)|\(?<a>y\)", "y", null )
                    .Test( @"(?P<a>x)|(?P<a>y)", "y", null ),
                new FeatureMatrixDetails( @"\1, \2, …, \9", @"Backreferences", fm => fm.Backref_Num == FeatureMatrix.BackrefEnum.OneDigit || fm.Backref_Num == FeatureMatrix.BackrefEnum.Any)
                    .Test( @"(x)\1", "xx", "x")
                    .Test( @"\(x\)\1", "xx", "x" ),
                new FeatureMatrixDetails( @"\nnn", @"Backreference, two or more digits", fm => fm.Backref_Num == FeatureMatrix.BackrefEnum.Any)
                    .Test( @"(x)(x)(x)(x)(x)(x)(x)(x)(x)(y)\10", "xxxxxxxxxyy", "x")
                    .Test( @"\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(y\)\10", "xxxxxxxxxyy", null ),
                new FeatureMatrixDetails( @"\k'name'", @"Backreference by name", fm => fm.Backref_kApos)
                    .Test( @"(?'n'x)\k'n'", "xx", null ),
                new FeatureMatrixDetails( @"\k<name>", @"Backreference by name", fm => fm.Backref_kLtGt)
                    .Test( @"(?<n>x)\k<n>", "xx", null ),
                new FeatureMatrixDetails( @"\k{name}", @"Backreference by name", fm => fm.Backref_kBrace)
                    .Test( @"(?<n>x)\k{n}", "xx", null ),
                new FeatureMatrixDetails( @"\kn", @"Backreference \k1, \k2, …", fm => fm.Backref_kNum)
                    .Test( @"(?<n>x)\k1", "xx", null ),
                new FeatureMatrixDetails( @"\k-n", @"Relative backreference \k-1, \k-2, …", fm => fm.Backref_kNegNum)
                    .Test( @"(?<n>x)\k-1", "xx", null ),
                new FeatureMatrixDetails( @"\g'…'", @"Backreference by name", fm => fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(?'n'.)\g'n'", "aa", "ab"),
                new FeatureMatrixDetails( @"\g'…'", @"Subroutine by name", fm => fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(?'n'.)\g'n'", "ab", null ),
                new FeatureMatrixDetails( @"\g<…>", @"Backreference by name", fm => fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(?<n>.)\g<n>", "aa", "ab"),
                new FeatureMatrixDetails( @"\g<…>", @"Subroutine by name", fm => fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(?<n>.)\g<n>", "ab", null ),
                new FeatureMatrixDetails( @"\gn", @"Backreference \g1, \g2, …", fm => fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(.)\g1", "aa", "ab"),
                new FeatureMatrixDetails( @"\gn", @"Subroutine \g1, \g2, …", fm => fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(.)\g1", "ab", null),
                new FeatureMatrixDetails( @"\g-n", @"Relative backreference \g-1, \g-2, …", fm => fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(.)\g-1", "aa", "ab"),
                new FeatureMatrixDetails( @"\g-n", @"Relative subroutine \g-1, \g-2, …", fm => fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(.)\g-1", "ab", null),
                new FeatureMatrixDetails( @"\g{…}", @"Backreference \g{name}, \g{number}, \g{-number}, g{+number}", fm => fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(?<n>.)\g{n}", "aa", "ab"),
                new FeatureMatrixDetails( @"\g{…}", @"Subroutine \g{name}, \g{number}, \g{-number}, g{+number}", fm => fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(?<n>.)\g{n}", "ab", null ),
                new FeatureMatrixDetails( @"(?P=name)", @"Backreference by name", fm => fm.Backref_PEqName)
                    .Test( @"(?P<n>.)(?P=n)", "aa", "ab"),
                //new FeatureMatrixDetails( @"\k< … >, \g< … >", @"Allow spaces like '\k < name >'", fm => fm.AllowSpacesInBackref ), // TODO
            ] ),

            new ( @"Grouping",
            [
                new FeatureMatrixDetails( @"(?:…)", @"Non-capturing group", fm => fm.NoncapturingGroup)
                    .Test( @"(?:x)", "x", null )
                    .Test( @"\(?:x\)", "x", null ),
                new FeatureMatrixDetails( @"(?=…)", @"Positive lookahead ", fm => fm.PositiveLookahead)
                    .Test( @"a(?=x)x", "ax", null )
                    .Test( @"\(?=x\)x", "ax", null ),
                new FeatureMatrixDetails( @"(?!…)", @"Negative lookahead ", fm => fm.NegativeLookahead)
                    .Test( @"a(?!x)y", "ay", null )
                    .Test( @"\(?!x\)y", "ay", null ),
                new FeatureMatrixDetails( @"(?<=…)", @"Positive lookbehind, fixed-length", fm => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.FixedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"(?<=x)a", "xa", null )
                    .Test( @"\(?<=x\)a", "xa", null ),
                new FeatureMatrixDetails( @"(?<=…)", @"Positive lookbehind, bounded-length", fm => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"(?<=x|yz)a", "xa", null )
                    .Test( @"\(?<=x|yz\)a", "xa", null ),
                new FeatureMatrixDetails( @"(?<=…)", @"Positive lookbehind, variable-length", fm => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"(?<=x.+)a", "x123a", null )
                    .Test( @"\(?<=x.+\)a", "x123a", null ),
                new FeatureMatrixDetails( @"(?<!…)", @"Negative lookbehind, fixed-length", fm => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.FixedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"(?<!x)a", "ya", "xa" )
                    .Test( @"\(?<!x\)a", "ya", "xa" ),
                new FeatureMatrixDetails( @"(?<!…)", @"Negative lookbehind, bounded-length", fm => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"(?<!x|yz)a", "ya", "xa" )
                    .Test( @"\(?<!x|yz\)a", "ya", "xa" ),
                new FeatureMatrixDetails( @"(?<!…)", @"Negative lookbehind, variable-length", fm => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"(?<!x.*)a", "ya", "xa" )
                    .Test( @"\(?<!x.*\)a", "ya", "xa" ),
                new FeatureMatrixDetails( @"(?>…)", @"Atomic group", fm => fm.AtomicGroup)
                    .Test( @"(?>x)", "x", null )
                    .Test( @"\(?>x\)", "x", null ),
                new FeatureMatrixDetails( @"(?|…)", @"Branch reset", fm => fm.BranchReset)
                    .Test( @"(?|(a)|(b)\1)", "bb", null )
                    .Test( @"\(?|\(a\)\|\(b\)\1\)", "bb", "x" ),
                new FeatureMatrixDetails( @"(?*…)", @"Non-atomic positive lookahead", fm => fm.NonatomicPositiveLookahead)
                    .Test( @"a(?*x)x", "ax", null ),
                new FeatureMatrixDetails( @"(?<*…)", @"Non-atomic positive lookbehind ", fm => fm.NonatomicPositiveLookbehind)
                    .Test( @"(?<*x)a", "xa", "x"),
                new FeatureMatrixDetails( @"(?~…)", @"Absent operator", fm => fm.AbsentOperator)
                    .Test( @"/\*(?~\*\/)\*\/", "/* abc */", null ),
                //new FeatureMatrixDetails( @"( ? … )", @"Allow spaces like '( ? < name >…)'", fm => fm.AllowSpacesInGroups ), // TODO
            ] ),

        new ( @"Recursive patterns",
            [
                new FeatureMatrixDetails( @"(?n)", @"Recursive subpattern by number", fm => fm.Recursive_Num)
                    .Test( @"(x.)(?1)", "xyxz", "xyZ"),
                new FeatureMatrixDetails( @"(?-n), (?+n)", @"Relative recursive subpattern by number", fm => fm.Recursive_PlusMinusNum)
                    .Test( @"(x(.))(?-1)", "xyz", null )
                    .Test( @"\(x\(.\)\)\(?-1\)", "xyz", null ),
                new FeatureMatrixDetails( @"(?R)", @"Recursive whole pattern", fm => fm.Recursive_R)
                    .Test( @"a(?R)*b", "aabb", "b"),
                new FeatureMatrixDetails( @"(?&name)", @"Recursive subpattern by name", fm => fm.Recursive_Name)
                    .Test( @"(?<n>a)(?&n)", "aa", null ),
                new FeatureMatrixDetails( @"(?P>name)", @"Recursive subpattern by name", fm => fm.Recursive_PGtName)
                    .Test( @"(?P<n>a)(?P>n)", "aa", null ),
                new FeatureMatrixDetails( @"(?…(grouplist))", @"Additionally return capturing groups", fm => fm.Recursive_ReturnGroups)
                    .Test( @"(?<a>A(?<b>.))(?&a(<b>))\k<b>", "ABACC", "ABACB"),
            ] ),

            new ( @"Quantifiers",
            [
                new FeatureMatrixDetails( @"*", @"Zero or more times", fm => fm.Quantifier_Asterisk)
                    .Test( @"xy*", "x", null ),
                new FeatureMatrixDetails( @"+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"xy+", "xyy", null ),
                new FeatureMatrixDetails( @"\+", @"One or more times", fm => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"xy\+", "xyy", null ),
                new FeatureMatrixDetails( @"?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"xy?", "x", null ),
                new FeatureMatrixDetails( @"\?", @"Zero or one time", fm => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"xy\?", "x", null ),
                new FeatureMatrixDetails( @"{n,m}", @"Between n and m times: {n}, {n,}, {n,m}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"x{2,3}", "xx", null ),
                new FeatureMatrixDetails( @"\{n,m\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", fm => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"x\{2,3\}", "xx", null ),
                //new FeatureMatrixDetails( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.Both ), // TODO
                //new FeatureMatrixDetails( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", fm => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.XModeOnly ), // TODO
                new FeatureMatrixDetails( @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", fm => fm.Quantifier_LowAbbrev)
                    .Test( @"x{,3}", "xxx", null )
                    .Test( @"x\{,3\}", "xxx", null ),
            ] ),

            new ( @"Conditionals",
            [
                new FeatureMatrixDetails( @"(?(number)…|…)", @"Conditionals by number, +number, -number", fm => fm.Conditional_BackrefByNumber)
                    .Test( @"(x)(?(1)y|z)", "xy", "bx"),
                new FeatureMatrixDetails( @"(?(name)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName)
                    .Test( @"(?<n>x)(?(n)y|z)", "xy", null )
                    .Test( @"(?P<n>x)(?(n)y|z)", "xy", null ),
                new FeatureMatrixDetails( @"(?(pattern)…|…)", @"Conditional subpattern", fm => fm.Conditional_Pattern)
                    .Test( @"x(?(?=.z)y|z)", "xyz", null )
                    .Test( @"x(?((?=.z))y|z)", "xyz", null ),
                new FeatureMatrixDetails( @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", fm => fm.Conditional_PatternOrBackrefByName)
                    .Test( @"x(?(y).|z)", "xy", null ),
                new FeatureMatrixDetails( @"(?('name')…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_Apos)
                    .Test( @"(?'n'x)(?('n')y|z)", "xy", null ),
                new FeatureMatrixDetails( @"(?(<name>)…|…)", @"Conditional by name", fm => fm.Conditional_BackrefByName_LtGt)
                    .Test( @"(?<n>x)(?(<n>)y|z)", "xy", null )
                    .Test( @"(?P<n>x)(?(<n>)y|z)", "xy", null ),
                new FeatureMatrixDetails( @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", fm => fm.Conditional_R)
                    .Test( @"(?(R)a+|(?R)b)", "aaaab", null ),
                new FeatureMatrixDetails( @"(?(R&name)…|…)", @"Recursive conditional by name", fm => fm.Conditional_RName)
                    .Test( @"(?<A>(?'B'abc(?(R)(?(R&A)1)(?(R&B)2)X|(?1)(?2)(?R))))", "abcabc1Xabc2XabcXabcabc", null ),
                new FeatureMatrixDetails( @"(?(DEFINE)…|…)", @"Defining subpatterns", fm => fm.Conditional_DEFINE)
                    .Test( @"(?(DEFINE)(?<n>x.z))(?&n)", "xyz", null ),
                new FeatureMatrixDetails( @"(?(VERSION…)…|…)", @"Check version using 'VERSION=decimal' or 'VERSION>=decimal'", fm => fm.Conditional_VERSION)
                    .Test( @"(?(VERSION>=1)xyz|abc)", "xyz", null ),
            ] ),

        new ( @"Miscellaneous",
            [
                new FeatureMatrixDetails( @"(*verb)", @"Control verbs: (*verb), (*verb:…), (*:name)", fm => fm.ControlVerbs)
                    .Test( @"x(*ACCEPT)|y(*FAIL)", "x", null )
                    .Test( @"(*UCP)a", "a", null )
                    .Test( @"x(*SKIP)y", "xy", null ),
                new FeatureMatrixDetails( @"(*…:…)", @"Script runs, such as (*atomic:…)", fm => fm.ScriptRuns)
                    .Test( @"(*atomic:x)", "x", null ),
                new FeatureMatrixDetails( @"(?Cn), (*func)", @"Callouts (custom functions)", fm => fm.Callouts ),

                new FeatureMatrixDetails( @"(?)", @"Empty construct", fm => fm.EmptyConstruct)
                    .Test( @"x(?)y", "xy", "x"),
                //new ( @"(? )", @"Empty construct", fm => fm.EmptyConstructX).Test( @"(?x)a(? )b", "ab", null ),
                new FeatureMatrixDetails( @"[]", @"Empty set", fm => fm.EmptySet)
                    .Test( @"x[]?", "x", null ),
                new FeatureMatrixDetails( @"Unicode", @"Supports Unicode characters, not just ASCII", fm => ! fm.AsciiOnly)
                    .Test( @"X.....Y", "XăîșțâY", null ),
                new FeatureMatrixDetails( @"Surrogates", @"“.” matches surrogate pairs as one entity (no split)", fm => ! fm.AsciiOnly && ! fm.SplitSurrogatePairs)
                    .Test( @"X.Y", "X💕Y", null ),

                new FeatureMatrixDetails( @"Fuzzy matching", @"Approximate matching using special patterns or parameters", fm => fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal || fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed || fm.FuzzyMatchingParams)
                    .Test( @"(test){i}", "teXst", null )
                    .Test( @"(test){+1}", "teXst", null )
                    .Test( @"\(test\)\{+1\}", "teXst", null )
                    .Test( (e, fm) => fm.FuzzyMatchingParams ),
                new FeatureMatrixDetails( "No hang", "No catastrophic infinite matching, no timeout errors", fm => fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Accept )
                    .Test( (e, fm) => CheckCatastrophicPattern( e, fm ) == CatastrophicBacktrackingResultEnum.Passed ),
                //new ( "No hang but error", "Give errors on possible catastrophic backtracking", fm => false ) { DirectCheck = (e, fm) => CheckCatastrophicPattern( e, fm ) == CatastrophicBacktrackingResultEnum.Error },
                new FeatureMatrixDetails( "Σσς", "Match letters that have multiple uppercase and lowercase variants", fm => fm.Σσς )
                    .IgnoreCase()
                    .Test( @"ΣΣΣ", "Σσς", null),
            ] ),

        ];
}
