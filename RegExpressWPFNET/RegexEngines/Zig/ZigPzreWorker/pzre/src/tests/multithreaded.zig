//! Multithreaded cache behavior tests.
//!
//! These tests exercise the `arch.Cache` infrastructure: shared-context
//! caching across threads, warmup-based resizing, capacity updates for new
//! machines, and resource lifecycle under allocation failure injection.
//!
//! The Cache design is strict: pool size equals worker count, each thread
//! holds at most one context at a time, acquire is infallible under correct
//! usage.  Tests respect this discipline — no test under-sizes the cache or
//! has threads acquire twice without releasing, since both would trigger an
//! unreachable rather than surfacing a recoverable error.

const std = @import("std");
const testing = std.testing;
const Allocator = std.mem.Allocator;
const Io = std.Io;

const pzre = @import("../root.zig");
const Config = pzre.compile.Config;
const Arch = pzre.Arch;
const regex = pzre.regex;

// ── Regex types ────────────────────────────────────────────────────────────

const re_dynamic = Arch{.minimal_nfa = .{ .offset_bp = .i8, .context = .{ .dynamic = .u16 } }};
const re_fixed = Arch{.minimal_nfa = .{ .offset_bp = .i8, .context = .{ .fixed = 40 } }};

// ── Worker harness ─────────────────────────────────────────────────────────

const ITERATIONS_PER_THREAD = 500;
const CHAOS_PROBABILITY: u8 = 8; // out of 10 — values above this trigger a sleep/yield

/// Random delay or yield, used to widen the race-detection window without
/// making tests slow.
inline fn chaos(rng: std.Random) !void {
  if (rng.boolean()) try std.Thread.yield();
  if (rng.intRangeAtMost(u8, 0, 10) > CHAOS_PROBABILITY) {
    try std.Io.sleep(testing.io, .{ .nanoseconds = rng.intRangeAtMost(u64, 1_000, 10_000) }, .cpu_thread);
  }
}

/// Generic worker: acquires a context, runs an operation, releases.  Acquire
/// is infallible under correct cache sizing, so no error handling is needed
/// for the cache itself — only for the chaos function and the operation.
fn worker(
  comptime Re: type,
  comptime CacheType: type,
  comptime op: fn (re: Re, ctx: *Re.Context) anyerror!void,
  re: Re,
  cache: *CacheType,
  seed: u64,
) !void {
  const io = testing.io;
  var prng = std.Random.DefaultPrng.init(seed);
  const rng = prng.random();

  var i: usize = 0;
  while (i < ITERATIONS_PER_THREAD) : (i += 1) {
    try chaos(rng);
    const ctx = try cache.acquire(io);
    defer cache.release(io, ctx);
    try chaos(rng);
    try op(re, ctx);
  }
}

// ── Tests ──────────────────────────────────────────────────────────────────

test "cache: basic threaded acquire/release cycle" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re = try regex.compile(re_dynamic, .{}, gpa, "needle");
  defer re.deinit(gpa);
  const Re = @TypeOf(re);

  // Cache sized exactly to the worker count — the canonical pattern.
  var cache = try re.initContextCache(gpa, io, 4, &.{});
  defer cache.deinit(gpa);

  const T = struct {
    fn op(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "needle"));
      try testing.expect(!r.matchesExact(ctx, "haystack"));
    }
  };

  var threads: [4]std.Thread = undefined;
  for (&threads, 0..) |*t, seed| {
    t.* = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, seed });
  }
  for (threads) |t| t.join();
}

test "cache: shared between multiple regex objects" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re_a = try regex.compile(re_dynamic, .{}, gpa, "a+");
  defer re_a.deinit(gpa);
  var re_b = try regex.compile(re_dynamic, .{}, gpa, "b+");
  defer re_b.deinit(gpa);

  const Re = @TypeOf(re_a);

  // Cache must be sized for both — initContextCache called on re_a is told
  // about re_b via `including` so the cache profile accommodates either.
  var cache = try re_a.initContextCache(gpa, io, 4, &.{re_b});
  defer cache.deinit(gpa);

  const T = struct {
    fn op_a(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "aaa"));
      try testing.expect(!r.matchesExact(ctx, "bbb"));
    }
    fn op_b(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "bbb"));
      try testing.expect(!r.matchesExact(ctx, "aaa"));
    }
  };

  var threads: [4]std.Thread = undefined;
  threads[0] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op_a, re_a, &cache, 1 });
  threads[1] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op_b, re_b, &cache, 2 });
  threads[2] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op_a, re_a, &cache, 3 });
  threads[3] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op_b, re_b, &cache, 4 });
  for (threads) |t| t.join();
}

