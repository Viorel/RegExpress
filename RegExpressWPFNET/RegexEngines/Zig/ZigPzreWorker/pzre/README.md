# Pragmatic Zig Regex
A general-purpose regex engine for Zig with a perfectly symmetrical runtime/comptime compilation pipeline.

## Contents
- [Core philosophy](#core-philosophy)
- [Showcase](#showcase)
- [Docs](#docs)
- [Adding to your project](#adding-to-your-project)
- [Current status](#current-status)
- [Things that might be implemented](#things-that-might-be-implemented)
- [Caveats](#caveats)
- [Relevant reading](#relevant-reading)

## Core philosophy
**Explicit dynamic dispatch.** The engine maintains a universe of architectures, each with their own search strategies and feature sets. You can compile patterns for a specific architecture-strategy combination, or declare a subset for the engine to dispatch across. An embedded build might include only a minimal NFA with 4-byte-per-state encoding and a segmented approach for capture group extraction; a general-purpose tool might include several. The engine analyzes each pattern and picks the optimal solver from your declared subset. It is *explicit* in the sense that only executable code of the architectures included will be linked.

**NOTE:** capture group extraction unimplemented as of writing.

**No bloat.** When patterns are exclusively compiled at comptime, no compiler code is included in the final binary, just the state tables and one matcher per requested architecture. Architectures you omit contribute zero bytes. The abstractions that can bloat the binary are well-documented and explicit opt-ins, such as polymorphic runtime compilation. Nothing is linked incidentally; everything is intentional.

**Runtime/comptime symmetry.** The API remains identical whether accessed at runtime or comptime. Compilation in either environment yields the exact same types. Patterns compiled at different times, across different codebases, or against different target machines can seamlessly share contexts and participate in the same thread-pool cache. A library can include statically compiled patterns, and a user application can register dynamic patterns into the exact same pool. Whether a regex was compiled ahead-of-time is genuinely never observable.

**No hidden allocations.** The system never stores allocators internally. Any API function capable of allocating requires an explicit `gpa` at every call site.

**Predictable performance.** Compiled machines are immutable. The core matching API is completely free of fallible signatures. There is no profile-guided optimization or incremental compilation. The cost of matching is strictly determined by state count and input length. This means that matching is always consistent with predictable performance.

**Untrusted input safe.** The compile-configuration exposes a strict resource contract to impose limits during compilation, including maximum state counts, AST depth, allocator upper bounds, and allowed language features. The default compile-config is always untrusted pattern safe, and the picked architectures and search algorithms are fully [ReDoS](https://en.wikipedia.org/wiki/ReDoS) immune, with linear execution time.

## Showcase
```zig
const std = @import("std");
const expectEqualStrings = std.testing.expectEqualStrings;
const expectEqual = std.testing.expectEqual;

const pzre = @import("pzre");
const regex = pzre.regex;
const Arch = pzre.Arch;

test "match" {
  // Compiling for a specific architecture
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{ .strategy = .bi_directional_pass }, "[A-Z][a-z_]+");
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
 
  var re2 = try regex.compile(arch, .{ .limits = .{
    .gpa_upper_bound = 1<<13,
  } }, gpa, "\\s*\\.field_name");
  defer re2.deinit(gpa);

  try ctx.update(gpa, &.{re2});

  try expectEqual(@TypeOf(re), @TypeOf(re2));
  if (re2.match(&ctx, "struct { .field_name }")) |match| {
    try expectEqualStrings(" .field_name", match.str);
  }
}
```

## Docs
- [Language and grammar](LANGUAGE.md)
- [Usage reference](REFERENCE.md)
- [Architecture](ARCHITECTURE.md) **WIP**

## Adding to your project
```zig
// build.zig
const pzre = b.dependency("pzre", .{
  .target = target,
  .optimize = optimize,
});
exe.root_module.addImport("pzre", pzre.module("pzre"));

// in build.zig.zon
.dependencies = .{
  .pzre = .{
    .url = "https://codeberg.org/jetill/pzre/archive/v0.2.1.tar.gz",
    .hash = "...", // zig build will print the correct hash to paste here
  },
},
```

## Current status
The core of the engine is fully implemented and usable. I have focused on implementing the foundation first with thought, instead of adding a bunch of features. Once the core is ready, additional architectures, features and optimizations will be added as long as they do not interfere with the [philosophy](#core-philosophy).

The engine has been tested extensively but bugs can still occur. The goal is to never make the engine bloat your binary implicitly. The design reflects this, but it has not been thoroughly verified yet as of writing.

Everything documented in the [reference](REFERENCE.md) has been implemented. The implied capture group extraction in the philosophy section is not implemented yet.

Critically important things not yet implemented:
- utf8
- capture group extraction architectures
- DFA construction
- lazy operators `x*?` `x+?` `x??` etc
- ascii class syntax `[[:upper:]]`
- machine packing algorithms and architectures optimized for packing
- more architectures
- more AST optimizations
- Leftmost-first semantics

## Things that might be implemented
This section is for things that have real (but rare) use-cases and it is not clear to me how they should be implemented.

- Dynamically managed context pooling. Similar to [Cache](src/arch/context.zig), but something that adapts better for peak-usage. Cache is preferred because it makes context-interactions by the threads non-fallible.
- serialize/deserialize. Due to Zig comptime, this is not as useful as in other engines.

## Caveats
- ASCII patterns do not allow for matching against `maxInt(u8)`
- Compiling at Zig comptime is not untrusted input safe. Meaning, if you hard-code malicious patterns while building your application, you can make the Zig compiler hang (probably). That is silly of course, but worth keeping in mind when metaprogramming. The comptime compilation process mirrors the runtime process, so the [Limits](src/compile/compile.zig) still apply. However, I have not tested how the Zig type resolution behaves for extremely hostile patterns. Also it is impossible to bound Zig's internal comptime allocator.

## Relevant reading
- [Regular Expression Matching Can Be Simple And Fast](https://swtch.com/~rsc/regexp/regexp1.html)
- Much of the philosophy of pzre aligns with [re2](https://github.com/google/re2)
