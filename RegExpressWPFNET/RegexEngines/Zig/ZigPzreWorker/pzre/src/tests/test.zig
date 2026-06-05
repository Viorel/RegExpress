//! pzre pattern test harness.
//!
//! Every public test function (testMatch, testMatches, ...) drives a cartesian
//! product over `ExecutionBlock`s. A block is a tuple of compatible lists:
//!
//!     ExecutionBlock = {
//!       regexes       - list of Regex types
//!       paths         - list of ExecutionPath values
//!       strategies    - list of Strategy values (null = auto-dispatch)
//!       optimizations - list of EnumSet(Optimization) values
//!     }
//!
//! Inside a block, every tuple permutation is
//! tested. Multiple blocks are used to express that some combinations are
//! impossible together - e.g. DynamicRegex cannot match at comptime because
//! its context is dynamic, so it lives in a block with paths = {rt_rt, ct_rt}.
//! A separate block for compact_fixed regexes includes ct_ct.
//!
//! Result comparison goes through pzre.lens.testing.expectDeeplyEqualWithSemantics
//! with default semantics - it handles iterators, error unions, optionals,
//! type mismatches, etc. on its own. No special-casing in this file.
//!
//! Allocation-failure coverage runs after the cartesian product, on a subset:
//! one (regex x strategy x optimization) tuple per block, rt_rt path only.

const std = @import("std");
const Allocator = std.mem.Allocator;

const pzre = @import("../root.zig");
const meta = pzre.meta;
const expectDeeplyEqual = pzre.lens.testing.expectDeeplyEqualWithSemantics;
const debug = pzre.lens.debug;

const phase_two = @import("phase_two.zig");

const Optimization = pzre.ast.optimize.Optimization;
const StrategyName = pzre.compile.strategy.Name;

pub const Config           = pzre.compile.Config;
pub const Match            = pzre.regex.Match;
pub const Replacement      = pzre.regex.Replacement;
pub const ManyReplacements = pzre.regex.ManyReplacements;
pub const E                = pzre.compile.Error;
pub const Regex     = pzre.regex.Regex;
const Arch          = pzre.arch.Arch;
const Global        = pzre.compile.Global;

/// Set this to true when you need to run runtime-only data inspection algorithms
const rt_only_switch = false;
/// Skips these in order to iterate on testing faster; very expensive
const no_memory_crash_tests = false;
const disable_optimizations = false;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("simple.zig");
    _ = @import("showcase.zig");

    // -- Language behavior --
    // These verify that everything documented in LANGUAGE.md is truthful
    _ = @import("encoding.zig");
    _ = @import("assertions.zig");
    _ = @import("epsilon.zig");
    _ = @import("precedence.zig");
    _ = @import("sets.zig");
    // 
    // -- Compilation --
    _ = @import("config.zig");
    _ = @import("error.zig");
    // 
    // --  API  --
    _ = @import("api_coverage.zig");
    _ = @import("iterate.zig");
    _ = @import("multithreaded.zig");
    _ = @import("search_and_replace.zig");
    _ = @import("semantics.zig");
    _ = @import("context.zig");

    // -- Strategies --
    // Tests targeting specific strategies. They can still cover other strategies due to the nature of the 
    // harness
    _ = @import("start_set.zig");

    // -- Graph Construction --
    _ = @import("topology.zig");
    _ = @import("submachines.zig");
    _ = @import("cast_alt_path.zig");

    // -- Stress tests --
    _ = @import("fuzzing.zig");
  }
}

// ---------------------------------------------
// -------- Testing Configuration --------------
// ---------------------------------------------

/// Which phases happen at comptime vs runtime.
pub const ExecutionPath = enum {
  rt_rt,  // compile and match at runtime
  ct_rt,  // compile at comptime, match at runtime
  ct_ct,  // compile and match at comptime
};

/// Compatible-configuration tuple. Within a block, every cartesian-product
/// combination is exercised.
///
/// Two compilation surfaces are covered:
///   regexes - AnyRegex types (type-erased). Compiled via their own methods
///             Re.compile / Re.compileComptimeNonIntercepting.
///   archs   - unresolved Arch values for the strictly-typed Regex path. These
///             are compiled via the top-level free functions in regex.zig
///             (regex.compile / regex.compileComptimeNonIntercepting), which
///             resolve the arch internally and return a Regex(...) value.
pub const ExecutionBlock = struct {
  name: []const u8,
  regexes: []const type = &.{},
  archs: []const Arch = &.{},
  global: Global = .{},
  paths: []const ExecutionPath,
  strategies: []const ?StrategyName,
  optimizations: []const std.EnumSet(Optimization),
  test_compact_fixed: bool = false,
  test_optimal_resolution: bool = false,
};

/// How to destroy a match result that allocates.
pub const MatchDestruction = enum {
  deinit, free, none,

  pub fn handle(comptime mode: MatchDestruction, m: anytype, gpa: Allocator) void {
    if (comptime mode == .none) return;
    const T = @TypeOf(m);
    if (comptime meta.isOptional(T)) {
      if (m) |v| handleInner(v, mode, gpa);
      return;
    }
    handleInner(m, mode, gpa);
  }

  fn handleInner(v: anytype, comptime mode: MatchDestruction, gpa: Allocator) void {
    switch (comptime mode) {
      .free   => gpa.free(v),
      .deinit => @constCast(&v).deinit(gpa),
      .none   => {},
    }
  }
};

const all_optimizations = [_]std.EnumSet(Optimization){
  std.EnumSet(Optimization).initFull(),
  std.EnumSet(Optimization).initEmpty(),
};