test "cache: fixed-context cache, multiple threads" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re = comptime regex.compileComptime(re_fixed, .{}, "foo|bar");
  const Re = @TypeOf(re);

  var cache = try re.initContextCache(gpa, io, 2, &.{});
  defer cache.deinit(gpa);

  const T = struct {
    fn op(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "foo"));
      try testing.expect(r.matchesExact(ctx, "bar"));
      try testing.expect(!r.matchesExact(ctx, "baz"));
    }
  };

  var threads: [2]std.Thread = undefined;
  threads[0] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, 1 });
  threads[1] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, 2 });
  for (threads) |t| t.join();
}

test "cache: warmup scales worker count up" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re = try regex.compile(re_dynamic, .{}, gpa, "x+");
  const Re = @TypeOf(re);
  defer re.deinit(gpa);

  // Start with 2 workers.
  var cache = try re.initContextCache(gpa, io, 2, &.{});
  defer cache.deinit(gpa);

  const T = struct {
    fn op(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "xxx"));
    }
  };

  {
    var threads: [2]std.Thread = undefined;
    for (&threads, 0..) |*t, seed|
      t.* = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, seed });
    for (threads) |t| t.join();
  }

  // Resize to 6 workers — all previous contexts have been released
  // (the joins above guarantee that).
  try re.warmupContextCache(&cache, gpa, io, 6, &.{});

  {
    var threads: [6]std.Thread = undefined;
    for (&threads, 0..) |*t, seed|
      t.* = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, seed });
    for (threads) |t| t.join();
  }
}

test "cache: warmup scales worker count down" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re = try regex.compile(re_dynamic, .{}, gpa, "y+");
  const Re = @TypeOf(re);
  defer re.deinit(gpa);

  var cache = try re.initContextCache(gpa, io, 6, &.{});
  defer cache.deinit(gpa);

  const T = struct {
    fn op(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "yyy"));
    }
  };

  {
    var threads: [6]std.Thread = undefined;
    for (&threads, 0..) |*t, seed|
      t.* = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, seed });
    for (threads) |t| t.join();
  }

  // Scale down to 1 worker — excess contexts must be destroyed.
  try re.warmupContextCache(&cache, gpa, io, 1, &.{});

  {
    var threads: [1]std.Thread = undefined;
    threads[0] = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re, &cache, 0 });
    for (threads) |t| t.join();
  }
}

test "cache: warmup updates capacity for a larger machine" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re_small = try regex.compile(re_dynamic, .{}, gpa, "a");
  defer re_small.deinit(gpa);
  // A pattern that compiles to noticeably more states than `a` but stays well
  // within the default compile resource cap.
  var re_large = try regex.compile(re_dynamic, .{}, gpa, "a{5,10}|b{3,8}|c+");
  defer re_large.deinit(gpa);

  // Initialize cache sized for re_small only.
  var cache = try re_small.initContextCache(gpa, io, 3, &.{});
  defer cache.deinit(gpa);

  // Warmup to also support re_large — contexts must be resized to fit the
  // larger machine.  Using re_large with this cache before warmup would be
  // a programmer error; after warmup it must work.
  try re_small.warmupContextCache(&cache, gpa, io, 3, &.{re_large});

  // Verify all 3 contexts can serve both machines.
  var ctxs: [3]*@TypeOf(cache).Ctx = undefined;
  for (&ctxs) |*c| c.* = try cache.acquire(io);
  defer for (ctxs) |c| cache.release(io, c);

  for (ctxs) |c| {
    try testing.expect(re_small.matchesExact(c, "a"));
    try testing.expect(re_large.matchesExact(c, "aaaaaaa"));
    try testing.expect(re_large.matchesExact(c, "ccc"));
  }
}

