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
const calculateFinalStatesCount = pzre.nfa.search_problem.calculateFinalStatesCount;

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
  try testParseErrorWithConfig("a{15}", error.TooManyStates, .{ .limits = .{ .max_states = 10 } });

  try testParseErrorWithConfig("a{15}", error.TooManyStates, .{
    .limits = .{ .max_states = 15 }, // off by one
  });
  try testMatchWithConfig("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_states = 16 }, // exact but fails because of bidirectional solver prefix
  });
  try testMatchWithConfig("a{15}", "a" ** 15, Match{ .str = "a" ** 15, .loc = .init(0, 15) }, .{
    .limits = .{ .max_states = 16 }, // Exact now
    .problem = .start_set_pass,
  });
  try testMatchWithConfig("a{15}", "a" ** 15, error.TooManyStates, .{
    .limits = .{ .max_states = 15 }, // Fails
    .problem = .start_set_pass,
  });


  // 3. max_submachine_states limits (Breakpoint Footprint)
  try testParseErrorWithConfig("a{128}", error.TooManyStates, .{
    .limits = .{ .max_states = 1000, .max_submachine_states = .i8 }, 
  });
  try testMatchWithConfig("a{126}", "a" ** 126, Match{ .str = "a" ** 126, .loc = .init(0, 126) }, .{
    .problem = .start_set_pass, // exact for this problem
    .limits = .{
      .max_states = 1000,
      .max_submachine_states = .i8,
    }, // exact
  });

  try testMatchWithConfig("a{126}", "a" ** 126, error.TooManyStates, .{
    .problem = .bi_directional_pass, // fails due to included prefix
    .limits = .{
      .max_states = 1000,
      .max_submachine_states = .i8,
    }, // exact
  });

  try testMatchWithConfig("a{124}", "a" ** 124, Match{ .str = "a" ** 124, .loc = .init(0, 124) }, .{
    .problem = .bi_directional_pass, // The prefix is two states, now works
    .limits = .{
      .max_states = 1000,
      .max_submachine_states = .i8,
    }, // exact
  });

  // Stepping the breakpoint up to .i16 handles the larger topology
  try testMatchWithConfig("a{150}", "a" ** 150, Match{ .str = "a" ** 150, .loc = .init(0, 150) }, .{
    .limits = .{ .max_states = 1000, .max_submachine_states = .i16 }, 
  });

  // 4. upper_bound limits (Physical Allocator Footprint)
  try testParseErrorWithConfig("a" ** 200, error.AllocationUpperbound, .{
    .limits = .{ .max_states = 1 << 16, .gpa_upper_bound = 64 },
  });
}
//
test "pzre max arbitrary repetition" {
  const config: Config = .{
    .limits = .{ .max_states = 300, .gpa_upper_bound = 1 << 20, .max_arbitrary_repetition = 32 },
  };

  try testParseErrorWithConfig("a{33}", error.TooHighArbitraryRepeat, config);
  try testParseErrorWithConfig("a{1,33}", error.TooHighArbitraryRepeat, config);
  try testParseErrorWithConfig("a{33,34}", error.TooHighArbitraryRepeat, config);

  try testMatchWithConfig("a{32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
  try testMatchWithConfig("a{32,32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);
  try testMatchWithConfig("a{3,32}", "a" ** 32, Match{ .str = "a" ** 32, .loc = .init(0, 32) }, config);

  // 5. Test edge case where max arbitrary rep = 1 disallows optionals 
  const strict_config: Config = .{
    .limits = .{ .max_states = 100, .max_arbitrary_repetition = 1 },
  };
  // try testParseErrorWithConfig("a?", error.TooHighArbitraryRepeat, strict_config);
  try testParseErrorWithConfig("a{2}", error.TooHighArbitraryRepeat, strict_config);
  try testMatchWithConfig("a{1}", "a", Match{ .str = "a", .loc = .init(0, 1) }, strict_config);
}