/// General-purpose strategies that can solve any valid pattern.
/// null = engine-chosen via dispatch.
/// to force-test each one against every pattern in the corpus.
/// 
/// Reminder: the strategy is another picking constraint against the architecture swarm that defined Regex
///   If Regex is narrow enough to not include an architecture that supports the strategy it will compile error
///   Only use this for general purpose Regex definitions
///   Only use strategies that are semantically valid for any-pattern
const general_strategies = [_]?StrategyName{
  null,
  .bi_directional_pass,
  .start_set_pass,
};

/// Path constraints exported for individual test functions to narrow.
const all_paths       = [_]ExecutionPath{ .rt_rt, .ct_rt, .ct_ct };
const rt_only         = [_]ExecutionPath{ .rt_rt };
const rt_match_paths  = [_]ExecutionPath{ .rt_rt, .ct_rt };
const ct_match_paths  = [_]ExecutionPath{ .ct_ct };
const ct_compile_only = [_]ExecutionPath{ .ct_ct, .ct_rt };

pub const dynamic_only = [_]ExecutionBlock{
  // Dynamic context: runtime allocation, cannot match at comptime.
  .{
    .name = "dynamic_only",
    .regexes = &.{ pzre.anyregex.DynamicRegex },
    .paths = if (rt_only_switch) &rt_only else &rt_match_paths,
    .strategies = &general_strategies,
    .optimizations = &all_optimizations,
  },
};

/// Primary list of blocks. Add additional regex types to the matching block
/// (or create new blocks) to extend coverage without restructuring tests.
pub const default_blocks = [_]ExecutionBlock{
  // Dynamic context: runtime allocation, cannot match at comptime.
  // .{
  //   .name = "default_dynamic",
  //   .regexes = &.{ pzre.anyregex.DynamicRegex },
  //   .paths = if (rt_only_switch) &rt_only else &rt_match_paths,
  //   .strategies = &general_strategies,
  //   .optimizations = &all_optimizations,
  // },
  // // Fixed-size compact context: supports all three paths.
  // .{
  //   .name = "default_fixed_64",
  //   .regexes = &.{ pzre.anyregex.FixedRegex(512) },
  //   .paths = if (rt_only_switch) &rt_only else &all_paths,
  //   .strategies = &general_strategies,
  //   .optimizations = &all_optimizations,
  // },
  .{  // This block also triggers .compact_fixed tests as its impossible to define them here
      // due to the pattern being type-defined
      //
      // The strictly-typed Regex path compiles from UNRESOLVED archs via the
      // top-level regex.compile* free functions, which resolve internally.
    .name = "Regex Generics",
    .regexes = &.{ pzre.anyregex.FixedRegex(512) },
    .archs = &.{
      // .{ .minimal_nfa = .{ .context = .{ .fixed = 100 }, .offset_bp = .i8 } },
      .{ .minimal_nfa = .{ .context = .{ .fixed = 200 } } },
    },
    .paths = if (rt_only_switch) &.{} else &ct_compile_only,
    .strategies = &general_strategies,
    .optimizations = &all_optimizations,
    .test_compact_fixed = true,
    .test_optimal_resolution = true,
  },
};

// ---------------------------------------------
// -------- Exported Testing Functions ---------
// ---------------------------------------------

pub fn testAnyError(comptime pattern: []const u8, comptime config: Config) !void {
  const gpa = std.testing.allocator;
  const duped = try gpa.dupe(u8, pattern);
  defer gpa.free(duped);

  inline for (all_optimizations) |opts| {
    const cfg = comptime withOpts(config, opts, null, null);
    if (pzre.anyregex.DynamicRegex.compile(cfg, gpa, duped)) |re_val| {
      var re = re_val;
      re.deinit(gpa);
      return error.ExpectedErrorGotSuccess;
    } else |_| {}
  }
}

pub fn testMatches(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: bool,
) !void {
  try harness(pattern, "matches", .{str}, expected, .{}, &all_paths, .none, &default_blocks);
}

/// Single string/expected pair for `testMatches`-style assertions.
pub const ExpectMatches = struct {
  str: []const u8,
  expected: bool,

  pub fn extraArgs(self: @This()) struct { []const u8 } {
    return .{ self.str };
  }
};

/// Compile the pattern once per cartesian-product cell and iterate every
/// expectation in `expectations` against the resulting machine.  Use this
/// when a single test has multiple inputs sharing the same pattern; it
/// avoids re-compiling (and re-running OOM injection) per input.
pub fn testMatchesMany(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatches,
) !void {
  try harnessMany(pattern, "matches", expectations, .{}, &all_paths, .none, &default_blocks);
}

pub fn testFind(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime start: usize,
  comptime expected: E!?Match,
) !void {
  try harness(pattern, "find", .{str, start, str.len}, expected, .{}, &all_paths, .none, &default_blocks);
}

/// Single input/start/expected triple for `testFind`-style assertions.
pub const ExpectFind = struct {
  str: []const u8,
  start: usize,
  expected: E!?Match,

  pub fn extraArgs(self: @This()) struct { []const u8, usize, usize } {
    return .{ self.str, self.start, self.str.len };
  }
};

/// See `testMatchesMany` -- same compile-once semantics for `find`.
pub fn testFindMany(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectFind,
) !void {
  try harnessMany(pattern, "find", expectations, .{}, &all_paths, .none, &default_blocks);
}

pub fn testMatch(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: E!?Match,
) !void {
  try testMatchWithConfig(pattern, str, expected, .{});
}

/// Single string/expected pair for `testMatch`-style assertions.
pub const ExpectMatch = struct {
  str: []const u8,
  expected: E!?Match,

  pub fn extraArgs(self: @This()) struct { []const u8 } {
    return .{ self.str };
  }
};

/// See `testMatchesMany` -- same compile-once semantics for `match`.
pub fn testMatchMany(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatch,
) !void {
  try testMatchManyWithConfig(pattern, expectations, .{});
}

pub fn testMatchWithConfig(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: E!?Match,
  comptime config: Config,
) !void {
  try harness(pattern, "match", .{str}, expected, config, &all_paths, .none, &default_blocks);
}

