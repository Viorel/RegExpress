using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.SyntaxColouring
{
    public readonly struct FeatureMatrix
    // ('struct', to simplify the calculation of hash code)
    {
        public enum PunctuationEnum
        {
            None,
            Normal,
            Backslashed
        }

        public enum SpaceUsageEnum
        {
            None,           // for example, cannot put spaces in 'a{ 3 }'; must use 'a{3}'
            XModeOnly,      // 'a{ 3 }' is valid if pattern whitespaces (x or xx flag) are enabled
            Both            // 'a{ 3 }' is valid regardless of flags
        }

        public enum OctalEnum
        {
            None,           // no octal numbers
            Octal_1_3,      // one, two or three digits: \3, \12, \101
            Octal_2_3,      // two or three digits: \12, \101
        }

        public enum BackrefEnum
        {
            None,           // 
            OneDigit,       // one digit: \1, \2, ... \9
            Any,            // one ore more digits: \1, \12, \123
        }

        public enum BackrefModeEnum
        {
            None,
            Value,          // match the found value
            Pattern,        // re-apply the pattern
        }

        public enum CatastrophicBacktrackingEnum
        {
            None,       // infinite matching or timeout on catastrophic patterns
            Accept,     // no catastrophic patterns; the patterns are solved in reasonable amount of time
            Reject      // detects possible catastrophic patterns and rejects them, giving an error
        }

        public PunctuationEnum Parentheses { get; init; }                 // (...) or \(...\)

        public bool Brackets { get; init; }                               // [...]
        public bool ExtendedBrackets { get; init; }                       // (?[...])

        public PunctuationEnum VerticalLine { get; init; }                // | or \| -- alternation
        public bool AlternationOnSeparateLines { get; init; }             // put alternatives on separate lines (separated by '\n')

        public bool InlineComments { get; init; }                         // (?#comment) or \(?#comment\)
        public bool AnomalousInlineComments { get; init; }                // ![comment] //TODO: remove such engines
        public bool XModeComments { get; init; }                          // #comment; see also XFlag
        public bool InsideSets_XModeComments { get; init; }               // #comment inside [...]; see also XFlag

        public bool Flags { get; init; }                                  // (?flags)
        public bool ScopedFlags { get; init; }                            // (?flags:...)
        public bool CircumflexFlags { get; init; }                        // (?^) or (?^flags), restore default flags like (?d-imnsx), set new flags
        public bool ScopedCircumflexFlags { get; init; }                  // (?^:...) or (?^flags:...), scoped variant
        public bool XFlag { get; init; }                                  // 'x' flag (enable subsequent spaces and comments)
        public bool XXFlag { get; init; }                                 // 'xx' flag (enable subsequent spaces and comments)

        public bool Literal_QE { get; init; }                             // \Q...\E
        public bool InsideSets_Literal_QE { get; init; }                  // [\Q...\E]
        public bool InsideSets_Literal_qBrace { get; init; }              // ex: \q{abc|d}

        public bool Esc_a { get; init; }                                  // \a
        public bool Esc_b { get; init; }                                  // \b -- backspace; see also Anchor_bB
        public bool Esc_e { get; init; }                                  // \e
        public bool Esc_f { get; init; }                                  // \f
        public bool Esc_n { get; init; }                                  // \n; see also \N
        public bool Esc_r { get; init; }                                  // \r
        public bool Esc_t { get; init; }                                  // \t
        public bool Esc_v { get; init; }                                  // \v -- vertical tab (0x0B); see also Class_vV
        public OctalEnum Esc_Octal { get; init; }                         // ex.: \5, \77, \101 -- octal
        public bool Esc_Octal0_1_3 { get; init; }                         // ex.: \05, \077, \0101 -- octal; one, two or three digits after '\0'
        public bool Esc_oBrace { get; init; }                             // ex.: \o{00101} -- octal
        public bool Esc_x2 { get; init; }                                 // ex: \x41, which is 'A'
        public bool Esc_xBrace { get; init; }                             // ex: \x{000041}
        public bool Esc_u4 { get; init; }                                 // ex: \u0041
        public bool Esc_U8 { get; init; }                                 // ex: \U00000041
        public bool Esc_uBrace { get; init; }                             // ex: \u{41}
        public bool Esc_UBrace { get; init; }                             // ex: \U{41}
        public bool Esc_c1 { get; init; }                                 // ex: \cM, which is \r, or \cZ, which is 0x1A
        public bool Esc_C1 { get; init; }                                 // same as Esc_c1
        public bool Esc_CMinus { get; init; }                             // ex: \C-Z, which is 0x1A
        public bool Esc_NBrace { get; init; }                             // ex: \N{U+0041}, \N{LATIN CAPITAL LETTER A}, \N{COMMA} (some want \N{comma})
        public bool GenericEscape { get; init; }                          // \c, where c -- any character

        public bool InsideSets_Esc_a { get; init; }                       // \a
        public bool InsideSets_Esc_b { get; init; }                       // \b -- backspace; see also Anchor_bB
        public bool InsideSets_Esc_e { get; init; }                       // \e
        public bool InsideSets_Esc_f { get; init; }                       // \f
        public bool InsideSets_Esc_n { get; init; }                       // \n
        public bool InsideSets_Esc_r { get; init; }                       // \r
        public bool InsideSets_Esc_t { get; init; }                       // \t
        public bool InsideSets_Esc_v { get; init; }                       // \v
        public OctalEnum InsideSets_Esc_Octal { get; init; }               // ex.: \5, \77, \101 -- octal
        public bool InsideSets_Esc_Octal0_1_3 { get; init; }              // ex.: \05, \077, \0101 -- octal, one, two or three digits after \0
        public bool InsideSets_Esc_oBrace { get; init; }                  // ex.: \o{00101} -- octal
        public bool InsideSets_Esc_x2 { get; init; }                      // ex: \x41
        public bool InsideSets_Esc_xBrace { get; init; }                  // ex: \x{000041}
        public bool InsideSets_Esc_u4 { get; init; }                      // ex: \u0041
        public bool InsideSets_Esc_U8 { get; init; }                      // ex: \U00000041
        public bool InsideSets_Esc_uBrace { get; init; }                  // ex: \u{41}
        public bool InsideSets_Esc_UBrace { get; init; }                  // ex: \U{41}
        public bool InsideSets_Esc_c1 { get; init; }                      // ex: \cm, which is \r, or \cZ, which is 0x1A
        public bool InsideSets_Esc_C1 { get; init; }                      // same as InsideSets_Esc_c1
        public bool InsideSets_Esc_CMinus { get; init; }                  // ex: \C-Z, which is 0x1A
        public bool InsideSets_Esc_NBrace { get; init; }                  // ex: \N{U+0041}, \N{LATIN CAPITAL LETTER A}, \N{COMMA} (some want \N{comma})
        public bool InsideSets_GenericEscape { get; init; }               // [\c], where c -- any character

        public bool Class_Dot { get; init; }                              // . -- any, except newline (\n), or including newline in single-line mode
        public bool Class_Cbyte { get; init; }                            // \C -- a single byte
        public bool Class_Ccp { get; init; }                              // \C -- a single code point
        public bool Class_dD { get; init; }                               // \d, \D -- digits
        public bool Class_hHhexa { get; init; }                           // \h, \H -- hexadecimal
        public bool Class_hHhorspace { get; init; }                       // \h, \H -- horizontal space
        public bool Class_lL { get; init; }                               // \l, \L -- lowercase
        public bool Class_N { get; init; }                                // \N -- any except \n
        public bool Class_O { get; init; }                                // \O -- any
        public bool Class_R { get; init; }                                // \R  -- linebreak; vertical space (see Class_v) or the "\r\n" sequence
        public bool Class_sS { get; init; }                               // \s, \S -- spaces
        public bool Class_sSx { get; init; }                              // \sx, \Sx -- syntax group; x is, for example, 's', ' ', '_', 'w', '.', ')', '(', '"', '\'', '>' and '<'.
        public bool Class_uU { get; init; }                               // \u, \U -- uppercase
        public bool Class_vV { get; init; }                               // \v, \V -- vertical spaces; see also Esc_v
        public bool Class_wW { get; init; }                               // \w, \W -- word characters
        public bool Class_X { get; init; }                                // \X -- eXtended grapheme cluster, or a non-combining character followed by a sequence of zero or more combining characters
        public bool Class_Not { get; init; }                              // \!c, where c -- any character, or \!\c, where \c -- an escape
        public bool Class_pP { get; init; }                               // ex.: \pL, \PL
        public bool Class_pPBrace { get; init; }                          // \p{...}, \P{...}, \p{^...}, \P{^...}
        public bool Class_Name { get; init; }                             // ex: [:digit:] 

        public bool InsideSets_Class_dD { get; init; }                    // \d, \D -- digits
        public bool InsideSets_Class_hHhexa { get; init; }                // \h, \H -- hexadecimal
        public bool InsideSets_Class_hHhorspace { get; init; }            // \h, \H -- horizontal space
        public bool InsideSets_Class_lL { get; init; }                    // \l, \L -- lowercase
        public bool InsideSets_Class_R { get; init; }                     // \R  -- linebreak
        public bool InsideSets_Class_sS { get; init; }                    // \s, \S -- spaces
        public bool InsideSets_Class_sSx { get; init; }                   // \sx, \Sx -- syntax group; x is, for example, 's', ' ', '_', 'w', '.', ')', '(', '"', '\'', '>' and '<'.
        public bool InsideSets_Class_uU { get; init; }                    // \u, \U -- uppercase
        public bool InsideSets_Class_vV { get; init; }                    // \v, \V -- vertical spaces
        public bool InsideSets_Class_wW { get; init; }                    // \w, \W -- word characters
        public bool InsideSets_Class_X { get; init; }                     // \X -- eXtended grapheme cluster
        public bool InsideSets_Class_pP { get; init; }                    // ex.: \pL, \PL
        public bool InsideSets_Class_pPBrace { get; init; }               // \p{...}, \P{...}, \p{^...}, \P{^...}
        public bool InsideSets_Class_Name { get; init; }                  // ex: [[:digit:]]
        public bool InsideSets_Equivalence { get; init; }                 // ex: [[=a=]], matches 'a', 'A' and 'Á'
        public bool InsideSets_Collating { get; init; }                   // ex: [[.ch.]], matches 'ch' as a single match, [[.comma.]] matches ','

        public bool InsideSets_Operators { get; init; }                   // allow operators inside [...]; see operators bellow; when 'Brackets' is 'true' 
        public bool InsideSets_OperatorsExtended { get; init; }           // allow operators inside (?[...]); see operators bellow; when 'ExtendedBrackets' is 'true' 
        public bool InsideSets_Operator_Ampersand { get; init; }          // [[...] & [...]]
        public bool InsideSets_Operator_Plus { get; init; }               // [[...] + [...]]
        public bool InsideSets_Operator_VerticalLine { get; init; }       // [[...] | [...]]
        public bool InsideSets_Operator_Minus { get; init; }              // [[...] - [...]]
        public bool InsideSets_Operator_Circumflex { get; init; }         // [[...] ^ [...]]
        public bool InsideSets_Operator_Exclamation { get; init; }        // [![...] ...]
        public bool InsideSets_Operator_DoubleAmpersand { get; init; }    // [[...] && [...]]
        public bool InsideSets_Operator_DoubleVerticalLine { get; init; } // [[...] || [...]]
        public bool InsideSets_Operator_DoubleMinus { get; init; }        // [[...] -- [...]]
        public bool InsideSets_Operator_DoubleTilde { get; init; }        // [[...] ~~ [...]]

        public bool Anchor_Circumflex { get; init; }                      // ^ -- beginning of string; in multiline mode: also beginning of line
        public bool Anchor_Dollar { get; init; }                          // $ -- end of the string or before \n at the end of the string; in multiline mode: also before \n at the end of the line
        public bool Anchor_A { get; init; }                               // \A -- start of the string
        public bool Anchor_Z { get; init; }                               // \Z -- end of the string or before \n at the end of the string
        public bool Anchor_z { get; init; }                               // \z -- end of the string
        public bool Anchor_G { get; init; }                               // \G -- end of previous match, or start of string
        public bool Anchor_bB { get; init; }                              // \b, \B -- boundary between \w and \W; see also Esc_b, InsideSets_Esc_b
        public bool Anchor_bg { get; init; }                              // \b{g} -- Unicode extended grapheme cluster boundary; see also Anchor_bBBrace
        public bool Anchor_bBBrace { get; init; }                         // \b{boundary_type}, \B{boundary_type}; ex.: \b{wb}, which is almost similar to \b
        public bool Anchor_K { get; init; }                               // \K -- Keep the stuff left of the \K
        public bool Anchor_mM { get; init; }                              // \m -- start of word, \M -- end of word
        public bool Anchor_LtGt { get; init; }                            // \<, \> -- start of a word, end of a word
        public bool Anchor_GraveApos { get; init; }                       // \`, \' -- start of string (like \A), end of string (like \z)
        public bool Anchor_yY { get; init; }                              // \y, \Y -- between graphemes (\X)

        // NOTE. If Parentheses is Backslashed, then it will use "\(?" instead of "(?"

        public bool NamedGroup_Apos { get; init; }                        // (?'name'...)
        public bool NamedGroup_LtGt { get; init; }                        // (?<name>...)
        public bool NamedGroup_PLtGt { get; init; }                       // (?P<name>...)
        public bool NamedGroup_AtApos { get; init; }                      // (?@'name'...)
        public bool NamedGroup_AtLtGt { get; init; }                      // (?@<name>...)
        public bool CapturingGroup { get; init; }                         // (?@...)
        public bool NoncapturingGroup { get; init; }                      // (?:...)
        public bool PositiveLookahead { get; init; }                      // (?=...)
        public bool NegativeLookahead { get; init; }                      // (?!...)
        public bool PositiveLookbehind { get; init; }                     // (?<=...)
        public bool NegativeLookbehind { get; init; }                     // (?<!...)
        public bool AtomicGroup { get; init; }                            // (?>...)
        public bool BranchReset { get; init; }                            // (?|...)
        public bool NonatomicPositiveLookahead { get; init; }             // (?*...)
        public bool NonatomicPositiveLookbehind { get; init; }            // (?<*...)
        public bool AbsentOperator { get; init; }                         // (?~...)
        public bool AllowSpacesInGroups { get; init; }                    // allow spaces like '( ? < name >...)' when whitespaces are enabled by options

        public BackrefEnum Backref_Num { get; init; }                     // ex.: \1, \20, \100
        public bool Backref_kApos { get; init; }                          // \k'name'
        public bool Backref_kLtGt { get; init; }                          // \k<name>
        public bool Backref_kBrace { get; init; }                         // \k{name}
        public bool Backref_kNum { get; init; }                           // ex: \k2
        public bool Backref_kNegNum { get; init; }                        // ex: \k-2
        public BackrefModeEnum Backref_gApos { get; init; }               // \g'name' or g'number'
        public BackrefModeEnum Backref_gLtGt { get; init; }               // \g<name> or g<number>
        public BackrefModeEnum Backref_gNum { get; init; }                // ex: \g2
        public BackrefModeEnum Backref_gNegNum { get; init; }             // ex: \g-2
        public BackrefModeEnum Backref_gBrace { get; init; }              // \g{name} or \g{number} or \g{-number} or g{+number}
        public bool Backref_PEqName { get; init; }                        // (?P=name)
        public bool AllowSpacesInBackref { get; init; }                   // allow spaces like '\k < name >' when whitespaces are enabled by options (currently only \k and \g with <name> and 'name' are supported)

        public bool Recursive_Num { get; init; }                          // ex: (?2), (?0)
        public bool Recursive_PlusMinusNum { get; init; }                 // ex: (?-2), (?+2)
        public bool Recursive_R { get; init; }                            // (?R)
        public bool Recursive_Name { get; init; }                         // (?&name)
        public bool Recursive_PGtName { get; init; }                      // (?P>name)

        public bool Quantifier_Asterisk { get; init; }                    // *
        public PunctuationEnum Quantifier_Plus { get; init; }             // + or \+
        public PunctuationEnum Quantifier_Question { get; init; }         // ? or \?
        public PunctuationEnum Quantifier_Braces { get; init; }           // {n}, {n,}, {n,m}, or \{n\}, \{n,\}, \{n,m\}
        public PunctuationEnum Quantifier_Braces_FreeForm { get; init; }  // {expr} or \{expr\}, where 'expr' is an expression (not parsed by this colourer),
                                                                          // usually related to Approximate Matching (example: "abcd{+1#2})". (See also 'FuzzyMatchingParams')
        public SpaceUsageEnum Quantifier_Braces_Spaces { get; init; }     // enable spaces like { n }, { n , m }
        public bool Quantifier_LowAbbrev { get; init; }                   // also allow {,m} if Quantifier_Braces is set

        public bool Conditional_BackrefByNumber { get; init; }            // (?(number)...|...), (?(+number)...|...), (?(-number)...|...)
        public bool Conditional_BackrefByName { get; init; }              // (?(name)...|...)
        public bool Conditional_Pattern { get; init; }                    // (?(pattern)...|...)
        public bool Conditional_PatternOrBackrefByName { get; init; }     // (?(xxx)...|...), where xxx is a name (if exists) or a pattern
        public bool Conditional_BackrefByName_Apos { get; init; }         // (?('name')...|...)
        public bool Conditional_BackrefByName_LtGt { get; init; }         // (?(<name>)...|...)
        public bool Conditional_R { get; init; }                          // (?(R)...|...), (?(R1)...|...), (?(R2)...|...), etc.
        public bool Conditional_RName { get; init; }                      // (?(R&name)...|...)
        public bool Conditional_DEFINE { get; init; }                     // (?(DEFINE)...|...)
        public bool Conditional_VERSION { get; init; }                    // (?(VERSION=decimal)...|...) or (?(VERSION>=decimal)...|...)

        public bool ControlVerbs { get; init; }                           // (*verb), (*verb:...), (*:name), where verb is PRUNE, SKIP, MARK, THEN, COMMIT, F, FAIL, ACCEPT, UTF, UTF8 and UCP; (*:name) is similar to (*MARK:name)
        public bool ScriptRuns { get; init; }                             // (*...:...), for ex.: (*atomic:...)
        public bool Callouts { get; init; }                               // "callouts": (*func) -- invoking custom functions 

        // 

        public bool EmptyConstruct { get; init; }                         // (?)
        public bool EmptyConstructX { get; init; }                        // (? ) when 'x' or 'xx' flags are enabled.
        public bool EmptySet { get; init; }                               // [], see also 'SyntaxOptions.AllowEmptySets'

        //

        public bool AsciiOnly { get; init; }                              // supports ASCII characters only (no Unicode)

        public bool SplitSurrogatePairs { get; init; }                    // when the text contains a surrogate pair (e.g. “😎” U+1F60E), then “.” matches two components separately: D83D and DE0E‎
                                                                          // if it is 'false', then '.' returns a single result (32-bit surrogate pair)

        public bool AllowDuplicateGroupName { get; init; }                // allow duplicate names like "(?<n>abc)|(?<n>def)"

        public bool FuzzyMatchingParams { get; init; }                    // parameters for fuzzy matching (programmatically, not pattern syntax); (see also 'Quantifier_Braces_FreeForm')

        public CatastrophicBacktrackingEnum TreatmentOfCatastrophicPatterns { get; init; } // what happens in case of "(a*)*b" on "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac"
    }
}

