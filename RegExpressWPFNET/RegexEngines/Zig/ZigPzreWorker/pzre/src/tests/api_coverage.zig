// Full API coverage test for both Regex and Regex.
//
// This test invokes every public method on both regex types at least once
// with a basic input.  It's a smoke test, not a correctness test — its job
// is to catch the case where a refactor breaks a method's signature or
// makes it stop compiling.  Result correctness is checked only loosely;
// the deep correctness tests live in other files.
//
// The pattern `a+` on input "aabba" is used throughout: simple, has
// multiple matches, exercises both matching and non-matching positions,
// and produces stable expected output across configurations.

const std = @import("std");
const testing = std.testing;
const Allocator = std.mem.Allocator;
const Io = std.Io;

const pzre = @import("../root.zig");
const Arch = pzre.Arch;
const regex = pzre.regex;

const Match = pzre.regex.Match;
const Replacement = pzre.regex.Replacement;
const ManyReplacements = pzre.regex.ManyReplacements;
const expectEqual = std.testing.expectEqual;

const dynamic_nfa = Arch{ .minimal_nfa = .{ .offset_bp = .i16, .context = .{ .dynamic = .u16 } } };
const fixed_nfa = Arch{ .minimal_nfa = .{ .offset_bp = .i8, .context = .{ .fixed = 64 } } };

const R = pzre.anyregex.DynamicRegex;
const RFixed = pzre.anyregex.FixedRegex(64);

const PATTERN = "a+";
const INPUT = "aabba";

test "regex API coverage: Regex runtime-compile" {
  const gpa = testing.allocator;
  const io = testing.io;

  // ── Compilation ────────────────────────────────────────────────────────
  var re = try regex.compile(dynamic_nfa, .{}, gpa, PATTERN);
  defer re.deinit(gpa);

  // ── Static-info methods ────────────────────────────────────────────────
  const required_len = re.requiredContextLen();
  try testing.expect(required_len > 0);

  // ── Context creation ───────────────────────────────────────────────────
  var ctx_a = try re.initContext(gpa);
  defer ctx_a.deinit(gpa);

  var ctx_b = try re.initContextIncluding(gpa, &.{re});
  defer ctx_b.deinit(gpa);

  try re.updateContext(&ctx_a, gpa, &.{re});

  // ── Match queries (non-allocating) ─────────────────────────────────────
  try testing.expect(re.matches(&ctx_a, INPUT));
  try testing.expect(!re.matchesExact(&ctx_a, INPUT));
  try testing.expect(re.matchesExact(&ctx_a, "aaa"));

  const m_start = re.matchStart(&ctx_a, INPUT);
  try testing.expect(m_start != null);

  const m = re.match(&ctx_a, INPUT);
  try testing.expect(m != null);

  const f = re.find(&ctx_a, INPUT, 0, INPUT.len);
  try testing.expect(f != null);

  // ── Allocating match operations ────────────────────────────────────────
  const all = try re.findAllAlloc(&ctx_a, gpa, INPUT);
  defer gpa.free(all);
  try testing.expect(all.len >= 1);

  // ── Iterator ───────────────────────────────────────────────────────────
  var it = re.matchIter(&ctx_a, INPUT);
  var iter_count: usize = 0;
  while (it.next()) |_| iter_count += 1;
  try testing.expect(iter_count >= 1);
  it.reset();
  // Confirm reset works by iterating again.
  iter_count = 0;
  while (it.next()) |_| iter_count += 1;
  try testing.expect(iter_count >= 1);

  // ── Replacement ────────────────────────────────────────────────────────
  if (try re.replaceFirst(&ctx_a, gpa, INPUT, "X")) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }
  if (try re.replaceFirstWithin(&ctx_a, gpa, INPUT, "X", 0, INPUT.len)) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }
  if (try re.replaceAll(&ctx_a, gpa, INPUT, "X")) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }
  if (try re.replaceAllWithin(&ctx_a, gpa, INPUT, "X", 0, INPUT.len)) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }

  // ── Cache ──────────────────────────────────────────────────────────────
  var cache = try re.initContextCache(gpa, io, 2, &.{});
  defer cache.deinit(gpa);
  try re.warmupContextCache(&cache, gpa, io, 3, &.{});
  try re.warmupContextCacheExact(&cache, gpa, io, 2, &.{});

  const ctx_c = try cache.acquire(io);
  defer cache.release(io, ctx_c);
  try testing.expect(re.matches(ctx_c, INPUT));
}

test "regex API coverage: Regex comptime-compile and findAllComptime" {
  const gpa = testing.allocator;

  // ── compileComptime ────────────────────────────────────────────────────
  var re = comptime regex.compileComptime(dynamic_nfa, .{}, PATTERN);

  // ── compileComptimeNonIntercepting ─────────────────────────────────────
  const re_ni = comptime try regex.compileComptimeNonIntercepting(dynamic_nfa, .{}, PATTERN);
  _ = re_ni;

  // ── Runtime context for comptime-compiled regex ────────────────────────
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  try testing.expect(re.matches(&ctx, INPUT));

  // ── findAllComptime (requires fixed-context regex) ─────────────────────
  comptime {
    @setEvalBranchQuota(1_000_000);
    var re_fx = regex.compileComptime(fixed_nfa, .{}, PATTERN);
    var ctx_fx = re_fx.initContextFixed();
    const matches_ct = re_fx.findAllComptime(&ctx_fx, INPUT);
    if (matches_ct.len < 1) @compileError("findAllComptime returned empty result");
  }
}

test "regex API coverage: Regex initContextFixed" {
  const re = comptime regex.compileComptime(fixed_nfa, .{}, PATTERN);
  var ctx = re.initContextFixed();
  try testing.expect(re.matches(&ctx, INPUT));
}