/// See `testMatchesMany` -- same compile-once semantics for `match` with config.
pub fn testMatchManyWithConfig(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatch,
  comptime config: Config,
) !void {
  try harnessMany(pattern, "match", expectations, config, &all_paths, .none, &default_blocks);
}

pub fn testMatchWithConfigDynamicOnly(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: E!?Match,
  comptime config: Config,
) !void {
  try harness(pattern, "match", .{str}, expected, config, &all_paths, .none, &dynamic_only);
}

/// See `testMatchesMany` -- same compile-once semantics for `match` with config,
/// restricted to dynamic-context regexes.
pub fn testMatchManyWithConfigDynamicOnly(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatch,
  comptime config: Config,
) !void {
  try harnessMany(pattern, "match", expectations, config, &all_paths, .none, &dynamic_only);
}

pub fn testMatchStart(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: ?[]const u8,
) !void {
  try harness(pattern, "matchStart", .{str}, expected, .{}, &all_paths, .none, &default_blocks);
}

/// Single string/expected pair for `testMatchStart`-style assertions.
pub const ExpectMatchStart = struct {
  str: []const u8,
  expected: ?[]const u8,

  pub fn extraArgs(self: @This()) struct { []const u8 } {
    return .{ self.str };
  }
};

/// See `testMatchesMany` -- same compile-once semantics for `matchStart`.
pub fn testMatchStartMany(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatchStart,
) !void {
  try harnessMany(pattern, "matchStart", expectations, .{}, &all_paths, .none, &default_blocks);
}

pub fn testMatchExact(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: bool,
) !void {
  try testMatchExactWithConfig(pattern, str, expected, .{});
}

/// Single string/expected pair for `testMatchExact`-style assertions.
pub const ExpectMatchExact = struct {
  str: []const u8,
  expected: bool,

  pub fn extraArgs(self: @This()) struct { []const u8 } {
    return .{ self.str };
  }
};

/// See `testMatchesMany` -- same compile-once semantics for `matchesExact`.
pub fn testMatchExactMany(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatchExact,
) !void {
  try testMatchExactManyWithConfig(pattern, expectations, .{});
}

pub fn testMatchExactWithConfig(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: bool,
  comptime config: Config,
) !void {
  try harness(pattern, "matchesExact", .{str}, expected, config, &all_paths, .none, &default_blocks);
}

/// See `testMatchesMany` -- same compile-once semantics for `matchesExact` with config.
pub fn testMatchExactManyWithConfig(
  comptime pattern: []const u8,
  comptime expectations: []const ExpectMatchExact,
  comptime config: Config,
) !void {
  try harnessMany(pattern, "matchesExact", expectations, config, &all_paths, .none, &default_blocks);
}

pub fn testParseError(comptime pattern: []const u8, comptime expected: E) !void {
  try testParseErrorWithConfig(pattern, expected, .{});
}

pub fn testParseErrorWithConfig(
  comptime pattern: []const u8,
  comptime expected: E,
  comptime config: Config,
) !void {
  try harness(pattern, "match", .{""}, expected, config, &rt_only, .none, &default_blocks);
}

pub fn testParseErrorWithConfigDynamicOnly(
  comptime pattern: []const u8,
  comptime expected: E,
  comptime config: Config,
) !void {
  try harness(pattern, "match", .{""}, expected, config, &rt_only, .none, &dynamic_only);
}

pub fn testFindAllMultiline(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: E![]const Match,
) !void {
  try testFindAll(pattern, str, expected, .{ .semantics = .{ .multiline = true } });
}

pub fn testFindAll(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime expected: E![]const Match,
  comptime config: Config,
) !void {
  const gpa = std.testing.allocator;
  try harness(pattern, "matchIter",       .{str},      expected, config, &rt_match_paths, .none,   &default_blocks);
  try harness(pattern, "findAllAlloc",    .{gpa, str}, expected, config, &rt_only,        .free,   &default_blocks);
  // try harness(pattern, "findAllComptime", .{str},      expected, config, &ct_match_paths, .none,   &default_blocks);
}

pub fn testSearchAndReplaceMultiline(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime replacement: []const u8,
  comptime start_idx: comptime_int,
  comptime max_base: comptime_int,
  comptime expected_first: Replacement,
  comptime expected_all: ManyReplacements,
) !void {
  try testSearchAndReplaceWithConfig(
    pattern, str, replacement, start_idx, max_base,
    expected_first, expected_all,
    .{ .semantics = .{ .multiline = true } },
  );
}

pub fn testSearchAndReplace(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime replacement: []const u8,
  comptime start_idx: comptime_int,
  comptime max_base: comptime_int,
  comptime expected_first: Replacement,
  comptime expected_all: ManyReplacements,
) !void {
  try testSearchAndReplaceWithConfig(
    pattern, str, replacement, start_idx, max_base,
    expected_first, expected_all, .{},
  );
}

pub fn testSearchAndReplaceWithConfig(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime replacement: []const u8,
  comptime start_idx: comptime_int,
  comptime max_base: comptime_int,
  comptime expected_first: Replacement,
  comptime expected_all: ManyReplacements,
  comptime config: Config,
) !void {
  const gpa = std.testing.allocator;
  try harness(pattern, "replaceFirstWithin", .{gpa, str, replacement, start_idx, max_base},
    expected_first, config, &rt_match_paths, .deinit, &default_blocks);
  try harness(pattern, "replaceAllWithin", .{gpa, str, replacement, start_idx, max_base},
    expected_all, config, &rt_match_paths, .deinit, &default_blocks);
}

// ----------------------------
// -------- HARNESS -----------
// ----------------------------