test "cache: warmup is a no-op for unchanged configuration" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re = try regex.compile(re_dynamic, .{}, gpa, "abc");
  defer re.deinit(gpa);

  var cache = try re.initContextCache(gpa, io, 3, &.{});
  defer cache.deinit(gpa);

  // Calling warmup with the same parameters should succeed cleanly —
  // contexts already satisfy the profile.
  try re.warmupContextCache(&cache, gpa, io, 3, &.{});
  try re.warmupContextCache(&cache, gpa, io, 3, &.{});

  const ctx = try cache.acquire(io);
  defer cache.release(io, ctx);
  try testing.expect(re.matchesExact(ctx, "abc"));
}

test "cache: no leaks under OOM injection at init" {
  const S = struct {
    fn run(alloc: Allocator) !void {
      const io = testing.io;

      var re = regex.compile(re_dynamic, .{}, alloc, "needle") catch |err| {
        if (err == error.OutOfMemory) return err;
        return; // deterministic parse errors not the failure surface here
      };
      defer re.deinit(alloc);

      var cache = re.initContextCache(alloc, io, 4, &.{}) catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer cache.deinit(alloc);

      // Confirm the cache is usable in the happy path.
      const ctx = try cache.acquire(io);
      defer cache.release(io, ctx);
      _ = re.matchesExact(ctx, "needle in a haystack");
    }
  };

  try S.run(testing.allocator);
  try testing.checkAllAllocationFailures(testing.allocator, S.run, .{});
}

test "cache: no leaks under OOM injection at warmup" {
  const S = struct {
    fn run(alloc: Allocator) !void {
      const io = testing.io;

      var re_small = regex.compile(re_dynamic, .{}, alloc, "a") catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer re_small.deinit(alloc);

      var re_large = regex.compile(re_dynamic, .{}, alloc, "a{5,10}|b{5,10}") catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer re_large.deinit(alloc);

      // The cache must be created for warmup to be exercised, but OOM at
      // this stage is a valid failure mode for checkAllAllocationFailures
      // to observe — propagate it.
      var cache = re_small.initContextCache(alloc, io, 2, &.{}) catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer cache.deinit(alloc);

      // The actual subject: warmup must be leak-free under OOM.  Includes
      // both grow (2 → 4 workers) and per-field resize (small → large).
      re_small.warmupContextCache(&cache, alloc, io, 4, &.{re_large}) catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };

      const ctx = try cache.acquire(io);
      defer cache.release(io, ctx);
      _ = re_large.matchesExact(ctx, "aaaaa");
    }
  };

  try S.run(testing.allocator);
  try testing.checkAllAllocationFailures(testing.allocator, S.run, .{});
}

test "cache: warmupExact shrinks context capacity" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re_small = try regex.compile(re_dynamic, .{}, gpa, "a");
  defer re_small.deinit(gpa);
  var re_large = try regex.compile(re_dynamic, .{}, gpa, "a{10,20}|b{5,15}");
  defer re_large.deinit(gpa);

  // Start sized to support the larger machine.
  var cache = try re_large.initContextCache(gpa, io, 3, &.{});
  defer cache.deinit(gpa);

  // Sanity: the large machine works at the initial profile.
  {
    const ctx = try cache.acquire(io);
    defer cache.release(io, ctx);
    try testing.expect(re_large.matchesExact(ctx, "aaaaaaaaaaaa"));
  }

  // Shrink-exact: bring the cache down to just re_small's requirements.
  // After this, re_large is no longer guaranteed to work with this cache —
  // the contexts have been forcibly trimmed to the smaller profile.
  try re_small.warmupContextCacheExact(&cache, gpa, io, 3, &.{});

  // re_small must still work after the exact shrink.
  var ctxs: [3]*@TypeOf(cache).Ctx = undefined;
  for (&ctxs) |*c| c.* = try cache.acquire(io);
  defer for (ctxs) |c| cache.release(io, c);
  for (ctxs) |c| try testing.expect(re_small.matchesExact(c, "a"));
}

