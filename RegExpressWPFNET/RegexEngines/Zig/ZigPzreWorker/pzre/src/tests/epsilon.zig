const t = @import("test.zig");

const testMatch= t.testMatch;
const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;
const Config = pzre.compile.Config;

test "pzre epsilon (ghost nodes)" {
  { // basic epsilon
    try testMatchExact("", "", true);
    try testMatchExact("", "a", false);
    try testMatchExact("()", "", true);
    try testMatchExact("()a", "a", true);
    try testMatchExact("a()b", "ab", true);
    try testMatchExact("a{0,0}", "", true);
  }

  { // at concat level
    try testMatchExact("(aa){0,0}", "", true);
    try testMatchExact("(aa){0,0}", "a", false);
    try testMatchExact("(aa){0,0}", "aa", false);
    try testMatchExact("(aa){0,0}", "aaa", false);
    try testMatchExact("(aa){0,0}", "aaaa", false);

    try testMatchExact("a{0,0}b{0,0}", "a", false);
    try testMatchExact("a{0,0}b{0,0}", "", true);
    try testMatchExact("a{0,0}mn", "a", false);
    try testMatchExact("a{0,0}b{1,2}c{1,2}", "a", false);
    try testMatchExact("a{0,0}b{1,2}c{1,2}", "ab", false);
    try testMatchExact("a{0,0}b{1,2}c{1,2}", "abc", false);
    try testMatchExact("a{0,0}b{1,2}c{1,2}", "bc", true);

    try testMatchExact("a{1,2}b{0,0}c{1,2}", "abc", false);
    try testMatchExact("a{1,2}b{0,0}c{1,2}", "abbc", false);
    try testMatchExact("a{1,2}b{0,0}c{1,2}", "aac", true);
    try testMatchExact("a{1,2}b{0,0}c{1,2}", "acc", true);
    try testMatchExact("a{1,2}b{0,0}c{1,2}", "ac", true);
    try testMatchExact("a{1,2}b{0,0}c{1,2}", "a", false);
    try testMatchExact("a{1,2}b{0,0}c{1,2}", "b", false);

    try testMatchExact("a{1,2}b{1,2}c{0,0}", "a", false);
    try testMatchExact("a{1,2}b{1,2}c{0,0}", "ab", true);
    try testMatchExact("a{1,2}b{1,2}c{0,0}", "abc", false);
  }

  { // at union level
    try testMatchExact("a{0,0}|b{0,0}", "", true);
    try testMatchExact("(a{0,0}|b{0,0})a", "a", true);
    try testMatchExact("(a{0,0}|b{0,0})a", "", false);
    try testMatchExact("a{0,0}|m|n", "", true);
    try testMatchExact("a{0,0}|b{1,2}|c{1,2}", "", true);
    try testMatchExact("a{0,0}|b{1,2}|c{1,2}", "a", false);
    try testMatchExact("a{0,0}|b{1,2}|c{1,2}", "b", true);
    try testMatchExact("a{0,0}|b{1,2}|c{1,2}", "c", true);

    try testMatchExact("a{1,2}|b{0,0}|c{1,2}", "", true);
    try testMatchExact("a{1,2}|b{0,0}|c{1,2}", "a", true);
    try testMatchExact("a{1,2}|b{0,0}|c{1,2}", "b", false);
    try testMatchExact("a{1,2}|b{0,0}|c{1,2}", "c", true);

    try testMatchExact("a{1,2}|b{1,2}|c{0,0}", "", true);
    try testMatchExact("a{1,2}|b{1,2}|c{0,0}", "a", true);
    try testMatchExact("a{1,2}|b{1,2}|c{0,0}", "b", true);
    try testMatchExact("a{1,2}|b{1,2}|c{0,0}", "c", false);
  }
}