test "regex API coverage: Regex (type-erased) runtime-compile" {
  const gpa = testing.allocator;
  const io = testing.io;

  // ── Compilation ────────────────────────────────────────────────────────
  var re = try R.compile(.{}, gpa, PATTERN);
  defer re.deinit(gpa);

  // ── Static-info methods ────────────────────────────────────────────────
  _ = re.requiredContextLen();

  // ── computeFieldLens ──────────────────────────────────────────────────
  _ = re.computeFieldLens(&.{});

  // ── Context creation ───────────────────────────────────────────────────
  var ctx_a = try re.initContext(gpa);
  defer ctx_a.deinit(gpa);

  var ctx_b = try re.initContextIncluding(gpa, &.{re});
  defer ctx_b.deinit(gpa);

  try re.updateContext(&ctx_a, gpa, &.{re});

  // ── Match queries ──────────────────────────────────────────────────────
  try testing.expect(re.matches(&ctx_a, INPUT));
  try testing.expect(!re.matchesExact(&ctx_a, INPUT));
  try testing.expect(re.matchesExact(&ctx_a, "aaa"));

  const m_start = re.matchStart(&ctx_a, INPUT);
  try testing.expect(m_start != null);

  const m = re.match(&ctx_a, INPUT);
  try testing.expect(m != null);

  const f = re.find(&ctx_a, INPUT, 0, INPUT.len);
  try testing.expect(f != null);

  // ── Allocating ─────────────────────────────────────────────────────────
  const all = try re.findAllAlloc(&ctx_a, gpa, INPUT);
  defer gpa.free(all);
  try testing.expect(all.len >= 1);

  // ── Iterator ───────────────────────────────────────────────────────────
  var it = re.matchIter(&ctx_a, INPUT);
  var iter_count: usize = 0;
  while (it.next()) |_| iter_count += 1;
  try testing.expect(iter_count >= 1);
  it.reset();
  iter_count = 0;
  while (it.next()) |_| iter_count += 1;
  try testing.expect(iter_count >= 1);

  // ── Replacement ────────────────────────────────────────────────────────
  if (try re.replaceFirst(&ctx_a, gpa, INPUT, "X")) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }
  if (try re.replaceFirstWithin(&ctx_a, gpa, INPUT, "X", 0, INPUT.len)) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }
  if (try re.replaceAll(&ctx_a, gpa, INPUT, "X")) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }
  if (try re.replaceAllWithin(&ctx_a, gpa, INPUT, "X", 0, INPUT.len)) |rep| {
    var r1 = rep;
    defer r1.deinit(gpa);
  }

  // ── Cache ──────────────────────────────────────────────────────────────
  var cache = try re.initContextCache(gpa, io, 2, &.{});
  defer cache.deinit(gpa);
  try re.warmupContextCache(&cache, gpa, io, 3, &.{});
  try re.warmupContextCacheExact(&cache, gpa, io, 2, &.{});

  const ctx_c = try cache.acquire(io);
  defer cache.release(io, ctx_c);
  try testing.expect(re.matches(ctx_c, INPUT));
}

test "regex API coverage: Regex comptime-compile" {
  const gpa = testing.allocator;

  // ── compileComptime (panicking variant) ────────────────────────────────
  const re = comptime R.compileComptime(.{}, PATTERN);

  // ── compileComptimeNonIntercepting (error-returning variant) ───────────
  const re_ni = comptime try R.compileComptimeNonIntercepting(.{}, PATTERN);
  _ = re_ni;

  // Runtime context, runtime match on the comptime-compiled regex.
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  try testing.expect(re.matches(&ctx, INPUT));
}

test "regex API coverage: Regex initContextFixed and findAllComptime" {
  const re = comptime RFixed.compileComptime(.{}, PATTERN);
  var ctx = re.initContextFixed();

  try testing.expect(re.matches(&ctx, INPUT));

  // findAllComptime on the type-erased Regex with fixed contexts.
  comptime {
    @setEvalBranchQuota(1_000_000);
    const re_fx = RFixed.compileComptime(.{}, PATTERN);
    var ctx_fx = re_fx.initContextFixed();
    const matches_ct = re_fx.findAllComptime(&ctx_fx, INPUT);
    if (matches_ct.len < 1) @compileError("Regex.findAllComptime returned empty result");
  }
}

test "regex API coverage: Match and Replacement deinit" {
  const gpa = testing.allocator;
  var re = try regex.compile(dynamic_nfa, .{}, gpa, PATTERN);
  defer re.deinit(gpa);

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  if (re.match(&ctx, INPUT)) |m_val| {
    _ = m_val;
  }

  if (try re.replaceFirst(&ctx, gpa, INPUT, "X")) |rep_val| {
    rep_val.deinit(gpa);
  }

  if (try re.replaceAll(&ctx, gpa, INPUT, "X")) |rep_val| {
    rep_val.deinit(gpa);
  }
}

test "regex API coverage: Building" {
  const gpa = std.testing.allocator;
  const Re = regex.Regex(.{
    .minimal_nfa = .{
      .context = .{ .fixed = 64 },
      .offset_bp = .i8,
    },
  }, .{});

  {
    const re = comptime Re.compileComptime(.{}, "^abc");
    try expectEqual(@TypeOf(re), Re);
  }
 
  {
    const re = comptime try Re.compileComptimeNonIntercepting(.{}, "^abc");
    try expectEqual(@TypeOf(re), Re);
  }
 
  {
    var re = try Re.compile(.{}, gpa, "^abc");
    defer re.deinit(gpa);
    try expectEqual(@TypeOf(re), Re);
  }
}
