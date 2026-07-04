//! Unified fuzzing test suite.
//!
//! Replaces stress.zig and memory_leak.zig.  Organized by what each test is
//! looking for:
//!
//! 1. CRASH RESISTANCE — high-volume random input, no OOM injection.
//!    Goal: no input produces a panic, segfault, or hang.
//!
//! 2. LEAK RESISTANCE — low-volume random input with full OOM injection.
//!    Goal: every allocation path is leak-free under failure.  Iteration
//!    count is low because cost is iterations × avg_allocs_per_pattern.
//!
//! 3. CURATED REGRESSION — hand-picked patterns historically known to expose
//!    bugs.  Small fixed lists, no fuzzing.
//!
//! Both the NFA-pipeline (`DynamicRegex.compile`) and the partial-pipeline
//! (`pzre.compile.generate`) APIs are exercised — they're distinct public
//! entry points with their own leak surfaces.
//!
//! Match-path coverage is included where patterns are likely to compile.

const std = @import("std");
const builtin = @import("builtin");
const Allocator = std.mem.Allocator;
const testing = std.testing;

const pzre = @import("../root.zig");

const Config = pzre.compile.Config;
const Match = pzre.regex.Match;
const lexer = pzre.compile.lexer;

// ── Fragment pools ─────────────────────────────────────────────────────────

const Fragments = struct {
  /// Well-formed atoms.
  const atoms = [_][]const u8{
    "a", "b", "c", "x", "0", "9",
    ".", "\\w", "\\W", "\\d", "\\D", "\\s", "\\S",
    "\\n", "\\t", "\\r", "\\0", "\\e",
    "\\\\", "\\.", "\\|", "\\*", "\\+", "\\?",
    "[a]", "[a-z]", "[A-Z0-9]", "[^a-zA-Z]",
    "[-a]", "[a-]", "[abc][cba]", "[mnrp\\d]",
    "[\\]\\^\\-]",
  };

  /// Quantifiers.
  const quantifiers = [_][]const u8{ "", "?", "*", "+", "{0,0}", "{2}", "{1,3}", "{0,5}", "{5,}" };

  /// Anchors.
  const anchors = [_][]const u8{ "^", "$", "\\b", "\\B", "\\A", "\\z" };

  /// Tokens drawn from the broken-pattern pool — likely to fail parse, but
  /// must never crash the engine.  Mirrors stress.zig's `valid_pool`.
  const chaos_chars = "a1P#*+?|()[]{}^$.,\\-";

  /// Wider chaos pool that includes whitespace and newlines, used with
  /// configs that have pat_ignore_whitespace etc. enabled.
  const chaos_chars_with_ws = "aAmM*+?|()[]{}^$.*+?|()[]{}^$.\\- \r\t\n";

  /// Quantifier-bound edge cases.  Empty string, valid small numbers,
  /// powers of two boundary, u64 max, negative, non-digit.
  const quant_bounds = [_][]const u8{
    "", "0", "1", "5", "256", "1024", "65535",
    "9999999999", "18446744073709551615", "-1", "a",
  };

  /// Atoms small enough to quantify cheaply.
  const quant_atoms = "a.(?:[a-z])\\d";

  /// Simple postfix quantifiers.
  const simple_quants = "*+?";
};

// ── Pattern generators ─────────────────────────────────────────────────────

fn pickStr(comptime T: type, rng: std.Random, pool: []const T) T {
  return pool[rng.intRangeLessThan(usize, 0, pool.len)];
}

fn pickByte(rng: std.Random, pool: []const u8) u8 {
  return pool[rng.intRangeLessThan(usize, 0, pool.len)];
}

/// Compose atoms, quantifiers, and connectives into a syntactically plausible
/// pattern.  Likely to compile, so this drives the match path too.
fn genPattern(gpa: Allocator, rng: std.Random, target_len: usize) ![]u8 {
  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);

  while (buf.items.len < target_len) {
    const choice = rng.intRangeLessThan(u8, 0, 100);
    if (choice < 10 and buf.items.len == 0) {
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.anchors));
    } else if (choice < 70) {
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.atoms));
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.quantifiers));
    } else if (choice < 85) {
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.atoms));
      try buf.append(gpa, '|');
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.atoms));
    } else {
      try buf.append(gpa, '(');
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.atoms));
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.quantifiers));
      try buf.append(gpa, ')');
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.quantifiers));
    }
  }
  return buf.toOwnedSlice(gpa);
}

