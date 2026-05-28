const t = @import("test.zig");
const std = @import("std");

const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testFind = t.testFind;
const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;

const Config = pzre.compile.Config;

test "pzre more iterations" {
  {
    const str = "aabbccndd";
    const pattern = "aa|bb|n|dd";
    const expected: []const Match = comptime &.{
      Match{.str = "aa", .loc = .init(0, 2)},
      Match{.str = "bb", .loc = .init(2, 4)},
      Match{.str = "n", .loc = .init(6, 7)},
      Match{.str = "dd", .loc = .init(7, 9)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const str = "aaa";
    const pattern = "a";
    const expected: []const Match = comptime &.{
      Match{.str = "a", .loc = .init(0, 1)},
      Match{.str = "a", .loc = .init(1, 2)},
      Match{.str = "a", .loc = .init(2, 3)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const str = "";
    const pattern = "a";
    const expected: []const Match = comptime &.{};
    try testFindAll(pattern, str, expected, .{});
  }
}


test "pzre find" {
  try testFind("abc", "abc abc abc", 0, Match{.loc = .init(0, 3), .str = "abc"});
  try testFind("abc", "abc abc abc", 1, Match{.loc = .init(4, 7), .str = "abc"});
  try testFind("abc", "abc abc abc", 2, Match{.loc = .init(4, 7), .str = "abc"});
  try testFind("abc", "abc abc abc", 3, Match{.loc = .init(4, 7), .str = "abc"});
  try testFind("abc", "abc abc abc", 8, Match{.loc = .init(8, 11), .str = "abc"});
  try testFind("abc", "abc abc abc", 9, null);
}

test "pzre find all empty matches" {
  const config = Config{
    .limits = .{ .max_states = std.math.maxInt(i8), .gpa_upper_bound = 1024 },
  };

  try testFindAll("a?", "aba", &[_]Match{
    .{ .str = "a", .loc = .init(0, 1) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "a", .loc = .init(2, 3) },
    .{ .str = "", .loc = .init(3, 3) },
  }, config);

  try testFindAll("a*", "baac", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "aa", .loc = .init(1, 3) },
    .{ .str = "", .loc = .init(3, 3) },
    .{ .str = "", .loc = .init(4, 4) },
  }, config);

  try testFindAll("a{0,0}", "ab", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
  }, config);

  try testFindAll("\\b", "a b", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
  }, config);

  try testFindAll("a?$", "bc", &[_]Match{
    .{ .str = "", .loc = .init(2, 2) },
  }, config);
      
  try testFindAll("a?$", "ba", &[_]Match{
    .{ .str = "a", .loc = .init(1, 2) },
    .{ .str = "", .loc = .init(2, 2) },
  }, config);

  try testFindAll("^$", "", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
  }, config);
}

test "pzre find all complex iterations" {
  const config = Config{};

  try testFindAll("aba", "ababa", &[_]Match{
    .{ .str = "aba", .loc = .init(0, 3) },
  }, config);

  try testFindAll("a{1,3}", "aaaaaaa", &[_]Match{
    .{ .str = "aaa", .loc = .init(0, 3) },
    .{ .str = "aaa", .loc = .init(3, 6) },
    .{ .str = "a", .loc = .init(6, 7) },
  }, config);

  try testFindAll("a|ab", "ababa", &[_]Match{
    .{ .str = "ab", .loc = .init(0, 2) },
    // After consuming ab, the next search starts at index 2
    .{ .str = "ab", .loc = .init(2, 4) },
    // After consuming the second ab, only a remains
    .{ .str = "a", .loc = .init(4, 5) },
  }, config);

  try testFindAll("\\d+", "abc123def45gh6", &[_]Match{
    .{ .str = "123", .loc = .init(3, 6) },
    .{ .str = "45", .loc = .init(9, 11) },
    .{ .str = "6", .loc = .init(13, 14) },
  }, config);

  try testFindAll("(ab)+", "ababcab", &[_]Match{
    .{ .str = "abab", .loc = .init(0, 4) },
    .{ .str = "ab", .loc = .init(5, 7) },
  }, config);

  try testFindAll("c?", "ab", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
  }, config);
}