test "pzre more epsilon tests" {

  { // find all
    const str = "mark";
    const pattern = "a|b{0,0}";
    const expected: []const Match = comptime &.{
      Match{.str = "", .loc = .init(0, 0)},
      Match{.str = "a", .loc = .init(1, 2)},
      Match{.str = "", .loc = .init(2, 2)},
      Match{.str = "", .loc = .init(3, 3)},
      Match{.str = "", .loc = .init(4, 4)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  try testMatchExact("A{0,0}B{0,0}C{0,0}", "", true);
  try testMatchExact("A{0,0}B{0,0}C{0,0}", "A", false);

  try testMatchExact("(A{0,0})+", "", true);
  try testMatchExact("(A{0,0})*", "", true);
  try testMatchExact("(A{0,0}){1,5}", "", true);

  try testMatchExact("((A{0,0})B{0,0})?", "", true);

  try testMatchExact("^A{0,0}$", "", true);
  try testMatchExact("^A{0,0}B{0,0}$", "", true);

  try testMatchExact("A{0,0}\\bB{0,0}$", "", false);

  try testMatchExact("A?", "", true);
  try testMatchExact("A?", "A", true);
  try testMatchExact("A?", "AA", false);

  {
    const str = "";
    const pattern = "A{0,0}"; // Term empty
    const expected: []const Match = comptime &.{
      Match{.str = "", .loc = .init(0, 0)},
    };
    try testFindAll(pattern, str, expected, .{});
  }
}

test "pzre variable length to epsilon" {
  try testMatchExact("(a*){0,0}", "", true);
  try testMatchExact("a|(a*){0,0}", "", true);
  try testMatchExact("(a*){0,0}|a", "", true);
  try testMatchExact("(((a*)a+?)?){0,0}", "", true);
  try testMatchExact("(a*){0,0}|(a*){0,0}", "", true);
}

test "pzre epsilon implied by union" {
  const config: Config = .{};

  // 1. Basic trailing and leading empty branches
  // In leftmost-longest, the non-empty branch must win if it matches.
  try testMatch("a|", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("|a", "a", .{ .loc = .init(0, 1), .str = "a" });

  // 2. Empty branches returning zero-length matches on failure
  // When the character doesn't match, the epsilon branch must succeed.
  try testMatch("a|", "b", .{ .loc = .init(0, 0), .str = "" });
  try testMatch("|a", "b", .{ .loc = .init(0, 0), .str = "" });

  // 3. Middle and multiple empty branches
  try testMatch("a||b", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("a||b", "b", .{ .loc = .init(0, 1), .str = "b" });
  try testMatch("a||b", "c", .{ .loc = .init(0, 0), .str = "" });
  try testMatch("||", "abc", .{ .loc = .init(0, 0), .str = "" });

  // 4. Nested groups with epsilon
  // (a|) is functionally equivalent to a?
  try testMatch("(a|)", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("(a|)", "b", .{ .loc = .init(0, 0), .str = "" });
  try testMatch("(|a)", "a", .{ .loc = .init(0, 1), .str = "a" });

  // 5. Union of epsilon and other branches
  // Evaluates L(a) ∪ {ε} ∪ L(b)
  try testMatch("(a|)|b", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("(a|)|b", "b", .{ .loc = .init(0, 1), .str = "b" });
  try testMatch("(a|)|b", "z", .{ .loc = .init(0, 0), .str = "" });

  // 6. Interaction with quantifiers
  // (a|)* can match "aaa" or ""
  try testMatch("(a|)*", "aaa", .{ .loc = .init(0, 3), .str = "aaa" });
  try testMatch("(|a)*", "aaa", .{ .loc = .init(0, 3), .str = "aaa" });

  // 7. Global search iteration with implied epsilon
  // Should find every position in the string
  try testFindAll("|", "ab", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
  }, config);

  // 8. Leftmost-longest across multiple epsilon-capable branches
  // "abc" is longer than the epsilon branch in (abc|)
  try testMatch("(abc|)|a", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  try testMatch("(abc|)|abcd", "abcd", .{ .loc = .init(0, 4), .str = "abcd" });
}