/// Single-char-at-a-time random sequence.  Most won't parse.
fn genChaos(gpa: Allocator, rng: std.Random, target_len: usize, pool: []const u8) ![]u8 {
  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);
  for (0..target_len) |_| try buf.append(gpa, pickByte(rng, pool));
  return buf.toOwnedSlice(gpa);
}

/// Char repetition variant — pick a char, repeat it 1-8 times, repeat.  This
/// is the original stress.zig "structural fuzzing" shape.  Produces patterns
/// like "aaaa****++++" that stress repeated-token handling.
fn genChaosRepeated(gpa: Allocator, rng: std.Random, target_len: usize, pool: []const u8) ![]u8 {
  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);
  while (buf.items.len < target_len) {
    const char = pickByte(rng, pool);
    const count = rng.intRangeAtMost(usize, 1, 8);
    try buf.appendNTimes(gpa, char, count);
  }
  return buf.toOwnedSlice(gpa);
}

/// Malformed set: `[...]` with chaotic inner contents, optionally missing
/// the closing bracket.  Draws from lexer-defined escape letters so every
/// recognized escape gets coverage.
fn genSetChaos(gpa: Allocator, rng: std.Random) ![]u8 {
  const escape_chars: []const u8 = lexer.perl_set_letters_string ++ lexer.escape_sequence_letters_string;
  const weight = 5;
  const escape_symbols = "\\" ** @divTrunc(escape_chars.len, weight);
  const structural_chars = "-^[]";
  const pool = escape_symbols ++ escape_chars ++ structural_chars;

  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);

  try buf.append(gpa, '[');
  if (rng.boolean()) try buf.append(gpa, '^');
  const inner_len = rng.intRangeAtMost(usize, 0, 16);
  for (0..inner_len) |_| try buf.append(gpa, pickByte(rng, pool));
  if (rng.boolean()) try buf.append(gpa, ']');
  return buf.toOwnedSlice(gpa);
}

/// Mix of 'a' and `\X` escapes where X is a random byte across the full
/// 0-255 range.  Exercises every possible escape codepoint.
fn genEscapeFuzz(gpa: Allocator, rng: std.Random) ![]u8 {
  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);
  for (0..16) |_| {
    if (rng.boolean()) {
      try buf.append(gpa, 'a');
    } else {
      try buf.append(gpa, '\\');
      try buf.append(gpa, rng.intRangeLessThan(u8, 0, 255));
    }
  }
  return buf.toOwnedSlice(gpa);
}

/// Build a pattern that ends in 1-4 quantifiers with random bounds.  Targets
/// the bound-parsing edge cases.
fn genQuantifierFuzz(gpa: Allocator, rng: std.Random) ![]u8 {
  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);

  try buf.append(gpa, pickByte(rng, Fragments.quant_atoms));

  const q_count = rng.intRangeAtMost(usize, 1, 4);
  for (0..q_count) |_| {
    if (rng.boolean()) {
      try buf.append(gpa, pickByte(rng, Fragments.simple_quants));
    } else {
      try buf.append(gpa, '{');
      try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.quant_bounds));
      if (rng.boolean()) {
        try buf.append(gpa, ',');
        try buf.appendSlice(gpa, pickStr([]const u8, rng, &Fragments.quant_bounds));
      }
      try buf.append(gpa, '}');
    }
  }
  return buf.toOwnedSlice(gpa);
}

fn genInput(gpa: Allocator, rng: std.Random, target_len: usize) ![]u8 {
  const alphabet = "abcxyz0123456789 \t.";
  var buf: std.ArrayList(u8) = .empty;
  errdefer buf.deinit(gpa);
  for (0..target_len) |_| try buf.append(gpa, pickByte(rng, alphabet));
  return buf.toOwnedSlice(gpa);
}

// ── Pipeline drivers ───────────────────────────────────────────────────────

