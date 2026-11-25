using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;
using System.Runtime.InteropServices.Marshalling;

namespace ExportFeatureMatrix.Fluency;

static class TreeData
{
    internal static readonly Tree Tree = new( );

    static TreeData( )
    {
        Tree

            .Category( @"General" )

                .Flag( @"(…)", @"Grouping constructs", context => context.Fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"(x)", "x", null, "x" )

                .Flag( @"\(…\)", @"Grouping constructs", context => context.Fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"\(x\)", "x", null, "x" )

                //.Direct( @"(…)", @"Grouping constructs" )
                //    .On( context => context.Fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal, ColourEnum.Green, "(…)" )
                //        .Test( @"(x)", "x", null, "x" )
                //    .On( context => context.Fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal.Backslashed, ColourEnum.Green, @"\(…\)" )
                //        .Test( @"\(x\)", "x", null, "x" )

                .Flag( @"[…]", @"Character group", context => context.Fm.Brackets )
                    .Test( @"[x]", "x", null, "x" )

                .Flag( @"(?[…])", @"Character group", context => context.Fm.ExtendedBrackets )
                    .Test( @"a(?[[x]])b", "axb", null, "axb" )

                .Flag( @"|", @"Alternation", context => context.Fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"x|y", "y", null, "y" )

                .Flag( @"\|", @"Alternation", context => context.Fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"x\|y", "y", null, "y" )

                .Flag( @"new line (\n)", @"Alternatives on separate lines", context => context.Fm.AlternationOnSeparateLines )
                    .Test( "x\ny", "y", null, "y" )

                .Flag( @"#comment", @"Comment", context => context.Fm.XModeComments )
                    .IgnorePatternWhitespace( )
                    .Test( "a#comment", "a", null, "a" )
                    .Test( "(?x)a#comment", "a", null, "a" )
                    .Test( "a#comment\nb", "ab", "b", "ab" ) // ('\n' is required by Hyperscan)
                    .Test( "(?x)a#comment\nb", "ab", "b", "ab" ) // ('\n' is required by Hyperscan)

                .Flag( @"(?#comment)", @"Inline comment", context => context.Fm.InlineComments )
                    .Test( @"a(?#comment)b", "ab", "a#commentb", "ab" )
                    .Test( @"a\(?#comment\)b", "ab", "a", "ab" )

                .Flag( @"[#comment]", @"Comment inside “[…]”", context => context.Fm.InsideSets_XModeComments )
                    .IgnorePatternWhitespace( )
                    .Test( "a[b#comment\nz]y", "azy", "acy", "azy" )
                    .Test( "(?x)a[b#comment\nz]y", "azy", "acy", "azy" )
                    .Test( "(?xx)a[b#comment\nz]y", "azy", "acy", "azy" )

                .Flag( @"(?flags)", @"Inline options", context => context.Fm.Flags )
                    .IgnoreCase( false )
                    .Test( @"(?i)x", "X", null, "X" )

                .Flag( @"(?flags:…)", @"Inline scoped options", context => context.Fm.ScopedFlags )
                    .IgnoreCase( false )
                    .Test( @"a(?i:x)b", "aXb", null, "aXb" )

                .Flag( @"(?^flags)", @"Inline fresh options", context => context.Fm.CircumflexFlags )
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^)x", "x", "X", "x" )

                .Flag( @"(?^flags:…)", @"Inline scoped fresh options", context => context.Fm.ScopedCircumflexFlags )
                    .IgnoreCase( false )
                    .Test( @"(?i)(?^:x)", "x", "X", "x" )

