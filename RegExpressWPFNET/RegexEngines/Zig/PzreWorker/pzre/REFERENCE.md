# Usage reference
back to [readme](README.md)
for the language, see [LANGUAGE.md](LANGUAGE.md)

This file is not meant to be read in sequence.

## Contents
- [Aliases used in this document](#aliases-used-in-this-document)
- [API objects](#api-objects)
  - [Regex](#regex)
  - [AnyRegex](#anyregex)
- [Compilation](#compilation)
  - [Architecture resolution](#architecture-resolution)
  - [Strategies](#strategies)
  - [Optimizations](#optimizations)
  - [Limits](#limits)
  - [Semantics](#semantics)
    - [multiline](#multiline)
    - [ignore whitespace](#ignore-whitespace)
    - [ignore all whitespace](#ignore-all-whitespace)
    - [ignore case](#ignore-case)
    - [dotall](#dotall)
    - [never implicit newline](#never-implicit-newline)
  - [Errors](#errors)
  - [compileOptimal](#compileoptimal)
- [Context](#context)
  - [Fixed type](#fixed-type)
    - [compact_fixed](#compactfixed)
  - [Dynamic type](#dynamic-type)
  - [Shareability](#shareability)
  - [Method summary](#context-method-summary)
- [Multithreading](#multithreading)
- [Matching API](#matching-api)
  - [match and matches](#match-and-matches)
  - [Iteration](#iteration)
  - [findAll](#findall)
  - [Replacement](#replacement)
- [Architectures](#architectures)
  - [Minimal nfa](#minimal-nfa)

## Aliases used in this document
```zig
const std = @import("std");
const gpa = std.testing.allocator;
const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;
const expectEqualStrings = std.testing.expectEqualStrings;

const pzre = @import("pzre");
const regex = pzre.regex;
const anyregex = pzre.anyregex;
const Arch = pzre.Arch;
const Range = pzre.Range;
```

## API objects
All objects expose the same matching and compilation API, and both can be compiled at Zig comptime or runtime. They differ only in how many architectures they hold and whether that set is fixed at the type level.

| object | architectures | dispatch | reach for it when |
| --- | --- | --- | --- |
| `Regex` | exactly one | static, zero-overhead | you know the single architecture you want |
| `AnyRegex` | a declared set | dynamic (tagged union) | you must store or pass machines of differing architectures under one type |
| `FixedRegex(n)` | predefined `AnyRegex` | dynamic | you want a fixed context of size `n` without declaring archs |
| `DynamicRegex` | predefined `AnyRegex` | dynamic | you want an allocator-managed context without declaring archs |

### Regex
*The primitive object: implements the matching API for exactly one architecture, with no dispatch overhead.*

`Regex` [impl](src/regex.zig) is the type that has the API implementations. The API is uniform between all API objects as all other objects call Regex internally.

The most explicit way to use the engine is to define a Regex type manually, and then compile it

```zig
const arch = ArchResolved{
  .minimal_nfa = .{
    .context = .{ .fixed = 64 },
    .offset_bp = .i8,
  },
};

const Re = regex.Regex(arch, .{});

var re = try Re.compile(.{}, gpa, "^abc");
defer re.deinit(gpa);

try expectEqual(Re, @TypeOf(re));

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matches(&ctx, "abc"));
```

The first argument of the Regex type is a resolved architecture which is completely unambiguous from interpretation. Architectures can be partially defined (see [architecture resolution](#architecture-resolution)) which will make compilation fill in the undefined fields as best it can

```zig
const re = comptime regex.compileComptime(.{
  .minimal_nfa = .{ .context = .compact_fixed },
}, .{}, "^abc");

const Expected = regex.Regex(.{
  .minimal_nfa = .{
    .context = .{.fixed = 3},
    .offset_bp = .i8,
  },
}, .{});
try expectEqual(Expected, @TypeOf(re));
```

This arch-targeting compilation exists for when you wish to be fully in control of what executable code is included in your binary. Every distinct Regex object defined for an architecture that matches at runtime will force the Zig compiler to include its executable code in the binary.

Regex objects can also be compiled directly by telling the system to deduce the most optimal architecture for a pattern at comptime. See [compileOptimal](#compileoptimal)

### AnyRegex
*A type-erased object holding any of a declared set of architectures; same API as `Regex`, with dynamic dispatch.*

`AnyRegex` [impl](src/anyregex.zig) is a completely type-erased pattern-matching object. It exposes the exact same matching and compilation API as `Regex`, but instead of being tied to one architecture, it can hold machines of any of the architectures you allow it to draw from. Reach for it when you need to store machines of differing architectures in the same slice or array, refer to them under one type in function signatures, or support a broad set of architectures for situations you cannot predict at comptime.

The set of allowed architectures is defined strictly by the slice you pass in.

```zig
const Re = anyregex.AnyRegex(&.{
  .{ .minimal_nfa = .{ .offset_bp = .i8,  .context = .{ .dynamic = .u16 } } },
  .{ .minimal_nfa = .{ .offset_bp = .i16, .context = .{ .dynamic = .u16 } } },
}, .{});
 
var re = try Re.compile(.{}, gpa, "[A-Z][a-z_]+");
defer re.deinit(gpa);
```

The second parameter to AnyRegex is the [Global](src/compile/compile.zig) config.

**Predefined wrappers.** For the common cases you do not need to define the architecture set yourself. Two predefined wrappers cover most usage:

`FixedRegex(n)` uses a fixed context of size `n`. Compiled machine sizes cannot exceed `n`, the context always consumes the same fixed amount of memory, and it supports matching at comptime.

`DynamicRegex` uses an allocator-managed context that starts small and can be extended or shrunk. It supports machines of any size, but matching cannot be performed at comptime.

```zig
var fixed = try anyregex.FixedRegex(128).compile(.{}, gpa, "[A-Z][a-z_]+");
defer fixed.deinit(gpa);
 
var dynamic = try anyregex.DynamicRegex.compile(.{}, gpa, "[A-Z][a-z_]+");
defer dynamic.deinit(gpa);
```

**Executable bloat.** The executable code for every included architecture is included in the final binary. The Zig compiler can sometimes eliminate code it can prove is unused, but especially when compiling at runtime it is impossible for the compiler to deduce which architectures will actually be used. Curate the architecture set carefully, since each extra architecture is extra code you carry.

**Performance.** Matching through `AnyRegex` incurs a minor dynamic dispatch overhead from the internal tagged-union switch. For zero-overhead static routing, use `Regex` directly.

**Context usage.** The `AnyRegex` context is a struct with one field for every unique context type its architectures require, so it is worth being mindful of how many distinct context-dependent architectures you include. The system compile-errors if architecture sub-definitions do not share the same context type, or if `.compact_fixed` is used.

```zig
// Compiles because pike_vm and minimal_nfa use fundamentally different context types
const ok = &.{
  .{ .minimal_nfa = .{ .context = .{ .fixed = 128 } } },
  .{ .pike_vm    = .{ .offset_bp = .i16, .context = .{ .dynamic = .u16 } } },
};

// Does not compile because the AnyRegex.Context definition would be bloated redundantly
const bad = &.{
  .{ .minimal_nfa = .{ .context = .{ .fixed = 128 } } },
  .{ .minimal_nfa = .{ .offset_bp = .i16, .context = .{ .dynamic = .u16 } } },
};
```

(`pike_vm` is not implemented as of writing.) It is redundant to include several variations of the same context architecture. For comptime matching, all included architectures must use fixed contexts; never use `compact_fixed`, as it would bloat the type-erased wrapper. Context definitions do not change whether runtime or comptime compilation is supported.

From a high-level perspective, `initContextIncluding` and all other context methods behave exactly as they do on `Regex`. You do not have to reason about which architectures are present for a given `AnyRegex`: always assume a context must be created, passed to the methods, and deinited at the end.

```zig
var re = try anyregex.DynamicRegex.compile(.{}, gpa, "[A-Z][a-z_]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
if (re.match(&ctx, "camelCase")) |m| {
  try expectEqualStrings("Case", m.str);
}
```

Whenever a new `AnyRegex` instance is created, you can grow an existing context to also support it via `updateContext`, just as with `Regex`. Contexts can be shared as long as they are sized to support each other and were generated from the same `AnyRegex` definition.

```zig
var a = try anyregex.DynamicRegex.compile(.{}, gpa, "[A-Z][a-z_]+");
defer a.deinit(gpa);
var b = try anyregex.DynamicRegex.compile(.{}, gpa, "#[0-9a-fA-F]{6}");
defer b.deinit(gpa);
 
// a context sized to support both machines
var ctx = try a.initContextIncluding(gpa, &.{b});
defer ctx.deinit(gpa);
 
try expect(a.matchesExact(&ctx, "Batman"));
try expect(b.matchesExact(&ctx, "#AAAFFF"));
```

Even if you include no architectures that require a context at all, still call the context management functions as a matter of good practice and let the system decide how to manage them internally.

## Compilation
Compilation has been designed to be explicit so that only executable code you actually require will be present in the compiled binary. No executable code will be included in your binary by accident. Executable code included is a direct consequence of how the API types are defined.

Compiled machines are always fully immutable.

Compilation is highly configurable. The [configuration](src/compile.zig) includes language definitions, resource limits, pattern semantics and more. **Runtime** compilation on untrusted patterns is perfectly safe with the default config.

```zig
var re = try Re.compile(.{}, gpa, "^abc");
defer re.deinit(gpa);

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matches(&ctx, "abc"));
```

At **compile-time**, the machine will be compiled directly to the binary

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

const re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

if (re.match(&ctx, "camelCase")) |match| {
  try expectEqualStrings("Case", match.str);
}
```

The entire pipeline is fully available at `comptime`, including matching

```zig
comptime {
  const arch = Arch{ .minimal_nfa = .{ .context = .compact_fixed } };

  const re = regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");
 
  var ctx = re.initContextFixed();
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}
```

Comptime compiled machines produce the exact same API types. As long as the defined architecture can be compiled both at runtime and comptime, It makes no difference whether something was originally compiled at comptime or runtime, or what compile-config was used. The type is a direct consequence of the architecture definition and its resolution. This makes the type predictable, so it can be more easily referenced uniformly in function signatures.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 128 }, .offset_bp = .i8 } };

const comptime_re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");

var re = try regex.compile(arch, .{.ast_optimizations = .initEmpty()}, gpa, "Santa Claus");
defer re.deinit(gpa);

var re2 = try regex.compile(arch, .{.semantics = .{ .multiline = true }}, gpa, "^\\s*\\[\\w+\\]");
defer re2.deinit(gpa);

try expectEqual(@TypeOf(comptime_re), @TypeOf(re));
try expectEqual(@TypeOf(re), @TypeOf(re2));
```

However, type mismatch can occur if the architecture definitions are left in a partially-defined state. See [Architecture resolution](#architecture-resolution).

### Architecture resolution
The user facing API in `regex.zig` expects unresolved `Arch` types. `Arch` fields left null during definition, mean that it is left up to the compiler to resolve such fields. 
```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 128 }, .offset_bp = null } };
```

This can make compilation return differing `Regex` types. For type-consistency its best to define all fields fully.

Other dynamic definition fields are explained in their respective doc-comments, such as the `.compact_fixed` context definition.

### Strategies
Each architecture implements various strategies they use for solving the search problem implied by the pattern. By default, this is determined automatically through AST analysis, but it can be overridden in the config.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
const re = comptime regex.compileComptime(arch, .{
  .strategy = .start_set_pass,
}, "[A-Z][a-z_]+");
```
Start-set pass produces a machine around half the size, but it is not [ReDoS](https://en.wikipedia.org/wiki/ReDoS)  safe, so its never picked automatically.

Another is the end-anchor reverse pass, which is automatically inferred when the pattern is end-anchored via `$`
```zig
const re = comptime regex.compileComptime(arch, .{
  .strategy = .end_anchor_reverse_pass,
}, "[A-Z][a-z_]+");
```

which produces the same machine as
```zig
const re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+$");
```

Strategies do not alter the underlying mechanisms of the machine itself, instead it defines how the machine is managed and what machine-variations are compiled. `.start_set_pass` uses less memory because only a single forward automata is built, the default ReDoS safe strategy `.bi_directional_pass` uses around twice as much because it also requires a reversed automata to be present.

For more information on strategies, see [strategy.zig](src/compile/strategy.zig) and [search.zig](src/arch/search.zig)

### Optimizations
AST optimizations can be turned off partially or completely.

```zig
const re = comptime regex.compileComptime(arch, .{.ast_optimizations = .initEmpty()}, "[A-Z][a-z_]+$");
```

### Limits
Limits define resource usage constraints during compilation so that untrusted patterns can be compiled safely. For supporting more complex patterns, crank up the values in the configuration

```zig
const re = comptime regex.compileComptime(arch, .{
  .limits = .{
    .gpa_upper_bound = 1 << 20, // how much memory can be allocated using the passed allocator
    .max_depth =  1 << 10,      // how deep the AST can be
    .max_states = 1 << 20       // how many total states the automata can require
  }
}, "[A-Z][a-z_]+$");
```

For more information see [Limits](src/compile/compile.zig), and for the returned error values on violation see [ParseError](src/compile/parse.zig).

### Semantics
Semantics (or flags) define how patterns are interpreted. Any number can be active at once.
#### multiline
Interpret `^` and `$` as start/end of line

```zig
  const re = comptime regex.compileComptime(arch, .{
    .semantics = .{ .multiline = true }
  }, "^\\s*\\.\\w+");

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  var field_it = re.matchIter(&ctx, 
    \\struct {
    \\  .name = ".mark",
    \\.word = "true",
    \\}
  );

  try expectEqualStrings("  .name", field_it.next().?.str);
  try expectEqualStrings(".word", field_it.next().?.str);
  try expectEqual(null, field_it.next());
```

#### ignore whitespace
All unescaped whitespace outside of sets in the pattern is ignored by the lexer
Sets are parsed normally

Allows complex patterns to be defined over multiple lines

```zig
const re = comptime regex.compileComptime(arch, .{
  .semantics = .{ .pat_ignore_whitespace = true }
},
  \\ ^
  \\   [A-Z]{3,9}
  \\   \s+
  \\   / [a-zA-Z0-9_./-]*
  \\   \s+
  \\   HTTP / [0-9] \. [0-9]
  \\ $
);

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matches(&ctx, "POST /api/v1/users HTTP/1.1"));
try expect(re.matches(&ctx, "GET / HTTP/1.0"));
try expect(!re.matches(&ctx, "UPDATE / HTTP/2"));
```

#### ignore all whitespace
Similar to [ignore whitespace](#ignore-whitespace) but also ignores whitespace within sets

```zig
const re = comptime regex.compileComptime(arch, .{
  .semantics = .{ .pat_ignore_all_whitespace = true }
},
  \\ ^
  \\   [ A - Z ]{3,9}
  \\   \s+
  \\   / [ a-z A-Z 0-9 _ . /-]*
  \\   \s+
  \\   HTTP / [ 0-9 ] \. [ 0-9 ]
  \\ $
);

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matches(&ctx, "POST /api/v1/users HTTP/1.1"));
try expect(re.matches(&ctx, "GET / HTTP/1.0"));
try expect(!re.matches(&ctx, "G E T / HTTP/1.0"));
try expect(!re.matches(&ctx, "UPDATE / HTTP/2"));
```

#### ignore case
All letters `[a-zA-Z]` are interpreted as sets, e.g. `a = A = [aA]`

This is respected on all levels, even in hex sequences \xNN

```zig
const re = comptime regex.compileComptime(arch, .{
  .semantics = .{ .ignore_case = true }
}, "any-?case");

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matchesExact(&ctx, "anyCase"));
try expect(re.matchesExact(&ctx, "AnyCase"));
try expect(re.matchesExact(&ctx, "any-case"));
```

#### dotall
dot_set `.` is ignored and universe `[^]` is used in its place
Equivalent to defining the dot_set manually to `[]`

```zig
const re = comptime regex.compileComptime(arch, .{
  .semantics = .{ .dotall = true }
}, "yeah.*fits");

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matchesExact(&ctx, "yeah\nthat\nfits"));
```

#### never implicit newline
Disables all forms of implicit newlines, meaning:

1. Disables newline from all builtin sets (including the dot operator set)
2. Newlines are automatically removed from inverted sets `[^a]`

The only way to match a newline is if it is explicitly present in the pattern

```zig
const re = comptime regex.compileComptime(arch, .{
  .semantics = .{ .never_implicit_newline = true }
}, "[^abc]+");

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matchesExact(&ctx, "Shrek"));
try expect(!re.matchesExact(&ctx, "\nShrek"));
```

### Errors
```zig
pub const Error = ParseError || dispatch.DispatchError;

pub const DispatchError = error{
  /// No provided architecture is capable of safely compiling the pattern's manifest.
  /// This also fires if a Regex defined for a single architecture cannot solve the implied problem
  NoAvailableArchitecture,
};

pub const ParseError = error{
  /// A language object was in the process of being parsed, but an unexpected token was met
  UnexpectedToken,
  /// A language object was in the process of being parsed, but the stream ended unexpectedly
  UnexpectedEof,
  /// An empty set was encountered ([]). Note that this is different from epsilon: a{0,0}
  EmptySet,
  /// Dynamic allocator reached the defined upper bound on memory usage
  AllocationUpperbound,
  /// Zig std allocator error
  OutOfMemory,
  /// a{5,2}
  InvalidRepeat,
  /// a{999999999999999999999999999999}
  /// This is returned whenever an integer overflow was detected
  Overflow,
  /// Too many parenthesis, or AST too deep
  TooDeep,
  /// (a
  UnmatchedParenthesis,
  /// The pattern required too many states which would have violated config.max_states
  /// This has higher precedence than ContextTooSmall
  TooManyStates,
  /// The pattern required too many states which would have violated the fixed context definition
  /// This can never trigger for dynamic contexts
  ContextTooSmall,
  /// When arbitrary repetition user defined cap was encountered a{300000}
  TooHighArbitraryRepeat,
  /// The pattern contained maxInt
  /// The engine does not allow for matching against the encoding maxInt value
  MaxInt,
  /// [a-b-c]
  IllegalHyphenChain,
  /// [b-a]
  ReversedHyphenRange,
  /// [a-a]
  RedundantRange,
  /// [\s-b]
  IllegalHyphenOperand,
  /// config.max_unique_sets violation
  TooManySets,
};

```

### compileOptimal
*Compiles a pattern by letting the engine pick the optimal architecture from its entire universe, returning a concrete `Regex` of whichever architecture won.*

`compile` requires you to name the architecture. `AnyRegex` lets the engine pick per-call from a set *you* declared. `compileOptimal` is the third option: it sweeps every architecture implemented in the library, runs the bidding pipeline against the pattern, and returns a concrete `Regex` of the single architecture it deems most optimal. You do not declare a set; the whole universe is the set (swept according to the passed context definition).

It is a comptime-only function. The third argument is the context [mode](#context) to use *if* the winning architecture needs a context; if the chosen architecture is context-free, it is ignored.

```zig
const re = comptime regex.compileOptimal(.{}, "[A-Z][a-z_]+", .{ .fixed = 64 });

var ctx = re.initContextFixed();

if (re.match(&ctx, "camelCase")) |match| {
  try expectEqualStrings("Case", match.str);
} else unreachable;
```

Because the returned architecture is chosen by the pattern, the resulting type is not predictable from the call site. Two different patterns may resolve to two different architectures, and therefore two different `Regex` types, which cannot be referred to under one name.

```zig
const a = comptime regex.compileOptimal(.{}, "[A-Z][a-z_]+", .{ .fixed = 64 });
const b = comptime regex.compileOptimal(.{}, "a|b|c|d|e|f", .{ .fixed = 64 });

// a and b may or may not share a type; do not rely on either outcome
_ = a;
_ = b;
```

This is the same property that makes it useful and makes it dangerous. It is useful for inspecting which architecture the system considers optimal for a given pattern. It is dangerous because every distinct `Regex` type is distinct executable code the compiler must embed: compiling many different patterns through `compileOptimal` can multiply the matcher code in your binary. When you need many patterns under one type, reach for [AnyRegex](#anyregex) instead, which is type-erased by design. Keep the number of concrete `Regex` types in an application small.

`compileOptimal` resolves its return type by running most of the compilation pipeline at the type level. If that resolution fails, the returned type degrades to `void` rather than producing a compile error at the resolution site; the error surfaces when the function body runs.

## Context
The first parameter of the matching API is the context, which is the mutable object used by the architectures. The API exposes functions for creating contexts for the defined architecture that match their required length.

There are two context types. The table below is the quick chooser; the sections after it explain each in depth.

| context | resizable | needs allocator | matches at comptime | typical use |
| --- | --- | --- | --- | --- |
| `fixed` | no | no | yes | a known upper bound; heap-free matching |
| `compact_fixed` | no | no | yes | comptime-only; smallest possible fixed size |
| `dynamic` | yes | yes | no | unknown sizes; exact-fit memory across a family |

### Fixed type
*A statically sized, allocation-free context whose memory is fixed by its architecture, independent of the pattern.*

The `fixed` context is the simplest context. It cannot be resized and its creation requires no allocators. It statically consumes an amount of memory directly proportional to its defined length as defined by its architecture. Its memory usage does not depend on the compiled pattern.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
defer re.deinit(gpa);

var ctx = re.initContext(undefined) catch unreachable;
```

Since it requires no allocators it is safe to pass `undefined` gpa for its creation, however it is heavily recommended to use `initContextFixed` for a comptime-assert

```zig
var ctx = re.initContextFixed();
```

The context API that has to do with resizing (e.g. `updateIncluding` etc) and `deinit` are no-ops for fixed contexts.

**Compilation and matching.** Both comptime and runtime compiled machines can be defined for fixed context, but only fixed context supports matching at comptime. This is a limitation due to the Zig allocator API being illegal for comptime.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

comptime {
  const re = regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");
 
  var ctx = re.initContextFixed();
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;
}
```

With fixed contexts and comptime compilation, the library can be fully used for runtime matching without ever allocating on the heap.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };
const re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");

var ctx = re.initContextFixed();

if (re.match(&ctx, "camelCase")) |match| {
  try expectEqualStrings("Case", match.str);
} else unreachable;
```

**Type and size errors.** Contexts cannot be [shared](#shareability) unless their types match, so fixed contexts can be safely shared between compiled machines. If a machine does not support it due to type mismatch, it is a Zig compile error

```zig
const small = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 8 } } };
const large = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };
 
var re_small = try regex.compile(small, .{}, gpa, "[A-Z][a-z_]+");
defer re_small.deinit(gpa);
var re_large = try regex.compile(large, .{}, gpa, "[A-Z][a-z_]+");
defer re_large.deinit(gpa);

var ctx = re_small.initContextFixed();

try expect(re_small.matches(&ctx, "Aa"));
try expect(re_large.matches(&ctx, "Aa")); // compile error
```

If instead a pattern is compiled at runtime for a fixed length context, and the machine ends up being too wide, it is a Pzre compile error.

```zig
const large = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

const re_large = regex.compile(large, .{}, gpa, "a{65}");
try expectError(error.ContextTooSmall, re_large);
```

This means that it is perfectly runtime-safe to restrict the machine upperbound with the fixed context size, and then compile untrusted patterns at runtime while recovering on too large patterns reactively, mirroring the behavior of [limits](#Limits).

**Integer sizing.** The integer types used by the fixed context are as small as possible. Any size less than or equal to `std.math.maxInt(u8)` will use `u8` integers etc. This means that the jump from a length `255` to `256` will be a significant jump in memory usage. This holds for all context types.

```zig
const arch_u8_1 = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 254 } }};
const arch_u8_2 = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 255 } }};

const arch_u16 = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 256 } }};

const re_u8_1 = comptime regex.compileComptime(arch_u8_1, .{}, "A");
const re_u8_2 = comptime regex.compileComptime(arch_u8_2, .{}, "B");
const re_u16 = comptime regex.compileComptime(arch_u16, .{}, "C");

const ctx_u8_1 = comptime re_u8_1.initContextFixed();
const ctx_u8_2 = comptime re_u8_2.initContextFixed();
const ctx_u16 = comptime re_u16.initContextFixed();

const incr = ctx_u8_2.sizeOf() - ctx_u8_1.sizeOf();
try expect(ctx_u16.sizeOf() - ctx_u8_2.sizeOf() > 50 * incr);
```

#### compact_fixed
*A comptime-only fixed context sized to the mathematically smallest length the pattern needs.*

When compiling at comptime, the context can be defined as `compact_fixed`, which will make the system analyze the most mathematically compact fixed context size for the pattern. It is bad news though for context shareability, see [Shareability](#shareability) for more information.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .compact_fixed }};
const re = comptime regex.compileComptime(arch, .{.strategy = .start_set_pass}, "[A-Z][a-z_]+");

var ctx = re.initContextFixed();

try expectEqual(
  @TypeOf(re),
  regex.Regex(.{ .minimal_nfa = .{ .context = .{ .fixed = 3 }, .offset_bp = .i8 } }, .{})
);
```

### Dynamic type
*An allocator-managed context that grows to exactly fit a family of machines; cannot match at comptime.*

Dynamic contexts are allocator managed contexts that can adapt dynamically as required for new compiled machines. They are defined using integer breakpoints, the larger the breakpoint, the more memory it consumes.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
defer re.deinit(gpa);

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

if (re.match(&ctx, "camelCase")) |match| {
  try expectEqualStrings("Case", match.str);
} else unreachable;
```

Dynamic contexts are pre-allocated to the exact width as required. This means that they always have to be deinited. It also means that it matters which object creates it, as it will only support machines smaller or equal to its creator. Good practice is to use `initContextIncluding` which will create a context that supports the entire family of machines.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
defer re.deinit(gpa);

var hex = try regex.compile(arch, .{}, gpa, "#[0-9a-fA-F]{6}");
defer hex.deinit(gpa);

var ctx = try re.initContextIncluding(gpa, &.{hex}); // init for supporting both
defer ctx.deinit(gpa);

try expect(re.matchesExact(&ctx, "Batman"));
try expect(hex.matchesExact(&ctx, "#AAAFFF"));
```

Dynamic contexts can also be updated to support new runtime requirements.

```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
defer re.deinit(gpa);

var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);

try expect(re.matchesExact(&ctx, "Batman"));

var hex = try regex.compile(arch, .{}, gpa, "#[0-9a-fA-F]{6}");
defer hex.deinit(gpa);

try ctx.update(gpa, &.{hex}); // update context to also support additional machines

try expect(hex.matchesExact(&ctx, "#AAAFFF"));
```

Similarly there is `updateExact` which extends it to only support the machines in the list, potentially shrinking it and invalidating any machines it previously supported.

Situationally, dynamic contexts use less memory than fixed contexts due to them being able to dynamically adjust to the exact required length for the family of machines. However, their usage within the state machines involve slightly more CPU instructions.

Dynamic context patterns can be compiled both at runtime and comptime, but they cannot match at comptime.

### Shareability
As shown, contexts can be shared between machines as long as they are wide enough to support them. Shareability also requires for the context architectures to be valid for the machine architectures that use them. This will be elaborated on in the future.

Additionally, it is currently a compile error if you attempt to use a different context type for a machine than what it was defined for due to the strictly-typed nature of the API. This could change in the future to be more lenient. The following definitions are incompatible with eachother:

```zig
const arch1 = Arch{.minimal_nfa = .{ .context = .{ .dynamic = .u8 } }};
const arch2 = Arch{.minimal_nfa = .{ .context = .{ .dynamic = .u16 } }};
const arch3 = Arch{.minimal_nfa = .{ .context = .{ .fixed = 17 } }};
const arch4 = Arch{.minimal_nfa = .{ .context = .{ .fixed = 18 } }};
```

regardless of how large the compiled state machines end up being.

### Context method summary
Every context-producing and context-managing method, for quick reference. All are methods on a compiled machine (`Regex`/`AnyRegex`) except where noted as a method on the context itself.

| method | for | description |
| --- | --- | --- |
| `initContext(gpa)` | dynamic | Allocate a context sized to this machine. Must be `deinit`ed. |
| `initContextIncluding(gpa, others)` | dynamic | Allocate a context sized to support this machine and every machine in `others`. |
| `initContextFixed()` | fixed | Create a fixed context with no allocator. Comptime-asserts the context is fixed. |
| `ctx.update(gpa, others)` | dynamic | Grow an existing context to also support `others`. No-op for fixed. |
| `ctx.updateExact(gpa, machines)` | dynamic | Resize to fit exactly `machines`, possibly shrinking and invalidating others. No-op for fixed. |
| `ctx.sizeOf()` | both | The context's current memory footprint in bytes. |
| `ctx.deinit(gpa)` | dynamic | Free a dynamic context. No-op for fixed. |

## Multithreading
The compiled machines are fully immutable, so a single `Regex` can be shared across any number of threads without synchronization. What cannot be shared is the context: it is the mutable scratch space the matching functions write into, so every thread that matches concurrently needs its own.
 
Handing each thread a freshly allocated context works, but it is recommended to use the context cache: a fixed pool of pre-allocated contexts that worker threads borrow and return. The cache is created from a `Regex` with `initContextCache`, sized to the number of concurrent workers.
 
```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u16 } } };
 
var re = try regex.compile(arch, .{}, gpa, "needle");
defer re.deinit(gpa);
 
const io = std.testing.io;
 
// A cache of 4 contexts, one per worker thread.
var cache = try re.initContextCache(gpa, io, 4, &.{});
defer cache.deinit(gpa);
```
 
Each thread acquires a context, matches with it, and releases it back to the cache. Acquire and release are the only two operations a worker performs, both of which are fast infallible operations.
 
```zig
fn worker(re: anytype, cache: anytype, io: std.Io) !void {
  const ctx = try cache.acquire(io);
  defer cache.release(io, ctx);
 
  try expect(re.matchesExact(ctx, "needle"));
}
```
 
The cache design is deliberately strict: the cache size equals the worker count, and each thread holds at most one context at a time. Under that discipline `acquire` is infallible, because a context is always available for a thread that does not already hold one. Violating it (under-sizing the cache, or acquiring twice without releasing) is a usage error that triggers an `unreachable`.
 
Just like a single context, a cached context is sized by the machine that created it. To share one cache across several machines, pass the others through `including` so every context in the cache is sized to satisfy all of them.
 
```zig
var re_a = try regex.compile(arch, .{}, gpa, "a+");
defer re_a.deinit(gpa);
var re_b = try regex.compile(arch, .{}, gpa, "b+");
defer re_b.deinit(gpa);
 
// Every context in the cache is sized to support both machines.
var cache = try re_a.initContextCache(gpa, io, 4, &.{re_b});
defer cache.deinit(gpa);
```
 
The cache can be re-tuned after creation with `warmupContextCache`. It resizes the cache to a new worker count and grows every context to also satisfy the `including` machines. This is how you adapt a long-lived cache to new machines or a changed thread count without tearing it down.
 
```zig
// Started with 2 workers, now scale up to 6 and make room for a new machine.
var cache = try re.initContextCache(gpa, io, 2, &.{});
defer cache.deinit(gpa);
 
var bigger = try regex.compile(arch, .{}, gpa, "[A-Za-z0-9_]{1,64}");
defer bigger.deinit(gpa);
 
try re.warmupContextCache(&cache, gpa, io, 6, &.{bigger});
```
 
The context warmup API is similar to the context update API: `warmupContextCache` only ever grows contexts; it never shrinks them, so the cache stays valid for every machine it previously supported and you do not need to re-list the old machines. When you do want to reclaim memory, `warmupContextCacheExact` sets the contexts to exactly fit the listed machines, potentially shrinking the cache and invalidating any machine not in the list.
 
Warmup requires that no contexts are currently acquired: drain every borrowed context back into the cache before re-tuning it. Both warmup calls can fail with an allocation error (they may allocate larger contexts), so they return an error union, whereas `acquire`/`release` do not.
 
For the full set of cache behaviors under concurrency, see [multithreaded.zig](src/tests/multithreaded.zig).


## Matching API
The API is implemented for a specific architecture through the `Regex` object. All other API objects have identical methods with identical signatures that under-the-hood manage a `Regex`. The API is fully untrusted input safe (when compiled with default configuration) and completely free from allocators and error signatures.

The compiled machines are fully immutable, and since no allocations ever occur, matching itself has perfectly consistent and predictable performance.

The core matching functions all take the context as their first parameter, then the input string. None of them allocate and none of them can error, so they have predictable performance and a clean call shape. A `Match` is a small struct:
 
```zig
pub const Match = struct {
  str: []const u8,   // the matched slice (aliases the input)
  loc: Range(usize), // .start and .end indices into the input
};
```
 
Searches are unanchored by default: the engine finds the leftmost match anywhere in the string. Use the `^` and `$` anchors (or the relevant strategy) to constrain this.
 
### match and matches
`match` finds the first (leftmost) match anywhere in the input and returns it, or `null` if there is none. `matches` is the boolean form: it returns whether any match exists.
 
```zig
const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
// match: returns the leftmost Match, with both the slice and its location
if (re.match(&ctx, "abc 123 def")) |m| {
  try expectEqualStrings("123", m.str);
  try expectEqual(@as(usize, 4), m.loc.start);
  try expectEqual(@as(usize, 7), m.loc.end);
} else unreachable;
 
// matches: the boolean form
try expect(re.matches(&ctx, "abc 123 def"));
try expect(!re.matches(&ctx, "no digits here"));
```
 
Two related helpers narrow the search:
 
`matchesExact` returns true only when the match spans the entire input (anchored at both ends).
 
`matchStart` attempts to match starting at index 0 and returns the matched head slice (or `null`). It does not require the whole string to match, only that a match begins at the start.
 
```zig
var re = try regex.compile(arch, .{}, gpa, "[a-z]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
// matchesExact: the whole string must match
try expect(re.matchesExact(&ctx, "hello"));
try expect(!re.matchesExact(&ctx, "hello world")); // trailing " world" is unmatched
 
// matchStart: a match must begin at index 0; returns the matched head
try expectEqualStrings("hello", re.matchStart(&ctx, "hello world").?);
try expectEqual(null, re.matchStart(&ctx, " hello")); // does not start at 0
```
 
For full control over the search window, `find` takes an explicit range: it finds the first match whose start lies within `[start_idx, max_base]` (end inclusive). All the helpers above are thin wrappers over `find`.
 
```zig
var re = try regex.compile(arch, .{}, gpa, "a+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
const str = "aa bb aa";
// only consider matches starting in [3, str.len]; skips the leading "aa"
if (re.find(&ctx, str, 3, str.len)) |m| {
  try expectEqual(@as(usize, 6), m.loc.start);
  try expectEqualStrings("aa", m.str);
} else unreachable;
```
 
### Iteration
`matchIter` returns a `MatchIterator` that yields every non-overlapping match left to right. The iterator borrows the context and input; calling `next()` returns the next `Match` or `null` when exhausted. This is the allocation-free way to walk all matches.
 
```zig
var re = try regex.compile(arch, .{}, gpa, "[a-z_]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
var it = re.matchIter(&ctx, "123 snake_case 456 PascalCase");
try expectEqualStrings("snake_case", it.next().?.str);
try expectEqualStrings("ascal", it.next().?.str); // "P" is uppercase, so [a-z]+ starts at "ascal"
try expectEqualStrings("ase", it.next().?.str);
try expectEqual(null, it.next());
```
 
An iterator can be rewound to the beginning with `reset`, reusing the same context and input:
 
```zig
var it = re.matchIter(&ctx, "ab cd ef");
var first_pass: usize = 0;
while (it.next()) |_| first_pass += 1;
 
it.reset();
var second_pass: usize = 0;
while (it.next()) |_| second_pass += 1;
 
try expectEqual(first_pass, second_pass);
```
 
### findAll
When you want every match collected into a slice rather than streamed, `findAllAlloc` runs the iterator internally and returns an owned `[]Match`. The caller owns the returned slice and must free it.
 
```zig
var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
const matches = try re.findAllAlloc(&ctx, gpa, "1 22 333");
defer gpa.free(matches);
 
try expectEqual(@as(usize, 3), matches.len);
try expectEqualStrings("1", matches[0].str);
try expectEqualStrings("22", matches[1].str);
try expectEqualStrings("333", matches[2].str);
```
 
At comptime, where the allocator API is unavailable, use `findAllComptime`. It returns a comptime `[]const Match` with no allocation. It requires a fixed context.
 
```zig
const fixed_arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };
 
comptime {
  const re = regex.compileComptime(fixed_arch, .{}, "[0-9]+");
  var ctx = re.initContextFixed();
 
  const matches = re.findAllComptime(&ctx, "1 22 333");
  if (matches.len != 3) @compileError("expected three matches");
}
```
 
### Replacement
The replacement functions find matches and splice a replacement string in their place, returning a newly allocated result string. They never mutate the input.
 
`replaceFirst` replaces only the leftmost match and returns a `Replacement`, or `null` if nothing matched. The result owns its `new` string; free it with `deinit`.
 
```zig
pub const Replacement = struct {
  span: Range(usize), // the region of the input that was replaced
  new: []const u8,    // the newly allocated result string
  pub fn deinit(r: Replacement, gpa: Allocator) void { ... }
};
```
 
```zig
var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
if (try re.replaceFirst(&ctx, gpa, "id 123 and 456", "N")) |result| {
  defer result.deinit(gpa);
  try expectEqualStrings("id N and 456", result.new);
} else unreachable;
```
 
`replaceAll` replaces every match and returns a `ManyReplacements`, which additionally reports how many replacements were made and the overall span they covered.
 
```zig
pub const ManyReplacements = struct {
  span: Range(usize), // region encompassing all replacements
  count: usize,       // number of replacements performed
  new: []const u8,    // the newly allocated result string
  pub fn deinit(r: ManyReplacements, gpa: Allocator) void { ... }
};
```
 
```zig
var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
if (try re.replaceAll(&ctx, gpa, "a1 b22 c333", "#")) |result| {
  defer result.deinit(gpa);
  try expectEqualStrings("a# b# c#", result.new);
  try expectEqual(@as(usize, 3), result.count);
} else unreachable;
```
 
Both have a `Within` variant (`replaceFirstWithin`, `replaceAllWithin`) that restricts replacement to matches starting in `[start_idx, max_base]` (end inclusive), mirroring `find`. The plain forms are exactly the `Within` forms called over the whole string.

```zig
var re = try regex.compile(arch, .{}, gpa, "a");
defer re.deinit(gpa);
 
var ctx = try re.initContext(gpa);
defer ctx.deinit(gpa);
 
const str = "a a a";
// only replace matches starting at index 2 or later; the first "a" is untouched
if (try re.replaceAllWithin(&ctx, gpa, str, "X", 2, str.len)) |result| {
  defer result.deinit(gpa);
  try expectEqualStrings("a X X", result.new);
  try expectEqual(@as(usize, 2), result.count);
} else unreachable;
```

## Architectures
### Minimal nfa
TODO