fn harness(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime allowed_paths: []const ExecutionPath,
  comptime destroy: MatchDestruction,
  comptime blocks: []const ExecutionBlock,
) !void {

  // Cartesian product over blocks * regexes * paths * strategies * optimizations
  inline for (blocks) |block| {
    inline for (block.regexes) |Re| {
      inline for (block.paths) |path| {
        if (comptime !pathAllowed(path, allowed_paths)) continue;

        // We do not iterate over strategies if the caller explicitly requested one
        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {

          inline for (block.optimizations) |opts| {
           
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);
           
            const result = runOne(Re, path, pattern, fn_name, extra_args, expected, cfg, destroy);
            if (result) |_| {
            } else |err| {
              // debug.prettyPrint(.{cfg});
              // debug.prettyPrint(.{
              //   .test_fail_with = block.name,
              //   .Regex = Re,
              //   .path = path,
              //   .maybe_strat = maybe_strat,
              //   .opts = opts,
              //   .config = cfg,
              //   .pattern = pattern,
              //   .fn_name = fn_name,
              //   .extra_args = extra_args,
              //   .expected = expected,
              // });
              return err;
            }
          }
        }
      }
    }

    // Strictly-typed Regex path: compile unresolved archs via the free functions.
    inline for (block.archs) |arch| {
      inline for (block.paths) |path| {
        if (comptime !pathAllowed(path, allowed_paths)) continue;

        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {
          inline for (block.optimizations) |opts| {
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);

            const result = runOneArch(arch, path, pattern, fn_name, extra_args, expected, cfg, destroy);
            if (result) |_| {} else |err| return err;
          }
        }
      }
    }
  }

  // Handle compact contexts below
  // These cannot be defined in the regexes list above
  inline for (blocks) |block| {
    if (comptime block.test_compact_fixed) {
      inline for (block.paths) |path| {
        if (comptime !pathAllowed(path, allowed_paths)) continue;
        // We do not iterate over strategies if the caller explicitly requested one
        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {

          inline for (block.optimizations) |opts| {
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);
           
            const compact_arch = Arch{.minimal_nfa = .{ .context = .compact_fixed }};
            const result = runOneArch(compact_arch, path, pattern, fn_name, extra_args, expected, cfg, destroy);
            if (result) |_| {
            } else |err| {
              // debug.prettyPrint(.{
              //   .test_fail_with = block.name,
              //   .arch = compact_arch,
              //   .path = path,
              //   .maybe_strat = maybe_strat,
              //   .opts = opts,
              //   .config = cfg,
              //   .pattern = pattern,
              //   .fn_name = fn_name,
              //   .extra_args = extra_args,
              //   .expected = expected,
              // });
              return err;
            }
          }
        }
      }
    }
  }

 // Handle optimal contexts below
  inline for (blocks) |block| {
    if (comptime block.test_optimal_resolution) {
      if (comptime isExpectingError(expected)) continue;

      inline for (block.paths) |path| {
        if (comptime path == .rt_rt) continue; 
        if (comptime !pathAllowed(path, allowed_paths)) continue;

        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {
          inline for (block.optimizations) |opts| {
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);
            
            const result = runOptimal(path, pattern, fn_name, extra_args, expected, cfg, destroy);
            if (result) |_| {} else |err| return err;
          }
        }
      }
    }
  }

  // Allocation-failure subset
  if (comptime isExpectingError(expected)) return;
  if (comptime !pathAllowed(.rt_rt, allowed_paths)) return;
  if (comptime no_memory_crash_tests) return;
  try oomSubset(pattern, fn_name, extra_args, config, destroy, blocks);
}

/// Compile-once-match-many variant of `harness`.  Compiles the pattern once
/// per cartesian-product cell and iterates every expectation against the
/// resulting machine.  The OOM-injection step is also amortized: a single
/// `checkAllAllocationFailures` invocation runs the compile-once + match-all
/// flow per block, rather than per expectation.
fn harnessMany(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime allowed_paths: []const ExecutionPath,
  comptime destroy: MatchDestruction,
  comptime blocks: []const ExecutionBlock,
) !void {

  // Cartesian product over blocks * regexes * paths * strategies * optimizations
  inline for (blocks) |block| {
    inline for (block.regexes) |Re| {
      inline for (block.paths) |path| {
        if (comptime !pathAllowed(path, allowed_paths)) continue;

        // We do not iterate over strategies if the caller explicitly requested one
        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {

          inline for (block.optimizations) |opts| {
           
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);
           
            const result = runOneMany(Re, path, pattern, fn_name, expectations, cfg, destroy);
            if (result) |_| {
            } else |err| {
              return err;
            }
          }
        }
      }
    }

    // Strictly-typed Regex path: compile unresolved archs via the free functions.
    inline for (block.archs) |arch| {
      inline for (block.paths) |path| {
        if (comptime !pathAllowed(path, allowed_paths)) continue;

        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {
          inline for (block.optimizations) |opts| {
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);

            const result = runOneArchMany(arch, path, pattern, fn_name, expectations, cfg, destroy);
            if (result) |_| {} else |err| return err;
          }
        }
      }
    }
  }

  // Handle compact contexts below
  // These cannot be defined in the regexes list above
  inline for (blocks) |block| {
    if (comptime block.test_compact_fixed) {
      inline for (block.paths) |path| {
        if (comptime !pathAllowed(path, allowed_paths)) continue;
        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {

          inline for (block.optimizations) |opts| {
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);

            const compact_arch = Arch{.minimal_nfa = .{ .context = .compact_fixed }};
            const result = runOneArchMany(compact_arch, path, pattern, fn_name, expectations, cfg, destroy);
            if (result) |_| {
            } else |err| {
              return err;
            }
          }
        }
      }
    }
  }

  inline for (blocks) |block| {
    if (comptime block.test_optimal_resolution) {
      if (comptime hasParseErrorExpectation(expectations)) continue;

      inline for (block.paths) |path| {
        if (path == .rt_rt) continue; 
        if (comptime !pathAllowed(path, allowed_paths)) continue;

        const proper_strats = if (config.strategy) |caller_strat| &.{caller_strat} else block.strategies;

        inline for (proper_strats) |maybe_strat| {
          inline for (block.optimizations) |opts| {
            const cfg = comptime withOpts(config, opts, maybe_strat, block.global);
            
            const result = runOptimalMany(path, pattern, fn_name, expectations, cfg, destroy);
            if (result) |_| {} else |err| return err;
          }
        }
      }
    }
  }

  // Allocation-failure subset.  Unlike `harness`, we cannot bail purely on
  // `isExpectingError(expected)` because individual expectations may differ;
  // we skip OOM on a per-expectation basis inside the closure.
  // However, if EVERY expectation expects an error, OOM injection produces
  // no useful signal -- the compile will fail deterministically and the
  // expectation loop never executes.  Skip the OOM run entirely in that case.
  if (comptime !pathAllowed(.rt_rt, allowed_paths)) return;
  if (comptime no_memory_crash_tests) return;
  if (comptime allExpectationsAreErrors(expectations)) return;
  try oomSubsetMany(pattern, fn_name, expectations, config, destroy, blocks);
}