/// Full pipeline: compile to NFA, optionally match, deinit.
fn driveNfa(gpa: Allocator, comptime config: Config, pattern: []const u8, input: ?[]const u8) !void {
  var re = pzre.anyregex.DynamicRegex.compile(config, gpa, pattern) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer re.deinit(gpa);

  if (input) |str| {
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
    _ = re.find(&ctx, str, 0, str.len);
  }
}

/// Partial pipeline: generate only AST + sets, then clean them up directly.
/// Distinct public API with its own leak surface.
fn driveGenerate(gpa: Allocator, comptime config: Config, pattern: []const u8) !void {

  var result = pzre.compile.parseObjects(config, .initMany(&.{.ast, .sets}), null, gpa, pattern) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  pzre.misc.destroySets(gpa, result.sets);
  result.ast.deinit(gpa);
}

/// Driver dispatch — split a fuzzed pattern between the NFA and generate-only
/// paths to share crash-resistance coverage across both APIs.
fn driveRandom(gpa: Allocator, rng: std.Random, comptime config: Config, pattern: []const u8) !void {
  if (rng.boolean()) {
    try driveNfa(gpa, config, pattern, null);
  } else {
    try driveGenerate(gpa, config, pattern);
  }
}

// ── OOM-injection runners ──────────────────────────────────────────────────
//
// Top-level free functions because checkAllAllocationFailures calls them as
// fn(Allocator, ...args).  A struct with a method puts `self` where the
// allocator should be — that's the bug fix from the previous iteration.

fn runNfaWithMatch(alloc: Allocator, pattern: []const u8, input: []const u8) !void {
  try driveNfa(alloc, .{}, pattern, input);
}

fn runNfaCompileOnly(alloc: Allocator, pattern: []const u8) !void {
  try driveNfa(alloc, .{}, pattern, null);
}

fn runGenerate(alloc: Allocator, pattern: []const u8) !void {
  try driveGenerate(alloc, .{}, pattern);
}

// ═══════════════════════════════════════════════════════════════════════════
// CRASH RESISTANCE — high-volume, no OOM injection
// ═══════════════════════════════════════════════════════════════════════════

test "fuzz/crash: well-formed patterns drive compile + match" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0xdeadbeef);
  const rng = prng.random();

  for (0..10_000) |_| {
    const pattern = try genPattern(gpa, rng, rng.intRangeAtMost(usize, 8, 64));
    defer gpa.free(pattern);
    const input = try genInput(gpa, rng, rng.intRangeAtMost(usize, 16, 128));
    defer gpa.free(input);
    try driveNfa(gpa, .{}, pattern, input);
  }
}

test "fuzz/crash: chaotic byte sequences (single-char)" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x1337beef);
  const rng = prng.random();

  for (0..50_000) |_| {
    const pattern = try genChaos(gpa, rng, rng.intRangeAtMost(usize, 1, 32), Fragments.chaos_chars);
    defer gpa.free(pattern);
    try driveRandom(gpa, rng, .{}, pattern);
  }
}

test "fuzz/crash: chaotic byte sequences (repeated-char)" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x5_7_0_c_7);
  const rng = prng.random();

  for (0..50_000) |_| {
    const pattern = try genChaosRepeated(gpa, rng, 32, Fragments.chaos_chars);
    defer gpa.free(pattern);
    try driveRandom(gpa, rng, .{}, pattern);
  }
}

test "fuzz/crash: malformed character sets (lexer-driven pool)" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x5e7bad);
  const rng = prng.random();

  for (0..50_000) |_| {
    const pattern = try genSetChaos(gpa, rng);
    defer gpa.free(pattern);
    try driveRandom(gpa, rng, .{}, pattern);
  }
}

test "fuzz/crash: random escape sequences over the full byte range" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0xfeedface);
  const rng = prng.random();

  for (0..50_000) |_| {
    const pattern = try genEscapeFuzz(gpa, rng);
    defer gpa.free(pattern);
    try driveRandom(gpa, rng, .{}, pattern);
  }
}

test "fuzz/crash: quantifier bound edge cases" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0xbacde);
  const rng = prng.random();

  for (0..50_000) |_| {
    const pattern = try genQuantifierFuzz(gpa, rng);
    defer gpa.free(pattern);
    try driveRandom(gpa, rng, .{}, pattern);
  }
}

