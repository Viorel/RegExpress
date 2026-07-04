//! Dedicated tests for the public Context lifecycle API exposed on Regex and
//! AnyRegex.
//!
//! Scope: the SINGLE-context surface and context sharing between machines:
//!   initContext, initContextFixed, initContextIncluding, updateContext,
//!   the Context type's own sizeOf, and reuse of one context across many
//!   match calls (contexts never require manual reset).
//!
//! Out of scope (covered by multithreaded.zig): initContextCache,
//! warmupContextCache(/Exact), and the threaded cache acquire/release cycle.
//!
//! Both compilation surfaces are exercised:
//!   - Regex      via regex.compile(arch, ...)            (strictly typed)
//!   - AnyRegex   via DynamicRegex / FixedRegex(n)        (type-erased)
//! and both context flavors:
//!   - dynamic       (heap-backed, initContext/deinit)
//!   - fixed         (inline, initContextFixed, non_allocator_context)

const std = @import("std");
const testing = std.testing;
const Allocator = std.mem.Allocator;

const pzre = @import("../root.zig");
const Config = pzre.compile.Config;
const Arch = pzre.Arch;
const regex = pzre.regex;
const anyregex = pzre.anyregex;
const Match = pzre.regex.Match;

const expect = testing.expect;
const expectEqual = testing.expectEqual;

// -- Representative archs -----------------------------------------------------

const arch_dynamic = Arch{ .minimal_nfa = .{ .offset_bp = .i8, .context = .{ .dynamic = .u16 } } };
const arch_fixed = Arch{ .minimal_nfa = .{ .offset_bp = .i8, .context = .{ .fixed = 64 } } };

// -----------------------------------------------------------------------------
// initContext / deinit: the dynamic single-context happy path
// -----------------------------------------------------------------------------

test "context: initContext then a single match (dynamic, Regex)" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "needle");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  try expect(re.matchesExact(&ctx, "needle"));
  try expect(!re.matchesExact(&ctx, "haystack"));
}

test "context: one context is reused across many matches without manual reset" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "ab");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  // The documented contract: contexts do not require manual reset between
  // calls. Run a mix of hits and misses repeatedly on the SAME context and
  // assert every result is independent of call order.
  var i: usize = 0;
  while (i < 50) : (i += 1) {
    try expect(re.matchesExact(&ctx, "ab"));
    try expect(!re.matchesExact(&ctx, "ba"));
    try expect(re.match(&ctx, "xxabxx") != null);
    try expect(re.match(&ctx, "xxxxxx") == null);
  }
}

test "context: shared across match, matchStart, find on the same Regex" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "a+");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  // Each entry point takes *Context; reusing one context across all of them
  // must be sound.
  try expect(re.matches(&ctx, "aaa"));
  const started = re.matchStart(&ctx, "aaab");
  try expect(started != null);
  const m = re.find(&ctx, "baaa", 0, 4);
  try expect(m != null);
}

// -----------------------------------------------------------------------------
// findAllAlloc / matchIter: iterator-driven context reuse
// -----------------------------------------------------------------------------

test "context: findAllAlloc reuses the context internally" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "a");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  const matches = try re.findAllAlloc(&ctx, gpa, "abaca");
  defer gpa.free(matches);

  try expectEqual(@as(usize, 3), matches.len);

  // The same context is immediately usable for a fresh single match.
  try expect(re.matchesExact(&ctx, "a"));
}

test "context: matchIter can be driven, then the context reused for a direct match" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "x");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  var it = re.matchIter(&ctx, "xax x");
  var count: usize = 0;
  while (it.next()) |_| count += 1;
  try expectEqual(@as(usize, 3), count);

  // Iterator shares the context; after exhausting it, the context is still
  // valid for a standalone call.
  try expect(re.match(&ctx, "x") != null);
}

// -----------------------------------------------------------------------------
// initContextFixed: the non-allocator (inline) context path
// -----------------------------------------------------------------------------

test "context: initContextFixed needs no allocator and matches (fixed, Regex)" {
  const gpa = testing.allocator;

  // Fixed context resolves to a non_allocator_context, so the context itself
  // requires no allocation.
  var re = try regex.compile(arch_fixed, .{}, gpa, "cat");
  defer re.deinit(gpa);

  comptime std.debug.assert(@TypeOf(re).non_allocator_context);

  var ctx = re.initContextFixed();
  // No ctx.deinit needed for a fixed context, but calling it must be harmless
  // if the type exposes it; we simply do not allocate, so nothing to free.

  try expect(re.matchesExact(&ctx, "cat"));
  try expect(!re.matchesExact(&ctx, "dog"));

  // Reuse the same fixed context repeatedly.
  var i: usize = 0;
  while (i < 25) : (i += 1) {
    try expect(re.match(&ctx, "a cat here") != null);
  }
}