// ----------------------------
// -------- RUNNERS -----------
// ----------------------------

fn runOne(
  comptime Re: type,
  comptime path: ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  // debug.prettyPrint(.{
  //   .path = path,
  //   .pattern = pattern,
  //   .fn_name = fn_name,
  //   .extra_args = extra_args,
  //   .config = config,
  // });

  switch (path) {
    .rt_rt => try runRtRt(Re, pattern, fn_name, extra_args, expected, config, destroy),
    .ct_rt => try runCtRt(Re, pattern, fn_name, extra_args, expected, config, destroy),
    .ct_ct => try comptime runCtCt(Re, pattern, fn_name, extra_args, expected, config, destroy),
  }
}

/// Compile-once-match-many counterpart of `runOne`.
fn runOneMany(
  comptime Re: type,
  comptime path: ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  switch (path) {
    .rt_rt => try runRtRtMany(Re, pattern, fn_name, expectations, config, destroy),
    .ct_rt => try runCtRtMany(Re, pattern, fn_name, expectations, config, destroy),
    .ct_ct => try comptime runCtCtMany(Re, pattern, fn_name, expectations, config, destroy),
  }
}

// ----------------------------------------------------------------
// Arch runners: strictly-typed Regex compiled via the free funcs.
// regex.compile / regex.compileComptimeNonIntercepting take an
// UNRESOLVED arch and resolve it internally, returning a Regex value.
// After compilation the resulting type drives the same matchRt/call
// helpers as the type-based runners.
// ----------------------------------------------------------------

fn runOneArch(
  comptime arch: Arch,
  comptime path: ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  switch (path) {
    .rt_rt => try runRtRtArch(arch, pattern, fn_name, extra_args, expected, config, destroy),
    .ct_rt => try runCtRtArch(arch, pattern, fn_name, extra_args, expected, config, destroy),
    .ct_ct => try comptime runCtCtArch(arch, pattern, fn_name, extra_args, expected, config, destroy),
  }
}

fn runOneArchMany(
  comptime arch: Arch,
  comptime path: ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  switch (path) {
    .rt_rt => try runRtRtArchMany(arch, pattern, fn_name, expectations, config, destroy),
    .ct_rt => try runCtRtArchMany(arch, pattern, fn_name, expectations, config, destroy),
    .ct_ct => try comptime runCtCtArchMany(arch, pattern, fn_name, expectations, config, destroy),
  }
}

fn runRtRtArch(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  const gpa = std.testing.allocator;
  const duped = try gpa.dupe(u8, pattern); // ensure pattern is truly runtime
  defer gpa.free(duped);

  var re = pzre.regex.compile(arch, config, gpa, duped) catch |err| {
    return try expectDeeplyEqual(expected, err, .{});
  };
  const Re = @TypeOf(re);
  defer re.deinit(gpa);

  try matchRt(Re, re, fn_name, extra_args, expected, destroy);
}

fn runCtRtArch(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  if (comptime pzre.regex.compileComptimeNonIntercepting(arch, config, pattern)) |re| {
    const Re = @TypeOf(re);
    try matchRt(Re, re, fn_name, extra_args, expected, destroy);
  } else |err| {
    try expectDeeplyEqual(expected, err, .{});
  }
}

fn runCtCtArch(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  _ = destroy; // no allocation at comptime

  comptime {
    if (pzre.regex.compileComptimeNonIntercepting(arch, config, pattern)) |re| {
      const Re = @TypeOf(re);
      var ctx = re.initContextFixed();
      const result = call(Re, fn_name, re, &ctx, extra_args);
      expectDeeplyEqual(expected, result, .{}) catch |err| {
        @compileError(std.fmt.comptimePrint(
          "\nct_ct match mismatch:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  got     : {any}\n  err     : {s}\n",
          .{ pattern, fn_name, expected, result, @errorName(err) },
        ));
      };
    } else |err| {
      expectDeeplyEqual(expected, err, .{}) catch {
        @compileError(std.fmt.comptimePrint(
          "\nct_ct unexpected compile error:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  err     : {s}\n",
          .{ pattern, fn_name, expected, @errorName(err) },
        ));
      };
    }
  }
}

fn runRtRtArchMany(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  const gpa = std.testing.allocator;
  const duped = try gpa.dupe(u8, pattern);
  defer gpa.free(duped);

  var re = pzre.regex.compile(arch, config, gpa, duped) catch |err| {
    inline for (expectations) |exp| {
      try expectDeeplyEqual(exp.expected, err, .{});
    }
    return;
  };
  const Re = @TypeOf(re);
  defer re.deinit(gpa);

  inline for (expectations) |exp| {
    try matchRt(Re, re, fn_name, comptime exp.extraArgs(), exp.expected, destroy);
  }
}