test "cache: warmupExact distinguishes from warmup (non-shrinking)" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re_small = try regex.compile(re_dynamic, .{}, gpa, "a");
  defer re_small.deinit(gpa);
  var re_large = try regex.compile(re_dynamic, .{}, gpa, "a{10,20}|b{5,15}");
  defer re_large.deinit(gpa);

  // Start sized to support the larger machine.
  var cache = try re_large.initContextCache(gpa, io, 2, &.{});
  defer cache.deinit(gpa);

  // Non-exact warmup with smaller profile: contexts retain their existing
  // (larger) capacity.  re_large must still work afterwards.
  try re_small.warmupContextCache(&cache, gpa, io, 2, &.{});

  const ctx = try cache.acquire(io);
  defer cache.release(io, ctx);
  try testing.expect(re_large.matchesExact(ctx, "aaaaaaaaaaaa"));
  try testing.expect(re_small.matchesExact(ctx, "a"));
}

test "cache: warmupExact growth path also works" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re_small = try regex.compile(re_dynamic, .{}, gpa, "a");
  defer re_small.deinit(gpa);
  var re_large = try regex.compile(re_dynamic, .{}, gpa, "a{10,20}|b{5,15}");
  defer re_large.deinit(gpa);

  // Start sized only for re_small.
  var cache = try re_small.initContextCache(gpa, io, 2, &.{});
  defer cache.deinit(gpa);

  // warmupExact to the larger profile.  For growth, exact and non-exact
  // behave identically: contexts get resized to match.
  try re_large.warmupContextCacheExact(&cache, gpa, io, 4, &.{});

  var ctxs: [4]*@TypeOf(cache).Ctx = undefined;
  for (&ctxs) |*c| c.* = try cache.acquire(io);
  defer for (ctxs) |c| cache.release(io, c);
  for (ctxs) |c| try testing.expect(re_large.matchesExact(c, "aaaaaaaaaaaa"));
}

test "cache: warmupExact threaded usage post-shrink" {
  const gpa = testing.allocator;
  const io = testing.io;

  var re_large = try regex.compile(re_dynamic, .{}, gpa, "x{5,10}");
  defer re_large.deinit(gpa);
  var re_small = try regex.compile(re_dynamic, .{}, gpa, "x");
  defer re_small.deinit(gpa);
  const Re = @TypeOf(re_small);

  var cache = try re_large.initContextCache(gpa, io, 4, &.{});
  defer cache.deinit(gpa);

  // Shrink to the small profile.
  try re_small.warmupContextCacheExact(&cache, gpa, io, 4, &.{});

  const T = struct {
    fn op(r: Re, ctx: *Re.Context) anyerror!void {
      try testing.expect(r.matchesExact(ctx, "x"));
    }
  };

  // Threaded usage at the shrunk profile must remain race-free.
  var threads: [4]std.Thread = undefined;
  for (&threads, 0..) |*t, seed|
    t.* = try std.Thread.spawn(.{}, worker, .{ Re, @TypeOf(cache), T.op, re_small, &cache, seed });
  for (threads) |t| t.join();
}

test "cache: no leaks under OOM injection at warmupExact" {
  const S = struct {
    fn run(alloc: Allocator) !void {
      const io = testing.io;

      var re_small = regex.compile(re_dynamic, .{}, alloc, "a") catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer re_small.deinit(alloc);

      var re_large = regex.compile(re_dynamic, .{}, alloc, "a{5,10}|b{5,10}") catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer re_large.deinit(alloc);

      // Initialize at the large profile; the shrink path is the subject.
      var cache = re_large.initContextCache(alloc, io, 2, &.{}) catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };
      defer cache.deinit(alloc);

      // warmupExact: shrink + per-field exact resize.  Must be leak-free
      // even if the internal reallocations during shrink fail.
      re_small.warmupContextCacheExact(&cache, alloc, io, 4, &.{}) catch |err| {
        if (err == error.OutOfMemory) return err;
        return;
      };

      const ctx = try cache.acquire(io);
      defer cache.release(io, ctx);
      _ = re_small.matchesExact(ctx, "a");
    }
  };

  try S.run(testing.allocator);
  try testing.checkAllAllocationFailures(testing.allocator, S.run, .{});
}
