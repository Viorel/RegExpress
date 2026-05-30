const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;
const meta = pzre.meta;
const eql = pzre.lens.mem.deeply_equal.deeplyEqual;

const pzre = @import("../root.zig");
const misc = pzre.misc;

const lens = pzre.lens;
const debug = lens.debug;

const compile = pzre.compile;
const Match = pzre.nfa.Match;
const E = pzre.parse.ParseError;

const context = pzre.nfa.context;

const state = pzre.nfa.state;
const parse = pzre.parse;

const Ast = pzre.ast.Ast;

const Nfa = pzre.nfa.Nfa;
const Replacement = pzre.nfa.Replacement;
const ManyReplacements = pzre.nfa.ManyReplacements;
const Config = pzre.compile.Config;

const TEST_ARBITRARY_MEMORY_ERROR = false;
const TEST_COMPTIME_RUNTIME = false;
const TEST_COMPTIME_COMPTIME = false;
const TEST_RUNTIME = true;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("simple.zig");
    _ = @import("assertions.zig");
    _ = @import("config.zig");
    _ = @import("encoding.zig");
    _ = @import("epsilon.zig");
    _ = @import("error.zig");
    _ = @import("iterate.zig");
    _ = @import("memory_leak.zig");
    _ = @import("multithreaded.zig");
    _ = @import("precedence.zig");
    // _ = @import("profiling.zig");
    _ = @import("search_and_replace.zig");
    _ = @import("semantics.zig");
    _ = @import("topology.zig");
    _ = @import("sets.zig");
    _ = @import("stress.zig");
  }
}

pub fn testAnyError(comptime pattern: []const u8, config: Config) !void {
  const gpa = std.testing.allocator;
  const duped_pattern = try gpa.dupe(u8, pattern);
  defer gpa.free(duped_pattern);

  if (config.optimize) {
    if (compile.nfa(config, gpa, duped_pattern)) |*nfa| {
      @constCast(nfa).deinit(gpa);
      std.debug.print("\nOptimized pipeline unexpectedly succeeded\n", .{});
      return error.ExpectedErrorGotSuccess;
    } else |_| {}
  } else {
    if (compile.nfaUnoptimized(config, gpa, pattern)) |*nfa| {
      @constCast(nfa).deinit(gpa);
      std.debug.print("\nUnoptimized pipeline unexpectedly succeeded\n", .{});
      return error.ExpectedErrorGotSuccess;
    } else |_| {}
  }
}

// State being an untagged union, we need to provide its comparison semantics
pub fn expectDeeplyEqual(comptime State: type, expected: anytype, actual: anytype) !void {
  const Semantics = pzre.lens.mem.deeply_equal.Semantics;

  const s = struct {
    pub fn eqlOverride(lhs_opaque: *anyopaque, rhs_opaque: *anyopaque) bool {
      if (State == void) {
        return false;
      } else {
        const lhs: *const State = @ptrCast(@alignCast(lhs_opaque));
        const rhs: *const State = @ptrCast(@alignCast(rhs_opaque));
        return State.eql(lhs.*, rhs.*);
      }
    }
  };

  const semantics = Semantics{
    .user_impls = &.{
      .{State, &s.eqlOverride},
    }
  };

  try pzre.lens.testing.expectDeeplyEqualWithSemantics(expected, actual, semantics);
}

pub fn testSearchAndReplaceMultiline(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime replacement: []const u8,
  comptime start_idx: comptime_int,
  comptime max_base: comptime_int,
  comptime expected_first: Replacement,
  comptime expected: ManyReplacements,
) !void {
  try testSearchAndReplaceWithConfig(pattern, str, replacement, start_idx, max_base, expected_first, expected, .{.semantics = .{.multiline = true}});
}

pub fn testSearchAndReplace(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime replacement: []const u8,
  comptime start_idx: comptime_int,
  comptime max_base: comptime_int,
  comptime expected_first: Replacement,
  comptime expected: ManyReplacements,
) !void {
  try testSearchAndReplaceWithConfig(pattern, str, replacement, start_idx, max_base, expected_first, expected, .{});
}

pub fn testSearchAndReplaceWithConfig(
  comptime pattern: []const u8,
  comptime str: []const u8,
  comptime replacement: []const u8,
  comptime start_idx: comptime_int,
  comptime max_base: comptime_int,
  comptime expected_first: Replacement,
  comptime expected: ManyReplacements,
  comptime config: Config,
) !void {
  const gpa = std.testing.allocator;

  try patternTestHarness(pattern, "replaceFirstWithin", .{gpa, str, replacement, start_idx, max_base}, expected_first, config, .both, .runtime, .deinit, true);

  try patternTestHarness(pattern, "replaceAllWithin", .{gpa, str, replacement, start_idx, max_base}, expected, config, .both, .runtime, .deinit, true);
}