fn runCtRtArchMany(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  if (comptime pzre.regex.compileComptimeNonIntercepting(arch, config, pattern)) |re| {
    const Re = @TypeOf(re);
    inline for (expectations) |exp| {
      try matchRt(Re, re, fn_name, comptime exp.extraArgs(), exp.expected, destroy);
    }
  } else |err| {
    inline for (expectations) |exp| {
      try expectDeeplyEqual(exp.expected, err, .{});
    }
  }
}

fn runCtCtArchMany(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  _ = destroy;

  comptime {
    if (pzre.regex.compileComptimeNonIntercepting(arch, config, pattern)) |re| {
      const Re = @TypeOf(re);
      for (expectations) |exp| {
        var ctx = re.initContextFixed();
        const args = exp.extraArgs();
        const result = call(Re, fn_name, re, &ctx, args);
        expectDeeplyEqual(exp.expected, result, .{}) catch |err| {
          @compileError(std.fmt.comptimePrint(
            "\nct_ct match mismatch:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  got     : {any}\n  err     : {s}\n",
            .{ pattern, fn_name, exp.expected, result, @errorName(err) },
          ));
        };
      }
    } else |err| {
      for (expectations) |exp| {
        expectDeeplyEqual(exp.expected, err, .{}) catch {
          @compileError(std.fmt.comptimePrint(
            "\nct_ct unexpected compile error:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  err     : {s}\n",
            .{ pattern, fn_name, exp.expected, @errorName(err) },
          ));
        };
      }
    }
  }
}

fn runRtRt(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  const gpa = std.testing.allocator;
  const duped = try gpa.dupe(u8, pattern); // ensure pattern is truly runtime
  defer gpa.free(duped);

  var re = Re.compile(config, gpa, duped) catch |err| {
    return try expectDeeplyEqual(expected, err, .{});
  };
  defer re.deinit(gpa);

  try matchRt(Re, re, fn_name, extra_args, expected, destroy);
}

fn matchRt(
  comptime Re: type,
  re: Re,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime destroy: MatchDestruction,
) !void {
  const gpa = std.testing.allocator;
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);

  const result = call(Re, fn_name, re, &ctx, extra_args);
  if (comptime meta.isErrorUnion(@TypeOf(result))) {
    if (result) |some| {
      defer MatchDestruction.handle(destroy, some, gpa);

      // debug.inspect(&.{
      //   .call = "matchRt",
      //   .re = re,
      //   .some = some,
      // });

      try expectDeeplyEqual(expected, some, .{});
    } else |_| {
      try expectDeeplyEqual(expected, result, .{});
    }
  } else {
    defer MatchDestruction.handle(destroy, result, gpa);
    try expectDeeplyEqual(expected, result, .{});
  }
}

fn runCtRt(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  if (comptime Re.compileComptimeNonIntercepting(config, pattern)) |re| {
    try matchRt(Re, re, fn_name, extra_args, expected, destroy);
  } else |err| {
    try expectDeeplyEqual(expected, err, .{});
  }
}

fn runCtCt(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  _ = destroy; // no allocation at comptime

  comptime {
    if (Re.compileComptimeNonIntercepting(config, pattern)) |re| {
      var ctx = re.initContextFixed();
      const result = call(Re, fn_name, re, &ctx, extra_args);
      expectDeeplyEqual(expected, result, .{}) catch |err| {
        @compileError(std.fmt.comptimePrint(
          "\nct_ct match mismatch:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  got     : {any}\n  err     : {s}\n",
          .{ pattern, fn_name, expected, result, @errorName(err) },
        ));
      };
    } else |err| {
      expectDeeplyEqual(expected, err, .{}) catch {
        @compileError(std.fmt.comptimePrint(
          "\nct_ct unexpected compile error:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  err     : {s}\n",
          .{ pattern, fn_name, expected, @errorName(err) },
        ));
      };
    }
  }
}

/// Compile-once-match-many counterpart of `runRtRt`.
fn runRtRtMany(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  const gpa = std.testing.allocator;
  const duped = try gpa.dupe(u8, pattern);
  defer gpa.free(duped);

  var re = Re.compile(config, gpa, duped) catch |err| {
    // All expectations share this compile.  If any expectation expected this
    // exact error, the test silently passes for that expectation; otherwise
    // the first mismatch propagates.  We check each expectation against the
    // compile error.
    inline for (expectations) |exp| {
      try expectDeeplyEqual(exp.expected, err, .{});
    }
    return;
  };
  defer re.deinit(gpa);

  inline for (expectations) |exp| {
    try matchRt(Re, re, fn_name, comptime exp.extraArgs(), exp.expected, destroy);
  }
}

/// Compile-once-match-many counterpart of `runCtRt`.
fn runCtRtMany(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  if (comptime Re.compileComptimeNonIntercepting(config, pattern)) |re| {
    inline for (expectations) |exp| {
      try matchRt(Re, re, fn_name, comptime exp.extraArgs(), exp.expected, destroy);
    }
  } else |err| {
    inline for (expectations) |exp| {
      try expectDeeplyEqual(exp.expected, err, .{});
    }
  }
}

/// Compile-once-match-many counterpart of `runCtCt`.
fn runCtCtMany(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  _ = destroy;

  comptime {
    if (Re.compileComptimeNonIntercepting(config, pattern)) |re| {
      for (expectations) |exp| {
        var ctx = re.initContextFixed();
        const args = exp.extraArgs();
        const result = call(Re, fn_name, re, &ctx, args);
        expectDeeplyEqual(exp.expected, result, .{}) catch |err| {
          @compileError(std.fmt.comptimePrint(
            "\nct_ct match mismatch:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  got     : {any}\n  err     : {s}\n",
            .{ pattern, fn_name, exp.expected, result, @errorName(err) },
          ));
        };
      }
    } else |err| {
      for (expectations) |exp| {
        expectDeeplyEqual(exp.expected, err, .{}) catch {
          @compileError(std.fmt.comptimePrint(
            "\nct_ct unexpected compile error:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  err     : {s}\n",
            .{ pattern, fn_name, exp.expected, @errorName(err) },
          ));
        };
      }
    }
  }
}