test "context: fixed context match at comptime via compileComptimeNonIntercepting" {
  comptime {
    @setEvalBranchQuota(1_000_000);
    var re = regex.compileComptimeNonIntercepting(arch_fixed, .{}, "dog") catch unreachable;
    var ctx = re.initContextFixed();
    if (!re.matchesExact(&ctx, "dog")) @compileError("comptime fixed-context match failed");
    if (re.matchesExact(&ctx, "cat")) @compileError("comptime fixed-context false positive");
  }
}

// -----------------------------------------------------------------------------
// Context.sizeOf: the context type reports its footprint
// -----------------------------------------------------------------------------

test "context: sizeOf is positive and stable across reuse (dynamic)" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "abcde");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  const before = ctx.sizeOf();
  try expect(before > 0);

  // Running matches must not change the context's footprint.
  try expect(re.matchesExact(&ctx, "abcde"));
  try expectEqual(before, ctx.sizeOf());
}

// -----------------------------------------------------------------------------
// initContextIncluding / updateContext: context sharing between machines
// -----------------------------------------------------------------------------

test "context: initContextIncluding sizes for the larger included machine" {
  const gpa = testing.allocator;

  // Two machines of the same context type but different state counts.
  var small = try regex.compile(arch_dynamic, .{}, gpa, "a");
  defer small.deinit(gpa);
  var large = try regex.compile(arch_dynamic, .{}, gpa, "abcdefghij");
  defer large.deinit(gpa);

  // A context created on `small` but sized to also support `large`.
  var ctx = try small.initContextIncluding(gpa, &.{large});
  defer ctx.deinit(gpa);

  // Both machines can run on the shared context.
  try expect(small.matchesExact(&ctx, "a"));
  try expect(large.matchesExact(&ctx, "abcdefghij"));

  // The shared context is at least as large as a context sized for `large`
  // alone.
  var large_only = try large.initContext(gpa);
  defer large_only.deinit(gpa);
  try expect(ctx.sizeOf() >= large_only.sizeOf());
}

test "context: updateContext grows an existing context to fit a new machine" {
  const gpa = testing.allocator;

  var small = try regex.compile(arch_dynamic, .{}, gpa, "a");
  defer small.deinit(gpa);
  var large = try regex.compile(arch_dynamic, .{}, gpa, "abcdefghijklmnop");
  defer large.deinit(gpa);

  // Start with a context sized only for `small`.
  var ctx = try small.initContext(gpa);
  defer ctx.deinit(gpa);
  try expect(small.matchesExact(&ctx, "a"));

  // Grow it in place to also support `large`.
  try small.updateContext(&ctx, gpa, &.{large});

  // Now both run on the same (grown) context.
  try expect(small.matchesExact(&ctx, "a"));
  try expect(large.matchesExact(&ctx, "abcdefghijklmnop"));
}

// -----------------------------------------------------------------------------
// AnyRegex: the type-erased surface mirrors the same context API
// -----------------------------------------------------------------------------

test "context: DynamicRegex initContext / deinit / reuse" {
  const gpa = testing.allocator;

  const Re = anyregex.DynamicRegex;
  var re = try Re.compile(.{}, gpa, "needle");
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  try expect(re.matchesExact(&ctx, "needle"));
  try expect(!re.matchesExact(&ctx, "thread"));

  // Reuse without reset.
  try expect(re.match(&ctx, "find the needle now") != null);
  try expect(re.match(&ctx, "nothing here") == null);
}

test "context: FixedRegex initContextFixed needs no allocator (AnyRegex)" {
  const gpa = testing.allocator;

  const Re = anyregex.FixedRegex(128);
  var re = try Re.compile(.{}, gpa, "fox");
  defer re.deinit(gpa);

  var ctx = re.initContextFixed();
  try expect(re.matchesExact(&ctx, "fox"));
  try expect(!re.matchesExact(&ctx, "cow"));
}

test "context: AnyRegex initContextIncluding shares a context across two machines" {
  const gpa = testing.allocator;

  const Re = anyregex.DynamicRegex;
  var a = try Re.compile(.{}, gpa, "a");
  defer a.deinit(gpa);
  var b = try Re.compile(.{}, gpa, "abcdefgh");
  defer b.deinit(gpa);

  var ctx = try a.initContextIncluding(gpa, &.{b});
  defer ctx.deinit(gpa);

  try expect(a.matchesExact(&ctx, "a"));
  try expect(b.matchesExact(&ctx, "abcdefgh"));
}

// -----------------------------------------------------------------------------
// Allocation-failure safety: initContext under OOM injection
// -----------------------------------------------------------------------------

test "context: initContext surfaces OutOfMemory rather than leaking" {
  const gpa = testing.allocator;

  var re = try regex.compile(arch_dynamic, .{}, gpa, "pattern");
  defer re.deinit(gpa);

  const Re = @TypeOf(re);
  const Closure = struct {
    fn run(alloc: Allocator, r: Re) !void {
      var ctx = r.initContext(alloc) catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer ctx.deinit(alloc);
      try expect(r.matchesExact(&ctx, "pattern"));
    }
  };

  try testing.checkAllAllocationFailures(gpa, Closure.run, .{re});
}