pub fn testMatches(comptime pattern: []const u8, comptime str: []const u8, comptime matches: bool) !void {
  try patternTestHarness(pattern, "matches", .{str}, matches, .{}, .both, .both, .none, true);
}

pub fn testFind(comptime pattern: []const u8, comptime str: []const u8, comptime start: usize, comptime match: E!?Match) !void {
  try patternTestHarness(pattern, "find", .{str, start, str.len}, match, .{}, .both, .both, .none, true);
}

pub fn testMatchStart(comptime pattern: []const u8, comptime str: []const u8, comptime expected: ?[]const u8) !void {
  try patternTestHarness(pattern, "matchStart", .{str}, expected, .{}, .both, .both, .none, true);
}

pub fn testMatchExact(comptime pattern: []const u8, comptime str: []const u8, comptime matches: bool) !void {
  try testMatchExactWithConfig(pattern, str, matches, .{});
}

pub fn testMatchExactWithConfig(comptime pattern: []const u8, comptime str: []const u8, comptime matches: bool, comptime config: Config) !void {
  try patternTestHarness(pattern, "matchesExact", .{str}, matches, config, .both, .both, .none, true);
}

pub fn testParseError(comptime pattern: []const u8, comptime expected: E) !void {
  try testParseErrorWithConfig(pattern, expected, .{});
} 

pub fn testParseErrorWithConfig(comptime pattern: []const u8, comptime expected: E, comptime config: Config) !void {
  try patternTestHarness(pattern, "match", .{""}, expected, config, .runtime, .runtime, .none, true);
} 

pub fn testMatch(comptime pattern: []const u8, comptime str: []const u8, comptime expected: E!?Match) !void {
  return testMatchWithConfig(pattern, str, expected, .{});
} 

pub fn testMatchWithConfig(comptime pattern: []const u8, comptime str: []const u8, comptime expected: E!?Match, comptime config: Config) !void {
  try patternTestHarness(pattern, "match", .{str}, expected, config, .both, .both, .none, true);
} 

pub fn testFindAllMultiline(comptime pattern: []const u8, comptime str: []const u8, comptime expected: E![]const Match) !void {
  return testFindAll(pattern, str, expected, .{ .semantics = .{.multiline = true} });
}

pub fn testFindAll(comptime pattern: []const u8, comptime str: []const u8, comptime expected: E![]const Match, comptime config: Config) !void {
  const gpa = std.testing.allocator;

  try patternTestHarness(pattern, "matchIter", .{str}, expected, config, .both, .runtime, .none, true);
  try patternTestHarness(pattern, "findAllAlloc", .{gpa, str}, expected, config, .runtime, .runtime, .free, true);
  try patternTestHarness(pattern, "findAllComptime", .{str}, expected, config, .compiletime, .compiletime, .none, false);
}

/// When to perform the operation
const TestMode = enum { compiletime, runtime, both };

/// Match resource destruction
const MatchDestruction = enum { deinit, free, none,

  pub fn handle(comptime mode: MatchDestruction, m: anytype) void {
    const gpa = std.testing.allocator;
    const um = if (comptime meta.isOptional(@TypeOf(m))) m.? else m;

    switch (comptime mode) {
      .free => gpa.free(um),
      .deinit => um.deinit(gpa),
      .none => {},
    }
  }
};