fn oomSubset(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
  comptime blocks: []const ExecutionBlock,
) !void {
  inline for (blocks) |block| {
    if (block.strategies.len == 0 or block.optimizations.len == 0) continue;

    const strat = block.strategies[0];
    const opts  = block.optimizations[0];
    const cfg   = comptime withOpts(config, opts, strat, block.global);

    if (block.regexes.len > 0) {
      const Re = block.regexes[0];
      const Closure = struct {
        fn run(alloc: Allocator) !void {
          try runRtRtOom(Re, pattern, fn_name, extra_args, cfg, destroy, alloc);
        }
      };
      try std.testing.checkAllAllocationFailures(std.testing.allocator, Closure.run, .{});
    }

    if (block.archs.len > 0) {
      const arch = block.archs[0];
      const ArchClosure = struct {
        fn run(alloc: Allocator) !void {
          try runRtRtOomArch(arch, pattern, fn_name, extra_args, cfg, destroy, alloc);
        }
      };
      try std.testing.checkAllAllocationFailures(std.testing.allocator, ArchClosure.run, .{});
    }
  }
}

fn runRtRtOom(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
  alloc: Allocator,
) !void {
  const duped = alloc.dupe(u8, pattern) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer alloc.free(duped);

  var re = Re.compile(config, alloc, duped) catch |err| {
    if (err == error.OutOfMemory) return err;
    return; // deterministic compile errors are fine in this test
  };
  defer re.deinit(alloc);

  var ctx = re.initContext(alloc) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer ctx.deinit(alloc);

  const result = call(Re, fn_name, re, &ctx, extra_args);

  if (comptime meta.isErrorUnion(@TypeOf(result))) {
    if (result) |v| {
      MatchDestruction.handle(destroy, v, alloc);
    } else |err| {
      if (err == error.OutOfMemory) return err;
    }
  } else {
    MatchDestruction.handle(destroy, result, alloc);
  }
}

/// Arch/free-function counterpart of `runRtRtOom`.
fn runRtRtOomArch(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
  alloc: Allocator,
) !void {
  const duped = alloc.dupe(u8, pattern) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer alloc.free(duped);

  var re = pzre.regex.compile(arch, config, alloc, duped) catch |err| {
    if (err == error.OutOfMemory) return err;
    return; // deterministic compile errors are fine in this test
  };
 
  const Re = @TypeOf(re);
  defer re.deinit(alloc);

  var ctx = re.initContext(alloc) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer ctx.deinit(alloc);

  const result = call(Re, fn_name, re, &ctx, extra_args);

  if (comptime meta.isErrorUnion(@TypeOf(result))) {
    if (result) |v| {
      MatchDestruction.handle(destroy, v, alloc);
    } else |err| {
      if (err == error.OutOfMemory) return err;
    }
  } else {
    MatchDestruction.handle(destroy, result, alloc);
  }
}
/// `checkAllAllocationFailures` invocation across all expectations: one call
/// per block, the closure performs compile-once + match-all.
fn oomSubsetMany(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
  comptime blocks: []const ExecutionBlock,
) !void {
  inline for (blocks) |block| {
    if (block.strategies.len == 0 or block.optimizations.len == 0) continue;

    const strat = block.strategies[0];
    const opts  = block.optimizations[0];
    const cfg   = comptime withOpts(config, opts, strat, block.global);

    if (block.regexes.len > 0) {
      const Re = block.regexes[0];
      const Closure = struct {
        fn run(alloc: Allocator) !void {
          try runRtRtOomMany(Re, pattern, fn_name, expectations, cfg, destroy, alloc);
        }
      };
      try std.testing.checkAllAllocationFailures(std.testing.allocator, Closure.run, .{});
    }

    if (block.archs.len > 0) {
      const arch = block.archs[0];
      const ArchClosure = struct {
        fn run(alloc: Allocator) !void {
          try runRtRtOomArchMany(arch, pattern, fn_name, expectations, cfg, destroy, alloc);
        }
      };
      try std.testing.checkAllAllocationFailures(std.testing.allocator, ArchClosure.run, .{});
    }
  }
}

/// Compile-once-match-many counterpart of `runRtRtOom`.  Performs a single
/// compile then iterates every expectation against the resulting machine.
fn runRtRtOomMany(
  comptime Re: type,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
  alloc: Allocator,
) !void {
  const duped = alloc.dupe(u8, pattern) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer alloc.free(duped);

  var re = Re.compile(config, alloc, duped) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer re.deinit(alloc);

  inline for (expectations) |exp| {
    var ctx = re.initContext(alloc) catch |err| {
      if (err == error.OutOfMemory) return err;
      return;
    };
    defer ctx.deinit(alloc);

    const args = comptime exp.extraArgs();
    const result = call(Re, fn_name, re, &ctx, args);

    if (comptime meta.isErrorUnion(@TypeOf(result))) {
      if (result) |v| {
        MatchDestruction.handle(destroy, v, alloc);
      } else |err| {
        if (err == error.OutOfMemory) return err;
      }
    } else {
      MatchDestruction.handle(destroy, result, alloc);
    }
  }
}

