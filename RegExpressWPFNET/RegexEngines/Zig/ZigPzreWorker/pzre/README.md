# Pragmatic Zig Regex (PZRE)
PZRE is a regular expression engine and analysis library built for Zig. The primary goal is to provide a well performing and feature-complete engine, that has its entire pipeline and list of features fully available at `comptime`, while also being fully safe and predictable to untrusted input at runtime.

## Predictability and Safety
At its core, the engine uses a [Thompson NFA](https://swtch.com/~rsc/regexp/regexp1.html) architecture to process patterns. This makes it part of the family of non-recursive engines that guarantee asymptotic time complexity of $O(mn)$ against input length $n$ and the maximum number of parallel states $m$. The core idea is that the engine consistently steps forward on input evaluating all possible matches in parallel without ever backtracking. This approach eliminates the risk of **ReDoS** and is popular in other great engines such as [RE2](https://github.com/google/re2/tree/main).

The default matching semantics adhere to [POSIX](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html) standards where the leftmost-longest match always wins. This is the traditional unix term utils behavior (e.g. `grep`). This was chosen as the default as it allows for a very simple and compact engine.

The compiled machines are fully immutable, and the core matching API performs no memory allocations. The multithreaded pooling system can be warmed up to guarantee allocation-free matching. Furthermore, there are no hidden allocations as allocators are never stored internally.

Runtime and comptime compiled objects return the same base types. Importantly, the API is designed to be functionally identical whether executed at comptime or runtime. Given this along with the heavy focus on Zig comptime optimizations, the system is highly generic. The unified API was carefully designed to accommodate [zls](https://github.com/zigtools/zls), ensuring it correctly continues to provide method completions everywhere.

PZRE treats all runtime inputs as untrusted, and a major goal is to make it safe for untrusted input. See [limits.zig](src/language.zig) for the resource contract.

## Fast with sensible defaults
The default engine has been designed to be as simple as possible. Currently, a single state occupies 4 bytes for most common-case patterns, and has been designed so that it scales well as the number of compiled machines increase. This machine does not support arbitrary capture grouping by design, as it would introduce all kinds of branching confusion. Instead a separate machine will be designed specifically for that purpose.

A core design philosophy is that input patterns should be analyzed and the best approach chosen. If the user hints that capture group extraction will be required, the engine does not automatically pick the slower machine, but instead it checks whether the simpler machine could still be used by compiling in a segmented manner.

The default machine has an experimental design with only relative indices so that they can be easily shuffled, concatenated, split and even embedded into each other. Making them highly flexible for clever use. This combined with the way contexts and integer sets are managed, the system should theoretically approach a 4-byte per state total memory usage as the number of compiled machines increases, without even accounting for machine overlapping.

Additionally an important goal is to detect when machines should not even be compiled in the first place, and instead deploy more efficient SIMD algorithms.

## Showcase
```zig
// build.zig
  const pzre = b.dependency(pzre, .{
    .target = target,
    .optimize = optimize,
  });
  exe.root_module.addImport(pzre, pzre.module(pzre));
```

```zig
const pzre = @import("pzre");
const compile = pzre.compile;
const Match = pzre.Match;

test "Showcase: Basic Matching" {
  const gpa = std.testing.allocator;

  var re = try compile.nfa(.{}, gpa, "[A-Za-z][a-z_]+");
  defer re.deinit(gpa);

  // Each machine requires a mutable context
  // In single threaded environments a single context should be used 
  //  that is shared between all compiled machines
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  // The core matching api cannot error, and will not perform allocations
  if (re.match(&ctx, ":: Mark ?")) |match| {
    const expected = Match{ .loc = .init(3, 7), .str = "Mark" };
    try expectDeeplyEqual(expected, match);
  } else unreachable;


  // Iteration
  var it = re.matchIter(&ctx, "123; snake_case; PascalCase");
  try expectEqualStrings("snake_case", it.next().?.str);
  try expectEqualStrings("Pascal", it.next().?.str);
  try expectEqualStrings("Case", it.next().?.str);
  try expectEqual(null, it.next());


  // All examples above compile and match at runtime
  // The entire compilation pipeline, and matching API are legal for comptime
  // 
  // The Nfa object returned by comptime compilation is the exact same type 
  //  as returned by runtime compilation. As such, both provide the same matching api

  comptime { // compiling and matching at comptime
    var hex = compile.nfaComptime(.{.context = .compact_fixed}, "abc\\b");
    var comptime_ctx = hex.initContextFixed();
    // initContextFixed is a wrapper for: initContext(undefined) catch undefined

    const input = "abc abcabc abc";
    var cit = hex.matchIter(&comptime_ctx, input);
    try expect(cit.next() != null);
    try expect(cit.next() != null);
    try expect(cit.next() != null);
    try expect(cit.next() == null);
  }

  { // compiling at comptime and matching at runtime
    var hex = comptime compile.nfaComptime(.{
      // We need to set this in order to use the context from the runtime pattern
      // See 'Showcase: context'
      .limits = .{ .context_breakpoint = .i16 }
    }, "abc\\b");
    try ctx.update(gpa, hex);

    try expect(hex.matches(&ctx, "abc"));
  }
}
```
For a longer tutorial see [showcase.zig](src/showcase.zig)

# Current status as of 0.1.0
The core of the engine is designed so that all of the language features are supported while being untrusted input safe including ReDoS, making it highly usable for an initial release. The simple 4-byte machine is fully implemented and highly optimized. The most pressing missing features are the detection of simpler patterns for SIMD-accelerated matching, optimizations for compiling large sets of machines, and several crucial AST optimizations.

Everything demonstrated in the showcases has been implemented, including proper context management and multithreading. The engine has been tested extensively, although bugs may still exist.

Other critical features remaining on the roadmap are capture group extraction and UTF-8 support. UTF-8 will be relatively straightforward to implement, while capture groups will require designing a dedicated, separate machine.

# Planned
- manual stack management for compilation recursive algorithms (optimization and parsing)
- improve (zig) comptime compilation performance
- lazy operators `x*?` `x+?` `x??` etc
- utf8
- capture groups
- DFA construction
- ascii class syntax `[[:upper:]]`
- serialize / deserialize
- missing search-problems.
- additional AST optimizations
- machine families/classes
- RegexSet
- Leftmost-first semantics

# Not planned
- backreferences
- subroutine calls
- lookaheads/behinds

# Syntax Overview
## Operators
- repetition `a*` `a+` `a{n}` `a{n,}` `a{n,m}` `a?`
- concatenation `ab`
- union `a|b`
- grouping `(ab)`
- Precedence: repetition > concatenation > union

## Sets
- `[]` empty set; matches nothing
- `[^]` universe
- `.` equivalent to `[^\n]`
- `[^...]` complement
- `[&-n]`, `[a-z]` utf8 codepoint range
- `\d` `\D` `\s` `\S` `\w` `\W` perl character classes 
- character classes are comptime configurable
- Magic is removed from characters in set context, e.g. `[)(])*]` matches `)`, `(`, `]`, `*`, and `[)]` is equivalent to `[\)]`
- Hyphens are treated literally if they are at the first or last position of the list, e.g. `[a-]` matches `a` or `-`
- Hyphens require a concrete value for each operand to be interpreted as such, e.g. `[\d-f]` fails
- Hyphens require the first operand to be strictly lower than the second operand, e.g. `[b-a]` fails
- Empty sets return an error as it is impossible for them to match anything
- The first closing bracket in set definition syntax `[]]` or `[]abc]` is interpreted as an element

## Epsilon
- The NFA state is unable to represent epsilon; epsilon is handled immediately when the NFA is being created
- The AST nodes can represent epsilon

**In the language**:
- `()` directly parsed into epsilon
- `a{0,0}` parsed into a quantifier (present in AST); optimizes to epsilon
- Either empty side of a union: `||`, `|`, `a|)`, `|b|a`, is directly parsed as epsilon
- The completely empty pattern parses into epsilon

## Assertions
- `^` beginning of line 
- `$` end of line
- `\A` start of text 
- `\z` end of text
- `\b` word boundary ; defined by `\w` class ; `\B` negation

## Magic
- Closing delimiters are treated as literals if they are unexpected, e.g. `a[` matches literally
- Any magic symbol can be escaped for literal representation, e.g. `\*`
- Escape sequences are treated uniformly outside and inside sets
- Escaping a magic symbol that already would have been treated literally does nothing, e.g. `a[` is equivalent to `a\[`

## Other escape sequences
- `\0` `0x00` null
- `\a` `0x07` bell
- `\e` `0x1B` escape
- `\f` `0x0C` form feed
- `\t` `0x09` tabulator
- `\n` `0x0A` newline
- `\r` `0x0D` carriage return
- `\v` `0x0B` vertical tab

## Matching semantics
- leftmost-longest match wins
- multiline mode: assertions `^` and `$` match beginning of line

## Arbitrary characters
- `\xNN` 8-bit Hex Byte. Currently `maxInt(u8)` cannot be used due to implementation

# Syntax specification
## Grammar 
- Lowercase starting names are terminals
- Uppercase starting names are nonterminals
- Rules are in precedence order, top (highest) to bottom (lowest)
- `? ... ?` is a freeform rule

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
Regex        := Union EOF

Union        := Concat ('|' Concat)*
Concat       := Quantifier Quantifier*

Quantifier   := Term (QuantifierOp)?
QuantifierOp := '*' | '+' | '?' | RepeatExact

Term         := Literal | Set | Assertion | '(' Union ')'
Assertion    := '^' | '$' | assert_escape_sequence

Set          := '[' '^'? '-'? (SetRange | SetChar | perl_set)* '-'? ']' | perl_set
SetRange     := SetChar '-' SetChar
SetChar      := escape_sequence | hex_sequence 
              | ? any Literal or magic_symbol but not '-' or ']' ?

Literal      := char | hex_sequence | ? any escape_sequence not acting as a magic_symbol ?
RepeatExact  := '{' digit+ '}' | '{' digit+ ',' digit* '}'
```

# Other similar libraries
The philosophy of pzre adheres closely to [re2](https://github.com/google/re2/tree/main). 
