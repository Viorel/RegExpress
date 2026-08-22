using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

partial class FeatureMatrixDetails
{
    internal static readonly FeatureMatrixGroup[] AllFeatureMatrixDetails =
        [

        new ( @"General",
            [
                new FeatureMatrixDetails( @"(…)", @"Grouping constructs", (e, fm) => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"(x)", "x", null, "x" ),
                new FeatureMatrixDetails( @"\(…\)", @"Grouping constructs", (e, fm) => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"\(x\)", "x", null, "x" ),

                new FeatureMatrixDetails( @"[…]", @"Character group", (e, fm) => fm.Brackets)
                    .Test( @"[x]", "x", null, "x" ),
                new FeatureMatrixDetails( @"(?[…])", @"Character group", (e, fm) => fm.ExtendedBrackets)
                    .Test( @"a(?[[x]])b", "axb", null, "axb" ),

                new FeatureMatrixDetails( @"|", @"Alternation", (e, fm) => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"x|y", "y", null, "y" ),
                new FeatureMatrixDetails( @"\|", @"Alternation", (e, fm) => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"x\|y", "y", null, "y" ),
                new FeatureMatrixDetails( @"new line (\n)", @"Alternatives on separate lines", (e, fm) => fm.AlternationOnSeparateLines)
                    .Test("x\ny", "y", null, "y" ),

                new FeatureMatrixDetails( @"#comment", @"Comment", (e, fm) => fm.XModeComments)
                    .IgnorePatternWhitespace()
                    .Test( "a#comment", "a", null, "a" )
                    .Test( "(?x)a#comment", "a", null, "a" )
                    .Test( "a#comment\nb", "ab", "b", "ab" ) // ('\n' is required by Hyperscan)
                    .Test( "(?x)a#comment\nb", "ab", "b", "ab" ), // ('\n' is required by Hyperscan)
                new FeatureMatrixDetails( @"(?#comment)", @"Inline comment", (e, fm) => fm.InlineComments)
                    .Test( @"a(?#comment)b", "ab", "a#commentb", "ab" )
                    .Test( @"a\(?#comment\)b", "ab", "a", "ab" ),
                new FeatureMatrixDetails( @"[#comment]", @"Comment inside […]", (e, fm) => fm.InsideSets_XModeComments)
                    .IgnorePatternWhitespace()
                    .Test( "a[b#comment\nz]y", "azy", "acy", "azy")
                    .Test( "(?x)a[b#comment\nz]y", "azy", "acy", "azy")
                    .Test( "(?xx)a[b#comment\nz]y", "azy", "acy", "azy"),

                new FeatureMatrixDetails( @"(?flags)", @"Inline options", (e, fm) => fm.Flags).IgnoreCase( false )
                    .Test( @"(?i)x", "X", null, "X" ),
                new FeatureMatrixDetails( @"(?flags:…)", @"Inline scoped options", (e, fm) => fm.ScopedFlags)
                    .IgnoreCase( false )
                    .Test( @"a(?i:x)b", "aXb", null, "aXb" ),
                new FeatureMatrixDetails( @"(?^flags)", @"Inline fresh options", (e, fm) => fm.CircumflexFlags)
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^)x", "x", "X", "x" ),
                new FeatureMatrixDetails( @"(?^flags:…)", @"Inline scoped fresh options", (e, fm) => fm.ScopedCircumflexFlags)
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^:x)", "x", "X", "x" ),
                new FeatureMatrixDetails( @"(?x)", @"Allow 'x' flag", (e, fm) => fm.XFlag)
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)a b", "ab", null, "ab" ),
                new FeatureMatrixDetails( @"(?xx)", @"Allow 'xx' flag", (e, fm) => fm.XXFlag)
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)[a b](?xx)[a b]", " a", "a ", " a"),

                new FeatureMatrixDetails( @"\Q…\E", @"Literal", (e, fm) => fm.Literal_QE)
                    .Test( @"a\Qx\E", "ax", "aQxE", "ax"),
                new FeatureMatrixDetails( @"[\Q…\E]", @"Literal inside […]", (e, fm) => fm.InsideSets_Literal_QE)
                    .Test( @"[\Qx\E]", "x", "Q", "x"),
                new FeatureMatrixDetails( @"[\q{…}]", @"Literal inside […]", (e, fm) => fm.InsideSets_Literal_qBrace)
                    .Test( @"a[\q{xy}]b", "axyb", "axb", "axyb"),
            ] ),

        new ( @"Quantifiers",
            [
                new FeatureMatrixDetails( @"*", @"Zero or more times", (e, fm) => fm.Quantifier_Asterisk)
                    .Test( @"xy*", "x", null, "x" ),
                new FeatureMatrixDetails( @"+", @"One or more times", (e, fm) => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"xy+z", "xyyz", null, "xyyz" ),
                new FeatureMatrixDetails( @"\+", @"One or more times", (e, fm) => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"xy\+", "xyy", null, "xyy" ),
                new FeatureMatrixDetails( @"?", @"Zero or one time", (e, fm) => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"xy?", "x", null, "x" ),
                new FeatureMatrixDetails( @"\?", @"Zero or one time", (e, fm) => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"xy\?", "x", null, "x" ),
                new FeatureMatrixDetails( @"{n,m}", @"Between n and m times: {n}, {n,}, {n,m}", (e, fm) => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal)
                    .Test( @"x{2,3}", "xx", null, "xx" ),
                new FeatureMatrixDetails( @"\{n,m\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", (e, fm) => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed)
                    .Test( @"x\{2,3\}", "xx", null, "xx" ),
                //new FeatureMatrixDetails( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", (e, fm) => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.Both ), // TODO
                //new FeatureMatrixDetails( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", (e, fm) => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.XModeOnly ), // TODO
                new FeatureMatrixDetails( @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", (e, fm) => fm.Quantifier_LowAbbrev)
                    .Test( @"x{,3}", "xxx", null, "xxx" )
                    .Test( @"x\{,3\}", "xxx", null, "xxx" ),
                new FeatureMatrixDetails( @"*?, +?, ??, {}?", @"Lazy (non-greedy) quantifiers — match as little as possible",
                        (e, fm) => fm.Quantifier_Asterisk && fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal && fm.Quantifier_Question== FeatureMatrix.PunctuationEnum.Normal && fm.Quantifier_Lazy )
                    .Test( @".+?A.*?AA??", "AAAAAA", null, "AAA" ),
                new FeatureMatrixDetails( @"*+, ++, ?+, {}+", @"Possessive quantifiers — match without backtracking",
                        (e, fm) => fm.Quantifier_Plus != FeatureMatrix.PunctuationEnum.None && fm.Quantifier_Possessive )
                    .Test( @"Xa++.Y", "XaaabY", "XaaaaY", "XaaabY" )
                    .Test( @"Xa\+\+.Y", "XaaabY", "XaaaaY", "XaaabY" ),
            ] ),

        new ( @"Escapes",
            [
                new FeatureMatrixDetails( @"\a", @"Bell, \u0007", (e, fm) => fm.Esc_a)
                    .Test( @"\a", "\u0007", null, "\u0007" ),
                new FeatureMatrixDetails( @"\b", @"Backspace, \u0008", (e, fm) => fm.Esc_b)
                    .Test( @"x\by", "x\u0008y", null, "x\u0008y" ),
                new FeatureMatrixDetails( @"\e", @"Escape, \u001B", (e, fm) => fm.Esc_e)
                    .Test( @"\e", "\u001B", null, "\u001B" ),
                new FeatureMatrixDetails( @"\f", @"Form feed, \u000C", (e, fm) => fm.Esc_f)
                    .Test( @"\f", "\u000C", null, "\u000C" ),
                new FeatureMatrixDetails( @"\n", @"New line, \u000A", (e, fm) => fm.Esc_n)
                    .Test( @"\n", "\u000A", null, "\u000A" ),
                new FeatureMatrixDetails( @"\r", @"Carriage return, \u000D", (e, fm) => fm.Esc_r)
                    .Test( @"\r", "\u000D", null, "\u000D" ),
                new FeatureMatrixDetails( @"\t", @"Tab, \u0009", (e, fm) => fm.Esc_t)
                    .Test( @"\t", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\v", @"Vertical tab, \u000B", (e, fm) => fm.Esc_v)
                    .Test( @"\v", "\u000B", "\f", "\u000B"),
                new FeatureMatrixDetails( @"\1..\7", @"Octal, one digit", (e, fm) => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3)
                    .Test( @"\3", "\u0003", null, "\u0003" ),
                new FeatureMatrixDetails( @"\nn, \nnn", @"Octal, two or three digits", (e, fm) => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 || fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3)
                    .Test( @"\11\101", "\u0009A", null, "\u0009A" )
                    .Test( @"\11\000\101\000", "\u0009A", null, "\u0009A" ), // ('\000' for Oniguruma)
                new FeatureMatrixDetails( @"\0nnn", @"Octal, up to three digits after '\0'", (e, fm) => fm.Esc_Octal0_1_3)
                    .Test( @"\03\011\0011", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                    .Test( @"\03\000\011\000\0011\000", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" ),
                new FeatureMatrixDetails( @"\o{nn…}", @"Octal", (e, fm) => fm.Esc_oBrace)
                    .Test( @"\o{11}", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\xXX", @"Hexadecimal code, two digits", (e, fm) => fm.Esc_x2)
                    .Test( @"\x09", "\u0009", null, "\u0009" )
                    .Test( @"\x09\x00", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\x{XX…}", @"Hexadecimal code", (e, fm) => fm.Esc_xBrace)
                    .Test( @"\x{0009}", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\uXXXX", @"Hexadecimal code, four digits", (e, fm) => fm.Esc_u4)
                    .Test( @"\u0009", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\UXXXXXXXX", @"Hexadecimal code, eight digits", (e, fm) => fm.Esc_U8)
                    .Test( @"\U00000009", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\u{XX…}", @"Hexadecimal code", (e, fm) => fm.Esc_uBrace)
                    .Test( @"\u{0009}", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\U{XX…}", @"Hexadecimal code", (e, fm) => fm.Esc_UBrace)
                    .Test( @"\U{0009}", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"\cC", @"Control character", (e, fm) => fm.Esc_c1)
                    .Test( @"\cM", "\r", null, "\r" ),
                new FeatureMatrixDetails( @"\CC", @"Control character", (e, fm) => fm.Esc_C1)
                    .Test( @"\CM", "\r", null, "\r" ),
                new FeatureMatrixDetails( @"\C-C", @"Control character", (e, fm) => fm.Esc_CMinus)
                    .Test( @"\C-M", "\r", "null", "\r" ),
                new FeatureMatrixDetails( @"\N{…}", @"Unicode name or 'U+code'", (e, fm) => fm.Esc_NBrace)
                    .Test( @"\N{COMMA}", ",", null, "," )
                    .Test( @"\N{comma}", ",", null, "," )
                    .Test( @"\N{LATIN CAPITAL LETTER A}", "A", null, "A" )
                    .Test( @"\N{U+0061}", "a", null, "a" ),
                new FeatureMatrixDetails( @"\any", @"Generic escape", (e, fm) => fm.GenericEscape)
                    .Test( @"a\j", @"aj", @"a\j", "aj" ),
            ] ),

        new ( @"Escapes inside […] sets",
            [
                new FeatureMatrixDetails( @"[\a]", @"Bell, \u0007", (e, fm) => fm.InsideSets_Esc_a)
                    .Test( @"[\a]", "\u0007", null, "\u0007" ),
                new FeatureMatrixDetails( @"[\b]", @"Backspace, \u0008", (e, fm) => fm.InsideSets_Esc_b)
                    .Test( @"[\b]", "\u0008", null, "\u0008" ),
                new FeatureMatrixDetails( @"[\e]", @"Escape, \u001B", (e, fm) => fm.InsideSets_Esc_e)
                    .Test( @"[\e]", "\u001B", null, "\u001B" ),
                new FeatureMatrixDetails( @"[\f]", @"Form feed, \u000C", (e, fm) => fm.InsideSets_Esc_f)
                    .Test( @"[\f]", "\u000C", null, "\u000C" ),
                new FeatureMatrixDetails( @"[\n]", @"New line, \u000A", (e, fm) => fm.InsideSets_Esc_n)
                    .Test( @"[\n]", "\u000A", null, "\u000A" ),
                new FeatureMatrixDetails( @"[\r]", @"Carriage return, \u000D", (e, fm) => fm.InsideSets_Esc_r)
                    .Test( @"[\r]", "\u000D", null, "\u000D" ),
                new FeatureMatrixDetails( @"[\t]", @"Tab, \u0009", (e, fm) => fm.InsideSets_Esc_t)
                    .Test( @"[\t]", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"[\v]", @"Vertical tab, \u000B", (e, fm) => fm.InsideSets_Esc_v)
                    .Test( @"[\v]", "\u000B", "\f", "\u000B"),
                new FeatureMatrixDetails( @"[\1..\7]", @"Octal, one digit", (e, fm) => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3)
                    .Test( @"[\3]", "\u0003", null, "\u0003" )
                    .Test( @"[\3\0]", "\u0003", null, "\u0003" ),
                new FeatureMatrixDetails( @"[\nn], [\nnn]", @"Octal, two or three digits", (e, fm) => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 || fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3)
                    .Test( @"[\11][\101]", "\u0009A", null, "\u0009A" )
                    .Test( @"[\11\000][\101\000]", "\u0009A", null, "\u0009A" ), // ('\000' for Oniguruma)
                new FeatureMatrixDetails( @"[\0nnn]", @"Octal, up to three digits after '\0'", (e, fm) => fm.InsideSets_Esc_Octal0_1_3)
                    .Test( @"[\03][\011][\0011]", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                    .Test( @"[\03\000][\011\000][\0011\000]", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" ),
                new FeatureMatrixDetails( @"[\o{nn…}]", @"Octal", (e, fm) => fm.InsideSets_Esc_oBrace)
                    .Test( @"[\o{11}]", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"[\xXX]", @"Hexadecimal code, two digits", (e, fm) => fm.InsideSets_Esc_x2)
                    .Test( @"[\x09]", "\u0009", null, "\u0009" )
                    .Test( @"[\x09\x00]", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"[\x{XX…}]", @"Hexadecimal code", (e, fm) => fm.InsideSets_Esc_xBrace)
                    .Test( @"[\x{0009}]", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"[\uXXXX]", @"Hexadecimal code, four digits", (e, fm) => fm.InsideSets_Esc_u4)
                    .Test( @"[\u0009]", "\u0009", null, "\u0009" ),
                new FeatureMatrixDetails( @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", (e, fm) => fm.InsideSets_Esc_U8)
                    .Test( @"[\U00000009]", "\u0009", "x", "\u0009"),
                new FeatureMatrixDetails( @"[\u{XX…}]", @"Hexadecimal code", (e, fm) => fm.InsideSets_Esc_uBrace)
                    .Test( @"[\u{0009}]", "\u0009", "X", "\u0009"),
                new FeatureMatrixDetails( @"[\U{XX…}]", @"Hexadecimal code", (e, fm) => fm.InsideSets_Esc_UBrace)
                    .Test( @"[\U{0009}]", "\u0009", "x", "\u0009"),
                new FeatureMatrixDetails( @"[\cC]", @"Control character", (e, fm) => fm.InsideSets_Esc_c1)
                    .Test( @"[\cM]", "\r", null, "\r" ),
                new FeatureMatrixDetails( @"[\CC]", @"Control character", (e, fm) => fm.InsideSets_Esc_C1)
                    .Test( @"[\CM]", "\r", null, "\r" ),
                new FeatureMatrixDetails( @"[\C-C]", @"Control character ", (e, fm) => fm.InsideSets_Esc_CMinus)
                    .Test( @"[\C-M]", "\r", null, "\r" ),
                new FeatureMatrixDetails( @"[\N{…}]", @"Unicode name or 'U+code'", (e, fm) => fm.InsideSets_Esc_NBrace)
                    .Test( @"[\N{COMMA}]", ",", "M", "," ) // (see also '\N' -- any except '\n')
                    .Test( @"[\N{comma}]", ",", "m", "," )
                    .Test( @"[\N{U+0061}]", "a", "U", "a" ),
                new FeatureMatrixDetails( @"[\any]", @"Generic escape", (e, fm) => fm.InsideSets_GenericEscape)
                    .Test( @"[\j]", "j", @"\"),
            ] ),

        new ( @"Classes",
            [
                new FeatureMatrixDetails( @".", @"Any, including or excepting newline (\n)", (e, fm) => fm.Class_Dot)
                    .Test( @".", "x", null, "x" ),
                new FeatureMatrixDetails( @"\C", @"Single byte", (e, fm) => fm.Class_Cbyte)
                    .Test( @"\C\C", "î", null, "î" ),
                new FeatureMatrixDetails( @"\C", @"Single code point", (e, fm) => fm.Class_Ccp)
                    .Test( @"\C\C", "îî", "î", "îî"),
                new FeatureMatrixDetails( @"\d, \D", @"Digit", (e, fm) => fm.Class_dD)
                    .Test( @"\d\D", "9x", null, "9x" ),
                new FeatureMatrixDetails( @"\h, \H", @"Hexadecimal character", (e, fm) => fm.Class_hHhexa)
                    .Test( @"\h\H", "Ax", null, "Ax" ),
                new FeatureMatrixDetails( @"\h, \H", @"Horizontal space", (e, fm) => fm.Class_hHhorspace)
                    .Test( @"\h\H", " x", null, " x" ),
                new FeatureMatrixDetails( @"\l, \L", @"Lowercase character", (e, fm) => fm.Class_lL)
                    .Test( @"\l\L", "xX", null, "xX" ),
                new FeatureMatrixDetails( @"\N", @"Any except '\n'", (e, fm) => fm.Class_N)
                    .Test( @"\N", "a", "\n", "a" ),
                new FeatureMatrixDetails( @"\O", @"Any", (e, fm) => fm.Class_O)
                    .Test( @"\O", "a", null, "a" ),
                new FeatureMatrixDetails( @"\R", @"Line break", (e, fm) => fm.Class_R)
                    .Test( @"a\Rb", "a\r\nb", null, "a\r\nb" ),
                new FeatureMatrixDetails( @"\s, \S", @"Space", (e, fm) => fm.Class_sS)
                    .Test( @"\s\S", " x", null, " x" ),
                new FeatureMatrixDetails( @"\sx, \Sx", @"Syntax group; 'x' — group", (e, fm) => fm.Class_sSx)
                    .Test( @"\ss", " ", null, " " ),
                new FeatureMatrixDetails( @"\u, \U", @"Uppercase character", (e, fm) => fm.Class_uU)
                    .Test( @"\u\U", "Xx", null, "Xx" ),
                new FeatureMatrixDetails( @"\v, \V", @"Vertical space", (e, fm) => fm.Class_vV)
                    .Test( @"\v\v", "\r\f", null, "\r\f" ),
                new FeatureMatrixDetails( @"\w, \W", @"Word character", (e, fm) => fm.Class_wW)
                    .Test( @"\w\w\w", "xyz", null, "xyz" ),
                new FeatureMatrixDetails( @"\X", @"Extended grapheme cluster", (e, fm) => fm.Class_X)
                    .Test( @"\X", "a", null, "a" ),
                new FeatureMatrixDetails( @"\pX, \PX", @"Unicode property, X — short property name", (e, fm) => fm.Class_pP)
                    .Test( @"\pL\PL", "x9", null, "x9" ),
                new FeatureMatrixDetails( @"\p{…}, \P{…}", @"Unicode property", (e, fm) => fm.Class_pPBrace)
                    .Test( @"\p{L}\P{L}", "x9", null, "x9" ),
            ] ),

        new (@"Classes inside […] sets",
            [
                new FeatureMatrixDetails( @"[\d], [\D]", @"Digit", (e, fm) => fm.InsideSets_Class_dD)
                    .Test( @"a[\d]", "a9", null, "a9" ),
                new FeatureMatrixDetails( @"[\h], [\H]", @"Hexadecimal character", (e, fm) => fm.InsideSets_Class_hHhexa)
                    .Test( @"[\h][\H]", "Ax", null, "Ax" ),
                new FeatureMatrixDetails( @"[\h], [\H]", @"Horizontal space", (e, fm) => fm.InsideSets_Class_hHhorspace)
                    .Test( @"[\h][\H]", " x", null, " x" ),
                new FeatureMatrixDetails( @"[\l], [\L]", @"Lowercase character", (e, fm) => fm.InsideSets_Class_lL)
                    .Test( @"[\l][\L]", "xX", null, "xX" ),
                new FeatureMatrixDetails( @"[\R]", @"Line break", (e, fm) => fm.InsideSets_Class_R)
                    .Test( @"a[\R]b", "a\r\nb", null, "a\r\nb" ),
                new FeatureMatrixDetails( @"[\s], [\S]", @"Space", (e, fm) => fm.InsideSets_Class_sS)
                    .Test( @"a[\s][\S]x", "a 9x", null, "a 9x" ),
                new FeatureMatrixDetails( @"[\sx], [\Sx]", @"Syntax group; 'x' — group", (e, fm) => fm.InsideSets_Class_sSx)
                    .Test( @"[\ss]", " ", "s", " "),
                new FeatureMatrixDetails( @"[\u], [\U]", @"Uppercase character", (e, fm) => fm.InsideSets_Class_uU)
                    .Test( @"[\u][\U]", "Xx", null, "Xx" ),
                new FeatureMatrixDetails( @"[\v], [\V]", @"Vertical space", (e, fm) => fm.InsideSets_Class_vV)
                    .Test( @"[\v][\v]", "\r\f", null, "\r\f" ),
                new FeatureMatrixDetails( @"[\w], [\W]", @"Word character", (e, fm) => fm.InsideSets_Class_wW)
                    .Test( @"a[\w][\w][\w]", "axyz", null, "axyz" ),
                new FeatureMatrixDetails( @"[\X]", @"Extended grapheme cluster", (e, fm) => fm.InsideSets_Class_X)
                    .Test( @"[\X]", "a", null, "a" ),
                new FeatureMatrixDetails( @"[\pX], [\PX]", @"Unicode property, X — short property name", (e, fm) => fm.InsideSets_Class_pP)
                    .Test( @"[\pL][\PL]", "x9", null, "x9" ),
                new FeatureMatrixDetails( @"[\p{…}], [\P{…}]", @"Unicode property", (e, fm) => fm.InsideSets_Class_pPBrace)
                    .Test( @"[\p{L}][\P{L}]", "x9", null, "x9" ),
                new FeatureMatrixDetails( @"[[:class:]]", @"Character class", (e, fm) => fm.InsideSets_Class_Name)
                    .Test( @"[[:alpha:]]", "X", null, "X" ),
                new FeatureMatrixDetails( @"[[=elem=]]", @"Equivalence", (e, fm) => fm.InsideSets_Equivalence)
                    .Test( @"[[=a=]][[=a=]]", "aA", null, "aA" ), // 'Á' not matched by STL regex.
                new FeatureMatrixDetails( @"[[.elem.]]", @"Collating symbol", (e, fm) => fm.InsideSets_Collating)
                    .Test( @"a[[.ch.]]x", "achx", null, "achx" )
                    .Test( @"a[[.comma.]]b", "a,b", null, "a,b" ), // STL seems to have a defect.
            ] ),

        new ( @"Operators inside […] sets",
            [
                new FeatureMatrixDetails( @"[[…] op […]]", @"Using operators for nested groups", (e, fm) => fm.InsideSets_Operators)
                    .Test( @"[[ab]&[bc]]", "b", "a&c", "b")
                    .Test( @"[[ab]&&[bc]]", "b", "a&c", "b"),
                new FeatureMatrixDetails( @"(?[[…] op […]])", @"Using operators for nested groups", (e, fm) => fm.InsideSets_OperatorsExtended)
                    .Test( @"(?[[ab]&[bc]])", "b", "a&c", "b"),
                new FeatureMatrixDetails( @"[…] & […]", @"Intersection", (e, fm) => fm.InsideSets_Operator_Ampersand)
                    .Test( @"[[ab]&[bc]]", "b", "a&c", "b")
                    .Test( @"(?[[ab]&[bc]])", "b", "a&c", "b"),
                new FeatureMatrixDetails( @"[…] + […]", @"Union", (e, fm) => fm.InsideSets_Operator_Plus)
                    .Test( @"[[a]+[b]]", "a", "+", "a")
                    .Test( @"(?[[a]+[b]])", "a", "+", "a" ),
                new FeatureMatrixDetails( @"[…] | […]", @"Union", (e, fm) => fm.InsideSets_Operator_VerticalLine)
                    .Test( @"[[a]|[b]]", "b", "|", "b")
                    .Test( @"(?[[a]|[b]])", "b", "|", "b"),
                new FeatureMatrixDetails( @"[…] - […]", @"Subtraction", (e, fm) => fm.InsideSets_Operator_Minus)
                    .Test( @"[[ab]-[b]]", "a", "b", "a")
                    .Test( @"(?[[ab]-[b]])", "a", "b", "a"),
                new FeatureMatrixDetails( @"[…] ^ […]", @"Symmetric difference", (e, fm) => fm.InsideSets_Operator_Circumflex)
                    .Test( @"[[ab]^[bc]]", "c", "^", "c")
                    .Test( @"(?[[ab]^[bc]])", "c", "^", "c"),
                new FeatureMatrixDetails( @"![…]", @"Complement", (e, fm) => fm.InsideSets_Operator_Exclamation)
                    .Test( @"a(?[![b]])y", "axy", null, "axy" )
                    .Test( @"a[![b]]y", "axy", null, "axy" ),
                new FeatureMatrixDetails( @"[…] && […]", @"Intersection", (e, fm) => fm.InsideSets_Operator_DoubleAmpersand)
                    .Test( @"[[ab]&&[bc]]", "b", "a&c", "b" ),
                new FeatureMatrixDetails( @"[…] || […]", @"Union", (e, fm) => fm.InsideSets_Operator_DoubleVerticalLine)
                    .Test( @"[[a]||[b]]", "b", "]|[", "b"),
                new FeatureMatrixDetails( @"[…] -- […]", @"Difference", (e, fm) => fm.InsideSets_Operator_DoubleMinus)
                    .Test( @"[[ab]--[b]]", "a", "][-b", "a"),
                new FeatureMatrixDetails( @"[…] ~~ […]", @"Symmetric difference", (e, fm) => fm.InsideSets_Operator_DoubleTilde)
                    .Test( @"[[ab]~~[bca]]", "c", "ab][~", "c"),
            ] ),

        new ( @"Anchors",
            [
                new FeatureMatrixDetails( @"^", @"Beginning of string or line", (e, fm) => fm.Anchor_Circumflex)
                    .Test( @"^x", "x", null, "x" ),
                new FeatureMatrixDetails( @"$", @"End, or before '\n' at end of string or line", (e, fm) => fm.Anchor_Dollar)
                    .Test( @"x$", "x", null, "x" ),
                new FeatureMatrixDetails( @"\A", @"Start of string", (e, fm) => fm.Anchor_A)
                    .Test( @"\Ax", "x", null, "x" ),
                new FeatureMatrixDetails( @"\Z", @"End of string, or before '\n' at end of string", (e, fm) => fm.Anchor_Z == FeatureMatrix.AnchorZModeEnum.Correct )
                    .Test( @"x\Z", "x\n", "xZ", "x" ),
                new FeatureMatrixDetails( @"\Z", @"End of string, same as '\z'", (e, fm) => fm.Anchor_Z == FeatureMatrix.AnchorZModeEnum.Compatible )
                    .Test( @"x\Z", "x", "xZ x\n", "x" ),
                new FeatureMatrixDetails( @"\z", @"End of string", (e, fm) => fm.Anchor_z)
                    .Test( @"x\z", "x", "xz", "x" ),
                new FeatureMatrixDetails( @"\G", @"start of string or end of previous match", (e, fm) => fm.Anchor_G)
                    .Test( @"\Gx", "x", null, "x" ),
                new FeatureMatrixDetails( @"\b, \B", @"Boundary between \w and \W", (e, fm) => fm.Anchor_bB)
                    .Test( @"\bx", "y x", null, "x" ),
                new FeatureMatrixDetails( @"\b{g}", @"Unicode extended grapheme cluster boundary", (e, fm) => fm.Anchor_bg)
                    .Test( @"\b{g}x", "y x", null, "x" ),
                new FeatureMatrixDetails( @"\b{…}, \B{…}", @"Typed boundary", (e, fm) => fm.Anchor_bBBrace)
                    .Test( @"\b{wb}x", "y x", null, "x" )
                    .Test( @"\b{start}x", "y x", null, "x" )
                    .Test( @"\b{start-half}x", "y x", null, "x" ),
                new FeatureMatrixDetails( @"[[:<:]], [[:>:]]", @"POSIX word boundary", (e, fm) => fm.Anchor_PosixWB)
                    .Test( @"[[:<:]]a[[:>:]]", "a", null, "a" ),
                new FeatureMatrixDetails( @"\m, \M", @"Start of word, end of word", (e, fm) => fm.Anchor_mM)
                    .Test( @"\mword\M", "some word here", null, "word" ),
                new FeatureMatrixDetails( @"\<, \>", @"Start of word, end of word", (e, fm) => fm.Anchor_LtGt)
                    .Test( @"\<word\>", "some word here", null, "word" ),
                new FeatureMatrixDetails( @"\`, \'", @"Start of string, end of string", (e, fm) => fm.Anchor_GraveApos)
                    .Test( @"\`x\'", "x", null, "x" ),
                new FeatureMatrixDetails( @"\y, \Y", @"Boundary between graphemes", (e, fm) => fm.Anchor_yY)
                    .Test( @"a\yb", "ab", null, "ab" ),
                new FeatureMatrixDetails( @"\K", @"Keep the stuff left of the \K", (e, fm) => fm.Anchor_K)
                    .Test( @"a\Kb", "ab", "aKb", "b" ),
            ] ),

        new ( @"Named groups, subroutines and backreferences",
            [
                new FeatureMatrixDetails( @"(?'name'…)", @"Named group", (e, fm) => fm.NamedGroup_Apos)
                    .Test( @"a(?'n'x)b", "axb", null, "axb" )
                    .Test( @"a\(?'n'x\)b", "axb", null, "axb" ),
                new FeatureMatrixDetails( @"(?<name>…)", @"Named group", (e, fm) => fm.NamedGroup_LtGt)
                    .Test( @"a(?<n>x)b", "axb", null, "axb" )
                    .Test( @"a\(?<n>x\)b", "axb", null, "axb" ),
                new FeatureMatrixDetails( @"(?P<name>…)", @"Named group", (e, fm) => fm.NamedGroup_PLtGt)
                    .Test( @"a(?P<n>x)b", "axb", null, "axb" )
                    .Test( @"a\(?P<n>x\)b", "x", null, "axb" ),
                new FeatureMatrixDetails( @"(?<name2-name1>…)", @"Balancing group", (e, fm) => (fm.NamedGroup_Apos || fm.NamedGroup_LtGt || fm.NamedGroup_PLtGt) && fm.BalancingGroup)
                    .Test( (e, fm) =>
                    {
                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(?<left>left).*(?<right-left>right)", "leftXYZright" );
                            if( matches.Count !=1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First();
                            if( !match.Success ) return false;

                            RegExpressLibrary.Matches.IGroup? g1 = match.Groups.Skip(1).FirstOrDefault();
                            if( g1 == null) return false;
                            if( g1.Success) return false;

                            RegExpressLibrary.Matches.IGroup? g2 = match.Groups.Skip(2).FirstOrDefault();
                            if( g2 == null) return false;
                            if( !g2.Success) return false;

                            if( g2.Value != "XYZ") return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    }),
                new FeatureMatrixDetails( @"Duplicate names", @"Allow duplicate group names", (e, fm) => fm.DuplicateGroupName)
                    .Test( @"(?<a>x)|(?<a>y)", "y", "z", "y" )
                    .Test( @"\(?<a>x\)|\(?<a>y\)", "y", "z", "y" )
                    .Test( @"(?P<a>x)|(?P<a>y)", "y", "z", "y" ),
                new FeatureMatrixDetails( @"\1, \2, …, \9", @"Backreferences", (e, fm) => fm.Backref_Num == FeatureMatrix.BackrefEnum.OneDigit || fm.Backref_Num == FeatureMatrix.BackrefEnum.Any)
                    .Test( @"(x)\1", "xx", "x", "xx")
                    .Test( @"\(x\)\1", "xx", "x", "xx" ),
                new FeatureMatrixDetails( @"\nnn", @"Backreference, two or more digits", (e, fm) => fm.Backref_Num == FeatureMatrix.BackrefEnum.Any)
                    .Test( @"(x)(x)(x)(x)(x)(x)(x)(x)(x)(y)\10", "xxxxxxxxxyy", "xxxxxxxxxy\x10", "xxxxxxxxxyy")
                    .Test( @"\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(y\)\10", "xxxxxxxxxyy", "xxxxxxxxxy\x10", "xxxxxxxxxyy" ),
                new FeatureMatrixDetails( @"\k'name'", @"Backreference by name", (e, fm) => fm.Backref_kApos)
                    .Test( @"(?'n'x)\k'n'", "xx", null, "xx" ),
                new FeatureMatrixDetails( @"\k<name>", @"Backreference by name", (e, fm) => fm.Backref_kLtGt)
                    .Test( @"(?<n>x)\k<n>", "xx", null, "xx" ),
                new FeatureMatrixDetails( @"\k{name}", @"Backreference by name", (e, fm) => fm.Backref_kBrace)
                    .Test( @"(?<n>x)\k{n}", "xx", null, "xx" ),
                new FeatureMatrixDetails( @"\kn", @"Backreference \k1, \k2, …", (e, fm) => fm.Backref_kNum)
                    .Test( @"(?<n>x)\k1", "xx", null, "xx" ),
                new FeatureMatrixDetails( @"\k-n", @"Relative backreference \k-1, \k-2, …", (e, fm) => fm.Backref_kNegNum)
                    .Test( @"(?<n>x)\k-1", "xx", null, "xx" ),
                new FeatureMatrixDetails( @"\g'…'", @"Backreference by name", (e, fm) => fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(?'n'.)\g'n'", "aa", "ab", "aa"),
                new FeatureMatrixDetails( @"\g'…'", @"Subroutine by name", (e, fm) => fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(?'n'.)\g'n'", "ab", null, "ab" ),
                new FeatureMatrixDetails( @"\g<…>", @"Backreference by name", (e, fm) => fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(?<n>.)\g<n>", "aa", "ab", "aa"),
                new FeatureMatrixDetails( @"\g<…>", @"Subroutine by name", (e, fm) => fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(?<n>.)\g<n>", "ab", null, "ab" ),
                new FeatureMatrixDetails( @"\gn", @"Backreference \g1, \g2, …", (e, fm) => fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(.)\g1", "aa", "ab", "aa"),
                new FeatureMatrixDetails( @"\gn", @"Subroutine \g1, \g2, …", (e, fm) => fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(.)\g1", "ab", null, "ab"),
                new FeatureMatrixDetails( @"\g-n", @"Relative backreference \g-1, \g-2, …", (e, fm) => fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(.)\g-1", "aa", "ab", "aa"),
                new FeatureMatrixDetails( @"\g-n", @"Relative subroutine \g-1, \g-2, …", (e, fm) => fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(.)\g-1", "ab", null, "ab"),
                new FeatureMatrixDetails( @"\g{…}", @"Backreference \g{name}, \g{number}, \g{-number}, g{+number}", (e, fm) => fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Value)
                    .Test( @"(?<n>.)\g{n}", "aa", "ab", "aa"),
                new FeatureMatrixDetails( @"\g{…}", @"Subroutine \g{name}, \g{number}, \g{-number}, g{+number}", (e, fm) => fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Pattern)
                    .Test( @"(?<n>.)\g{n}", "ab", null, "ab" ),
                new FeatureMatrixDetails( @"(?P=name)", @"Backreference by name", (e, fm) => fm.Backref_PEqName)
                    .Test( @"(?P<n>.)(?P=n)", "aa", "ab", "aa"),
                //new FeatureMatrixDetails( @"\k< … >, \g< … >", @"Allow spaces like '\k < name >'", (e, fm) => fm.AllowSpacesInBackref ), // TODO
            ] ),

        new ( @"Grouping and Lookaround",
            [
                new FeatureMatrixDetails( @"(?:…)", @"Non-capturing group", (e, fm) => fm.NoncapturingGroup)
                    .Test( @"a(?:x)y", "axy", null, "axy" )
                    .Test( @"a\(?:x\)y", "axy", null, "axy" ),
                new FeatureMatrixDetails( @"(?=…)", @"Positive lookahead ", (e, fm) => fm.PositiveLookahead)
                    .Test( @"a(?=xz).z", "axz", "atz", "axz" )
                    .Test( @"a\(?=xz\).z", "axz", "atz", "axz" ),
                new FeatureMatrixDetails( @"(?!…)", @"Negative lookahead ", (e, fm) => fm.NegativeLookahead)
                    .Test( @"a(?!x).y", "azy", @"axy", "azy" )
                    .Test( @"a\(?!x\).y", "azy", @"axy", "azy" ),
                new FeatureMatrixDetails( @"(?<=…)", @"Positive lookbehind, fixed-length", (e, fm) => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.FixedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"x.(?<=xy)a", "xya", "xta", "xya" )
                    .Test( @"x.\(?<=xy\)a", "xya", "xta", "xya" ),
                new FeatureMatrixDetails( @"(?<=…)", @"Positive lookbehind, bounded-length", (e, fm) => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"x..(?<=x{2,3}y)a", "xxya", "xtta", "xxya" )
                    .Test( @"x..\(?<=x{2,3}y\)a", "xxya", "xtta", "xxya" )
                    .Test( @"x..\(?<=xx?x?y\)a", "xxya", "xtta", "xxya" ),
                new FeatureMatrixDetails( @"(?<=…)", @"Positive lookbehind, variable-length", (e, fm) => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"x.(?<=x+y)a", "xxxxya", "xta", "xya" )
                    .Test( @"x.\(?<=x+y\)a", "xxxxya", "xta", "xya" ),
                new FeatureMatrixDetails( @"(?<!…)", @"Negative lookbehind, fixed-length", (e, fm) => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.FixedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @".(?<!xy)a", "ya", "xya", "ya" )
                    .Test( @".\(?<!xy\)a", "ya", "xya", "ya" ),
                new FeatureMatrixDetails( @"(?<!…)", @"Negative lookbehind, bounded-length", (e, fm) => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @".(?<!x{2,7})a", "xa", "xxa", "xa" )
                    .Test( @"\(?<!x{2,7}\)a", "xa", "xxa", "xa" )
                    .Test( @"\(?<!xxx?x?\)a", "xa", "xxa", "xa" ),
                new FeatureMatrixDetails( @"(?<!…)", @"Negative lookbehind, variable-length", (e, fm) => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @".(?<!x.*)a", "ya", "xa", "ya" )
                    .Test( @"\(?<!x.*\)a", "ya", "xa", "ya" ),
                new FeatureMatrixDetails( @"(?=…(?=…))", @"Nested lookarounds", (e, fm) => ( fm.PositiveLookahead || fm.NegativeLookahead || fm.PositiveLookbehind != FeatureMatrix.LookModeEnum.None || fm.NegativeLookbehind != FeatureMatrix.LookModeEnum.None ) && fm.NestedLookaround )
                    .Test( @"a(?=b(?=c))bc", "abc", null, "abc" )
                    .Test( @"a\(?=b\(?=c\)\)bc", "abc", null, "abc" )
                    .Test( @"a(?<=ya(?<=xya))", "xyabc", null, "abc" )
                    .Test( @"a\(?<=ya\(?<=xya\)\)", "xyabc", null, "abc" ),
                new FeatureMatrixDetails( @"(?>…)", @"Atomic group", (e, fm) => fm.AtomicGroup)
                    .Test( @"a(?>x)b", "axb", null, "axb" )
                    .Test( @"a\(?>x\)b", "axb", null, "axb" ),
                new FeatureMatrixDetails( @"(?|…)", @"Branch reset", (e, fm) => fm.BranchReset)
                    .Test( @"(?|(a)|(b))\1", "bb", "1", "bb" )
                    .Test( @"\(?|\(a\)\|\(b\)\)\1", "bb", "1", "bb" ),
                new FeatureMatrixDetails( @"(?*…)", @"Non-atomic positive lookahead", (e, fm) => fm.NonatomicPositiveLookahead)
                    .Test( @"a(?*x)x", "ax", null, "ax" ),
                new FeatureMatrixDetails( @"(?<*…)", @"Non-atomic positive lookbehind", (e, fm) => fm.NonatomicPositiveLookbehind)
                    .Test( @"(?<*x)a", "xa", "ta", "a"),
                new FeatureMatrixDetails( @"(?~…)", @"Absent operator", (e, fm) => fm.AbsentOperator)
                    .Test( @"/\*(?~\*\/)\*\/", "/* abc */", null, "/* abc */" ),
                //new FeatureMatrixDetails( @"( ? … )", @"Allow spaces like '( ? < name >…)'", (e, fm) => fm.AllowSpacesInGroups ), // TODO
            ] ),

        new ( @"Recursive patterns",
            [
                new FeatureMatrixDetails( @"(?n)", @"Recursive subpattern by number", (e, fm) => fm.Recursive_Num)
                    .Test( @"(x.)(?1)", "xyxz", "xyZ", "xyxz"),
                new FeatureMatrixDetails( @"(?-n), (?+n)", @"Relative recursive subpattern by number", (e, fm) => fm.Recursive_PlusMinusNum)
                    .Test( @"(a|(b))(?-1)", "ab", "a", "ab" )
                    .Test( @"\(x\(.\)\)\(?-1\)", "xyz", null ),
                new FeatureMatrixDetails( @"(?R)", @"Recursive whole pattern", (e, fm) => fm.Recursive_R)
                    .Test( @"a((?R))*b", "aabb", "b", "aabb"),
                    //.Test( @"\(((?>[^()]+)|(?R))*\)", "(a(b)c)", "b"),
                new FeatureMatrixDetails( @"(?&name)", @"Recursive subpattern by name", (e, fm) => fm.Recursive_Name)
                    .Test( @"(?<n>a)(?&n)", "aa", "" ),
                new FeatureMatrixDetails( @"(?P>name)", @"Recursive subpattern by name", (e, fm) => fm.Recursive_PGtName)
                    .Test( @"(?P<n>a)(?P>n)", "aa", "" ),
                new FeatureMatrixDetails( @"(?…(grouplist))", @"Additionally return capturing groups", (e, fm) => fm.Recursive_ReturnGroups)
                    .Test( @"(?<a>A(?<b>.))(?&a(<b>))\k<b>", "ABACC", "ABACB"),
            ] ),

        new ( @"Conditionals",
            [
                new FeatureMatrixDetails( @"(?(number)…|…)", @"Conditionals by number, +number, -number", (e, fm) => fm.Conditional_BackrefByNumber)
                    .Test( @"(x)(?(1)y|z)", "xy", "bx"),
                new FeatureMatrixDetails( @"(?(name)…|…)", @"Conditional by name", (e, fm) => fm.Conditional_BackrefByName)
                    .Test( @"(?<n>x)(?(n)y|z)", "xy", "", "xy" )
                    .Test( @"(?P<n>x)(?(n)y|z)", "xy", "", "xy" ),
                new FeatureMatrixDetails( @"(?(pattern)…|…)", @"Conditional subpattern", (e, fm) => fm.Conditional_Pattern)
                    .Test( @"x(?(?=.z)y|z)", "xyz", "x", "xy" )
                    .Test( @"x(?((?=.z))y|z)", "xyz", "x", "xy" ),
                new FeatureMatrixDetails( @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", (e, fm) => fm.Conditional_PatternOrBackrefByName)
                    .Test( @"x(?(y).|z)", "xy", "x", "xy" ),
                new FeatureMatrixDetails( @"(?('name')…|…)", @"Conditional by name", (e, fm) => fm.Conditional_BackrefByName_Apos)
                    .Test( @"(?'n'x)(?('n')y|z)", "xy", "", "xy" ),
                new FeatureMatrixDetails( @"(?(<name>)…|…)", @"Conditional by name", (e, fm) => fm.Conditional_BackrefByName_LtGt)
                    .Test( @"(?<n>x)(?(<n>)y|z)", "xy", "", "xy" )
                    .Test( @"(?P<n>x)(?(<n>)y|z)", "xy", "", "xy" ),
                new FeatureMatrixDetails( @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", (e, fm) => fm.Conditional_R)
                    .Test( @"(?(R)a+|(?R)b)", "aaaab", "", "aaaab" ),
                new FeatureMatrixDetails( @"(?(R&name)…|…)", @"Recursive conditional by name", (e, fm) => fm.Conditional_RName)
                    .Test( @"(?<A>(?'B'abc(?(R)(?(R&A)1)(?(R&B)2)X|(?1)(?2)(?R))))", "abcabc1Xabc2XabcXabcabc", "" ),
                new FeatureMatrixDetails( @"(?(DEFINE)…|…)", @"Defining subpatterns", (e, fm) => fm.Conditional_DEFINE)
                    .Test( @"(?(DEFINE)(?<n>x.z))(?&n)", "xyz", "", "xyz" )
                    .Test( @"\g<x>(?(DEFINE)(?<x>x))", "x", "", "x" ),
                new FeatureMatrixDetails( @"(?(VERSION…)…|…)", @"Check version using 'VERSION=decimal' or 'VERSION>=decimal'", (e, fm) => fm.Conditional_VERSION)
                    .Test( @"(?(VERSION>=1)xyz|abc)", "xyz", "", "xyz" ),
            ] ),

        new ( @"Miscellaneous",
            [
                new FeatureMatrixDetails( @"(*verb)", @"Control verbs: (*verb), (*verb:…), (*:name)", (e, fm) => fm.ControlVerbs)
                    .Test( @"x(*ACCEPT)|y(*FAIL)", "x", "y", "x" )
                    .Test( @"(*UCP)^a", "a", "", "a" )
                    .Test( @"x(*SKIP)y", "xy", "xSKIPy", "xy" )
                    .Test( @"a(*FAIL)|b", "b", "a", "b" ),
                new FeatureMatrixDetails( @"(*…:…)", @"Script runs, such as (*atomic:…)", (e, fm) => fm.ScriptRuns)
                    .Test( @"(*atomic:x)", "x", "", "x" ),

                new FeatureMatrixDetails( @"(?Cn), (*func)", @"Callouts (custom functions)", (e, fm) => fm.Callouts ),
                new FeatureMatrixDetails( @"(?)", @"Empty construct", (e, fm) => fm.EmptyConstruct)
                    .Test( @"x(?)y", "xy", "x", "xy"),
                //new ( @"(? )", @"Empty construct", (e, fm) => fm.EmptyConstructX).Test( @"(?x)a(? )b", "ab", null ),
                new FeatureMatrixDetails( @"[]", @"Empty set (always fails)", (e, fm) => fm.EmptySet)
                    .Test( @"x[]?", "x", null, "x" ),
                new FeatureMatrixDetails( @"[^]", @"Any character, even newline", (e, fm) => fm.EmptySetAny)
                    .Test( @"a[^][^][^]b", "ax\r\nb", "ab", "ax\r\nb" ),
                new FeatureMatrixDetails( @"Unicode “.”", @"“.” matches Unicode characters, not just ASCII", (e, fm) => fm.Unicode_Class_Dot )
                    .Test( @"X.....Y", "XăîșțâY", null, "XăîșțâY" ),
                new FeatureMatrixDetails( @"Unicode “\w”", @"“\w” matches Unicode characters, not just ASCII", (e, fm) => fm.Unicode_Class_vW )
                    .Test( @"X\w\w\w\w\wY", "XăîșțâY", null, "XăîșțâY" )
                    .Test( @"(?u)X\w\w\w\w\wY", "XăîșțâY", null, "XăîșțâY" )
                    .Test( @"(?u:X\w\w\w\w\wY)", "XăîșțâY", null, "XăîșțâY" ),
                new FeatureMatrixDetails( @"[Unicode]", @"Support Unicode characters inside sets", (e, fm) => fm.InsideSets_Unicode )
                    .Test( @"X[é]Y", "XéY", null, "XéY" )
                    .Test( @"(?u)X[é]Y", "XéY", null, "XéY" )
                    .Test( @"(?u:X[é]Y)", "XéY", null, "XéY" ),
                new FeatureMatrixDetails( @"Surrogates", @"“.” matches surrogate pairs as one entity (no split)", (e, fm) => fm.Unicode_Class_Dot && fm.KeepSurrogatePairs )
                    .Test( @"X.Y", "X💕Y", null, "X💕Y" ),
                new FeatureMatrixDetails( @"é=É", @"Support Unicode case folding", (e, fm) => fm.UnicodeCaseFolding )
                    .IgnoreCase()
                    .Test( @"XéY", "XÉY", null, "XÉY" )
                    .Test( @"(?i)XéY", "XÉY", null, "XÉY" )
                    .Test( @"(?i:XéY)", "XÉY", null, "XÉY" ),
                new FeatureMatrixDetails( "Σσς", "Match letters that have multiple uppercase and lowercase variants", (e, fm) => fm.Σσς )
                    .IgnoreCase()
                    .Test( @"ΣΣΣ", "Σσς", null, "Σσς")
                    .Test( @"(?i)ΣΣΣ", "Σσς", null, "Σσς"),
                new FeatureMatrixDetails( "ß=ss", "Match “ß ↔ ss” (and “ß ↔ SS” in case-insensitive mode)", (e, fm) => fm.ßSS )
                    .IgnoreCase()
                    .Test( @"aßb", "aSSb", "aSb", "aSSb")
                    .Test( @"(?i)aßb", "aSSb", "aSb", "aSSb"),
                /*
                 * find defect
                new FeatureMatrixDetails( "ß=S", "", (e, fm) => false )
                    .IgnoreCase()
                    .Test( @"ß", "s S", null)
                    .Test( @"(?i)ß", "s S", null),
                */

                new FeatureMatrixDetails( @"Fuzzy matching", @"Approximate matching using special patterns or parameters", (e, fm) => fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal || fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed || fm.FuzzyMatchingParams)
                    .Test( @"(test){i}", "teXst", null, "teXst" )
                    .Test( @"(test){+1}", "teXst", null, "teXst" )
                    .Test( @"\(test\)\{+1\}", "teXst", null, "teXst" )
                    .Test( (e, fm) => fm.FuzzyMatchingParams ),
                new FeatureMatrixDetails( "No hang, no ReDoS", "No catastrophic infinite matching, no timeout", (e, fm) => fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Accept )
                    .Test( (e, fm) => CheckCatastrophicPattern( e, fm ) == CatastrophicBacktrackingResultEnum.Passed ),
                new FeatureMatrixDetails( "Reject ReDoS", "Give error on possible ReDoS", (e, fm) => fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Reject )
                    .Test( (e, fm) => CheckCatastrophicPattern( e, fm ) == CatastrophicBacktrackingResultEnum.Error ),
            ] ),

        new ( @"Specific extensions",
            [
                new FeatureMatrixDetails( @"[:class:]", @"Character class outside sets", (e, fm) => fm.Ext_Class_Name)
                    .Test( @"[:alpha:]", "X", null, "X" ),
                new FeatureMatrixDetails( @"(?@…)", @"Capturing group", (e, fm) => fm.Ext_NamedGroup_AtApos || fm.Ext_NamedGroup_AtLtGt || fm.CapturingGroup)
                    .Test( @"(?@<n>x)", "x", "", "x" ),
                new FeatureMatrixDetails( @"\!c, \!\c", @"Complement (“not ‘c’”); 'c' — character", (e, fm) => fm.Ext_Class_Not)
                    .Test( @"\!x", "a", null, "a" ),
                new FeatureMatrixDetails( @"![comment]", @"Inline comment", (e, fm) => fm.Ext_AnomalousInlineComments)
                    .Test( @"a![comment]b", "ab", "a!cb", "ab" ),
                new FeatureMatrixDetails( @"_", @"Universal wildcard (any character including newlines)", (e, fm) => fm.Ext_UniversalWildcard)
                    .Test( @"a__b_", "a\r\nbc", null, "a\r\nbc" ),
                new FeatureMatrixDetails( @"&", @"Intersection (both patterns must match)", (e, fm) => fm.Ext_Operator_Intersection)
                    .Test( @"a.+&.+b", "axb", null, "axb" ),
                new FeatureMatrixDetails( @"~(…)", @"Complement (pattern must not match)", (e, fm) => fm.Ext_Operator_Complement)
                    .Test( @"ab~(x)c", "abc", null, "abc" ),
                new FeatureMatrixDetails( @"", @"Get all captures matched by group", (e, fm) => ! e.Capabilities.HasFlag( RegExpressLibrary.RegexEngineCapabilityEnum.NoGroups) && ! e.Capabilities.HasFlag( RegExpressLibrary.RegexEngineCapabilityEnum.NoCaptures))
                    .Test( (e, fm) =>
                    {
                        e.SetCollectCaptures( true );

                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(.)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First();
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray();
                            if( groups.Length != 2) return false; // including default group

                            RegExpressLibrary.Matches.IGroup group = groups[1];
                            if( !group.Success) return false;

                            var captures = group.Captures.ToArray();
                            if( captures.Length != 2) return false;

                            if( captures[0].Value != "a" ) return false;
                            if( captures[1].Value != "b" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    })
                    .Test( (e, fm) =>
                    {
                        e.SetCollectCaptures( true );

                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"\(.\)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First();
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray();
                            if( groups.Length != 2) return false; // including default group

                            RegExpressLibrary.Matches.IGroup group = groups[1];
                            if( !group.Success) return false;

                            var captures = group.Captures.ToArray();
                            if( captures.Length != 2) return false;

                            if( captures[0].Value != "a" ) return false;
                            if( captures[1].Value != "b" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    })
                    .Test( (e, fm) =>
                    {
                        e.SetCollectCaptures( true );

                        try
                        {
                            // Oniguruma's way 

                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(?@.)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First();
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray();
                            if( groups.Length != 2) return false; // including default group

                            RegExpressLibrary.Matches.IGroup group = groups[1];
                            if( !group.Success) return false;

                            var captures = group.Captures.ToArray();
                            if( captures.Length != 2) return false;

                            if( captures[0].Value != "a" ) return false;
                            if( captures[1].Value != "b" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    }),
                new FeatureMatrixDetails( @"", @"Also support alternative syntax", (e, fm) => fm.Ext_AlternativeLanguage),
            ]),

        ];
}
