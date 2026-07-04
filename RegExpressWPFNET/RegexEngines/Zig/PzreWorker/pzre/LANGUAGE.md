# Syntax and language reference
back to [README](README.md)
See the (parsing) [Semantics](src/compile/compile.zig) configuration field for the fields that affect parsing.

See [ParseError](src/compile/parse.zig) for errors that can be encountered during syntax analysis

**DISCLAIMER** utf8 has not been implemented yet

The compilation pipeline uses `usize` integers

## Contents
- [Syntax](#syntax)
  - [Literals](#literals)
  - [Operators](#operators)
  - [Sets](#sets)
    - [Perl character classes](#perl-character-classes)
    - [Hyphen](#hyphen)
  - [Magic](#magic)
  - [Assertions](#assertions)
  - [Other escape sequences](#other-escape-sequences)
  - [Epsilon](#epsilon)
  - [Captures](#captures)
  - [Interpretation of user intent](#interpretation-of-user-intent)
- [Grammar](#grammar)
  - [Lexical](#lexical)
  - [Language](#language)

## Syntax
The goal is for the syntax to align with what is expected from established engines. 

### Literals
Every integer value that the *encoding* permits, except for the maximum possible value, can be used as a literal. This includes the zero-byte `\0`, as well as any other control value. Special characters (magic) can be escaped for literal interpretation.

If a pattern contains `maxInt` it produces a compile-error. A string (being matched against) that contains `maxInt` will silently never match.

So for ASCII, the entire range `[0, 255)` can be matched against.

If you match against an utf8 encoded string with a machine compiled for ASCII, it will simply interpret each utf8 code point as a literal within the range `[0, 255)`

### Operators
- repetition `a*` `a+` `a{n}` `a{n,}` `a{n,m}` `a?` (all greedy)
- concatenation `ab`
- union `a|b`
- grouping `(ab)`
- Precedence: repetition > concatenation > union

### Sets
- `.` equivalent to `[^\r\n]`
- `[^...]` complement
- `[&-n]`, `[a-z]` utf8 codepoint range
- Empty sets return an error as it is impossible for them to match anything
- The first closing bracket in set definition syntax `[]]` or `[]abc]` is interpreted as an element
- `[aab]` equiv to `[ab]`
- `[a-md-z]` equiv to `[a-z]`
- `x|(a|b|c)+|y` equiv to `[xy]|[abc]+`

#### Perl character classes
- `\d` equiv to `[0-9]`
- `\D` equiv to `[^0-9]`
- `\w` equiv to `[0-9A-Za-z_]`
- `\W` equiv to `[^0-9A-Za-z_]`
- `\s` equiv to `[ \t\n\r\f\v]`
- `\S` equiv to `[^ \t\n\r\f\v]`
- Perl sets work within set context `[\d]` equivalent to `\d`
- These are configurable. See [Sets](src/compile/compile.zig)

#### Hyphen
- Hyphens are treated literally if they are at the first or last position of the list, e.g. `[a-]` matches `a` or `-`
- Hyphens require a concrete value for each operand to be interpreted as such, e.g. `[\d-f]` fails
- Hyphens require the first operand to be strictly lower than the second operand, e.g. `[b-a]` fails. For the same reason `[---]` fails
- Hyphens cannot be chained `[a-b-c]`. This is valid though `[!--c]`


### Magic
- Any magic symbol can be escaped for literal representation, e.g. `\*`
- Magic is removed from characters in set context, e.g. `[)(])*]` matches `)`, `(`, `]`, `*`, and `[)]` is equivalent to `[\)]`
- Escaping a magic symbol that was already being treated as a literal in set-context does nothing, e.g. `[*]` is equivalent to `[\*]`

### Assertions
- `^` in multiline mode: beginning of line (after `[\r\n]`), in non-multiline mode equivalent to `\A`
- `$` in multiline mode: end of line (after `[\r\n]`), in non-multiline mode equivalent to `\z`
- `\A` start of text 
- `\z` end of text
- `\b` word boundary; defined by `\w` class; `\B` for negation. The word class is configurable. See [Sets](src/compile/compile.zig). End of/start of input is interpreted as a word boundary.

### Other escape sequences
- `\0` `0x00` null
- `\a` `0x07` bell
- `\e` `0x1B` escape
- `\f` `0x0C` form feed
- `\t` `0x09` tabulator
- `\n` `0x0A` newline
- `\r` `0x0D` carriage return
- `\v` `0x0B` vertical tab
- `\xNN` 8-bit Hex Byte in the integer range `[0, 255)`. Currently `maxInt(u8)` cannot be used due to implementation.

### Epsilon
- The NFA state is unable to represent epsilon; epsilon is handled immediately when the NFA is being created
- The AST nodes can represent epsilon

**In the language**:
- `()` directly parsed into epsilon
- `a{0,0}` parsed into a quantifier (present in AST); non-existent in state machines
- Either empty side of a union: `||`, `|`, `a|)`, `|b|a`, is directly parsed as epsilon
- The completely empty pattern parses into epsilon

### Captures
**CURRENTLY NOT IMPLEMENTED**
- `(a*)*`  how is this captured?

### Interpretation of user intent
The parsing engine avoids interpreting user intent and prefers erroring. Only "widely used" behavior will be implemented.

#### Implemented interpretations
- The hyphen/end-bracket behavior as explained in the [sets](#sets) section, e.g. `[]]` and `[-abc]`

#### implementation that differ from some engines
- `*` any quantifier at start-of-pattern is `error.UnexpectedToken` instead of `\*`

## Grammar 
- Lowercase starting names are terminals
- Uppercase starting names are nonterminals
- Rules are in precedence order, top (highest) to bottom (lowest)
- `? ... ?` is a freeform rule
- The grammar assumes default configuration.

I am not confident that it perfectly reflects the engine. But I think its 90% there. If anyone finds issues with it, let me know and I will fix it.

### Lexical
The lexer is strictly context-independent. All contextual meaning is resolved during the parsing phase.

```BNF
perl_set               := \d \D \s \S \w \W
escape_sequence        := \a \f \t \n \r \v \\ \* \+ \? \{ \} \| \. \[ \( \) \] \- \^ \$ \0 \e
assert_escape_sequence := \b \B \A \z
hex_sequence           := \xNN
magic_symbol           := * + ? | ^ $ [ ( {

digit                  := [0-9]
char                   := [a-zA-Z \t..]  // default case, any that does not match any above
```

### Language
```
Regex         := Union EOF

Union         := Concat ('|' Concat)*
Concat        := Quantifier Quantifier*

Quantifier    := Term (QuantifierOp)?
QuantifierOp  := '*' | '+' | '?' | RepeatExact

Term          := Literal | Set | Assertion | '(' Union ')'
Assertion     := '^' | '$' | assert_escape_sequence

Set           := '[' '^'? '-'? (SetRange | SetChar | perl_set)* '-'? ']' | perl_set
SetRange      := SetChar '-' SetChar
SetChar       := escape_sequence | hex_sequence 
              | ? any Literal or magic_symbol but not '-' or ']' ?

Literal       := char | digit | ',' | '-' | hex_sequence
              | ? any escape_sequence not acting as a magic_symbol ?
RepeatExact   := '{' digit+ '}' | '{' digit+ ',' digit* '}'
```
