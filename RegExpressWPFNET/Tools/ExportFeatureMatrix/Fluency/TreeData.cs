using DocumentFormat.OpenXml.Drawing;
using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

static class TreeData
{
    internal static readonly Tree Tree = new( );

    static TreeData( )
    {
        Tree

            .Category( @"General" )

                .Flag( @"(…)", @"Grouping constructs", ( e, fm ) => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"(x)", "x", null, "x" )
                .Flag( @"\(…\)", @"Grouping constructs", ( e, fm ) => fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"\(x\)", "x", null, "x" )

                //.Direct( @"(…)", @"Grouping constructs" )
                //    .On( ( e, fm ) => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal, ColourEnum.Green, "(…)" )
                //        .Test( @"(x)", "x", null, "x" )
                //    .On( ( e, fm ) => fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal.Backslashed, ColourEnum.Green, @"\(…\)" )
                //        .Test( @"\(x\)", "x", null, "x" )

                .Flag( @"[…]", @"Character group", ( e, fm ) => fm.Brackets )
                    .Test( @"[x]", "x", null, "x" )
                .Flag( @"(?[…])", @"Character group", ( e, fm ) => fm.ExtendedBrackets )
                    .Test( @"a(?[[x]])b", "axb", null, "axb" )

                .Flag( @"|", @"Alternation", ( e, fm ) => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"x|y", "y", null, "y" )
                .Flag( @"\|", @"Alternation", ( e, fm ) => fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"x\|y", "y", null, "y" )
                .Flag( @"new line (\n)", @"Alternatives on separate lines", ( e, fm ) => fm.AlternationOnSeparateLines )
                    .Test( "x\ny", "y", null, "y" )

                .Flag( @"#comment", @"Comment", ( e, fm ) => fm.XModeComments )
                    .IgnorePatternWhitespace( )
                    .Test( "a#comment", "a", null, "a" )
                    .Test( "(?x)a#comment", "a", null, "a" )
                    .Test( "a#comment\nb", "ab", "b", "ab" ) // ('\n' is required by Hyperscan)
                    .Test( "(?x)a#comment\nb", "ab", "b", "ab" ) // ('\n' is required by Hyperscan)
                .Flag( @"(?#comment)", @"Inline comment", ( e, fm ) => fm.InlineComments )
                    .Test( @"a(?#comment)b", "ab", "a#commentb", "ab" )
                    .Test( @"a\(?#comment\)b", "ab", "a", "ab" )
                .Flag( @"[#comment]", @"Comment inside […]", ( e, fm ) => fm.InsideSets_XModeComments )
                    .IgnorePatternWhitespace( )
                    .Test( "a[b#comment\nz]y", "azy", "acy", "azy" )
                    .Test( "(?x)a[b#comment\nz]y", "azy", "acy", "azy" )
                    .Test( "(?xx)a[b#comment\nz]y", "azy", "acy", "azy" )

                .Flag( @"(?flags)", @"Inline options", ( e, fm ) => fm.Flags ).IgnoreCase( false )
                    .Test( @"(?i)x", "X", null, "X" )
                .Flag( @"(?flags:…)", @"Inline scoped options", ( e, fm ) => fm.ScopedFlags )
                    .IgnoreCase( false )
                    .Test( @"a(?i:x)b", "aXb", null, "aXb" )
                .Flag( @"(?^flags)", @"Inline fresh options", ( e, fm ) => fm.CircumflexFlags )
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^)x", "x", "X", "x" )
                .Flag( @"(?^flags:…)", @"Inline scoped fresh options", ( e, fm ) => fm.ScopedCircumflexFlags )
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^:x)", "x", "X", "x" )
                .Flag( @"(?x)", @"Allow 'x' flag", ( e, fm ) => fm.XFlag )
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)a b", "ab", null, "ab" )
                .Flag( @"(?xx)", @"Allow 'xx' flag", ( e, fm ) => fm.XXFlag )
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)[a b](?xx)[a b]", " a", "a ", " a" )

                .Flag( @"\Q…\E", @"Literal", ( e, fm ) => fm.Literal_QE )
                    .Test( @"a\Qx\E", "ax", "aQxE", "ax" )
                .Flag( @"[\Q…\E]", @"Literal inside […]", ( e, fm ) => fm.InsideSets_Literal_QE )
                    .Test( @"[\Qx\E]", "x", "Q", "x" )
                .Flag( @"[\q{…}]", @"Literal inside […]", ( e, fm ) => fm.InsideSets_Literal_qBrace )
                    .Test( @"a[\q{xy}]b", "axyb", "axb", "axyb" )

            .Category( @"Quantifiers" )

                .Flag( @"*", @"Zero or more times", ( e, fm ) => fm.Quantifier_Asterisk )
                    .Test( @"xy*", "x", null, "x" )
                .Flag( @"+", @"One or more times", ( e, fm ) => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"xy+z", "xyyz", null, "xyyz" )
                .Flag( @"\+", @"One or more times", ( e, fm ) => fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"xy\+", "xyy", null, "xyy" )
                .Flag( @"?", @"Zero or one time", ( e, fm ) => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"xy?", "x", null, "x" )
                .Flag( @"\?", @"Zero or one time", ( e, fm ) => fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"xy\?", "x", null, "x" )
                .Flag( @"{n,m}", @"Between n and m times: {n}, {n,}, {n,m}", ( e, fm ) => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"x{2,3}", "xx", null, "xx" )
                .Flag( @"\{n,m\}", @"Between n and m times: \{n\}, \{n,\}, \{n,m\}", ( e, fm ) => fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"x\{2,3\}", "xx", null, "xx" )
                //.Flag( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", (e, fm) => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.Both ) // TODO
                //.Flag( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", (e, fm) => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.XModeOnly ) // TODO
                .Flag( @"{,m}, \{,m\}", @"Equivalent to {0,m} or \{0,m\}", ( e, fm ) => fm.Quantifier_LowAbbrev )
                    .Test( @"x{,3}", "xxx", null, "xxx" )
                    .Test( @"x\{,3\}", "xxx", null, "xxx" )
                .Flag( @"*?, +?, ??, {}?", @"Lazy (non-greedy) quantifiers — match as little as possible",
                        ( e, fm ) => fm.Quantifier_Asterisk && fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal && fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal && fm.Quantifier_Lazy )
                    .Test( @".+?A.*?AA??", "AAAAAA", null, "AAA" )
                .Flag( @"*+, ++, ?+, {}+", @"Possessive quantifiers — match without backtracking",
                        ( e, fm ) => fm.Quantifier_Plus != FeatureMatrix.PunctuationEnum.None && fm.Quantifier_Possessive )
                    .Test( @"Xa++.Y", "XaaabY", "XaaaaY", "XaaabY" )
                    .Test( @"Xa\+\+.Y", "XaaabY", "XaaaaY", "XaaabY" )

            .Category( @"Escapes" )

                .Flag( @"\a", @"Bell, \u0007", ( e, fm ) => fm.Esc_a )
                    .Test( @"\a", "\u0007", null, "\u0007" )
                .Flag( @"\b", @"Backspace, \u0008", ( e, fm ) => fm.Esc_b )
                    .Test( @"x\by", "x\u0008y", null, "x\u0008y" )
                .Flag( @"\e", @"Escape, \u001B", ( e, fm ) => fm.Esc_e )
                    .Test( @"\e", "\u001B", null, "\u001B" )
                .Flag( @"\f", @"Form feed, \u000C", ( e, fm ) => fm.Esc_f )
                    .Test( @"\f", "\u000C", null, "\u000C" )
                .Flag( @"\n", @"New line, \u000A", ( e, fm ) => fm.Esc_n )
                    .Test( @"\n", "\u000A", null, "\u000A" )
                .Flag( @"\r", @"Carriage return, \u000D", ( e, fm ) => fm.Esc_r )
                    .Test( @"\r", "\u000D", null, "\u000D" )
                .Flag( @"\t", @"Tab, \u0009", ( e, fm ) => fm.Esc_t )
                    .Test( @"\t", "\u0009", null, "\u0009" )
                .Flag( @"\v", @"Vertical tab, \u000B", ( e, fm ) => fm.Esc_v )
                    .Test( @"\v", "\u000B", "\f", "\u000B" )
                .Flag( @"\1..\7", @"Octal, one digit", ( e, fm ) => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 )
                    .Test( @"\3", "\u0003", null, "\u0003" )
                .Flag( @"\nn, \nnn", @"Octal, two or three digits", ( e, fm ) => fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 || fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 )
                    .Test( @"\11\101", "\u0009A", null, "\u0009A" )
                    .Test( @"\11\000\101\000", "\u0009A", null, "\u0009A" ) // ('\000' for Oniguruma)
                .Flag( @"\0nnn", @"Octal, up to three digits after '\0'", ( e, fm ) => fm.Esc_Octal0_1_3 )
                    .Test( @"\03\011\0011", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                    .Test( @"\03\000\011\000\0011\000", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                .Flag( @"\o{nn…}", @"Octal", ( e, fm ) => fm.Esc_oBrace )
                    .Test( @"\o{11}", "\u0009", null, "\u0009" )
                .Flag( @"\xXX", @"Hexadecimal code, two digits", ( e, fm ) => fm.Esc_x2 )
                    .Test( @"\x09", "\u0009", null, "\u0009" )
                    .Test( @"\x09\x00", "\u0009", null, "\u0009" )
                .Flag( @"\x{XX…}", @"Hexadecimal code", ( e, fm ) => fm.Esc_xBrace )
                    .Test( @"\x{0009}", "\u0009", null, "\u0009" )
                .Flag( @"\uXXXX", @"Hexadecimal code, four digits", ( e, fm ) => fm.Esc_u4 )
                    .Test( @"\u0009", "\u0009", null, "\u0009" )
                .Flag( @"\UXXXXXXXX", @"Hexadecimal code, eight digits", ( e, fm ) => fm.Esc_U8 )
                    .Test( @"\U00000009", "\u0009", null, "\u0009" )
                .Flag( @"\u{XX…}", @"Hexadecimal code", ( e, fm ) => fm.Esc_uBrace )
                    .Test( @"\u{0009}", "\u0009", null, "\u0009" )
                .Flag( @"\U{XX…}", @"Hexadecimal code", ( e, fm ) => fm.Esc_UBrace )
                    .Test( @"\U{0009}", "\u0009", null, "\u0009" )
                .Flag( @"\cC", @"Control character", ( e, fm ) => fm.Esc_c1 )
                    .Test( @"\cM", "\r", null, "\r" )
                .Flag( @"\CC", @"Control character", ( e, fm ) => fm.Esc_C1 )
                    .Test( @"\CM", "\r", null, "\r" )
                .Flag( @"\C-C", @"Control character", ( e, fm ) => fm.Esc_CMinus )
                    .Test( @"\C-M", "\r", "null", "\r" )
                .Flag( @"\N{…}", @"Unicode name or 'U+code'", ( e, fm ) => fm.Esc_NBrace )
                    .Test( @"\N{COMMA}", ",", null, "," )
                    .Test( @"\N{comma}", ",", null, "," )
                    .Test( @"\N{LATIN CAPITAL LETTER A}", "A", null, "A" )
                    .Test( @"\N{U+0061}", "a", null, "a" )
                .Flag( @"\any", @"Generic escape", ( e, fm ) => fm.GenericEscape )
                    .Test( @"a\j", @"aj", @"a\j", "aj" )

            .Category( @"Escapes inside sets" )

                .Flag( @"[\a]", @"Bell, \u0007", ( e, fm ) => fm.InsideSets_Esc_a )
                    .Test( @"[\a]", "\u0007", null, "\u0007" )
                .Flag( @"[\b]", @"Backspace, \u0008", ( e, fm ) => fm.InsideSets_Esc_b )
                    .Test( @"[\b]", "\u0008", null, "\u0008" )
                .Flag( @"[\e]", @"Escape, \u001B", ( e, fm ) => fm.InsideSets_Esc_e )
                    .Test( @"[\e]", "\u001B", null, "\u001B" )
                .Flag( @"[\f]", @"Form feed, \u000C", ( e, fm ) => fm.InsideSets_Esc_f )
                    .Test( @"[\f]", "\u000C", null, "\u000C" )
                .Flag( @"[\n]", @"New line, \u000A", ( e, fm ) => fm.InsideSets_Esc_n )
                    .Test( @"[\n]", "\u000A", null, "\u000A" )
                .Flag( @"[\r]", @"Carriage return, \u000D", ( e, fm ) => fm.InsideSets_Esc_r )
                    .Test( @"[\r]", "\u000D", null, "\u000D" )
                .Flag( @"[\t]", @"Tab, \u0009", ( e, fm ) => fm.InsideSets_Esc_t )
                    .Test( @"[\t]", "\u0009", null, "\u0009" )
                .Flag( @"[\v]", @"Vertical tab, \u000B", ( e, fm ) => fm.InsideSets_Esc_v )
                    .Test( @"[\v]", "\u000B", "\f", "\u000B" )
                .Flag( @"[\1..\7]", @"Octal, one digit", ( e, fm ) => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 )
                    .Test( @"[\3]", "\u0003", null, "\u0003" )
                    .Test( @"[\3\0]", "\u0003", null, "\u0003" )
                .Flag( @"[\nn], [\nnn]", @"Octal, two or three digits", ( e, fm ) => fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 || fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 )
                    .Test( @"[\11][\101]", "\u0009A", null, "\u0009A" )
                    .Test( @"[\11\000][\101\000]", "\u0009A", null, "\u0009A" ) // ('\000' for Oniguruma)
                .Flag( @"[\0nnn]", @"Octal, up to three digits after '\0'", ( e, fm ) => fm.InsideSets_Esc_Octal0_1_3 )
                    .Test( @"[\03][\011][\0011]", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                    .Test( @"[\03\000][\011\000][\0011\000]", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                .Flag( @"[\o{nn…}]", @"Octal", ( e, fm ) => fm.InsideSets_Esc_oBrace )
                    .Test( @"[\o{11}]", "\u0009", null, "\u0009" )
                .Flag( @"[\xXX]", @"Hexadecimal code, two digits", ( e, fm ) => fm.InsideSets_Esc_x2 )
                    .Test( @"[\x09]", "\u0009", null, "\u0009" )
                    .Test( @"[\x09\x00]", "\u0009", null, "\u0009" )
                .Flag( @"[\x{XX…}]", @"Hexadecimal code", ( e, fm ) => fm.InsideSets_Esc_xBrace )
                    .Test( @"[\x{0009}]", "\u0009", null, "\u0009" )
                .Flag( @"[\uXXXX]", @"Hexadecimal code, four digits", ( e, fm ) => fm.InsideSets_Esc_u4 )
                    .Test( @"[\u0009]", "\u0009", null, "\u0009" )
                .Flag( @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", ( e, fm ) => fm.InsideSets_Esc_U8 )
                    .Test( @"[\U00000009]", "\u0009", "x", "\u0009" )
                .Flag( @"[\u{XX…}]", @"Hexadecimal code", ( e, fm ) => fm.InsideSets_Esc_uBrace )
                    .Test( @"[\u{0009}]", "\u0009", "X", "\u0009" )
                .Flag( @"[\U{XX…}]", @"Hexadecimal code", ( e, fm ) => fm.InsideSets_Esc_UBrace )
                    .Test( @"[\U{0009}]", "\u0009", "x", "\u0009" )
                .Flag( @"[\cC]", @"Control character", ( e, fm ) => fm.InsideSets_Esc_c1 )
                    .Test( @"[\cM]", "\r", null, "\r" )
                .Flag( @"[\CC]", @"Control character", ( e, fm ) => fm.InsideSets_Esc_C1 )
                    .Test( @"[\CM]", "\r", null, "\r" )
                .Flag( @"[\C-C]", @"Control character ", ( e, fm ) => fm.InsideSets_Esc_CMinus )
                    .Test( @"[\C-M]", "\r", null, "\r" )
                .Flag( @"[\N{…}]", @"Unicode name or 'U+code'", ( e, fm ) => fm.InsideSets_Esc_NBrace )
                    .Test( @"[\N{COMMA}]", ",", "M", "," ) // (see also '\N' -- any except '\n')
                    .Test( @"[\N{comma}]", ",", "m", "," )
                    .Test( @"[\N{U+0061}]", "a", "U", "a" )
                .Flag( @"[\any]", @"Generic escape", ( e, fm ) => fm.InsideSets_GenericEscape )
                    .Test( @"[\j]", "j", @"\" )

            .Category( @"Classes" )

                .Flag( @".", @"Any, including or excepting newline (\n)", ( e, fm ) => fm.Class_Dot )
                    .Test( @".", "x", null, "x" )
                .Flag( @"\C", @"Single byte", ( e, fm ) => fm.Class_Cbyte )
                    .Test( @"\C\C", "î", null, "î" )
                .Flag( @"\C", @"Single code point", ( e, fm ) => fm.Class_Ccp )
                    .Test( @"\C\C", "îî", "î", "îî" )
                .Flag( @"\d, \D", @"Digit", ( e, fm ) => fm.Class_dD )
                    .Test( @"\d\D", "9x", null, "9x" )
                .Flag( @"\h, \H", @"Hexadecimal character", ( e, fm ) => fm.Class_hHhexa )
                    .Test( @"\h\H", "Ax", null, "Ax" )
                .Flag( @"\h, \H", @"Horizontal space", ( e, fm ) => fm.Class_hHhorspace )
                    .Test( @"\h\H", " x", null, " x" )
                .Flag( @"\l, \L", @"Lowercase character", ( e, fm ) => fm.Class_lL )
                    .Test( @"\l\L", "xX", null, "xX" )
                .Flag( @"\N", @"Any except '\n'", ( e, fm ) => fm.Class_N )
                    .Test( @"\N", "a", "\n", "a" )
                .Flag( @"\O", @"Any", ( e, fm ) => fm.Class_O )
                    .Test( @"\O", "a", null, "a" )
                .Flag( @"\R", @"Line break", ( e, fm ) => fm.Class_R )
                    .Test( @"a\Rb", "a\r\nb", null, "a\r\nb" )
                .Flag( @"\s, \S", @"Space", ( e, fm ) => fm.Class_sS )
                    .Test( @"\s\S", " x", null, " x" )
                .Flag( @"\sx, \Sx", @"Syntax group; 'x' — group", ( e, fm ) => fm.Class_sSx )
                    .Test( @"\ss", " ", null, " " )
                .Flag( @"\u, \U", @"Uppercase character", ( e, fm ) => fm.Class_uU )
                    .Test( @"\u\U", "Xx", null, "Xx" )
                .Flag( @"\v, \V", @"Vertical space", ( e, fm ) => fm.Class_vV )
                    .Test( @"\v\v", "\r\f", null, "\r\f" )
                .Flag( @"\w, \W", @"Word character", ( e, fm ) => fm.Class_wW )
                    .Test( @"\w\w\w", "xyz", null, "xyz" )
                .Flag( @"\X", @"Extended grapheme cluster", ( e, fm ) => fm.Class_X )
                    .Test( @"\X", "a", null, "a" )
                .Flag( @"\pX, \PX", @"Unicode property, X — short property name", ( e, fm ) => fm.Class_pP )
                    .Test( @"\pL\PL", "x9", null, "x9" )
                .Flag( @"\p{…}, \P{…}", @"Unicode property", ( e, fm ) => fm.Class_pPBrace )
                    .Test( @"\p{L}\P{L}", "x9", null, "x9" )

            .Category( @"Classes inside sets" )

                .Flag( @"[\d], [\D]", @"Digit", ( e, fm ) => fm.InsideSets_Class_dD )
                    .Test( @"a[\d]", "a9", null, "a9" )
                .Flag( @"[\h], [\H]", @"Hexadecimal character", ( e, fm ) => fm.InsideSets_Class_hHhexa )
                    .Test( @"[\h][\H]", "Ax", null, "Ax" )
                .Flag( @"[\h], [\H]", @"Horizontal space", ( e, fm ) => fm.InsideSets_Class_hHhorspace )
                    .Test( @"[\h][\H]", " x", null, " x" )
                .Flag( @"[\l], [\L]", @"Lowercase character", ( e, fm ) => fm.InsideSets_Class_lL )
                    .Test( @"[\l][\L]", "xX", null, "xX" )
                .Flag( @"[\R]", @"Line break", ( e, fm ) => fm.InsideSets_Class_R )
                    .Test( @"a[\R]b", "a\r\nb", null, "a\r\nb" )
                .Flag( @"[\s], [\S]", @"Space", ( e, fm ) => fm.InsideSets_Class_sS )
                    .Test( @"a[\s][\S]x", "a 9x", null, "a 9x" )
                .Flag( @"[\sx], [\Sx]", @"Syntax group; 'x' — group", ( e, fm ) => fm.InsideSets_Class_sSx )
                    .Test( @"[\ss]", " ", "s", " " )
                .Flag( @"[\u], [\U]", @"Uppercase character", ( e, fm ) => fm.InsideSets_Class_uU )
                    .Test( @"[\u][\U]", "Xx", null, "Xx" )
                .Flag( @"[\v], [\V]", @"Vertical space", ( e, fm ) => fm.InsideSets_Class_vV )
                    .Test( @"[\v][\v]", "\r\f", null, "\r\f" )
                .Flag( @"[\w], [\W]", @"Word character", ( e, fm ) => fm.InsideSets_Class_wW )
                    .Test( @"a[\w][\w][\w]", "axyz", null, "axyz" )
                .Flag( @"[\X]", @"Extended grapheme cluster", ( e, fm ) => fm.InsideSets_Class_X )
                    .Test( @"[\X]", "a", null, "a" )
                .Flag( @"[\pX], [\PX]", @"Unicode property, X — short property name", ( e, fm ) => fm.InsideSets_Class_pP )
                    .Test( @"[\pL][\PL]", "x9", null, "x9" )
                .Flag( @"[\p{…}], [\P{…}]", @"Unicode property", ( e, fm ) => fm.InsideSets_Class_pPBrace )
                    .Test( @"[\p{L}][\P{L}]", "x9", null, "x9" )
                .Flag( @"[[:class:]]", @"Character class", ( e, fm ) => fm.InsideSets_Class_Name )
                    .Test( @"[[:alpha:]]", "X", null, "X" )
                .Flag( @"[[=elem=]]", @"Equivalence", ( e, fm ) => fm.InsideSets_Equivalence )
                    .Test( @"[[=a=]][[=a=]]", "aA", null, "aA" ) // 'Á' not matched by STL regex.
                .Flag( @"[[.elem.]]", @"Collating symbol", ( e, fm ) => fm.InsideSets_Collating )
                    .Test( @"a[[.ch.]]x", "achx", null, "achx" )
                    .Test( @"a[[.comma.]]b", "a,b", null, "a,b" ) // STL seems to have a defect.

            .Category( @"Operators inside sets" )

                .Flag( @"[[…] op […]]", @"Using operators for nested groups", ( e, fm ) => fm.InsideSets_Operators )
                    .Test( @"[[ab]&[bc]]", "b", "a&c", "b" )
                    .Test( @"[[ab]&&[bc]]", "b", "a&c", "b" )
                .Flag( @"(?[[…] op […]])", @"Using operators for nested groups", ( e, fm ) => fm.InsideSets_OperatorsExtended )
                    .Test( @"(?[[ab]&[bc]])", "b", "a&c", "b" )
                .Flag( @"[…] & […]", @"Intersection", ( e, fm ) => fm.InsideSets_Operator_Ampersand )
                    .Test( @"[[ab]&[bc]]", "b", "a", "b" )
                    .Test( @"(?[[ab]&[bc]])", "b", "a", "b" )
                .Flag( @"[…] + […]", @"Union", ( e, fm ) => fm.InsideSets_Operator_Plus )
                    .Test( @"[[a]+[b]]", "a", "+", "a" )
                    .Test( @"(?[[a]+[b]])", "a", "+", "a" )
                .Flag( @"[…] | […]", @"Union", ( e, fm ) => fm.InsideSets_Operator_VerticalLine )
                    .Test( @"[[a]|[b]]", "b", "|", "b" )
                    .Test( @"(?[[a]|[b]])", "b", "|", "b" )
                .Flag( @"[…] - […]", @"Subtraction", ( e, fm ) => fm.InsideSets_Operator_Minus )
                    .Test( @"[[ab]-[b]]", "a", "b", "a" )
                    .Test( @"(?[[ab]-[b]])", "a", "b", "a" )
                .Flag( @"[…] ^ […]", @"Symmetric difference", ( e, fm ) => fm.InsideSets_Operator_Circumflex )
                    .Test( @"[[ab]^[bc]]", "c", "^", "c" )
                    .Test( @"(?[[ab]^[bc]])", "c", "^", "c" )
                .Flag( @"![…]", @"Complement", ( e, fm ) => fm.InsideSets_Operator_Exclamation )
                    .Test( @"a(?[![b]])y", "axy", null, "axy" )
                    .Test( @"a[![b]]y", "axy", null, "axy" )
                .Flag( @"[…] && […]", @"Intersection", ( e, fm ) => fm.InsideSets_Operator_DoubleAmpersand )
                    .Test( @"[[ab]&&[bc]]", "b", "a&c", "b" )
                .Flag( @"[…] || […]", @"Union", ( e, fm ) => fm.InsideSets_Operator_DoubleVerticalLine )
                    .Test( @"[[a]||[b]]", "b", "]|[", "b" )
                .Flag( @"[…] -- […]", @"Difference", ( e, fm ) => fm.InsideSets_Operator_DoubleMinus )
                    .Test( @"[[ab]--[b]]", "a", "][-b", "a" )
                .Flag( @"[…] ~~ […]", @"Symmetric difference", ( e, fm ) => fm.InsideSets_Operator_DoubleTilde )
                    .Test( @"[[ab]~~[bca]]", "c", "ab][~", "c" )

            .Category( @"Anchors" )

                .Flag( @"^", @"Beginning of string or line", ( e, fm ) => fm.Anchor_Circumflex )
                    .Test( @"^x", "x", null, "x" )
                .Flag( @"$", @"End, or before '\n' at end of string or line", ( e, fm ) => fm.Anchor_Dollar )
                    .Test( @"x$", "x", null, "x" )
                .Flag( @"\A", @"Start of string", ( e, fm ) => fm.Anchor_A )
                    .Test( @"\Ax", "x", null, "x" )
                .Flag( @"\Z", @"End of string, or before '\n' at end of string", ( e, fm ) => fm.Anchor_Z == FeatureMatrix.AnchorZModeEnum.Correct )
                    .Test( @"x\Z", "x\n", "xZ", "x" )
                .Flag( @"\Z", @"End of string, same as '\z'", ( e, fm ) => fm.Anchor_Z == FeatureMatrix.AnchorZModeEnum.Compatible )
                    .Test( @"x\Z", "x", "xZ x\n", "x" )
                .Flag( @"\z", @"End of string", ( e, fm ) => fm.Anchor_z )
                    .Test( @"x\z", "x", "xz", "x" )
                .Flag( @"\G", @"start of string or end of previous match", ( e, fm ) => fm.Anchor_G )
                    .Test( @"\Gx", "x", null, "x" )
                .Flag( @"\b, \B", @"Boundary between \w and \W", ( e, fm ) => fm.Anchor_bB )
                    .Test( @"\bx", "y x", null, "x" )
                .Flag( @"\b{g}", @"Unicode extended grapheme cluster boundary", ( e, fm ) => fm.Anchor_bg )
                    .Test( @"\b{g}x", "y x", null, "x" )
                .Flag( @"\b{…}, \B{…}", @"Typed boundary", ( e, fm ) => fm.Anchor_bBBrace )
                    .Test( @"\b{wb}x", "y x", null, "x" )
                    .Test( @"\b{start}x", "y x", null, "x" )
                    .Test( @"\b{start-half}x", "y x", null, "x" )
                .Flag( @"[[:<:]], [[:>:]]", @"POSIX word boundary", ( e, fm ) => fm.Anchor_PosixWB )
                    .Test( @"[[:<:]]a[[:>:]]", "a", null, "a" )
                .Flag( @"\m, \M", @"Start of word, end of word", ( e, fm ) => fm.Anchor_mM )
                    .Test( @"\mword\M", "some word here", null, "word" )
                .Flag( @"\<, \>", @"Start of word, end of word", ( e, fm ) => fm.Anchor_LtGt )
                    .Test( @"\<word\>", "some word here", null, "word" )
                .Flag( @"\`, \'", @"Start of string, end of string", ( e, fm ) => fm.Anchor_GraveApos )
                    .Test( @"\`x\'", "x", null, "x" )
                .Flag( @"\y, \Y", @"Boundary between graphemes", ( e, fm ) => fm.Anchor_yY )
                    .Test( @"a\yb", "ab", null, "ab" )
                .Flag( @"\K", @"Keep the stuff left of the \K", ( e, fm ) => fm.Anchor_K )
                    .Test( @"a\Kb", "ab", "aKb", "b" )

            .Category( @"Named groups, subroutines and backreferences" )

                .Flag( @"(?'name'…)", @"Named group", ( e, fm ) => fm.NamedGroup_Apos )
                    .Test( @"a(?'n'x)b", "axb", null, "axb" )
                    .Test( @"a\(?'n'x\)b", "axb", null, "axb" )
                .Flag( @"(?<name>…)", @"Named group", ( e, fm ) => fm.NamedGroup_LtGt )
                    .Test( @"a(?<n>x)b", "axb", null, "axb" )
                    .Test( @"a\(?<n>x\)b", "axb", null, "axb" )
                .Flag( @"(?P<name>…)", @"Named group", ( e, fm ) => fm.NamedGroup_PLtGt )
                    .Test( @"a(?P<n>x)b", "axb", null, "axb" )
                    .Test( @"a\(?P<n>x\)b", "x", null, "axb" )
                .Flag( @"(?<name2-name1>…)", @"Balancing group", ( e, fm ) => ( fm.NamedGroup_Apos || fm.NamedGroup_LtGt || fm.NamedGroup_PLtGt ) && fm.BalancingGroup )
                    .Test( ( e, fm ) =>
                    {
                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(?<left>left).*(?<right-left>right)", "leftXYZright" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            RegExpressLibrary.Matches.IGroup? g1 = match.Groups.Skip( 1 ).FirstOrDefault( );
                            if( g1 == null ) return false;
                            if( g1.Success ) return false;

                            RegExpressLibrary.Matches.IGroup? g2 = match.Groups.Skip( 2 ).FirstOrDefault( );
                            if( g2 == null ) return false;
                            if( !g2.Success ) return false;

                            if( g2.Value != "XYZ" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    } )
                .Flag( @"Duplicate names", @"Allow duplicate group names", ( e, fm ) => fm.DuplicateGroupName )
                    .Test( @"(?<a>x)|(?<a>y)", "y", "z", "y" )
                    .Test( @"\(?<a>x\)|\(?<a>y\)", "y", "z", "y" )
                    .Test( @"(?P<a>x)|(?P<a>y)", "y", "z", "y" )
                .Flag( @"\1, \2, …, \9", @"Backreferences", ( e, fm ) => fm.Backref_Num == FeatureMatrix.BackrefEnum.OneDigit || fm.Backref_Num == FeatureMatrix.BackrefEnum.Any )
                    .Test( @"(x)\1", "xx", "x", "xx" )
                    .Test( @"\(x\)\1", "xx", "x", "xx" )
                .Flag( @"\nnn", @"Backreference, two or more digits", ( e, fm ) => fm.Backref_Num == FeatureMatrix.BackrefEnum.Any )
                    .Test( @"(x)(x)(x)(x)(x)(x)(x)(x)(x)(y)\10", "xxxxxxxxxyy", "xxxxxxxxxy\x10", "xxxxxxxxxyy" )
                    .Test( @"\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(y\)\10", "xxxxxxxxxyy", "xxxxxxxxxy\x10", "xxxxxxxxxyy" )
                .Flag( @"\k'name'", @"Backreference by name", ( e, fm ) => fm.Backref_kApos )
                    .Test( @"(?'n'x)\k'n'", "xx", null, "xx" )
                .Flag( @"\k<name>", @"Backreference by name", ( e, fm ) => fm.Backref_kLtGt )
                    .Test( @"(?<n>x)\k<n>", "xx", null, "xx" )
                .Flag( @"\k{name}", @"Backreference by name", ( e, fm ) => fm.Backref_kBrace )
                    .Test( @"(?<n>x)\k{n}", "xx", null, "xx" )
                .Flag( @"\kn", @"Backreference \k1, \k2, …", ( e, fm ) => fm.Backref_kNum )
                    .Test( @"(?<n>x)\k1", "xx", null, "xx" )
                .Flag( @"\k-n", @"Relative backreference \k-1, \k-2, …", ( e, fm ) => fm.Backref_kNegNum )
                    .Test( @"(?<n>x)\k-1", "xx", null, "xx" )
                .Flag( @"\g'…'", @"Backreference by name", ( e, fm ) => fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(?'n'.)\g'n'", "aa", "ab", "aa" )
                .Flag( @"\g'…'", @"Subroutine by name", ( e, fm ) => fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(?'n'.)\g'n'", "ab", null, "ab" )
                .Flag( @"\g<…>", @"Backreference by name", ( e, fm ) => fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(?<n>.)\g<n>", "aa", "ab", "aa" )
                .Flag( @"\g<…>", @"Subroutine by name", ( e, fm ) => fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(?<n>.)\g<n>", "ab", null, "ab" )
                .Flag( @"\gn", @"Backreference \g1, \g2, …", ( e, fm ) => fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(.)\g1", "aa", "ab", "aa" )
                .Flag( @"\gn", @"Subroutine \g1, \g2, …", ( e, fm ) => fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(.)\g1", "ab", null, "ab" )
                .Flag( @"\g-n", @"Relative backreference \g-1, \g-2, …", ( e, fm ) => fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(.)\g-1", "aa", "ab", "aa" )
                .Flag( @"\g-n", @"Relative subroutine \g-1, \g-2, …", ( e, fm ) => fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(.)\g-1", "ab", null, "ab" )
                .Flag( @"\g{…}", @"Backreference \g{name}, \g{number}, \g{-number}, g{+number}", ( e, fm ) => fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(?<n>.)\g{n}", "aa", "ab", "aa" )
                .Flag( @"\g{…}", @"Subroutine \g{name}, \g{number}, \g{-number}, g{+number}", ( e, fm ) => fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(?<n>.)\g{n}", "ab", null, "ab" )
                .Flag( @"(?P=name)", @"Backreference by name", ( e, fm ) => fm.Backref_PEqName )
                    .Test( @"(?P<n>.)(?P=n)", "aa", "ab", "aa" )
            //.Flag( @"\k< … >, \g< … >", @"Allow spaces like '\k < name >'", (e, fm) => fm.AllowSpacesInBackref ) // TODO

            .Category( @"Grouping and Lookaround" )

                .Flag( @"(?:…)", @"Non-capturing group", ( e, fm ) => fm.NoncapturingGroup )
                    .Test( @"a(?:x)y", "axy", null, "axy" )
                    .Test( @"a\(?:x\)y", "axy", null, "axy" )
                .Flag( @"(?=…)", @"Positive lookahead ", ( e, fm ) => fm.PositiveLookahead )
                    .Test( @"a(?=xz).z", "axz", "atz", "axz" )
                    .Test( @"a\(?=xz\).z", "axz", "atz", "axz" )
                .Flag( @"(?!…)", @"Negative lookahead ", ( e, fm ) => fm.NegativeLookahead )
                    .Test( @"a(?!x).y", "azy", @"axy", "azy" )
                    .Test( @"a\(?!x\).y", "azy", @"axy", "azy" )
                .Flag( @"(?<=…)", @"Positive lookbehind, fixed-length", ( e, fm ) => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.FixedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"x.(?<=xy)a", "xya", "xta", "xya" )
                    .Test( @"x.\(?<=xy\)a", "xya", "xta", "xya" )
                .Flag( @"(?<=…)", @"Positive lookbehind, bounded-length", ( e, fm ) => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"x..(?<=x{2,3}y)a", "xxya", "xtta", "xxya" )
                    .Test( @"x..\(?<=x{2,3}y\)a", "xxya", "xtta", "xxya" )
                    .Test( @"x..\(?<=xx?x?y\)a", "xxya", "xtta", "xxya" )
                .Flag( @"(?<=…)", @"Positive lookbehind, variable-length", ( e, fm ) => fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @"x.(?<=x+y)a", "xxxxya", "xta", "xya" )
                    .Test( @"x.\(?<=x+y\)a", "xxxxya", "xta", "xya" )
                .Flag( @"(?<!…)", @"Negative lookbehind, fixed-length", ( e, fm ) => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.FixedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @".(?<!xy)a", "ya", "xya", "ya" )
                    .Test( @".\(?<!xy\)a", "ya", "xya", "ya" )
                .Flag( @"(?<!…)", @"Negative lookbehind, bounded-length", ( e, fm ) => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength || fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @".(?<!x{2,7})a", "xa", "xxa", "xa" )
                    .Test( @"\(?<!x{2,7}\)a", "xa", "xxa", "xa" )
                    .Test( @"\(?<!xxx?x?\)a", "xa", "xxa", "xa" )
                .Flag( @"(?<!…)", @"Negative lookbehind, variable-length", ( e, fm ) => fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength )
                    .Test( @".(?<!x.*)a", "ya", "xa", "ya" )
                    .Test( @"\(?<!x.*\)a", "ya", "xa", "ya" )
                .Flag( @"(?=…(?=…))", @"Nested lookarounds", ( e, fm ) => ( fm.PositiveLookahead || fm.NegativeLookahead || fm.PositiveLookbehind != FeatureMatrix.LookModeEnum.None || fm.NegativeLookbehind != FeatureMatrix.LookModeEnum.None ) && fm.NestedLookaround )
                    .Test( @"a(?=b(?=c))bc", "abc", null, "abc" )
                    .Test( @"a\(?=b\(?=c\)\)bc", "abc", null, "abc" )
                    .Test( @"a(?<=ya(?<=xya))", "xyabc", null, "abc" )
                    .Test( @"a\(?<=ya\(?<=xya\)\)", "xyabc", null, "abc" )
                .Flag( @"(?>…)", @"Atomic group", ( e, fm ) => fm.AtomicGroup )
                    .Test( @"a(?>x)b", "axb", null, "axb" )
                    .Test( @"a\(?>x\)b", "axb", null, "axb" )
                .Flag( @"(?|…)", @"Branch reset", ( e, fm ) => fm.BranchReset )
                    .Test( @"(?|(a)|(b))\1", "bb", "1", "bb" )
                    .Test( @"\(?|\(a\)\|\(b\)\)\1", "bb", "1", "bb" )
                .Flag( @"(?*…)", @"Non-atomic positive lookahead", ( e, fm ) => fm.NonatomicPositiveLookahead )
                    .Test( @"a(?*x)x", "ax", null, "ax" )
                .Flag( @"(?<*…)", @"Non-atomic positive lookbehind", ( e, fm ) => fm.NonatomicPositiveLookbehind )
                    .Test( @"(?<*x)a", "xa", "ta", "a" )
                .Flag( @"(?~…)", @"Absent operator", ( e, fm ) => fm.AbsentOperator )
                    .Test( @"/\*(?~\*\/)\*\/", "/* abc */", null, "/* abc */" )
            //.Flag( @"( ? … )", @"Allow spaces like '( ? < name >…)'", (e, fm) => fm.AllowSpacesInGroups ) // TODO

            .Category( @"Recursive patterns" )

                .Flag( @"(?n)", @"Recursive subpattern by number", ( e, fm ) => fm.Recursive_Num )
                    .Test( @"(x.)(?1)", "xyxz", "xyZ", "xyxz" )
                .Flag( @"(?-n) (?+n)", @"Relative recursive subpattern by number", ( e, fm ) => fm.Recursive_PlusMinusNum )
                    .Test( @"(a|(b))(?-1)", "ab", "a", "ab" )
                    .Test( @"\(x\(.\)\)\(?-1\)", "xyz", null )
                .Flag( @"(?R)", @"Recursive whole pattern", ( e, fm ) => fm.Recursive_R )
                    .Test( @"a((?R))*b", "aabb", "b", "aabb" )
                //.Test( @"\(((?>[^()]+)|(?R))*\)", "(a(b)c)", "b")
                .Flag( @"(?&name)", @"Recursive subpattern by name", ( e, fm ) => fm.Recursive_Name )
                    .Test( @"(?<n>a)(?&n)", "aa", "" )
                .Flag( @"(?P>name)", @"Recursive subpattern by name", ( e, fm ) => fm.Recursive_PGtName )
                    .Test( @"(?P<n>a)(?P>n)", "aa", "" )
                .Flag( @"(?…(grouplist))", @"Additionally return capturing groups", ( e, fm ) => fm.Recursive_ReturnGroups )
                    .Test( @"(?<a>A(?<b>.))(?&a(<b>))\k<b>", "ABACC", "ABACB" )

            .Category( @"Conditionals" )

                .Flag( @"(?(number)…|…)", @"Conditionals by number, +number, -number", ( e, fm ) => fm.Conditional_BackrefByNumber )
                    .Test( @"(x)(?(1)y|z)", "xy", "bx" )
                .Flag( @"(?(name)…|…)", @"Conditional by name", ( e, fm ) => fm.Conditional_BackrefByName )
                    .Test( @"(?<n>x)(?(n)y|z)", "xy", "", "xy" )
                    .Test( @"(?P<n>x)(?(n)y|z)", "xy", "", "xy" )
                .Flag( @"(?(pattern)…|…)", @"Conditional subpattern", ( e, fm ) => fm.Conditional_Pattern )
                    .Test( @"x(?(?=.z)y|z)", "xyz", "x", "xy" )
                    .Test( @"x(?((?=.z))y|z)", "xyz", "x", "xy" )
                .Flag( @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", ( e, fm ) => fm.Conditional_PatternOrBackrefByName )
                    .Test( @"x(?(y).|z)", "xy", "x", "xy" )
                .Flag( @"(?('name')…|…)", @"Conditional by name", ( e, fm ) => fm.Conditional_BackrefByName_Apos )
                    .Test( @"(?'n'x)(?('n')y|z)", "xy", "", "xy" )
                .Flag( @"(?(<name>)…|…)", @"Conditional by name", ( e, fm ) => fm.Conditional_BackrefByName_LtGt )
                    .Test( @"(?<n>x)(?(<n>)y|z)", "xy", "", "xy" )
                    .Test( @"(?P<n>x)(?(<n>)y|z)", "xy", "", "xy" )
                .Flag( @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", ( e, fm ) => fm.Conditional_R )
                    .Test( @"(?(R)a+|(?R)b)", "aaaab", "", "aaaab" )
                .Flag( @"(?(R&name)…|…)", @"Recursive conditional by name", ( e, fm ) => fm.Conditional_RName )
                    .Test( @"(?<A>(?'B'abc(?(R)(?(R&A)1)(?(R&B)2)X|(?1)(?2)(?R))))", "abcabc1Xabc2XabcXabcabc", "" )
                .Flag( @"(?(DEFINE)…|…)", @"Defining subpatterns", ( e, fm ) => fm.Conditional_DEFINE )
                    .Test( @"(?(DEFINE)(?<n>x.z))(?&n)", "xyz", "", "xyz" )
                    .Test( @"\g<x>(?(DEFINE)(?<x>x))", "x", "", "x" )
                .Flag( @"(?(VERSION…)…|…)", @"Check version using 'VERSION=decimal' or 'VERSION>=decimal'", ( e, fm ) => fm.Conditional_VERSION )
                    .Test( @"(?(VERSION>=1)xyz|abc)", "xyz", "", "xyz" )

            .Category( @"Miscellaneous" )

                .Flag( @"Captures", @"Get all captures matched by group", ( e, fm ) => !e.Capabilities.HasFlag( RegExpressLibrary.RegexEngineCapabilityEnum.NoGroups ) && e.Capabilities.HasFlag( RegExpressLibrary.RegexEngineCapabilityEnum.HasCaptures ) )
                    .Test( ( e, fm ) =>
                    {
                        e.SetCollectCaptures( true );

                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(.)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray( );
                            if( groups.Length != 2 ) return false; // including default group

                            RegExpressLibrary.Matches.IGroup group = groups[1];
                            if( !group.Success ) return false;

                            var captures = group.Captures.ToArray( );
                            if( captures.Length != 2 ) return false;

                            if( captures[0].Value != "a" ) return false;
                            if( captures[1].Value != "b" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                        finally
                        {
                            e.SetCollectCaptures( false );
                        }
                    } )
                    .Test( ( e, fm ) =>
                    {
                        e.SetCollectCaptures( true );

                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"\(.\)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray( );
                            if( groups.Length != 2 ) return false; // including default group

                            RegExpressLibrary.Matches.IGroup group = groups[1];
                            if( !group.Success ) return false;

                            var captures = group.Captures.ToArray( );
                            if( captures.Length != 2 ) return false;

                            if( captures[0].Value != "a" ) return false;
                            if( captures[1].Value != "b" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                        finally
                        {
                            e.SetCollectCaptures( false );
                        }
                    } )
                    .Test( ( e, fm ) =>
                    {
                        e.SetCollectCaptures( true );

                        try
                        {
                            // Oniguruma's way 

                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(?@.)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray( );
                            if( groups.Length != 2 ) return false; // including default group

                            RegExpressLibrary.Matches.IGroup group = groups[1];
                            if( !group.Success ) return false;

                            var captures = group.Captures.ToArray( );
                            if( captures.Length != 2 ) return false;

                            if( captures[0].Value != "a" ) return false;
                            if( captures[1].Value != "b" ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                        finally
                        {
                            e.SetCollectCaptures( false );
                        }
                    } )
                .Flag( @"Empty≠Failed", @"Differentiate between empty groups and failed groups", ( e, fm ) => !e.Capabilities.HasFlag( RegExpressLibrary.RegexEngineCapabilityEnum.NoGroups ) && !e.Capabilities.HasFlag( RegExpressLibrary.RegexEngineCapabilityEnum.NoGroupSuccessFlag ) )
                    .Test( ( e, fm ) =>
                    {
                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"(a)(b*)(c)*(d)", "ad" );
                            // "()" in "(a)" to fix an issue of tiny-regex-c);
                            // "(d)" to ensure that previous groups are returned;
                            // "*" instead of "?" because some engines do not support "?"

                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            RegExpressLibrary.Matches.IGroup[] groups = match.Groups.ToArray( );
                            if( groups.Length != 5 ) return false; // including default group

                            if( !groups[1].Success ) return false;
                            if( !groups[2].Success ) return false;
                            if( groups[3].Success ) return false;
                            if( !groups[4].Success ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    } )
                    .Test( ( e, fm ) =>
                    {
                        try
                        {
                            RegExpressLibrary.Matches.RegexMatches matches = e.GetMatches( RegExpressLibrary.ICancellable.NonCancellable, @"\(a\)\(b*\)\(c\)*\(d\)", "ad" );
                            if( matches.Count != 1 ) return false;

                            RegExpressLibrary.Matches.IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            RegExpressLibrary.Matches.IGroup[] groups = match.Groups.ToArray( );
                            if( groups.Length != 5 ) return false; // including default group

                            if( !groups[1].Success ) return false;
                            if( !groups[2].Success ) return false;
                            if( groups[3].Success ) return false;
                            if( !groups[4].Success ) return false;

                            return true;
                        }
                        catch
                        {
                            return false;
                        }
                    } )
                .Flag( @"(*verb)", @"Control verbs: (*verb) (*verb:…) (*:name)", ( e, fm ) => fm.ControlVerbs )
                    .Test( @"x(*ACCEPT)|y(*FAIL)", "x", "y", "x" )
                    .Test( @"(*UCP)^a", "a", "", "a" )
                    .Test( @"x(*SKIP)y", "xy", "xSKIPy", "xy" )
                    .Test( @"a(*FAIL)|b", "b", "a", "b" )
                .Flag( @"(*…:…)", @"Script runs, such as (*atomic:…)", ( e, fm ) => fm.ScriptRuns )
                    .Test( @"(*atomic:x)", "x", "", "x" )

                .Flag( @"(?Cn) (*func)", @"Callouts (custom functions)", ( e, fm ) => fm.Callouts )

                .Flag( @"(?)", @"Empty construct", ( e, fm ) => fm.EmptyConstruct )
                    .Test( @"x(?)y", "xy", "x", "xy" )
                //.Category( @"(? )", @"Empty construct", (e, fm) => fm.EmptyConstructX).Test( @"(?x)a(? )b", "ab", null )
                .Flag( @"[]", @"Empty set (always fails)", ( e, fm ) => fm.EmptySet )
                    .Test( @"x[]?", "x", null, "x" )
                .Flag( @"[^]", @"Any character, even newline", ( e, fm ) => fm.EmptySetAny )
                    .Test( @"a[^][^][^]b", "ax\r\nb", "ab", "ax\r\nb" )
                .Flag( @"Unicode “.”", @"“.” matches Unicode characters, not just ASCII", ( e, fm ) => fm.Unicode_Class_Dot )
                    .Test( @"X.....Y", "XăîșțâY", null, "XăîșțâY" )
                .Flag( @"Unicode “\w”", @"“\w” matches Unicode characters, not just ASCII", ( e, fm ) => fm.Unicode_Class_vW )
                    .Test( @"X\w\w\w\w\wY", "XăîșțâY", null, "XăîșțâY" )
                    .Test( @"(?u)X\w\w\w\w\wY", "XăîșțâY", null, "XăîșțâY" )
                    .Test( @"(?u:X\w\w\w\w\wY)", "XăîșțâY", null, "XăîșțâY" )
                .Flag( @"[Unicode]", @"Support Unicode characters inside sets", ( e, fm ) => fm.InsideSets_Unicode )
                    .Test( @"X[é]Y", "XéY", null, "XéY" )
                    .Test( @"(?u)X[é]Y", "XéY", null, "XéY" )
                    .Test( @"(?u:X[é]Y)", "XéY", null, "XéY" )
                .Flag( @"Surrogates", @"“.” matches surrogate pairs as one entity (no split)", ( e, fm ) => fm.Unicode_Class_Dot && fm.KeepSurrogatePairs )
                    .Test( @"X.Y", "X💕Y", null, "X💕Y" )
                .Flag( @"é=É", @"Support Unicode case folding", ( e, fm ) => fm.UnicodeCaseFolding )
                    .IgnoreCase( )
                    .Test( @"XéY", "XÉY", null, "XÉY" )
                    .Test( @"(?i)XéY", "XÉY", null, "XÉY" )
                    .Test( @"(?i:XéY)", "XÉY", null, "XÉY" )
                .Flag( @"é=e+´", @"Canonical equivalence of characters", ( e, fm ) => fm.Ext_Canon_Eq )
                    .Test( "\u00E9", "e\u0301", null )
                .Flag( "Σσς", "Match letters that have multiple uppercase and lowercase variants", ( e, fm ) => fm.Σσς )
                    .IgnoreCase( )
                    .Test( @"ΣΣΣ", "Σσς", null, "Σσς" )
                    .Test( @"(?i)ΣΣΣ", "Σσς", null, "Σσς" )
                .Flag( "ß=ss", "Match “ß ↔ ss” (and “ß ↔ SS” in case-insensitive mode)", ( e, fm ) => fm.ßSS )
                    .IgnoreCase( )
                    .Test( @"aßb", "aSSb", "aSb", "aSSb" )
                    .Test( @"(?i)aßb", "aSSb", "aSb", "aSSb" )

                /*
                 * find defect
                .Flag( "ß=S", "", (e, fm) => false )
                    .IgnoreCase()
                    .Test( @"ß", "s S", null)
                    .Test( @"(?i)ß", "s S", null)
                */

                .Flag( "No hang, no ReDoS", "No catastrophic infinite matching, no timeout", ( e, fm ) => fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Accept )
                    .Test( ( e, fm ) => SimpleReDosChecker.CheckCatastrophicPattern( e, fm ) == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Passed )
                .Flag( "Reject ReDoS", "Give error on possible ReDoS", ( e, fm ) => fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Reject )
                    .Test( ( e, fm ) => SimpleReDosChecker.CheckCatastrophicPattern( e, fm ) == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Error )

            .Category( @"Specific extensions" )

                .Flag( @"Fuzzy matching", @"Approximate matching using special patterns or parameters", ( e, fm ) => fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal || fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed || fm.FuzzyMatchingParams )
                    .Test( @"(test){i}", "teXst", null, "teXst" )
                    .Test( @"(test){+1}", "teXst", null, "teXst" )
                    .Test( @"\(test\)\{+1\}", "teXst", null, "teXst" )
                    .Test( ( e, fm ) => fm.FuzzyMatchingParams )
                .Flag( @"[:class:]", @"Character class outside sets", ( e, fm ) => fm.Ext_Class_Name )
                    .Test( @"[:alpha:]", "X", null, "X" )
                .Flag( @"(?@…)", @"Capturing group", ( e, fm ) => fm.Ext_NamedGroup_AtApos || fm.Ext_NamedGroup_AtLtGt || fm.CapturingGroup )
                    .Test( @"(?@<n>x)", "x", "", "x" )
                    .Test( @"(?@x)", "x", "", "x" )
                .Flag( @"\!c, \!\c", @"Complement (“not ‘c’”); 'c' — character", ( e, fm ) => fm.Ext_Class_Not )
                    .Test( @"\!x", "a", null, "a" )
                .Flag( @"![comment]", @"Inline comment", ( e, fm ) => fm.Ext_AnomalousInlineComments )
                    .Test( @"a![comment]b", "ab", "a!cb", "ab" )
                .Flag( @"_", @"Universal wildcard (any character including newlines)", ( e, fm ) => fm.Ext_UniversalWildcard )
                    .Test( @"a__b_", "a\r\nbc", null, "a\r\nbc" )
                .Flag( @"&", @"Intersection (both patterns must match)", ( e, fm ) => fm.Ext_Operator_Intersection )
                    .Test( @"a.+&.+b", "axb", null, "axb" )
                .Flag( @"~(…)", @"Complement (pattern must not match)", ( e, fm ) => fm.Ext_Operator_Complement )
                    .Test( @"ab~(x)c", "abc", null, "abc" )
                .Flag( @"Alt. syntax", @"Support alternative syntax", ( e, fm ) => fm.Ext_AlternativeLanguage )

            /*
            .Category( @"Philosophical aspects" )

                //.Flag( @"""a(b)?\1"" ∋ ""a""", @"""a(b)?\1"" matches ""a""", ( e, fm ) => false )
                //    .Test( @"a(b)?\1", "a" )
                //    .Test( @"a\(b\)?\1", "a" )
                .Flag( @"""a(b)?\1"" ∋ ""ab""", @"""a(b)?\1"" matches ""ab""", ( e, fm ) => false )
                    .Test( @"a(b)?\1", "ab" )
                    .Test( @"a\(b\)?\1", "ab" )
                .Flag( @"""(a*)*"" gr. ""a""", @"Group 1 is ""a""", ( e, fm ) => false )
                    .Test( ( e, fm ) =>
                    {
                        return
                            fm.Parentheses == RegExpressLibrary.SyntaxColouring.FeatureMatrix.PunctuationEnum.Normal && Check( @"(a*)*", "a", "a" ) ||
                            fm.Parentheses == RegExpressLibrary.SyntaxColouring.FeatureMatrix.PunctuationEnum.Backslashed && Check( @"\(a*\)*", "a", "a" );

                        bool Check( string pattern, string text, string g1 )
                        {
                            try
                            {
                                var matches = e.GetMatches( ICancellable.NonCancellable, pattern, text );

                                if( matches.Count > 0 )
                                {
                                    var match = matches.Matches.First( );

                                    return match.Groups.Count( ) > 1 && match.Groups.ElementAt( 1 ).Value == g1;
                                }
                            }
                            catch
                            {
                                // ignore
                            }

                            return false;
                        }
                    }
                    )
            */
            ;
    }
}