/// Arch/free-function counterpart of `runRtRtOomMany`.
fn runRtRtOomArchMany(
  comptime arch: Arch,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
  alloc: Allocator,
) !void {
  const duped = alloc.dupe(u8, pattern) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  defer alloc.free(duped);

  var re = pzre.regex.compile(arch, config, alloc, duped) catch |err| {
    if (err == error.OutOfMemory) return err;
    return;
  };
  const Re = @TypeOf(re);
  defer re.deinit(alloc);

  inline for (expectations) |exp| {
    var ctx = re.initContext(alloc) catch |err| {
      if (err == error.OutOfMemory) return err;
      return;
    };
    defer ctx.deinit(alloc);

    const args = comptime exp.extraArgs();
    const result = call(Re, fn_name, re, &ctx, args);

    if (comptime meta.isErrorUnion(@TypeOf(result))) {
      if (result) |v| {
        MatchDestruction.handle(destroy, v, alloc);
      } else |err| {
        if (err == error.OutOfMemory) return err;
      }
    } else {
      MatchDestruction.handle(destroy, result, alloc);
    }
  }
}

/// Invokes a named method on a Regex value via @call, coercing the argument
/// tuple to the function's declared parameter types.
inline fn call(
  comptime Re: type,
  comptime fn_name: []const u8,
  re: anytype,
  ctx: anytype,
  comptime extra_args: anytype,
) ReturnType(Re, fn_name) {
  const Fn = @field(Re, fn_name);
  const Args = meta.FunctionArgumentsTuple(@TypeOf(Fn));
  const args = meta.concatTupleCoerced(Args, .{ re, ctx }, extra_args);
  return @call(.auto, Fn, args);
}

fn ReturnType(comptime Re: type, comptime fn_name: []const u8) type {
  const Fn = @field(Re, fn_name);
  return @typeInfo(@TypeOf(Fn)).@"fn".return_type.?;
}

fn withOpts(
  comptime config: Config,
  comptime opts: std.EnumSet(Optimization),
  comptime maybe_strat: ?StrategyName,
  comptime global: ?Global,
) Config {
  comptime {
    var c = config;

    c.ast_optimizations = opts;
    if (global) |g| c.global = g;
    if (maybe_strat) |s| c.strategy = s;

    if (disable_optimizations) c.ast_optimizations = .initEmpty();
    return c;
  }
}

fn pathAllowed(comptime path: ExecutionPath, comptime allowed: []const ExecutionPath) bool {
  comptime {
    for (allowed) |a| if (a == path) return true;
    return false;
  }
}

fn isExpectingError(comptime expected: anytype) bool {
  comptime {
    const T = @TypeOf(expected);
    if (meta.isErrorSet(T)) return true;
    if (meta.isErrorUnion(T)) {
      if (expected) |_| return false else |_| return true;
    }
    return false;
  }
}

fn hasParseErrorExpectation(comptime expectations: anytype) bool {
  comptime {
    for (expectations) |exp| {
      if (isExpectingError(exp.expected)) return true;
    }
    return false;
  }
}

fn allExpectationsAreErrors(comptime expectations: anytype) bool {
  comptime {
    if (expectations.len == 0) return false;
    for (expectations) |exp| {
      if (!isExpectingError(exp.expected)) return false;
    }
    return true;
  }
}

// OPTIMAL RUN VARIANTS

fn runOptimal(
  comptime path: ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  switch (path) {
    .rt_rt => unreachable,
    .ct_rt => try runOptimalCtRt(pattern, fn_name, extra_args, expected, config, destroy),
    .ct_ct => try comptime runOptimalCtCt(pattern, fn_name, extra_args, expected, config, destroy),
  }
}

fn runOptimalCtRt(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  
  const re = comptime pzre.regex.compileOptimal(config, pattern, .{ .dynamic = .u16 });
  const Re = @TypeOf(re);
  try matchRt(Re, re, fn_name, extra_args, expected, destroy);
}

fn runOptimalCtCt(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime extra_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  _ = destroy;

  comptime {
    const re = pzre.regex.compileOptimal(config, pattern, .compact_fixed);
    const Re = @TypeOf(re);
    var ctx = re.initContextFixed();
    const result = call(Re, fn_name, re, &ctx, extra_args);
    expectDeeplyEqual(expected, result, .{}) catch |err| {
      @compileError(std.fmt.comptimePrint(
        "\nct_ct optimal match mismatch:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  got     : {any}\n  err     : {s}\n",
        .{ pattern, fn_name, expected, result, @errorName(err) },
      ));
    };
  }
}

// ---------------------------------------------------------
// Many Variants
// ---------------------------------------------------------

fn runOptimalMany(
  comptime path: ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  switch (path) {
    .rt_rt => unreachable,
    .ct_rt => try runOptimalCtRtMany(pattern, fn_name, expectations, config, destroy),
    .ct_ct => try comptime runOptimalCtCtMany(pattern, fn_name, expectations, config, destroy),
  }
}

fn runOptimalCtRtMany(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  
  const re = comptime pzre.regex.compileOptimal(config, pattern, .{ .dynamic = .u16 });
  const Re = @TypeOf(re);
  inline for (expectations) |exp| {
    try matchRt(Re, re, fn_name, comptime exp.extraArgs(), exp.expected, destroy);
  }
}

fn runOptimalCtCtMany(
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime expectations: anytype,
  comptime config: Config,
  comptime destroy: MatchDestruction,
) !void {
  @setEvalBranchQuota(1_000_000);
  _ = destroy;

  comptime {
    const re = pzre.regex.compileOptimal(config, pattern, .compact_fixed);
    const Re = @TypeOf(re);
    for (expectations) |exp| {
      var ctx = re.initContextFixed();
      const args = exp.extraArgs();
      const result = call(Re, fn_name, re, &ctx, args);
      expectDeeplyEqual(exp.expected, result, .{}) catch |err| {
        @compileError(std.fmt.comptimePrint(
          "\nct_ct optimal match mismatch:\n  pattern : {s}\n  fn      : {s}\n  expected: {any}\n  got     : {any}\n  err     : {s}\n",
          .{ pattern, fn_name, exp.expected, result, @errorName(err) },
        ));
      };
    }
  }
}
