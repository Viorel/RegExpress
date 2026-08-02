# Pragmatic Zig Regex
A general-purpose [regex](https://en.wikipedia.org/wiki/Regular_expression) engine for Zig with a perfectly symmetrical runtime/comptime compilation pipeline.

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
**Explicit dynamic dispatch.** The engine maintains a set of architectures, each with their own search strategies and features. You can target specific architectures while compiling patterns, or declare a set of architectures for the engine to dispatch across. The engine analyzes each pattern and picks the optimal solver from your declared subset. It is *explicit* in the sense that only executable code of the architectures included will be linked.

**No bloat.** When patterns are exclusively compiled at comptime, no compiler code is included in the final binary, just the state tables and one matcher per requested architecture. Architectures you omit contribute zero bytes. The abstractions that can bloat the binary are well-documented and explicit opt-ins.

**Runtime/comptime symmetry.** The API remains identical whether accessed at runtime or comptime. Compilation in either environment yields the exact same types. Whether a regex was compiled ahead-of-time is genuinely never observable.

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
    .url = "https://codeberg.org/jetill/pzre/archive/v0.2.3.tar.gz",
    .hash = "...", // zig build will print the correct hash to paste here
  },
},
```

## Current status
The core of the engine is fully implemented and usable (with leftmost-longest semantics). I have focused on implementing the foundation first with thought, instead of adding a bunch of features. Once the core is ready, additional architectures, features and optimizations will be added as long as they do not interfere with the [philosophy](#core-philosophy).

The engine has been tested extensively but bugs can still occur. The goal is to never make the engine bloat your binary implicitly. The design reflects this, but it has not been thoroughly verified yet as of writing.

Everything documented in the [reference](REFERENCE.md) has been implemented. The implied capture group extraction in the philosophy section is not implemented yet.

The throughput is currently **not good** due to the lack of bit-architectures, a proper orchestrator and SIMD-powered scanners. 0.3.0 will implement all of the pieces for competetive throughput and capture group extraction. I will also provide benchmark results for 0.3.0

Critically important things not yet implemented:
- SIMD scanners
- utf8
- capture group extraction architectures
- DFA construction
- lazy operators `x*?` `x+?` `x??` etc
- ascii class syntax `[[:upper:]]`
- machine packing algorithms and architectures optimized for packing
- more architectures
- more AST optimizations

## Things that might be implemented
This section is for things with real (sometimes rare) use cases where it is unclear to me either how they should be implemented, or whether they matter enough to justify implementing rather than solving some other way.

- Dynamically managed context pooling. Similar to [Cache](src/arch/context.zig), but something that adapts better for peak-usage. Cache is preferred because it makes context-interactions by the threads non-fallible.
- serialize/deserialize. Due to Zig comptime, this is not as useful as in other engines.
- Lazy DFA. Very unlikely to ever be implemented. It is common for modern engines to have lazy DFA construction during matching, however it is unclear to me how important this truly is. Implementing this would break the immutable machine guarantee, the performance consistency guarantee of the matching API and the non-fallible nature of the matching API, and make the API require allocators. Note that this does not refer to proper (pzre) compiletime DFA construction.
- Leftmost-first semantics. Depending on the background of the person they might be used to leftmost-first semantics and might find leftmost-longest unintuitive. It is unclear to me how important it truly is to support both types of semantics. It is also unclear as of now how much it would complicate the codebase. I am focusing on implementing all of the basic features and proper optimizations before considering this. This might be implemented post `1.0.0` release.

## Caveats
- ASCII patterns do not allow for matching against `maxInt(u8)`
- Compiling at Zig comptime is not untrusted input safe. Meaning, if you hard-code malicious patterns while building your application, you can make the Zig compiler hang (probably). That is silly of course, but worth keeping in mind when metaprogramming. The comptime compilation process mirrors the runtime process, so the [Limits](src/compile/compile.zig) still apply. However, I have not tested how the Zig compiler semantic analysis behaves for extremely hostile patterns. Also it is impossible to bound Zig's internal comptime allocator.

## Relevant reading
- [Regular Expression Matching Can Be Simple And Fast](https://swtch.com/~rsc/regexp/regexp1.html)
- Much of the philosophy of pzre aligns with [re2](https://github.com/google/re2)
