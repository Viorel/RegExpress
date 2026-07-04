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
const testMatchWithConfigDynamicOnly = t.testMatchWithConfigDynamicOnly;
const testMatchStart = t.testMatchStart;
const testMatches = t.testMatches;
const testParseError = t.testParseError;
const testParseErrorWithConfig= t.testParseErrorWithConfig;
const testParseErrorWithConfigDynamicOnly = t.testParseErrorWithConfigDynamicOnly;
const testAnyError = t.testAnyError;
const calculateFinalStatesCount = pzre.nfa.search.Formulation.calculateFinalStatesCount;

const Config = pzre.compile.Config;
const pzre = @import("../root.zig");
const Match = pzre.regex.Match;

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
  try testParseErrorWithConfigDynamicOnly("(a)", error.TooDeep, .{ .limits = .{ .max_depth = 0 } });
  try testParseErrorWithConfigDynamicOnly("(((a)))", error.TooDeep, .{ .limits = .{ .max_depth = 2 } });
  try testMatchWithConfigDynamicOnly("(((a)))", "a", Match{ .str = "a", .loc = .init(0, 1) }, .{ .limits = .{ .max_depth = 5 } });

  // 2. max_states limits (Logical NFA Footprint)
  try testParseErrorWithConfigDynamicOnly("a{15}", error.TooManyStates, .{
    .limits = .{ .max_states = 10 },
  });

  try testParseErrorWithConfigDynamicOnly("a{15}", error.TooManyStates, .{
    .limits = .{ .max_states = 14 }, // of by one
    .strategy = .start_set_pass,
  });

  try testMatchWithConfigDynamicOnly("a{15}", "a" ** 15, Match{ .str = "a" ** 15, .loc = .init(0, 15) }, .{
    .limits = .{ .max_states = 15 }, // Exact now
    .strategy = .start_set_pass, // only using the start set pass
  });

  try testMatchWithConfigDynamicOnly("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_states = 15 },
    .strategy = .bi_directional_pass, // fails due to prefix 
  });

  try testMatchWithConfigDynamicOnly("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_states = 17 },
    .strategy = .bi_directional_pass, // still fails
  });

  const proper_len = 15 * 2 + 2;
  try testMatchWithConfigDynamicOnly("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_states = proper_len - 1}, // of by one
    .strategy = .bi_directional_pass,
  });

  try testMatchWithConfigDynamicOnly("a{15}", "a" ** 15, Match{ .str = "a" ** 15, .loc = .init(0, 15) }, .{
    .limits = .{ .max_states = proper_len },
    .strategy = .bi_directional_pass,
  });

  // 4. upper_bound limits (Physical Allocator Footprint)
  try testParseErrorWithConfigDynamicOnly("a" ** 200, error.AllocationUpperbound, .{
    .limits = .{ .gpa_upper_bound = 64 },
  });
}

test "pzre max arbitrary repetition" {
  const config: Config = .{
    .limits = .{ .gpa_upper_bound = 1 << 20, .max_arbitrary_repetition = 32 },
  };

  try testParseErrorWithConfig("a{33}", error.TooHighArbitraryRepeat, config);
  try testParseErrorWithConfig("a{1,33}", error.TooHighArbitraryRepeat, config);
  try testParseErrorWithConfig("a{33,34}", error.TooHighArbitraryRepeat, config);

  try testMatchWithConfig("a{32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
  try testMatchWithConfig("a{32,32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
  try testMatchWithConfig("a{3,32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);

  // 5. Test edge case where max arbitrary rep = 1 disallows optionals 
  const strict_config: Config = .{
    .limits = .{ .max_arbitrary_repetition = 1 },
  };
  // try testParseErrorWithConfig("a?", error.TooHighArbitraryRepeat, strict_config);
  try testParseErrorWithConfig("a{2}", error.TooHighArbitraryRepeat, strict_config);
  try testMatchWithConfig("a{1}", "a", Match{ .str = "a", .loc = .init(0, 1) }, strict_config);
}