test "fuzz/crash: configuration matrix × random patterns" {
  const gpa = testing.allocator;

  // Full Config matrix.  Every public field on Config / Limits / Semantics /
  // Features represented.  ast_optimizations toggled between empty and full;
  // strategy left as null to keep the matrix size manageable (strategy
  // selection has its own dedicated coverage elsewhere).
  const configs = comptime b: {
    @setEvalBranchQuota(100_000);
    var res: [32]Config = undefined;
    var prng = std.Random.DefaultPrng.init(0xc0af16);
    const r = prng.random();
    for (&res) |*cfg| {
      cfg.* = .{
        .ast_optimizations = if (r.boolean()) .initFull() else .initEmpty(),
        .semantics = .{
          .multiline = r.boolean(),
          .ignore_case = r.boolean(),
          .pat_ignore_whitespace = r.boolean(),
          .pat_ignore_all_whitespace = r.boolean(),
          .never_implicit_newline = r.boolean(),
          .dotall = r.boolean(),
        },
        .limits = .{
          .gpa_upper_bound = if (r.boolean()) 1 << 10 else 1 << 20,
          .max_states = if (r.boolean()) 10 else 10000,
          .max_depth = if (r.boolean()) 5 else 255,
          .max_arbitrary_repetition = if (r.boolean()) r.intRangeAtMost(usize, 0, 100) else null,
        },
        .features = .{
          .capture_groups = r.boolean(),
          .word_boundary = r.boolean(),
        },
      };
    }
    break :b res;
  };

  var prng = std.Random.DefaultPrng.init(0xbadc0de);
  const rng = prng.random();

  for (0..10_000) |_| {
    const pattern = try genChaosRepeated(gpa, rng, 64, Fragments.chaos_chars_with_ws);
    defer gpa.free(pattern);

    const cfg_idx = rng.intRangeLessThan(usize, 0, configs.len);
    inline for (configs, 0..) |cfg, i| {
      if (i == cfg_idx) try driveNfa(gpa, cfg, pattern, null);
    }
  }
}

test "fuzz/crash: large match input doesn't leak in match path" {
  const gpa = testing.allocator;

  const pattern = ".*(a|b|c)+.*$";
  var re = pzre.anyregex.DynamicRegex.compile(.{}, gpa, pattern) catch return;
  defer re.deinit(gpa);

  const input = try gpa.alloc(u8, 1024 * 1024);
  defer gpa.free(input);
  @memset(input, 'd');

  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
  try testing.expect(re.find(&ctx, input, 0, input.len) == null);

  // Context reuse: list capacity growth must be bounded across many matches.
  for (0..100) |_| _ = re.find(&ctx, input, 0, input.len);
}

// ═══════════════════════════════════════════════════════════════════════════
// LEAK RESISTANCE — low-volume, OOM injection at every allocation site
// ═══════════════════════════════════════════════════════════════════════════

test "fuzz/leak: well-formed patterns through full pipeline" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x1eaf);
  const rng = prng.random();

  for (0..50) |_| {
    const pattern = try genPattern(gpa, rng, rng.intRangeAtMost(usize, 6, 24));
    defer gpa.free(pattern);
    const input = try genInput(gpa, rng, 16);
    defer gpa.free(input);

    try runNfaWithMatch(gpa, pattern, input);
    try testing.checkAllAllocationFailures(gpa, runNfaWithMatch, .{ pattern, input });
  }
}

test "fuzz/leak: chaotic patterns through full pipeline" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x1eaf2);
  const rng = prng.random();

  for (0..100) |_| {
    const pattern = try genChaos(gpa, rng, rng.intRangeAtMost(usize, 4, 16), Fragments.chaos_chars);
    defer gpa.free(pattern);

    try runNfaCompileOnly(gpa, pattern);
    try testing.checkAllAllocationFailures(gpa, runNfaCompileOnly, .{pattern});
  }
}

test "fuzz/leak: generate-only path (AST + sets, no NFA)" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x1eaf3);
  const rng = prng.random();

  // Partial-pipeline path has its own leak surface: AST and sets must both
  // be cleaned up correctly when the user stops without building an NFA.
  for (0..100) |_| {
    const pattern = try genChaos(gpa, rng, rng.intRangeAtMost(usize, 4, 16), Fragments.chaos_chars);
    defer gpa.free(pattern);

    try runGenerate(gpa, pattern);
    try testing.checkAllAllocationFailures(gpa, runGenerate, .{pattern});
  }
}