/// Performs a rigorous test on the pattern by testing all memory models and compilation patterns
/// 
/// Tests all memory failure points
/// 
/// A single context is produced for each pattern
///
/// TODO: add size calculations to memInspect: return a breakdown of heap/stack sizes
/// 
fn patternTestHarness(
  comptime pattern: []const u8,
  comptime match_fn_name: []const u8,
  comptime additional_match_args: anytype,
  comptime expected: anytype,
  comptime config: Config,
  comptime compile_mode: TestMode,
  comptime match_mode: TestMode,
  /// Whether to destroy the output of a match using testing.gpa
  comptime destroy_result: MatchDestruction,
  comptime matching_requires_context: bool
) !void {

  const E1 = Allocator.Error.OutOfMemory;
  const E2 = parse.ParseError.AllocationUpperbound;

  const match = struct {
    fn f(comptime NfaType: type, gpa: Allocator, nfa: NfaType) !bool {
      comptime meta.propertyAssertNeg("Pointer", meta.isOnePointer, NfaType);
      const matchFn = @field(NfaType, match_fn_name);

      var ctx = try nfa.initContext(gpa);
      defer if (!@inComptime()) ctx.deinit(gpa);

      const MatchArgs = meta.FunctionArgumentsTuple(@TypeOf(matchFn));
      const match_args = if (comptime matching_requires_context) 
        meta.concatTupleCoerced(MatchArgs, .{nfa, &ctx}, additional_match_args)
        else meta.concatTupleCoerced(MatchArgs, .{nfa}, additional_match_args);

      const m_result = @call(.auto, matchFn, match_args);

      const State = NfaType.State;

      if (comptime meta.isErrorUnion(@TypeOf(m_result))) {
        if (m_result) |m| {
          defer if (comptime destroy_result != .none) destroy_result.handle(m);
          try expectDeeplyEqual(State, expected, m);
        } else |err| {
          assert(err == Allocator.Error.OutOfMemory);
          return true;
        }
      } else {
        try expectDeeplyEqual(State, expected, m_result);
      }
      return false;
    }
  };

  const generate = struct {
    pub fn runtime(gpa: Allocator, is_oom_test: bool) !void {
      const duped_pattern = try gpa.dupe(u8, pattern);
      defer gpa.free(duped_pattern);

      var mem_error: bool = false;

      if (config.optimize) {
        var optimized_nfa = compile.nfa(config, gpa, duped_pattern);
        
        if (optimized_nfa) |*nfa| {
          defer nfa.deinit(gpa);
          mem_error = try match.f(@TypeOf(nfa.*), gpa, nfa.*);
        } else |err| {
          if ((err == E1 or err == E2) and is_oom_test) {
            mem_error = true;
          } else if (!is_oom_test) {
            try expectDeeplyEqual(void, expected, err);
          }
        }
      } else {
        var unoptimized_nfa = compile.nfaUnoptimized(config, gpa, pattern);

        if (unoptimized_nfa) |*nfa| {
          defer nfa.deinit(gpa);
          mem_error = try match.f(@TypeOf(nfa.*), gpa, nfa.*);
        } else |err| {
          if ((err == E1 or err == E2) and is_oom_test) {
            mem_error = true;
          } else if (!is_oom_test) {
            try expectDeeplyEqual(void, expected, err);
          }
        }
      }

      if (mem_error) return Allocator.Error.OutOfMemory;
    }

    pub fn compiletime(gpa: Allocator) !void {
      @setEvalBranchQuota(1000000);
      const with_nfa_config = struct {
        fn f(conf: compile.Config, _gpa: Allocator) !void {
          if (conf.optimize) {
            const optimized_nfa = comptime compile.nfaComptime(conf, pattern);
            _ = try match.f(@TypeOf(optimized_nfa), _gpa, optimized_nfa);
          } else {
            const literal_nfa = comptime compile.nfaUnoptimizedComptime(conf, pattern);
            _ = try match.f(@TypeOf(literal_nfa), _gpa, literal_nfa);
          }
        }
      };

      comptime var nfa_config_dyn = config;
      nfa_config_dyn.context = .dynamic;

      comptime var nfa_config_fixed = config;
      nfa_config_fixed.context = .compact_fixed;

      try with_nfa_config.f(nfa_config_fixed, gpa);
      if (!@inComptime()) try with_nfa_config.f(nfa_config_dyn, gpa);
    }
  };

  const gpa = std.testing.allocator;

  // runtime generation // runtime matching  
  if (TEST_RUNTIME) {
    if (comptime compile_mode == .runtime or compile_mode == .both) {
      try generate.runtime(gpa, false);
      if (TEST_ARBITRARY_MEMORY_ERROR) {
        // dont run this when expecting memory errors
        if (!try eql(gpa, E1, expected, .{}) and !try eql(gpa, E2, expected, .{})) {
          try std.testing.checkAllAllocationFailures(gpa, generate.runtime, .{true});
        }
      }
    }
  }

  // Errors cause comptime errors at comptime
  if (comptime meta.isErrorSet(@TypeOf(expected))) return;
  if (comptime meta.UnwrapErrorE(@TypeOf(expected))) |_| return;

  // comptime generation // runtime matching
  if (TEST_COMPTIME_RUNTIME) {
    if (comptime compile_mode == .compiletime or compile_mode == .both) {
      if (comptime match_mode == .runtime or match_mode == .both) try generate.compiletime(gpa);
    }
  }

  // comptime generation // comptime matching
  if (TEST_COMPTIME_COMPTIME) {
    if (comptime compile_mode == .compiletime or compile_mode == .both) {
      if (comptime match_mode == .compiletime or match_mode == .both) try comptime generate.compiletime(undefined);
    }
  }
}
