const std = @import("std");
const compile = pzre.compile;
const misc = pzre.misc;
const t = @import("test.zig");

const expectDeeplyEqual = t.expectDeeplyEqual;
const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testMatch = t.testMatch;
const testMatchWithConfig = t.testMatchWithConfig;
const testMatchStart = t.testMatchStart;
const testMatches = t.testMatches;
const testParseError = t.testParseError;
const testParseErrorWithConfig = t.testParseErrorWithConfig;
const testAnyError = t.testAnyError;

const Config = pzre.compile.Config;

const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;

test "pzre default config bounds prevent crashes" {
  const config: Config = .{};

  // State calculations intercepting massive logical structures before allocation
  try testAnyError("a" ** 40000, config);
  try testAnyError("a{4000}" ** 40000, .{});

  // AST depth tracking catching stack overflow attempts before they happen
  try testAnyError("(" ** 500 ++ "a" ++ ")" ** 500, config);
  try testAnyError("a|" ** 500 ++ "a", config);
}

test "pzre configuration limits are strictly enforced" {
  // 1. max_depth limits (AST Structure)
  try testParseErrorWithConfig("(a)", error.TooDeep, .{ .limits = .{ .max_depth = 0 } });
  try testParseErrorWithConfig("(((a)))", error.TooDeep, .{ .limits = .{ .max_depth = 2 } });
  try testMatchWithConfig("(((a)))", "a", Match{ .str = "a", .loc = .init(0, 1) }, .{ .limits = .{ .max_depth = 5 } });

  // 2. max_states limits (Logical NFA Footprint)
  try testParseErrorWithConfig("a{15}", error.TooManyStates, .{ .limits = .{ .max_submachine_states = 10 } });
  try testMatchWithConfig("a{15}", "a" ** 15, Match{ .str = "a" ** 15, .loc = .init(0, 15) }, .{ .limits = .{ .max_submachine_states = 20 }});

  try testParseErrorWithConfig("a{15}", error.TooManyStates, .{
    .limits = .{ .max_submachine_states = 15 }, // off by one
  });
  try testMatchWithConfig("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_submachine_states = 16 }, // exact but fails because of bidirectional solver prefix
  });
  try testMatchWithConfig("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_submachine_states = 17 }, // off by one due to prefix
  });
  try testMatchWithConfig("a{15}", "a" ** 15, Match{ .str = "a" ** 15, .loc = .init(0, 15) }, .{
    .limits = .{ .max_submachine_states = 18 }, // exact accounting for prefix
  });

  try testParseErrorWithConfig("^a{15}", error.TooManyStates, .{
    .limits = .{ .max_submachine_states = 15 }, // off by one
  });
  try testMatchWithConfig("^a{15}", "a" ** 15, Match{ .str = "a" ** 15, .loc = .init(0, 15) }, .{
    .limits = .{ .max_submachine_states = 16 }, // exact due to left-anchored problem
  });

  // 3. upper_bound limits (Physical Allocator Footprint)
  try testParseErrorWithConfig("a" ** 200, error.AllocationUpperbound, .{
    .limits = .{ .max_submachine_states = 1 << 16, .gpa_upper_bound = 64 },
  });
  try testMatchWithConfig("a" ** 50, "a" ** 50, Match{ .str = "a" ** 200, .loc = .init(0, 200) }, .{
    .limits = .{ .max_submachine_states = 1 << 16, .gpa_upper_bound = 1 << 12, .max_depth = 1 << 20 },
  });
}

test "pzre max arbitrary repetition" {
  const config: Config = .{
    .limits = .{ .max_submachine_states = 15, .gpa_upper_bound = 1 << 20, .max_arbitrary_repetition = 32 },
  };

  try testParseErrorWithConfig("a{33}", error.TooHighArbitraryRepeat, config);
  try testParseErrorWithConfig("a{1,33}", error.TooHighArbitraryRepeat, config);
  try testParseErrorWithConfig("a{33,34}", error.TooHighArbitraryRepeat, config);

  try testMatchWithConfig("a{32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
  try testMatchWithConfig("a{32,32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
  try testMatchWithConfig("a{3,32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
}