test "fuzz/leak: set malformation through generate" {
  const gpa = testing.allocator;
  var prng = std.Random.DefaultPrng.init(0x1eaf4);
  const rng = prng.random();

  for (0..100) |_| {
    const pattern = try genSetChaos(gpa, rng);
    defer gpa.free(pattern);

    try runGenerate(gpa, pattern);
    try testing.checkAllAllocationFailures(gpa, runGenerate, .{pattern});
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// CURATED REGRESSION — patterns historically known to expose bugs
// ═══════════════════════════════════════════════════════════════════════════

test "regression: structural imbalances" {
  const gpa = testing.allocator;
  const patterns = [_][]const u8{
    "(", ")", "(()", "())", "a(b", "b)a",
    "[", "]", "[]", "[^]", "[a-", "[-a]", "[a-b-c]",
    "{", "}", "a{", "a}", "a{1", "a{1,", "a{1,2", "a{,2}",
    "*", "+", "?", "a**", "a+*", "a?+", "(*)", "(|)*",
    "a{2,1}",
    "\\", "a\\", "[\\", "\\q", "\\x", "\\u",
    "|", "a|", "|a", "a||b", "(|)",
    "([{\\+*?^$|", "*+?{}[]()|\\",
  };
  for (patterns) |p| try driveNfa(gpa, .{}, p, null);
}

test "regression: integer-boundary quantifier attacks" {
  const gpa = testing.allocator;
  const patterns = [_][]const u8{
    "a{256}",
    "a{99999999999999999999999999}",
    "a{18446744073709551615}",
    "a{0,18446744073709551615}",
  };
  for (patterns) |p| try driveNfa(gpa, .{}, p, null);
}

test "regression: encoding edge cases" {
  const gpa = testing.allocator;
  const patterns = [_][]const u8{
    "\x00",
    "a\x00b",
    "\x00" ** 10000,
    "\\x", "\\u", "\\u{", "\\u{10FFFF",
    "\xFF", "\x80\x80", "[\xFF-\xFF]",
  };
  for (patterns) |p| try driveNfa(gpa, .{}, p, null);
}

test "regression: set edge cases" {
  const gpa = testing.allocator;
  const patterns = [_][]const u8{
    "[z-a]",          // reversed range
    "[]]", "[-a]", "[a-]", "[---]", "[^]]",
    "[a-zA-Z0-9a-z]", // overlapping ranges
    "[^]",            // empty inversion
  };
  for (patterns) |p| try driveNfa(gpa, .{}, p, null);
}

test "regression: resource exhaustion patterns return error, don't hang" {
  const gpa = testing.allocator;
  const config: Config = .{ .limits = .{ .gpa_upper_bound = 1 << 18 } };

  const patterns = [_][]const u8{
    // Massive exact quantifiers
    "a{50000}",
    "(a|b){50000}",
    // Nested quantifiers
    "((a{10}){10}){10}",
    "(((a+)+)+)+",
    "(((a*)*)*)*",
    // Catastrophic alternation
    "a" ** 10000,
    "a" ++ "|a" ** 10000,
    "(a|" ** 5000 ++ "b" ++ ")" ** 5000,
    // High-density sets and groups
    "[" ++ "a-z" ** 1000 ++ "]",
    "[" ++ "\\w\\W\\d\\D\\s\\S" ** 1000 ++ "]",
    "()" ** 10000,
    "(a)" ** 10000,
    // Many anchors
    "^" ** 10000,
    "\\b" ** 10000,
    // Unbalanced openers
    "(" ** 10000,
    "[" ** 10000,
  };
  for (patterns) |p| try driveNfa(gpa, config, p, null);
}

test "regression: deeply nested groups don't blow the stack" {
  const gpa = testing.allocator;
  const config: Config = .{ .limits = .{ .max_depth = 255 } };
  // 40 nested groups around a single atom.
  const pattern = "((((((((((((((((((((((((((((((((((((((((a))))))))))))))))))))))))))))))))))))))))";
  try driveNfa(gpa, config, pattern, null);
}
