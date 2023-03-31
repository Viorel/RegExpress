using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary
{
    public struct FeatureMatrix
    // (Must not be changed to class)
    {
        public enum PunctuationEnum
        {
            None,
            Normal,
            Backslashed
        }


        public enum SpaceUsage
        {
            None,           // for example, cannot put spaces in 'a{ 3 }'; must use 'a{3}'
            XModeOnly,      // 'a{ 3 }' is valid if pattern whitespaces (x or xx flag) are enabled
            Both            // 'a{ 3 }' is valid regardless of flags
        }


        public PunctuationEnum Parentheses;                 // (...) or \(...\)

        public bool Brackets;                               // [...]
        public bool ExtendedBrackets;                       // (?[...])

        public PunctuationEnum VerticalLine;                // | or \| -- alternation

        public bool InlineComments;                         // (?#comment) or \(?#comment\)
        public bool XModeComments;                          // #comment; see also XFlag
        public bool InsideSets_XModeComments;               // #comment inside [...]; see also XFlag

        public bool Flags;                                  // (?flags)
        public bool ScopedFlags;                            // (?flags:...)
        public bool CircumflexFlags;                        // (?^flags)
        public bool ScopedCircumflexFlags;                  // (?^flags:...)
        public bool XFlag;                                  // 'x' flag
        public bool XXFlag;                                 // 'xx' flag

        public bool Literal_QE;                             // \Q...\E
        public bool InsideSets_Literal_QE;                  // [\Q...\E]

        public bool Esc_a;                                  // \a
        public bool Esc_b;                                  // \b -- backspace; see also Anchor_bB
        public bool Esc_e;                                  // \e
        public bool Esc_f;                                  // \f
        public bool Esc_n;                                  // \n; see also \N
        public bool Esc_r;                                  // \r
        public bool Esc_t;                                  // \t
        public bool Esc_v;                                  // \v -- vertical tab (0x0B); see also Class_vV
        public bool Esc_Octal0_1_3;                         // ex.: \05, \077, \0101 -- octal, one, two or three digits after \0
        public bool Esc_Octal_1_3;                          // ex.: \5, \77, \101 -- octal, one, two or three digits
        public bool Esc_Octal_2_3;                          // ex.: \77, \101 -- octal, two or three digits
        public bool Esc_oBrace;                             // ex.: \o{00101} -- octal
        public bool Esc_x2;                                 // ex: \x41, which is 'A'
        public bool Esc_xBrace;                             // ex: \x{000041}
        public bool Esc_u4;                                 // ex: \u0041
        public bool Esc_U8;                                 // ex: \U00000041
        public bool Esc_uBrace;                             // ex: \u{41}
        public bool Esc_UBrace;                             // ex: \U{41}
        public bool Esc_c1;                                 // ex: \cZ, which is 0x1A
        public bool Esc_CMinus;                             // ex: \C-Z, which is 0x1A
        public bool Esc_NBrace;                             // ex: \N{U+0041}, \N{unicode name}
        public bool GenericEscape;                          // \c, where c -- any character

        public bool InsideSets_Esc_a;                       // \a
        public bool InsideSets_Esc_b;                       // \b -- backspace; see also Anchor_bB
        public bool InsideSets_Esc_e;                       // \e
        public bool InsideSets_Esc_f;                       // \f
        public bool InsideSets_Esc_n;                       // \n
        public bool InsideSets_Esc_r;                       // \r
        public bool InsideSets_Esc_t;                       // \t
        public bool InsideSets_Esc_v;                       // \v
        public bool InsideSets_Esc_Octal0_1_3;              // ex.: \05, \077, \0101 -- octal, one, two or three digits after \0
        public bool InsideSets_Esc_Octal_1_3;               // ex.: \5, \77, \101 -- octal, one, two or three digits
        public bool InsideSets_Esc_Octal_2_3;               // ex.: \77, \101 -- octal, two or three digits
        public bool InsideSets_Esc_oBrace;                  // ex.: \o{00101} -- octal
        public bool InsideSets_Esc_x2;                      // ex: \x41
        public bool InsideSets_Esc_xBrace;                  // ex: \x{000041}
        public bool InsideSets_Esc_u4;                      // ex: \u0041
        public bool InsideSets_Esc_U8;                      // ex: \U00000041
        public bool InsideSets_Esc_uBrace;                  // ex: \u{41}
        public bool InsideSets_Esc_UBrace;                  // ex: \U{41}
        public bool InsideSets_Esc_c1;                      // ex: \cZ, which is 0x1A
        public bool InsideSets_Esc_CMinus;                  // ex: \C-Z, which is 0x1A
        public bool InsideSets_Esc_NBrace;                  // ex: \N{U+0041}, \N{unicode name}
        public bool InsideSets_GenericEscape;               // [\c], where c -- any character

        public bool Class_Dot;                              // . -- any, except newline (\n), or including newline in single-line mode
        public bool Class_Cbyte;                            // \C -- a single byte
        public bool Class_Ccp;                              // \C -- a single code point
        public bool Class_dD;                               // \d, \D -- digits
        public bool Class_hHhexa;                           // \h, \H -- hexadecimal
        public bool Class_hHhorspace;                       // \h, \H -- horizontal space
        public bool Class_lL;                               // \l, \L -- lowercase
        public bool Class_N;                                // \N -- any except \n
        public bool Class_O;                                // \O -- any
        public bool Class_R;                                // \R  -- linebreak
        public bool Class_sS;                               // \s, \S -- spaces
        public bool Class_sSx;                              // \sx, \Sx -- syntax group; x is, for example, 's', ' ', '_', 'w', '.', ')', '(', '"', '\'', '>' and '<'.
        public bool Class_uU;                               // \u, \U -- uppercase
        public bool Class_vV;                               // \v, \V -- vertical spaces; see also Esc_v
        public bool Class_wW;                               // \w, \W -- word characters
        public bool Class_X;                                // \X -- eXtended grapheme cluster, or a non-combining character followed by a sequence of zero or more combining characters
        public bool Class_Not;                              // \!c, where c -- any character, or \!\c, where \c -- an escape

        public bool Class_pP;                               // ex.: \pL, \PL
        public bool Class_pPBrace;                          // \p{...}, \P{...}, \p{^...}, \P{^...}

        public bool InsideSets_Class_dD;                    // \d, \D -- digits
        public bool InsideSets_Class_hHhexa;                // \h, \H -- hexadecimal
        public bool InsideSets_Class_hHhorspace;            // \h, \H -- horizontal space
        public bool InsideSets_Class_lL;                    // \l, \L -- lowercase
        public bool InsideSets_Class_R;                     // \R  -- linebreak
        public bool InsideSets_Class_sS;                    // \s, \S -- spaces
        public bool InsideSets_Class_sSx;                   // \sx, \Sx -- syntax group; x is, for example, 's', ' ', '_', 'w', '.', ')', '(', '"', '\'', '>' and '<'.
        public bool InsideSets_Class_uU;                    // \u, \U -- uppercase
        public bool InsideSets_Class_vV;                    // \v, \V -- vertical spaces
        public bool InsideSets_Class_wW;                    // \w, \W -- word characters
        public bool InsideSets_Class_X;                     // \X -- eXtended grapheme cluster
        public bool InsideSets_Class_pP;                    // ex.: \pL, \PL
        public bool InsideSets_Class_pPBrace;               // \p{...}, \P{...}, \p{^...}, \P{^...}
        public bool InsideSets_Class;                       // ex: [:digit:] inside [...]
        public bool InsideSets_Equivalence;                 // ex: [=a=] inside [...]
        public bool InsideSets_Collating;                   // ex: [.ch.] inside [...]

        public bool InsideSets_Operators;                   // allow operators inside [...]; see operators bellow
        public bool InsideSets_OperatorsExtended;           // allow operators inside (?[...]); see operators bellow
        public bool InsideSets_Operator_Ampersand;          // [[...] & [...]]
        public bool InsideSets_Operator_Plus;               // [[...] + [...]]
        public bool InsideSets_Operator_VerticalLine;       // [[...] | [...]]
        public bool InsideSets_Operator_Minus;              // [[...] - [...]]
        public bool InsideSets_Operator_Circumflex;         // [[...] ^ [...]]
        public bool InsideSets_Operator_Exclamation;        // [![...] ...]
        public bool InsideSets_Operator_DoubleAmpersand;    // [[...] && [...]]
        public bool InsideSets_Operator_DoubleVerticalLine; // [[...] || [...]]
        public bool InsideSets_Operator_DoubleMinus;        // [[...] -- [...]]
        public bool InsideSets_Operator_DoubleTilde;        // [[...] ~~ [...]]

        public bool Anchor_Circumflex;                      // ^ -- beginning of string; in multiline mode: also beginning of line
        public bool Anchor_Dollar;                          // $ -- end of the string or before \n at the end of the string; in multiline mode: also before \n at the end of the line
        public bool Anchor_A;                               // \A -- start of the string
        public bool Anchor_Z;                               // \Z -- end of the string or before \n at the end of the string
        public bool Anchor_z;                               // \z -- end of the string
        public bool Anchor_G;                               // \G -- end of previous match, or start of string
        public bool Anchor_bB;                              // \b, \B -- boundry between \w and \W; see also Esc_b, InsideSets_Esc_b
        public bool Anchor_bg;                              // \b{g} -- Unicode extended grapheme cluster boundary; see also Anchor_bBBrace
        public bool Anchor_bBBrace;                         // \b{boundry_type}, \B{boundry_type}; ex.: \b{wb}, which is almost similar to \b
        public bool Anchor_K;                               // \K -- Keep the stuff left of the \K
        public bool Anchor_LtGt;                            // \<, \> -- start of a word, end of a word
        public bool Anchor_GraveApos;                       // \`, \' -- start of string (like \A), end of string (like \z)
        public bool Anchor_yY;                              // \y, \Y -- between graphemes (\X)

        // NOTE. If Parentheses is Backslashed, then it will use "\(?" instead of "(?"

        public bool NamedGroup_Apos;                        // (?'name'...)
        public bool NamedGroup_LtGt;                        // (?<name>...)
        public bool NamedGroup_PLtGt;                       // (?P<name>...)
        public bool NoncapturingGroup;                      // (?:...)
        public bool PositiveLookahead;                      // (?=...)
        public bool NegativeLookahead;                      // (?!...)
        public bool PositiveLookbehind;                     // (?<=...)
        public bool NegativeLookbehind;                     // (?<!...)
        public bool AtomicGroup;                            // (?>...)
        public bool BranchReset;                            // (?|...)
        public bool NonatomicPositiveLookahead;             // (?*...)
        public bool NonatomicPositiveLookbehind;            // (?<*...)
        public bool AbsentOperator;                         // (?~...)
        public bool AllowSpacesInGroups;                    // allow spaces like '( ? < name >...)' when whitespaces are enabled by options

        public bool Backref_1_9;                            // \1, \2, ..., \9
        public bool Backref_Num;                            // ex.: \1, \20, \100; (more digits after '\')
        public bool Backref_kApos;                          // \k'name'
        public bool Backref_kLtGt;                          // \k<name>
        public bool Backref_kBrace;                         // \k{name}
        public bool Backref_kNum;                           // ex: \k2
        public bool Backref_kNegNum;                        // ex: \k-2
        public bool Backref_gApos;                          // \g'name' or g'number'
        public bool Backref_gLtGt;                          // \g<name> or g<number>
        public bool Backref_gNum;                           // ex: \g2
        public bool Backref_gNegNum;                        // ex: \g-2
        public bool Backref_gBrace;                         // \g{name} or \g{number} or \g{-number} or g{+number}
        public bool Backref_PEqName;                        // (?P=name)
        public bool AllowSpacesInBackref;                   // allow spaces like '\k < name >' when whitespaces are enabled by options (currently only \k and \g with <name> and 'name' are supported)

        public bool Recursive_Num;                          // ex: (?2), (?0)
        public bool Recursive_PlusMinusNum;                 // ex: (?-2), (?+2)
        public bool Recursive_R;                            // (?R)
        public bool Recursive_Name;                         // (?&name)
        public bool Recursive_PGtName;                      // (?P>name)

        public bool Quantifier_Asterisk;                    // *
        public PunctuationEnum Quantifier_Plus;             // + or \+
        public PunctuationEnum Quantifier_Question;         // ? or \?
        public PunctuationEnum Quantifier_Braces;           // {n}, {n,}, {n,m}, or \{n\}, \{n,\}, \{n,m\}
        public SpaceUsage Quantifier_Braces_Spaces;         // enable spaces like { n }, { n , m }
        public bool Quantifier_LowAbbrev;                   // also allow {,m} if Quantifier_Braces is set

        public bool Conditional_BackrefByNumber;            // (?(number)...|...), (?(+number)...|...), (?(-number)...|...)
        public bool Conditional_BackrefByName;              // (?(name)...|...)
        public bool Conditional_Pattern;                    // (?(pattern)...|...)
        public bool Conditional_PatternOrBackrefByName;     // (?(xxx)...|...), where xxx is a name (if exists) or a pattern
        public bool Conditional_BackrefByName_Apos;         // (?('name')...|...)
        public bool Conditional_BackrefByName_LtGt;         // (?(<name>)...|...)
        public bool Conditional_R;                          // (?(R)...|...), (?(R1)...|...), (?(R2)...|...), etc.
        public bool Conditional_RName;                      // (?(R&name)...|...)
        public bool Conditional_DEFINE;                     // (?(DEFINE)...|...)
        public bool Conditional_VERSION;                    // (?(VERSION=decimal)...|...) or (?(VERSION>=decimal)...|...)

        public bool ControlVerbs;                           // (*verb), (*verb:...), (*:name), where verb is PRUNE, SKIP, MARK, THEN, COMMIT, F, FAIL, ACCEPT, UTF, UTF8 and UCP; (*:name) is similar to (*MARK:name)
        public bool ScriptRuns;                             // (*...:...), for ex.: (*atomic:...)

        // 

        public bool EmptyConstruct;                         // (?)
        public bool EmptyConstructX;                        // (? ) when 'x' or 'xx' flags are enabled.
        public bool EmptySet;                               // []
    }
}