                .Flag( @"(?x)", @"Allow “x” flag", context => context.Fm.XFlag )
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)a b", "ab", null, "ab" )

                .Flag( @"(?xx)", @"Allow “xx” flag", context => context.Fm.XXFlag )
                    .IgnorePatternWhitespace( false )
                    .Test( @"(?x)[a b](?xx)[a b]", " a", "a ", " a" )

                .Flag( @"\Q…\E", @"Literal", context => context.Fm.Literal_QE )
                    .Test( @"a\Qx\E", "ax", "aQxE", "ax" )

                .Flag( @"[\Q…\E]", @"Literal inside “[…]”", context => context.Fm.InsideSets_Literal_QE )
                    .Test( @"[\Qx\E]", "x", "Q", "x" )

                .Flag( @"[\q{…}]", @"Literal inside “[…]”", context => context.Fm.InsideSets_Literal_qBrace )
                    .Test( @"a[\q{xy}]b", "axyb", "axb", "axyb" )

            .Category( @"Quantifiers" )

                .Flag( @"*", @"Zero or more times", context => context.Fm.Quantifier_Asterisk )
                    .Test( @"xy*", "x", null, "x" )

                .Flag( @"+", @"One or more times", context => context.Fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"xy+z", "xyyz", null, "xyyz" )

                .Flag( @"\+", @"One or more times", context => context.Fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"xy\+", "xyy", null, "xyy" )

                .Flag( @"?", @"Zero or one time", context => context.Fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"xy?", "x", null, "x" )

                .Flag( @"\?", @"Zero or one time", context => context.Fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"xy\?", "x", null, "x" )

                .Flag( @"{n,m}", @"Between n and m times: “{n}”, “{n,}”, “{n,m}”", context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal )
                    .Test( @"x{2,3}", "xx", null, "xx" )

                .Flag( @"\{n,m\}", @"Between n and m times: “\{n\}”, “\{n,\}”, “\{n,m\}”", context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed )
                    .Test( @"x\{2,3\}", "xx", null, "xx" )
                //.Flag( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", (e, fm) => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.Both ) // TODO
                //.Flag( @"{ n, m } ", @"Allow spaces within {…} or \{…\}", (e, fm) => fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsageEnum.XModeOnly ) // TODO

                .Flag( @"{,m}, \{,m\}", @"Equivalent to “{0,m}” or “\{0,m\}”", context => context.Fm.Quantifier_LowAbbrev )
                    .Test( @"x{,3}", "xxx", null, "xxx" )
                    .Test( @"x\{,3\}", "xxx", null, "xxx" )

                .Flag( @"*?, +?, ??, {}?", @"Lazy (non-greedy) quantifiers — match as little as possible",
                        context => context.Fm.Quantifier_Asterisk && context.Fm.Quantifier_Plus == FeatureMatrix.PunctuationEnum.Normal && context.Fm.Quantifier_Question == FeatureMatrix.PunctuationEnum.Normal && context.Fm.Quantifier_Lazy )
                    .Test( @".+?A.*?AA??", "AAAAAA", null, "AAA" )

                .Flag( @"*+, ++, ?+, {}+", @"Possessive quantifiers — match without backtracking",
                        context => context.Fm.Quantifier_Plus != FeatureMatrix.PunctuationEnum.None && context.Fm.Quantifier_Possessive )
                    .Test( @"Xa++.Y", "XaaabY", "XaaaaY", "XaaabY" )
                    .Test( @"Xa\+\+.Y", "XaaabY", "XaaaaY", "XaaabY" )

            .Category( @"Escapes" )

                .Flag( @"\a", @"Bell, \u0007", context => context.Fm.Esc_a )
                    .Test( @"\a", "\u0007", null, "\u0007" )

                .Flag( @"\b", @"Backspace, \u0008", context => context.Fm.Esc_b )
                    .Test( @"x\by", "x\u0008y", null, "x\u0008y" )

                .Flag( @"\e", @"Escape, \u001B", context => context.Fm.Esc_e )
                    .Test( @"\e", "\u001B", null, "\u001B" )

                .Flag( @"\f", @"Form feed, \u000C", context => context.Fm.Esc_f )
                    .Test( @"\f", "\u000C", null, "\u000C" )

                .Flag( @"\n", @"New line, \u000A", context => context.Fm.Esc_n )
                    .Test( @"\n", "\u000A", null, "\u000A" )

                .Flag( @"\r", @"Carriage return, \u000D", context => context.Fm.Esc_r )
                    .Test( @"\r", "\u000D", null, "\u000D" )

                .Flag( @"\t", @"Tab, \u0009", context => context.Fm.Esc_t )
                    .Test( @"\t", "\u0009", null, "\u0009" )

                .Flag( @"\v", @"Vertical tab, \u000B", context => context.Fm.Esc_v )
                    .Test( @"\v", "\u000B", "\f", "\u000B" )

                .Flag( @"\1..\7", @"Octal, one digit", context => context.Fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 )
                    .Test( @"\3", "\u0003", null, "\u0003" )

                .Flag( @"\nn, \nnn", @"Octal, two or three digits", context => context.Fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 || context.Fm.Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 )
                    .Test( @"\11\101", "\u0009A", null, "\u0009A" )
                    .Test( @"\11\000\101\000", "\u0009A", null, "\u0009A" ) // ('\000' for Oniguruma)

                .Flag( @"\0nnn", @"Octal, up to three digits after “\0”", context => context.Fm.Esc_Octal0_1_3 )
                    .Test( @"\03\011\0011", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                    .Test( @"\03\000\011\000\0011\000", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )

                .Flag( @"\o{nn…}", @"Octal", context => context.Fm.Esc_oBrace )
                    .Test( @"\o{11}", "\u0009", null, "\u0009" )

                .Flag( @"\xXX", @"Hexadecimal code, two digits", context => context.Fm.Esc_x2 )
                    .Test( @"\x09", "\u0009", null, "\u0009" )
                    .Test( @"\x09\x00", "\u0009", null, "\u0009" )

                .Flag( @"\x{XX…}", @"Hexadecimal code", context => context.Fm.Esc_xBrace )
                    .Test( @"\x{0009}", "\u0009", null, "\u0009" )

                .Flag( @"\uXXXX", @"Hexadecimal code, four digits", context => context.Fm.Esc_u4 )
                    .Test( @"\u0009", "\u0009", null, "\u0009" )

                .Flag( @"\UXXXXXXXX", @"Hexadecimal code, eight digits", context => context.Fm.Esc_U8 )
                    .Test( @"\U00000009", "\u0009", null, "\u0009" )

                .Flag( @"\u{XX…}", @"Hexadecimal code", context => context.Fm.Esc_uBrace )
                    .Test( @"\u{0009}", "\u0009", null, "\u0009" )

                .Flag( @"\U{XX…}", @"Hexadecimal code", context => context.Fm.Esc_UBrace )
                    .Test( @"\U{0009}", "\u0009", null, "\u0009" )

                .Flag( @"\cC", @"Control character", context => context.Fm.Esc_c1 )
                    .Test( @"\cM", "\r", null, "\r" )

                .Flag( @"\CC", @"Control character", context => context.Fm.Esc_C1 )
                    .Test( @"\CM", "\r", null, "\r" )

                .Flag( @"\C-C", @"Control character", context => context.Fm.Esc_CMinus )
                    .Test( @"\C-M", "\r", "null", "\r" )

                .Flag( @"\N{…}", @"Unicode name or “U+code”", context => context.Fm.Esc_NBrace )
                    .Test( @"\N{COMMA}", ",", null, "," )
                    .Test( @"\N{comma}", ",", null, "," )
                    .Test( @"\N{LATIN CAPITAL LETTER A}", "A", null, "A" )
                    .Test( @"\N{U+0061}", "a", null, "a" )

                .Flag( @"\any", @"Generic escape", context => context.Fm.GenericEscape )
                    .Test( @"a\j", @"aj", @"a\j", "aj" )

            .Category( @"Escapes inside sets" )

                .Flag( @"[\a]", @"Bell, \u0007", context => context.Fm.InsideSets_Esc_a )
                    .Test( @"[\a]", "\u0007", null, "\u0007" )

                .Flag( @"[\b]", @"Backspace, \u0008", context => context.Fm.InsideSets_Esc_b )
                    .Test( @"[\b]", "\u0008", null, "\u0008" )

                .Flag( @"[\e]", @"Escape, \u001B", context => context.Fm.InsideSets_Esc_e )
                    .Test( @"[\e]", "\u001B", null, "\u001B" )

                .Flag( @"[\f]", @"Form feed, \u000C", context => context.Fm.InsideSets_Esc_f )
                    .Test( @"[\f]", "\u000C", null, "\u000C" )

                .Flag( @"[\n]", @"New line, \u000A", context => context.Fm.InsideSets_Esc_n )
                    .Test( @"[\n]", "\u000A", null, "\u000A" )

                .Flag( @"[\r]", @"Carriage return, \u000D", context => context.Fm.InsideSets_Esc_r )
                    .Test( @"[\r]", "\u000D", null, "\u000D" )

                .Flag( @"[\t]", @"Tab, \u0009", context => context.Fm.InsideSets_Esc_t )
                    .Test( @"[\t]", "\u0009", null, "\u0009" )

                .Flag( @"[\v]", @"Vertical tab, \u000B", context => context.Fm.InsideSets_Esc_v )
                    .Test( @"[\v]", "\u000B", "\f", "\u000B" )

                .Flag( @"[\1..\7]", @"Octal, one digit", context => context.Fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 )
                    .Test( @"[\3]", "\u0003", null, "\u0003" )
                    .Test( @"[\3\0]", "\u0003", null, "\u0003" )

                .Flag( @"[\nn], [\nnn]", @"Octal, two or three digits", context => context.Fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_2_3 || context.Fm.InsideSets_Esc_Octal == FeatureMatrix.OctalEnum.Octal_1_3 )
                    .Test( @"[\11][\101]", "\u0009A", null, "\u0009A" )
                    .Test( @"[\11\000][\101\000]", "\u0009A", null, "\u0009A" ) // ('\000' for Oniguruma)

                .Flag( @"[\0nnn]", @"Octal, up to three digits after “\0”", context => context.Fm.InsideSets_Esc_Octal0_1_3 )
                    .Test( @"[\03][\011][\0011]", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )
                    .Test( @"[\03\000][\011\000][\0011\000]", "\u0003\u0009\u0009", null, "\u0003\u0009\u0009" )

                .Flag( @"[\o{nn…}]", @"Octal", context => context.Fm.InsideSets_Esc_oBrace )
                    .Test( @"[\o{11}]", "\u0009", null, "\u0009" )

                .Flag( @"[\xXX]", @"Hexadecimal code, two digits", context => context.Fm.InsideSets_Esc_x2 )
                    .Test( @"[\x09]", "\u0009", null, "\u0009" )
                    .Test( @"[\x09\x00]", "\u0009", null, "\u0009" )

                .Flag( @"[\x{XX…}]", @"Hexadecimal code", context => context.Fm.InsideSets_Esc_xBrace )
                    .Test( @"[\x{0009}]", "\u0009", null, "\u0009" )

                .Flag( @"[\uXXXX]", @"Hexadecimal code, four digits", context => context.Fm.InsideSets_Esc_u4 )
                    .Test( @"[\u0009]", "\u0009", null, "\u0009" )

                .Flag( @"[\UXXXXXXXX]", @"Hexadecimal code, eight digits", context => context.Fm.InsideSets_Esc_U8 )
                    .Test( @"[\U00000009]", "\u0009", "x", "\u0009" )

                .Flag( @"[\u{XX…}]", @"Hexadecimal code", context => context.Fm.InsideSets_Esc_uBrace )
                    .Test( @"[\u{0009}]", "\u0009", "X", "\u0009" )

                .Flag( @"[\U{XX…}]", @"Hexadecimal code", context => context.Fm.InsideSets_Esc_UBrace )
                    .Test( @"[\U{0009}]", "\u0009", "x", "\u0009" )

                .Flag( @"[\cC]", @"Control character", context => context.Fm.InsideSets_Esc_c1 )
                    .Test( @"[\cM]", "\r", null, "\r" )

                .Flag( @"[\CC]", @"Control character", context => context.Fm.InsideSets_Esc_C1 )
                    .Test( @"[\CM]", "\r", null, "\r" )

                .Flag( @"[\C-C]", @"Control character ", context => context.Fm.InsideSets_Esc_CMinus )
                    .Test( @"[\C-M]", "\r", null, "\r" )

                .Flag( @"[\N{…}]", @"Unicode name or “U+code”", context => context.Fm.InsideSets_Esc_NBrace )
                    .Test( @"[\N{COMMA}]", ",", "M", "," ) // (see also '\N' -- any except '\n')
                    .Test( @"[\N{comma}]", ",", "m", "," )
                    .Test( @"[\N{U+0061}]", "a", "U", "a" )

                .Flag( @"[\any]", @"Generic escape", context => context.Fm.InsideSets_GenericEscape )
                    .Test( @"[\j]", "j", @"\" )

            .Category( @"Classes" )

                .Flag( @".", @"Any, including or excepting newline (“\n”)", context => context.Fm.Class_Dot )
                    .Test( @".", "x", null, "x" )

                .Flag( @"\C", @"Single byte", context => context.Fm.Class_Cbyte )
                    .Test( @"\C\C", "î", null, "î" )

                .Flag( @"\C", @"Single code point", context => context.Fm.Class_Ccp )
                    .Test( @"\C\C", "îî", "î", "îî" )

                .Flag( @"\d, \D", @"Digit", context => context.Fm.Class_dD )
                    .Test( @"\d\D", "9x", null, "9x" )

                .Flag( @"\h, \H", @"Hexadecimal character", context => context.Fm.Class_hHhexa )
                    .Test( @"\h\H", "Ax", null, "Ax" )

                .Flag( @"\h, \H", @"Horizontal space", context => context.Fm.Class_hHhorspace )
                    .Test( @"\h\H", " x", null, " x" )

                .Flag( @"\l, \L", @"Lowercase character", context => context.Fm.Class_lL )
                    .Test( @"\l\L", "xX", null, "xX" )

                .Flag( @"\N", @"Any except “\n”", context => context.Fm.Class_N )
                    .Test( @"\N", "a", "\n", "a" )

                .Flag( @"\O", @"Any", context => context.Fm.Class_O )
                    .Test( @"\O", "a", null, "a" )

                .Flag( @"\R", @"Line break", context => context.Fm.Class_R )
                    .Test( @"a\Rb", "a\r\nb", null, "a\r\nb" )

                .Flag( @"\s, \S", @"Space", context => context.Fm.Class_sS )
                    .Test( @"\s\S", " x", null, " x" )

                .Flag( @"\sx, \Sx", @"Syntax group; x — emacs group", context => context.Fm.Class_sSx )
                    .Test( @"\ss", " ", null, " " )

                .Flag( @"\u, \U", @"Uppercase character", context => context.Fm.Class_uU )
                    .Test( @"\u\U", "Xx", null, "Xx" )

                .Flag( @"\v, \V", @"Vertical space", context => context.Fm.Class_vV )
                    .Test( @"\v\v", "\r\f", null, "\r\f" )

                .Flag( @"\w, \W", @"Word character", context => context.Fm.Class_wW )
                    .Test( @"\w\w\w", "xyz", null, "xyz" )

                .Flag( @"\X", @"Extended grapheme cluster", context => context.Fm.Class_X )
                    .Test( @"\X", "a", null, "a" )

                .Flag( @"\pX, \PX", @"Unicode property; X — short property name", context => context.Fm.Class_pP )
                    .Test( @"\pL\PL", "x9", null, "x9" )

                .Flag( @"\p{…}, \P{…}", @"Unicode property", context => context.Fm.Class_pPBrace )
                    .Test( @"\p{L}\P{L}", "x9", null, "x9" )

            .Category( @"Classes inside sets" )

                .Flag( @"[\d], [\D]", @"Digit", context => context.Fm.InsideSets_Class_dD )
                    .Test( @"a[\d]", "a9", null, "a9" )

                .Flag( @"[\h], [\H]", @"Hexadecimal character", context => context.Fm.InsideSets_Class_hHhexa )
                    .Test( @"[\h][\H]", "Ax", null, "Ax" )

                .Flag( @"[\h], [\H]", @"Horizontal space", context => context.Fm.InsideSets_Class_hHhorspace )
                    .Test( @"[\h][\H]", " x", null, " x" )

                .Flag( @"[\l], [\L]", @"Lowercase character", context => context.Fm.InsideSets_Class_lL )
                    .Test( @"[\l][\L]", "xX", null, "xX" )

                .Flag( @"[\R]", @"Line break", context => context.Fm.InsideSets_Class_R )
                    .Test( @"a[\R]b", "a\r\nb", null, "a\r\nb" )

                .Flag( @"[\s], [\S]", @"Space", context => context.Fm.InsideSets_Class_sS )
                    .Test( @"a[\s][\S]x", "a 9x", null, "a 9x" )

                .Flag( @"[\sx], [\Sx]", @"Syntax group; x — emacs group", context => context.Fm.InsideSets_Class_sSx )
                    .Test( @"[\ss]", " ", "s", " " )

                .Flag( @"[\u], [\U]", @"Uppercase character", context => context.Fm.InsideSets_Class_uU )
                    .Test( @"[\u][\U]", "Xx", null, "Xx" )

                .Flag( @"[\v], [\V]", @"Vertical space", context => context.Fm.InsideSets_Class_vV )
                    .Test( @"[\v][\v]", "\r\f", null, "\r\f" )

                .Flag( @"[\w], [\W]", @"Word character", context => context.Fm.InsideSets_Class_wW )
                    .Test( @"a[\w][\w][\w]", "axyz", null, "axyz" )

                .Flag( @"[\X]", @"Extended grapheme cluster", context => context.Fm.InsideSets_Class_X )
                    .Test( @"[\X]", "a", null, "a" )

                .Flag( @"[\pX], [\PX]", @"Unicode property, X — short property name", context => context.Fm.InsideSets_Class_pP )
                    .Test( @"[\pL][\PL]", "x9", null, "x9" )

                .Flag( @"[\p{…}], [\P{…}]", @"Unicode property", context => context.Fm.InsideSets_Class_pPBrace )
                    .Test( @"[\p{L}][\P{L}]", "x9", null, "x9" )

                .Flag( @"[[:class:]]", @"Character class", context => context.Fm.InsideSets_Class_Name )
                    .Test( @"[[:alpha:]]", "X", null, "X" )

                .Flag( @"[[=elem=]]", @"Equivalence", context => context.Fm.InsideSets_Equivalence )
                    .Test( @"[[=a=]][[=a=]]", "aA", null, "aA" ) // 'Á' not matched by STL regex.

                .Flag( @"[[.elem.]]", @"Collating symbol", context => context.Fm.InsideSets_Collating )
                    .Test( @"a[[.ch.]]x", "achx", null, "achx" )
                    .Test( @"a[[.comma.]]b", "a,b", null, "a,b" ) // STL seems to have a defect.

            .Category( @"Operators inside sets" )

                .Flag( @"[[…] op […]]", @"Using operators for nested groups", context => context.Fm.InsideSets_Operators )
                    .Test( @"[[ab]&[bc]]", "b", "a&c", "b" )
                    .Test( @"[[ab]&&[bc]]", "b", "a&c", "b" )
                    .Test( @"X[abc-[b]]Y", "XaY", "XbY", "XaY" )

                .Flag( @"(?[[…] op […]])", @"Using operators for nested groups", context => context.Fm.InsideSets_OperatorsExtended )
                    .Test( @"(?[[ab]&[bc]])", "b", "a&c", "b" )

                .Flag( @"[…]&[…]", @"Intersection", context => context.Fm.InsideSets_Operator_Ampersand )
                    .Test( @"[[ab]&[bc]]", "b", "ac[]&", "b" )
                    .Test( @"(?[[ab]&[bc]])", "b", "ac[]&", "b" )

                .Flag( @"[…]+[…]", @"Union", context => context.Fm.InsideSets_Operator_Plus )
                    .Test( @"[[a]+[b]]", "a", "+", "a" )
                    .Test( @"(?[[a]+[b]])", "a", "+", "a" )

                .Flag( @"[…]|[…]", @"Union", context => context.Fm.InsideSets_Operator_VerticalLine )
                    .Test( @"[[a]|[b]]", "b", "|", "b" )
                    .Test( @"(?[[a]|[b]])", "b", "|", "b" )

                .Flag( @"[…]-[…]", @"Subtraction", context => context.Fm.InsideSets_Operator_Minus )
                    .Test( @"[[ab]-[b]]", "a", "b", "a" )
                    .Test( @"(?[[ab]-[b]])", "a", "b", "a" )

                .Flag( @"…-[…]", @"Subtraction", context => context.Fm.InsideSets_Operator_MinusBased )
                    .Test( @"X[abc-[b]]Y", "XaY", "XbY", "XaY" )

                .Flag( @"[…]^[…]", @"Symmetric difference", context => context.Fm.InsideSets_Operator_Circumflex )
                    .Test( @"[[ab]^[bc]]", "c", "^", "c" )
                    .Test( @"(?[[ab]^[bc]])", "c", "^", "c" )

                .Flag( @"![…]", @"Complement", context => context.Fm.InsideSets_Operator_Exclamation )
                    .Test( @"a(?[![b]])y", "axy", null, "axy" )
                    .Test( @"a[![b]]y", "axy", null, "axy" )

                .Flag( @"[…]&&[…]", @"Intersection", context => context.Fm.InsideSets_Operator_DoubleAmpersand )
                    .Test( @"[[ab]&&[bc]]", "b", "ac[]&", "b" )

                .Flag( @"[…]||[…]", @"Union", context => context.Fm.InsideSets_Operator_DoubleVerticalLine )
                    .Test( @"[[a]||[b]]", "b", "]|[", "b" )

                .Flag( @"[…]--[…]", @"Difference", context => context.Fm.InsideSets_Operator_DoubleMinus )
                    .Test( @"[[ab]--[b]]", "a", "][-b", "a" )

                .Flag( @"[…]~~[…]", @"Symmetric difference", context => context.Fm.InsideSets_Operator_DoubleTilde )
                    .Test( @"[[ab]~~[bca]]", "c", "ab][~", "c" )

            .Category( @"Anchors" )

                .Flag( @"^", @"Beginning of string or line", context => context.Fm.Anchor_Circumflex )
                    .Test( @"^x", "x", null, "x" )

                .Flag( @"$", @"End, or before “\n” at end of string or line", context => context.Fm.Anchor_Dollar )
                    .Test( @"x$", "x", null, "x" )

                .Flag( @"\A", @"Start of string", context => context.Fm.Anchor_A )
                    .Test( @"\Ax", "x", null, "x" )

                .Flag( @"\Z", @"End of string, or before “\n” at end of string", context => context.Fm.Anchor_Z == FeatureMatrix.AnchorZModeEnum.Correct )
                    .Test( @"x\Z", "x\n", "xZ", "x" )

                .Flag( @"\Z", @"End of string, same as “\z”", context => context.Fm.Anchor_Z == FeatureMatrix.AnchorZModeEnum.Compatible )
                    .Test( @"x\Z", "x", "xZ x\n", "x" )

                .Flag( @"\z", @"End of string", context => context.Fm.Anchor_z )
                    .Test( @"x\z", "x", "xz", "x" )

                .Flag( @"\G", @"start of string or end of previous match", context => context.Fm.Anchor_G )
                    .Test( @"\Gx", "x", null, "x" )

                .Flag( @"\b, \B", @"Boundary between \w and \W", context => context.Fm.Anchor_bB )
                    .Test( @"\bx", "y x", null, "x" )

                .Flag( @"\b{g}", @"Unicode extended grapheme cluster boundary", context => context.Fm.Anchor_bg )
                    .Test( @"\b{g}x", "y x", null, "x" )

                .Flag( @"\b{…}, \B{…}", @"Typed boundary", context => context.Fm.Anchor_bBBrace )
                    .Test( @"\b{wb}x", "y x", null, "x" )
                    .Test( @"\b{start}x", "y x", null, "x" )
                    .Test( @"\b{start-half}x", "y x", null, "x" )

                .Flag( @"[[:<:]], [[:>:]]", @"POSIX word boundary", context => context.Fm.Anchor_PosixWB )
                    .Test( @"[[:<:]]a[[:>:]]", "a", null, "a" )

                .Flag( @"\m, \M", @"Start of word, end of word", context => context.Fm.Anchor_mM )
                    .Test( @"\mword\M", "some word here", null, "word" )

                .Flag( @"\<, \>", @"Start of word, end of word", context => context.Fm.Anchor_LtGt )
                    .Test( @"\<word\>", "some word here", null, "word" )

                .Flag( @"\`, \'", @"Start of string, end of string", context => context.Fm.Anchor_GraveApos )
                    .Test( @"\`x\'", "x", null, "x" )

                .Flag( @"\y, \Y", @"Boundary between graphemes", context => context.Fm.Anchor_yY )
                    .Test( @"a\yb", "ab", null, "ab" )

                .Flag( @"\K", @"Keep the stuff left of the \K", context => context.Fm.Anchor_K )
                    .Test( @"a\Kb", "ab", "aKb", "b" )

            .Category( @"Groups, subroutines and backreferences" )

                .Flag( @"(?:…)", @"Non-capturing group", context => context.Fm.NoncapturingGroup )
                    .Test( @"a(?:x)y", "axy", null, "axy" )
                    .Test( @"a\(?:x\)y", "axy", null, "axy" )

                .Flag( @"(?'name'…)", @"Named group", context => context.Fm.NamedGroup_Apos )
                    .Test( @"a(?'n'x)b", "axb", null, "axb" )
                    .Test( @"a\(?'n'x\)b", "axb", null, "axb" )

                .Flag( @"(?<name>…)", @"Named group", context => context.Fm.NamedGroup_LtGt )
                    .Test( @"a(?<n>x)b", "axb", null, "axb" )
                    .Test( @"a\(?<n>x\)b", "axb", null, "axb" )

                .Flag( @"(?P<name>…)", @"Named group", context => context.Fm.NamedGroup_PLtGt )
                    .Test( @"a(?P<n>x)b", "axb", null, "axb" )
                    .Test( @"a\(?P<n>x\)b", "x", null, "axb" )

                .Flag( @"Duplicate names", @"Allow duplicate group names", context => context.Fm.DuplicateGroupName )
                    .Test( @"(?<a>x)|(?<a>y)", "y", "z", "y" )
                    .Test( @"\(?<a>x\)|\(?<a>y\)", "y", "z", "y" )
                    .Test( @"(?P<a>x)|(?P<a>y)", "y", "z", "y" )

                .Flag( @"\1, \2, …, \9", @"Backreferences", context => context.Fm.Backref_Num == FeatureMatrix.BackrefEnum.OneDigit || context.Fm.Backref_Num == FeatureMatrix.BackrefEnum.Any )
                    .Test( @"(x)\1", "xx", "x", "xx" )
                    .Test( @"\(x\)\1", "xx", "x", "xx" )

                .Flag( @"\nnn", @"Backreference, two or more digits", context => context.Fm.Backref_Num == FeatureMatrix.BackrefEnum.Any )
                    .Test( @"(x)(x)(x)(x)(x)(x)(x)(x)(x)(y)\10", "xxxxxxxxxyy", "xxxxxxxxxy\x10", "xxxxxxxxxyy" )
                    .Test( @"\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(x\)\(y\)\10", "xxxxxxxxxyy", "xxxxxxxxxy\x10", "xxxxxxxxxyy" )

                .Flag( @"\k'name'", @"Backreference by name", context => context.Fm.Backref_kApos )
                    .Test( @"(?'n'x)\k'n'", "xx", null, "xx" )

                .Flag( @"\k<name>", @"Backreference by name", context => context.Fm.Backref_kLtGt )
                    .Test( @"(?<n>x)\k<n>", "xx", null, "xx" )

                .Flag( @"\k{name}", @"Backreference by name", context => context.Fm.Backref_kBrace )
                    .Test( @"(?<n>x)\k{n}", "xx", null, "xx" )

                .Flag( @"\kn", @"Backreference \k1, \k2, …", context => context.Fm.Backref_kNum )
                    .Test( @"(?<n>x)\k1", "xx", null, "xx" )

                .Flag( @"\k-n", @"Relative backreference \k-1, \k-2, …", context => context.Fm.Backref_kNegNum )
                    .Test( @"(?<n>x)\k-1", "xx", null, "xx" )

                .Flag( @"\g'…'", @"Backreference by name", context => context.Fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(?'n'.)\g'n'", "aa", "ab", "aa" )

                .Flag( @"\g'…'", @"Subroutine by name", context => context.Fm.Backref_gApos == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(?'n'.)\g'n'", "ab", null, "ab" )

                .Flag( @"\g<…>", @"Backreference by name", context => context.Fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(?<n>.)\g<n>", "aa", "ab", "aa" )

                .Flag( @"\g<…>", @"Subroutine by name", context => context.Fm.Backref_gLtGt == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(?<n>.)\g<n>", "ab", null, "ab" )

                .Flag( @"\gn", @"Backreference \g1, \g2, …", context => context.Fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(.)\g1", "aa", "ab", "aa" )

                .Flag( @"\gn", @"Subroutine \g1, \g2, …", context => context.Fm.Backref_gNum == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(.)\g1", "ab", null, "ab" )

                .Flag( @"\g-n", @"Relative backreference \g-1, \g-2, …", context => context.Fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(.)\g-1", "aa", "ab", "aa" )

                .Flag( @"\g-n", @"Relative subroutine \g-1, \g-2, …", context => context.Fm.Backref_gNegNum == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(.)\g-1", "ab", null, "ab" )

                .Flag( @"\g{…}", @"Backreference \g{name}, \g{number}, \g{-number}, g{+number}", context => context.Fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Value )
                    .Test( @"(?<n>.)\g{n}", "aa", "ab", "aa" )

                .Flag( @"\g{…}", @"Subroutine \g{name}, \g{number}, \g{-number}, g{+number}", context => context.Fm.Backref_gBrace == FeatureMatrix.BackrefModeEnum.Pattern )
                    .Test( @"(?<n>.)\g{n}", "ab", null, "ab" )

                .Flag( @"(?P=name)", @"Backreference by name", context => context.Fm.Backref_PEqName )
                    .Test( @"(?P<n>.)(?P=n)", "aa", "ab", "aa" )

            //.Flag( @"\k< … >, \g< … >", @"Allow spaces like '\k < name >'", (e, fm) => fm.AllowSpacesInBackref ) // TODO

            .Category( @"Lookaround" )

                .Flag( @"(?=…)", @"Positive lookahead ", context => context.Fm.PositiveLookahead )
                    .Test( @"a(?=xz).z", "axz", "atz", "axz" )
                    .Test( @"a\(?=xz\).z", "axz", "atz", "axz" )

                .Flag( @"(?!…)", @"Negative lookahead ", context => context.Fm.NegativeLookahead )
                    .Test( @"a(?!x).y", "azy", @"axy", "azy" )
                    .Test( @"a\(?!x\).y", "azy", @"axy", "azy" )

                .Direct( @"(?<=.{n})", "Positive lookbehind (“F”\u200A—\u200Afixed, “B”\u200A—\u200Abounded, “+”\u200A—\u200Aany)", context =>
                {
                    FeatureMatrix fm = context.Fm;

                    if( !context.Validate )
                    {
                        return fm.PositiveLookbehind switch
                        {
                            FeatureMatrix.LookModeEnum.None => null,
                            FeatureMatrix.LookModeEnum.FixedLength => new IndicatorData( ColourEnum.Green, "F" ),
                            FeatureMatrix.LookModeEnum.BoundedLength => new IndicatorData( ColourEnum.Green, "B" ),
                            FeatureMatrix.LookModeEnum.AnyLength => new IndicatorData( ColourEnum.Green, "+" ),
                            _ => throw new NotSupportedException( )
                        };
                    }

                    string? declared = fm.PositiveLookbehind switch
                    {
                        FeatureMatrix.LookModeEnum.None => null,
                        FeatureMatrix.LookModeEnum.FixedLength => "F",
                        FeatureMatrix.LookModeEnum.BoundedLength => "B",
                        FeatureMatrix.LookModeEnum.AnyLength => "+",
                        _ => throw new NotSupportedException( )
                    };

                    ColourEnum col = fm.PositiveLookbehind switch
                    {
                        FeatureMatrix.LookModeEnum.None => ColourEnum.Info,
                        FeatureMatrix.LookModeEnum.FixedLength => ColourEnum.Green,
                        FeatureMatrix.LookModeEnum.BoundedLength => ColourEnum.Green,
                        FeatureMatrix.LookModeEnum.AnyLength => ColourEnum.Green,
                        _ => throw new NotSupportedException( )
                    };

                    string t = fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.AnyLength ? "+" : declared + "???+";

                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && PatternTestObj.Try( context, @"x.(?<=x+y)a", "xxxxya", "xta", "xya" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"x.\(?<=x+y\)a", "xxxxya", "xta", "xya" ) ) return new IndicatorData( col, t );

                    t = fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.BoundedLength ? "B" : declared + "???B";

                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && PatternTestObj.Try( context, @"x..(?<=x{2,3}y)a", "xxya", "xtta", "xxya" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"x..\(?<=x{2,3}y\)a", "xxya", "xtta", "xxya" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"x..\(?<=xx?x?y\)a", "xxya", "xtta", "xxya" ) ) return new IndicatorData( col, t );

                    t = fm.PositiveLookbehind == FeatureMatrix.LookModeEnum.FixedLength ? "F" : declared + "???F";

                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && PatternTestObj.Try( context, @"x.(?<=xy)a", "xya", "xta", "xya" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"x.\(?<=xy\)a", "xya", "xta", "xya" ) ) return new IndicatorData( col, t );

                    return null;
                } )

                .Direct( @"(?<!.{n})", "Negative lookbehind (“F”\u200A—\u200Afixed, “B”\u200A—\u200Abounded, “+”\u200A—\u200Aany)", context =>
                {
                    FeatureMatrix fm = context.Fm;

                    if( !context.Validate )
                    {
                        return fm.NegativeLookbehind switch
                        {
                            FeatureMatrix.LookModeEnum.None => null,
                            FeatureMatrix.LookModeEnum.FixedLength => new IndicatorData( ColourEnum.Green, "F" ),
                            FeatureMatrix.LookModeEnum.BoundedLength => new IndicatorData( ColourEnum.Green, "B" ),
                            FeatureMatrix.LookModeEnum.AnyLength => new IndicatorData( ColourEnum.Green, "+" ),
                            _ => throw new NotSupportedException( )
                        };
                    }

                    string? declared = fm.NegativeLookbehind switch
                    {
                        FeatureMatrix.LookModeEnum.None => null,
                        FeatureMatrix.LookModeEnum.FixedLength => "F",
                        FeatureMatrix.LookModeEnum.BoundedLength => "B",
                        FeatureMatrix.LookModeEnum.AnyLength => "+",
                        _ => throw new NotSupportedException( )
                    };

                    ColourEnum col = fm.NegativeLookbehind switch
                    {
                        FeatureMatrix.LookModeEnum.None => ColourEnum.Info,
                        FeatureMatrix.LookModeEnum.FixedLength => ColourEnum.Green,
                        FeatureMatrix.LookModeEnum.BoundedLength => ColourEnum.Green,
                        FeatureMatrix.LookModeEnum.AnyLength => ColourEnum.Green,
                        _ => throw new NotSupportedException( )
                    };

                    string t = fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.AnyLength ? "+" : declared + "???+";

                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && PatternTestObj.Try( context, @".(?<!x.*)a", "ya", "xa", "ya" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"\(?<!x.*\)a", "ya", "xa", "ya" ) ) return new IndicatorData( col, t );

                    t = fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.BoundedLength ? "B" : declared + "???B";

                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && PatternTestObj.Try( context, @".(?<!x{2,7})a", "xa", "xxa", "xa" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"\(?<!x{2,7}\)a", "xa", "xxa", "xa" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @"\(?<!xxx?x?\)a", "xa", "xxa", "xa" ) ) return new IndicatorData( col, t );

                    t = fm.NegativeLookbehind == FeatureMatrix.LookModeEnum.FixedLength ? "F" : declared + "???F";

                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && PatternTestObj.Try( context, @".(?<!xy)a", "ya", "xya", "ya" ) ) return new IndicatorData( col, t );
                    if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && PatternTestObj.Try( context, @".\(?<!xy\)a", "ya", "xya", "ya" ) ) return new IndicatorData( col, t );

                    return null;
                } )

                .Flag( @"(?=…(?=…))", @"Nested lookarounds", context => ( context.Fm.PositiveLookahead || context.Fm.NegativeLookahead || context.Fm.PositiveLookbehind != FeatureMatrix.LookModeEnum.None || context.Fm.NegativeLookbehind != FeatureMatrix.LookModeEnum.None ) && context.Fm.NestedLookaround )
                    .Test( @"a(?=b(?=c))bc", "abc", null, "abc" )
                    .Test( @"a\(?=b\(?=c\)\)bc", "abc", null, "abc" )
                    .Test( @"a(?<=ya(?<=xya))", "xyabc", null, "abc" )
                    .Test( @"a\(?<=ya\(?<=xya\)\)", "xyabc", null, "abc" )

                .Flag( @"(?>…)", @"Atomic group", context => context.Fm.AtomicGroup )
                    .Test( @"a(?>x)b", "axb", null, "axb" )
                    .Test( @"a\(?>x\)b", "axb", null, "axb" )

                .Flag( @"(?|…)", @"Branch reset", context => context.Fm.BranchReset )
                    .Test( @"(?|(a)|(b))\1", "bb", "1", "bb" )
                    .Test( @"\(?|\(a\)\|\(b\)\)\1", "bb", "1", "bb" )

                .Flag( @"(?*…)", @"Non-atomic positive lookahead", context => context.Fm.NonatomicPositiveLookahead )
                    .Test( @"a(?*x)x", "ax", null, "ax" )

                .Flag( @"(?<*…)", @"Non-atomic positive lookbehind", context => context.Fm.NonatomicPositiveLookbehind )
                    .Test( @"(?<*x)a", "xa", "ta", "a" )

                .Flag( @"(?~…)", @"Absent operator", context => context.Fm.AbsentOperator )
                    .Test( @"/\*(?~\*\/)\*\/", "/* abc */", null, "/* abc */" )
                //.Flag( @"( ? … )", @"Allow spaces like '( ? < name >…)'", (e, fm) => fm.AllowSpacesInGroups ) // TODO

                .Flag( @"(?<=(.+))", "Capture lookaround groups (fixed-, bounded- or variable-length)", context => context.Fm.CapturingLookaroundGroups )
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        if( fm.Parentheses == FeatureMatrix.PunctuationEnum.None ) return false;

                        try
                        {
                            string pattern =
                                fm.Parentheses switch
                                {
                                    FeatureMatrix.PunctuationEnum.Normal => @"(?<=(.+))c",
                                    FeatureMatrix.PunctuationEnum.Backslashed => @"\(?<=\(.+\)\)c",
                                    _ => throw new NotSupportedException( )
                                };
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, pattern, "abc" );
                            if( matches.Count > 0 )
                            {
                                IGroup? g = matches.Matches.First( ).Groups.ElementAtOrDefault( 1 );
                                if( g != null && g.Success )
                                {
                                    if( g.Value == "ab" ) return true;
                                }
                            }
                        }
                        catch
                        {
                        }

                        try
                        {
                            string pattern =
                                fm.Parentheses switch
                                {
                                    FeatureMatrix.PunctuationEnum.Normal =>
                                        fm.Quantifier_Braces switch
                                        {
                                            FeatureMatrix.PunctuationEnum.Normal => @"(?<=(.{1,2}))c",
                                            FeatureMatrix.PunctuationEnum.Backslashed => @"(?<=(.\{1,2\}))c",
                                            _ => throw new NotSupportedException( )
                                        },
                                    FeatureMatrix.PunctuationEnum.Backslashed =>
                                        fm.Quantifier_Braces switch
                                        {
                                            FeatureMatrix.PunctuationEnum.Normal => @"\(?<=\(.{1,2}\)\)c",
                                            FeatureMatrix.PunctuationEnum.Backslashed => @"\(?<=\(.\{1,2\}\)\)c",
                                            _ => throw new NotSupportedException( )
                                        },
                                    _ => throw new NotSupportedException( )
                                };
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, pattern, "abc" );
                            if( matches.Count > 0 )
                            {
                                IGroup? g = matches.Matches.First( ).Groups.ElementAtOrDefault( 1 );
                                if( g != null && g.Success )
                                {
                                    if( g.Value == "ab" ) return true;
                                }
                            }
                        }
                        catch
                        {
                        }

                        try
                        {
                            string pattern =
                                fm.Parentheses switch
                                {
                                    FeatureMatrix.PunctuationEnum.Normal =>
                                        fm.Quantifier_Braces switch
                                        {
                                            FeatureMatrix.PunctuationEnum.Normal => @"(?<=(.{2}))c",
                                            FeatureMatrix.PunctuationEnum.Backslashed => @"(?<=(.\{2\}))c",
                                            _ => throw new NotSupportedException( )
                                        },
                                    FeatureMatrix.PunctuationEnum.Backslashed =>
                                        fm.Quantifier_Braces switch
                                        {
                                            FeatureMatrix.PunctuationEnum.Normal => @"\(?<=\(.{2}\)\)c",
                                            FeatureMatrix.PunctuationEnum.Backslashed => @"\(?<=\(.\{2\}\)\)c",
                                            _ => throw new NotSupportedException( )
                                        },
                                    _ => throw new NotSupportedException( )
                                };
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, pattern, "abc" );
                            if( matches.Count > 0 )
                            {
                                IGroup? g = matches.Matches.First( ).Groups.ElementAtOrDefault( 1 );
                                if( g != null && g.Success )
                                {
                                    if( g.Value == "ab" ) return true;
                                }
                            }
                        }
                        catch
                        {
                        }

                        return false;
                    } )

            .Category( @"Recursive patterns" )

                .Flag( @"(?n)", @"Recursive subpattern by number", context => context.Fm.Recursive_Num )
                    .Test( @"(x.)(?1)", "xyxz", "xyZ", "xyxz" )

                .Flag( @"(?-n) (?+n)", @"Relative recursive subpattern by number", context => context.Fm.Recursive_PlusMinusNum )
                    .Test( @"(a|(b))(?-1)", "ab", "a", "ab" )
                    .Test( @"\(x\(.\)\)\(?-1\)", "xyz", null )

                .Flag( @"(?R)", @"Recursive whole pattern", context => context.Fm.Recursive_R )
                    .Test( @"a((?R))*b", "aabb", "b", "aabb" )
                //.Test( @"\(((?>[^()]+)|(?R))*\)", "(a(b)c)", "b")

                .Flag( @"(?&name)", @"Recursive subpattern by name", context => context.Fm.Recursive_Name )
                    .Test( @"(?<n>a)(?&n)", "aa", "" )

                .Flag( @"(?P>name)", @"Recursive subpattern by name", context => context.Fm.Recursive_PGtName )
                    .Test( @"(?P<n>a)(?P>n)", "aa", "" )

                .Flag( @"(?…(grouplist))", @"Additionally return capturing groups", context => context.Fm.Recursive_ReturnGroups )
                    .Test( @"(?<a>A(?<b>.))(?&a(<b>))\k<b>", "ABACC", "ABACB" )

            .Category( @"Conditionals" )

                .Flag( @"(?(number)…|…)", @"Conditionals by number, +number, -number", context => context.Fm.Conditional_BackrefByNumber )
                    .Test( @"(x)(?(1)y|z)", "xy", "bx" )

                .Flag( @"(?(name)…|…)", @"Conditional by name", context => context.Fm.Conditional_BackrefByName )
                    .Test( @"(?<n>x)(?(n)y|z)", "xy", "", "xy" )
                    .Test( @"(?P<n>x)(?(n)y|z)", "xy", "", "xy" )

                .Flag( @"(?(pattern)…|…)", @"Conditional subpattern", context => context.Fm.Conditional_Pattern )
                    .Test( @"x(?(?=.z)y|z)", "xyz", "x", "xy" )
                    .Test( @"x(?((?=.z))y|z)", "xyz", "x", "xy" )

                .Flag( @"(?(xxx)…|…)", @"Conditional by xxx name, or by xxx subpattern, if no such name", context => context.Fm.Conditional_PatternOrBackrefByName )
                    .Test( @"x(?(y).|z)", "xy", "x", "xy" )

                .Flag( @"(?('name')…|…)", @"Conditional by name", context => context.Fm.Conditional_BackrefByName_Apos )
                    .Test( @"(?'n'x)(?('n')y|z)", "xy", "", "xy" )

                .Flag( @"(?(<name>)…|…)", @"Conditional by name", context => context.Fm.Conditional_BackrefByName_LtGt )
                    .Test( @"(?<n>x)(?(<n>)y|z)", "xy", "", "xy" )
                    .Test( @"(?P<n>x)(?(<n>)y|z)", "xy", "", "xy" )

                .Flag( @"(?(R)…|…)", @"Recursive conditional: R, R+number, R-number", context => context.Fm.Conditional_R )
                    .Test( @"(?(R)a+|(?R)b)", "aaaab", "", "aaaab" )

                .Flag( @"(?(R&name)…|…)", @"Recursive conditional by name", context => context.Fm.Conditional_RName )
                    .Test( @"(?<A>(?'B'abc(?(R)(?(R&A)1)(?(R&B)2)X|(?1)(?2)(?R))))", "abcabc1Xabc2XabcXabcabc", "" )

                .Flag( @"(?(DEFINE)…|…)", @"Defining subpatterns", context => context.Fm.Conditional_DEFINE )
                    .Test( @"(?(DEFINE)(?<n>x.z))(?&n)", "xyz", "", "xyz" )
                    .Test( @"\g<x>(?(DEFINE)(?<x>x))", "x", "", "x" )

                .Flag( @"(?(VERSION…)…|…)", @"Check version using 'VERSION=decimal' or 'VERSION>=decimal'", context => context.Fm.Conditional_VERSION )
                    .Test( @"(?(VERSION>=1)xyz|abc)", "xyz", "", "xyz" )

            .Category( @"Miscellaneous" )

                .Flag( @"Sticky", @"Support Sticky mode by “\G”, “(?y)” or an option", context => context.Fm.Anchor_G || context.Fm.Anchor_G_Option )
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        e.SetStickyMode( true );

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"a", "aaba" );
                            if( matches.Count == 2 ) return true;
                        }
                        catch
                        {
                            // ignore
                        }
                        finally
                        {
                            e.SetStickyMode( false );
                        }

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"\Ga", "aaba" );
                            if( matches.Count == 2 ) return true;
                        }
                        catch
                        {
                            // ignore
                        }

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"(?y)a", "aaba" );
                            if( matches.Count == 2 ) return true;
                        }
                        catch
                        {
                            // ignore
                        }

                        return false;
                    } )

                .Flag( @"Captures", @"Get all captures matched by group", context => !context.Engine.Capabilities.HasFlag( RegexEngineCapabilityEnum.NoGroups ) && context.Engine.Capabilities.HasFlag( RegexEngineCapabilityEnum.HasCaptures ) )
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        e.SetCollectCaptures( true );

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"(.)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray( );
                            if( groups.Length != 2 ) return false; // including default group

                            IGroup group = groups[1];
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
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        e.SetCollectCaptures( true );

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"\(.\)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray( );
                            if( groups.Length != 2 ) return false; // including default group

                            IGroup group = groups[1];
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
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        e.SetCollectCaptures( true );

                        try
                        {
                            // Oniguruma's way 

                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"(?@.)+", "ab" );
                            if( matches.Count != 1 ) return false;

                            IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            var groups = match.Groups.ToArray( );
                            if( groups.Length != 2 ) return false; // including default group

                            IGroup group = groups[1];
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

                .Flag( @"Empty≠Failed", @"Differentiate between empty groups and failed groups", context => !context.Engine.Capabilities.HasFlag( RegexEngineCapabilityEnum.NoGroups ) && !context.Engine.Capabilities.HasFlag( RegexEngineCapabilityEnum.NoGroupSuccessFlag ) )
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"(a)(b*)(c)*(d)", "ad" );
                            // "()" in "(a)" to fix an issue of tiny-regex-c);
                            // "(d)" to ensure that previous groups are returned;
                            // "*" instead of "?" because some engines do not support "?"

                            if( matches.Count != 1 ) return false;

                            IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            IGroup[] groups = match.Groups.ToArray( );
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
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"\(a\)\(b*\)\(c\)*\(d\)", "ad" );
                            if( matches.Count != 1 ) return false;

                            IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            IGroup[] groups = match.Groups.ToArray( );
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

                .Flag( @"(*verb)", @"Control verbs: “(*verb)”, “(*verb:…)”, “(*:name)”", context => context.Fm.ControlVerbs )
                    .Test( @"x(*ACCEPT)|y(*FAIL)", "x", "y", "x" )
                    .Test( @"(*UCP)^a", "a", "", "a" )
                    .Test( @"x(*SKIP)y", "xy", "xSKIPyx*SKIPy", "xy" )
                    .Test( @"a(*FAIL)|ab", "ab", "aFAILba*FAILb", "ab" )

                .Flag( @"(*…:…)", @"Script runs, such as “(*atomic:…)”", context => context.Fm.ScriptRuns )
                    .Test( @"(*atomic:x)", "x", "", "x" )

                .Flag( @"(?Cn) (*func)", @"Callouts (custom functions)", context => context.Fm.Callouts )

                .Flag( @"(?)", @"Empty construct", context => context.Fm.EmptyConstruct )
                    .Test( @"x(?)y", "xy", "x", "xy" )
                //.Flag( @"(? )", @"Empty construct", (e, fm) => fm.EmptyConstructX).Test( @"(?x)a(? )b", "ab", null )

                .Flag( @"[]", @"Empty set (always fails)", context => context.Fm.EmptySet )
                    .Test( @"x[]?", "x", null, "x" )

                .Flag( @"[^]", @"Any character, even newline", context => context.Fm.EmptySetAny )
                    .Test( @"a[^][^][^]b", "ax\r\nb", "ab", "ax\r\nb" )

                .Flag( @"{…} is error", "lone “{\u200A}”, “{n}” and “{text}” are errors, not literals", context => context.Fm.Quantifier_Braces != FeatureMatrix.PunctuationEnum.None && context.Fm.Quantifier_Braces_BadIsError )
                    .And( )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal, @"{a}", null, "{a}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal, @"{}", null, "{}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal, @"{3}", null, "{3}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Normal, @"{0}", null, "{0}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed, @"\{a\}", null, "{a}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed, @"\{\}", null, "{}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed, @"\{3\}", null, "{3}", null )
                        .Test( context => context.Fm.Quantifier_Braces == FeatureMatrix.PunctuationEnum.Backslashed, @"\{0\}", null, "{0}", null )

                .Flag( @"Unicode “.”", @"“.” matches Unicode characters, not just ASCII", context => context.Fm.Unicode_Class_Dot )
                    .Test( @"ș.ș", "șșș", "?ș", "șșș" ) // (Here and bellow 'ș' can be interpreted as '?' if Unicode not supported)

                .Flag( @"Unicode “\w”", @"“\w” matches Unicode characters, not just ASCII", context => context.Fm.Unicode_Class_wW )
                    .Test( @"X\w\w\w\w\wY", "XăîșțâY", null, "XăîșțâY" )
                    .Test( @"(?u)X\w\w\w\w\wY", "XăîșțâY", null, "XăîșțâY" )
                    .Test( @"(?u:X\w\w\w\w\wY)", "XăîșțâY", null, "XăîșțâY" )

                .Flag( @"[Unicode]", @"Support Unicode characters inside sets", context => context.Fm.InsideSets_Unicode )
                    .Test( @"X[ș]Y", "XșY", "X?Y", "XșY" )
                    .Test( @"(?u)X[ș]Y", "XșY", "X?Y", "XșY" )
                    .Test( @"(?u:X[ș]Y)", "XșY", "X?Y", "XșY" )

                .Flag( @"Surrogates", @"“.” matches surrogate pairs as one entity (no split)", context => context.Fm.Unicode_Class_Dot && context.Fm.KeepSurrogatePairs )
                    .Test( @"X.Y", "X💕Y", null, "X💕Y" )

                .Flag( @"é=É", @"Support Unicode case folding", context => context.Fm.UnicodeCaseFolding )
                    .IgnoreCase( )
                    .Test( @"XéY", "XÉY", "Y", "XÉY" )
                    .Test( @"(?i)XéY", "XÉY", "Y", "XÉY" )
                    .Test( @"(?i:XéY)", "XÉY", "Y", "XÉY" )

                .Flag( @"é=e+´", @"Canonical equivalence of characters", context => context.Fm.Ext_Canon_Eq )
                    .Test( "ș\u00E9", "șe\u0301", null )

                .Flag( "Σσς", "Match multiple uppercase and lowercase variants", context => context.Fm.Σσς )
                    .IgnoreCase( )
                    .Test( @"șΣΣΣ", "șΣσς", null, "șΣσς" )
                    .Test( @"(?i)șΣΣΣ", "șΣσς", null, "șΣσς" )
                    .Test( @"(?i:șΣΣΣ)", "șΣσς", null, "șΣσς" )

                .Flag( "ß=ss", "Match “ß ↔ ss” (and “ß ↔ SS” in case-insensitive mode)", context => context.Fm.ßSS )
                    .IgnoreCase( )
                    .Test( @"șaßb", "șaSSb", "șaSb", "șaSSb" )
                    .Test( @"(?i)șaßb", "șaSSb", "șaSb", "șaSSb" )
                    .Test( @"(?i:șaßb)", "șaSSb", "șaSb", "șaSSb" )

                /*
                 * find defect
                .Flag( "ß=S", "", (e, fm) => false )
                    .IgnoreCase()
                    .Test( @"ß", "s S", null)
                    .Test( @"(?i)ß", "s S", null)
                */

                /*
                .Flag( "No hang, no ReDoS", "No catastrophic infinite matching, no timeout", context => context.Fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Accept )
                    .Test( ( e, fm ) => SimpleReDosChecker.CheckCatastrophicPattern( e, fm ) == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Passed )
                .Flag( "Reject ReDoS", "Give error on possible ReDoS", context => context.Fm.TreatmentOfCatastrophicPatterns == FeatureMatrix.CatastrophicBacktrackingEnum.Reject )
                    .Test( ( e, fm ) => SimpleReDosChecker.CheckCatastrophicPattern( e, fm ) == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Error )
                */

                .Direct( "ReDoS protection", "“+” — protected, “e” — pattern rejected, give error",
                    context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        if( !context.Validate )
                        {
                            return fm.TreatmentOfCatastrophicPatterns switch
                            {
                                FeatureMatrix.CatastrophicBacktrackingEnum.None => null,
                                FeatureMatrix.CatastrophicBacktrackingEnum.Accept => new IndicatorData( ColourEnum.Green, "+" ),
                                FeatureMatrix.CatastrophicBacktrackingEnum.Reject => new IndicatorData( ColourEnum.Warning, "e" ),
                                _ => throw new NotImplementedException( ),
                            };
                        }
                        else
                        {
                            e.SetIgnoreCase( true ); // (for example, 'zig-regex' depends on this)

                            var r = SimpleReDosChecker.CheckCatastrophicPattern( e, fm );

                            e.SetIgnoreCase( false );

                            return fm.TreatmentOfCatastrophicPatterns switch
                            {
                                FeatureMatrix.CatastrophicBacktrackingEnum.None =>
                                    r == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Timeout ? null : new IndicatorData( ColourEnum.Warning, "???" + r ),
                                FeatureMatrix.CatastrophicBacktrackingEnum.Accept => new IndicatorData( ColourEnum.Green,
                                    r == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Passed ? "+" : ( "+???" + r ) ),
                                FeatureMatrix.CatastrophicBacktrackingEnum.Reject => new IndicatorData( ColourEnum.Warning,
                                    r == SimpleReDosChecker.CatastrophicBacktrackingResultEnum.Error ? "e" : ( "e???" + r ) ),
                                _ => throw new NotImplementedException( ),
                            };
                        }
                    } )

            .Category( @"Extensions" )

                .Flag( @"(?<name2-name1>…)", @"Balancing group", context => ( context.Fm.NamedGroup_Apos || context.Fm.NamedGroup_LtGt || context.Fm.NamedGroup_PLtGt ) && context.Fm.BalancingGroup )
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        try
                        {
                            RegexMatches matches = e.GetMatches( ICancellable.NonCancellable, @"(?<left>left).*(?<right-left>right)", "leftXYZright" );
                            if( matches.Count != 1 ) return false;

                            IMatch match = matches.Matches.First( );
                            if( !match.Success ) return false;

                            IGroup? g1 = match.Groups.Skip( 1 ).FirstOrDefault( );
                            if( g1 == null ) return false;
                            if( g1.Success ) return false;

                            IGroup? g2 = match.Groups.Skip( 2 ).FirstOrDefault( );
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

                .Flag( @"Fuzzy matching", @"Approximate matching using special patterns or parameters", context => context.Fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Normal || context.Fm.Quantifier_Braces_FreeForm == FeatureMatrix.PunctuationEnum.Backslashed || context.Fm.FuzzyMatchingParams )
                    .Test( @"(test){i}", "teXst", null, "teXst" )
                    .Test( @"(test){+1}", "teXst", null, "teXst" )
                    .Test( @"\(test\)\{+1\}", "teXst", null, "teXst" )
                    .Test( context => context.Fm.FuzzyMatchingParams )

                .Flag( @"[:class:]", @"Character class outside sets", context => context.Fm.Ext_Class_Name )
                    .Test( @"[:alpha:]", "X", null, "X" )

                .Flag( @"(?@…)", @"Capturing group", context => context.Fm.Ext_NamedGroup_AtApos || context.Fm.Ext_NamedGroup_AtLtGt || context.Fm.CapturingGroup )
                    .Test( @"(?@<n>x)", "x", "", "x" )
                    .Test( @"(?@x)", "x", "", "x" )

                .Flag( @"\!c, \!\c", @"Complement (“not ‘c’”); c — character", context => context.Fm.Ext_Class_Not )
                    .Test( @"\!x", "a", null, "a" )

                .Flag( @"![comment]", @"Inline comment", context => context.Fm.Ext_AnomalousInlineComments )
                    .Test( @"a![comment]b", "ab", "a!cb", "ab" )

                .Flag( @"_", @"Universal wildcard (any character including newlines)", context => context.Fm.Ext_UniversalWildcard )
                    .Test( @"a__b_", "a\r\nbc", null, "a\r\nbc" )

                .Flag( @"&", @"Intersection (both patterns must match)", context => context.Fm.Ext_Operator_Intersection )
                    .Test( @"a.+&.+b", "axb", null, "axb" )

                .Flag( @"~(…)", @"Complement (pattern must not match)", context => context.Fm.Ext_Operator_Complement )
                    .Test( @"ab~(x)c", "abc", null, "abc" )

                .Flag( @"Alt. syntax", @"Support alternative syntax", context => context.Fm.Ext_AlternativeLanguage )

            .Category( @"Philosophical aspects" )

                //.Flag( @"a(b)?\1", @"“a(b)?\1” matches “a”", context => context.Fm.Ext_Ph1, isInfo: true )
                //    .Test( @"a(b)?\1", "a" )
                //    .Test( @"a\(b\)?\1", "a" )
                .Flag( @"a(b)?\1", @"“a(b)?\1” matches “ab”", context => context.Fm.Ext_Ph1, isInfo: true )
                    .Test( @"a(b)?\1", "ab" )
                    .Test( @"a\(b\)?\1", "ab" )

                .Flag( @"(a*)* group 1", @"Group 1 is “a”, not empty", context => context.Fm.Ext_Ph2, isInfo: true )
                    .Test( context =>
                    {
                        RegexEngine e = context.Engine;
                        FeatureMatrix fm = context.Fm;

                        return
                            fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal && Check( @"(a*)*", "a", "a" ) ||
                            fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed && Check( @"\(a*\)*", "a", "a" );

                        bool Check( string pattern, string text, string g1 )
                        {
                            bool result = false;
                            SimpleCancellable cnc = new( );

                            var t = new Thread( ( ) =>
                            {
                                try
                                {
                                    var matches = e.GetMatches( cnc, pattern, text );

                                    if( matches.Count > 0 )
                                    {
                                        var match = matches.Matches.First( );

                                        result = match.Groups.Count( ) > 1 && match.Groups.ElementAt( 1 ).Value == g1;
                                    }
                                }
                                catch
                                {
                                    // ignore
                                }
                            } )
                            {
                                IsBackground = true
                            };

                            t.SetApartmentState( ApartmentState.STA );
                            t.Start( );

                            bool no_timeout = t.Join( 4444 );
                            cnc.SetCancel( );

                            if( !no_timeout )
                            {
                                t.Join( 1111 );
                                t.Interrupt( );
                                t.Join( 1111 );
                            }

                            return result;
                        }
                    } )

                .Flag( @"“.” matches “\r”", "“.” matches “\\r” (0D) by default", context => !context.Fm.Ext_Ph3, isInfo: true )
                    .Test( @".", "\r", null, "\r" )

            ;
    }
}
