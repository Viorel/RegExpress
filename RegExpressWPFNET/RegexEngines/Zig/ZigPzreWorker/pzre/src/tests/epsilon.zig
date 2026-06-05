const t = @import("test.zig");

const testMatch = t.testMatch;
const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testMatchMany = t.testMatchMany;
const testMatchExactMany = t.testMatchExactMany;

const ExpectMatch = t.ExpectMatch;
const ExpectMatchExact = t.ExpectMatchExact;

const pzre = @import("../root.zig");
const Match = pzre.regex.Match;

test "epsilon: completely empty pattern matches empty input only" {
  try testMatchExactMany("", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "abc", .expected = false },
  });
}

test "epsilon: empty group `()` produces epsilon" {
  try testMatchExactMany("()", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExact("()a", "a", true);
  try testMatchExact("a()", "a", true);
  try testMatchExact("a()b", "ab", true);
  try testMatchExact("a()()()b", "ab", true);
  try testMatchExact("(())", "", true);
  try testMatchExact("((()))", "", true);
}

test "epsilon: a{0,0} compiles to zero-state machine" {
  try testMatchExactMany("a{0,0}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "aa", .expected = false },
  });
  try testMatchExact("a{0,0}a{0,0}", "", true);
  try testMatchExact("a{0,0}a{0,0}a{0,0}", "", true);
}

test "epsilon: empty union side parses as epsilon (all positions)" {
  try testMatchMany("a|", &.{
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "b", .expected = Match{ .str = "", .loc = .init(0, 0) } },
  });
  try testMatchMany("|a", &.{
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "b", .expected = Match{ .str = "", .loc = .init(0, 0) } },
  });
  try testMatchMany("a||b", &.{
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "b", .expected = Match{ .str = "b", .loc = .init(0, 1) } },
    .{ .str = "c", .expected = Match{ .str = "", .loc = .init(0, 0) } },
  });
  try testMatch("|", "x", Match{ .str = "", .loc = .init(0, 0) });
  try testMatch("||", "x", Match{ .str = "", .loc = .init(0, 0) });
  try testMatch("|||", "x", Match{ .str = "", .loc = .init(0, 0) });
}

test "epsilon: epsilon in concat is a no-op" {
  try testMatchExact("a()b", "ab", true);
  try testMatchExact("()ab", "ab", true);
  try testMatchExact("ab()", "ab", true);
  try testMatchExact("()a()b()", "ab", true);
  try testMatchExact("a()()()b", "ab", true);
  try testMatchExact("a{0,0}b", "b", true);
  try testMatchExact("ab{0,0}", "a", true);

  try testMatchExactMany("a{0,0}b{0,0}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "ab", .expected = false },
    .{ .str = "a", .expected = false },
  });

  try testMatchExactMany("(aa){0,0}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "aa", .expected = false },
    .{ .str = "aaa", .expected = false },
    .{ .str = "aaaa", .expected = false },
  });

  try testMatchExact("a{0,0}mn", "a", false);

  try testMatchExactMany("a{0,0}b{1,2}c{1,2}", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "ab", .expected = false },
    .{ .str = "abc", .expected = false },
    .{ .str = "bc", .expected = true },
  });

  try testMatchExactMany("a{1,2}b{0,0}c{1,2}", &.{
    .{ .str = "abc", .expected = false },
    .{ .str = "abbc", .expected = false },
    .{ .str = "aac", .expected = true },
    .{ .str = "acc", .expected = true },
    .{ .str = "ac", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = false },
  });

  try testMatchExactMany("a{1,2}b{1,2}c{0,0}", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "ab", .expected = true },
    .{ .str = "abc", .expected = false },
  });
}

test "epsilon: epsilon at union level" {
  try testMatchExact("a{0,0}|b{0,0}", "", true);
  try testMatchExactMany("(a{0,0}|b{0,0})a", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "", .expected = false },
  });
  try testMatchExact("a{0,0}|m|n", "", true);

  try testMatchExactMany("a{0,0}|b{1,2}|c{1,2}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = true },
    .{ .str = "c", .expected = true },
  });

  try testMatchExactMany("a{1,2}|b{0,0}|c{1,2}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
    .{ .str = "c", .expected = true },
  });

  try testMatchExactMany("a{1,2}|b{1,2}|c{0,0}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "c", .expected = false },
  });
}

test "epsilon: epsilon between non-trivial subpatterns" {
  try testMatchExactMany("(a|b)()(c|d)", &.{
    .{ .str = "ac", .expected = true },
    .{ .str = "bd", .expected = true },
    .{ .str = "ab", .expected = false },
  });
  try testMatchExact("a+()b+", "aaabb", true);
  try testMatchExact("a+(){0,0}b+", "aaabb", true);
}

test "epsilon: quantifier over epsilon still produces epsilon" {
  try testMatchExactMany("()*", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("()+", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("()?", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("(){5}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExact("(){0,10}", "", true);

  try testMatchExactMany("(a{0,0})+", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExact("(a{0,0})*", "", true);
  try testMatchExact("(a{0,0}){1,5}", "", true);
}

test "epsilon: nested epsilon-producing constructs" {
  try testMatchExact("((a{0,0})b{0,0})?", "", true);
  try testMatchExact("(()|())", "", true);
  try testMatchExact("((((((()))))))", "", true);
  try testMatchExact("(a*){0,0}", "", true);
  try testMatchExact("(((a*)a+?)?){0,0}", "", true);
  try testMatchExact("a|(a*){0,0}", "", true);
  try testMatchExact("(a*){0,0}|a", "", true);
  try testMatchExact("(a*){0,0}|(a*){0,0}", "", true);
}

test "epsilon: union with epsilon is equivalent to optional" {
  try testMatchMany("(a|)", &.{
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "b", .expected = Match{ .str = "", .loc = .init(0, 0) } },
  });
  try testMatchMany("(|a)", &.{
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "b", .expected = Match{ .str = "", .loc = .init(0, 0) } },
  });

  try testMatchExactMany("(a|)b", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "b", .expected = true },
  });
  try testMatchExactMany("(|a)b", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "b", .expected = true },
  });
 
  try testMatchMany("a|", &.{
    .{ .str = "a", .expected = .{ .loc = .init(0, 1), .str = "a" } },
    .{ .str = "b", .expected = .{ .loc = .init(0, 0), .str = "" } },
  });
  try testMatchMany("|a", &.{
    .{ .str = "a", .expected = .{ .loc = .init(0, 1), .str = "a" } },
    .{ .str = "b", .expected = .{ .loc = .init(0, 0), .str = "" } },
  });
  try testMatchMany("a||b", &.{
    .{ .str = "a", .expected = .{ .loc = .init(0, 1), .str = "a" } },
    .{ .str = "b", .expected = .{ .loc = .init(0, 1), .str = "b" } },
    .{ .str = "c", .expected = .{ .loc = .init(0, 0), .str = "" } },
  });
  try testMatch("||", "abc", .{ .loc = .init(0, 0), .str = "" });
  try testMatchMany("(a|)|b", &.{
    .{ .str = "a", .expected = .{ .loc = .init(0, 1), .str = "a" } },
    .{ .str = "b", .expected = .{ .loc = .init(0, 1), .str = "b" } },
    .{ .str = "z", .expected = .{ .loc = .init(0, 0), .str = "" } },
  });
  try testMatchMany("(a|)*", &.{
    .{ .str = "aaa", .expected = .{ .loc = .init(0, 3), .str = "aaa" } },
  });
  try testMatch("(|a)*", "aaa", .{ .loc = .init(0, 3), .str = "aaa" });
  try testMatch("(abc|)|a", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  try testMatch("(abc|)|abcd", "abcd", .{ .loc = .init(0, 4), .str = "abcd" });
}

test "epsilon: leftmost-longest selects non-empty branch when possible" {
  try testMatchMany("(a|)|b", &.{
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "b", .expected = Match{ .str = "b", .loc = .init(0, 1) } },
    .{ .str = "z", .expected = Match{ .str = "", .loc = .init(0, 0) } },
  });
  try testMatch("(abc|)|a", "abc", Match{ .str = "abc", .loc = .init(0, 3) });
  try testMatch("(abc|)|abcd", "abcd", Match{ .str = "abcd", .loc = .init(0, 4) });
}

test "epsilon: union with epsilon under quantifier" {
  try testMatchMany("(a|)*", &.{
    .{ .str = "", .expected = Match{ .str = "", .loc = .init(0, 0) } },
    .{ .str = "aaa", .expected = Match{ .str = "aaa", .loc = .init(0, 3) } },
  });
  try testMatch("(|a)*", "aaa", Match{ .str = "aaa", .loc = .init(0, 3) });
  try testMatch("(a|){5}", "aaaaa", Match{ .str = "aaaaa", .loc = .init(0, 5) });
}

test "epsilon: variable-length subpattern erased by {0,0}" {
  try testMatchExactMany("(a*){0,0}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExact("a|(a*){0,0}", "", true);
  try testMatchExact("(a*){0,0}|a", "", true);
  try testMatchExact("(a*){0,0}|(a*){0,0}", "", true);
}

test "epsilon: epsilon with anchors matches empty input" {
  try testMatchExactMany("^$", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExact("^A{0,0}$", "", true);
  try testMatchExact("^A{0,0}B{0,0}$", "", true);
  try testMatchExact("^(){5}$", "", true);
}

test "epsilon: epsilon at word boundary cannot match in empty input" {
  try testMatchExact("A{0,0}\\bB{0,0}", "", false);
  try testMatchExact("\\b", "", false);
}

test "epsilon: findAll yields zero-width match at every position" {
  try testFindAll("|", "ab", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "", .loc = .init(1, 1) },
    .{ .str = "", .loc = .init(2, 2) },
  }, .{});
  try testFindAll("|", "", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
  }, .{});
}

test "epsilon: findAll with mixed epsilon and char alternation" {
  try testFindAll("a|", "mark", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
    .{ .str = "a", .loc = .init(1, 2) },
    .{ .str = "", .loc = .init(2, 2) },
    .{ .str = "", .loc = .init(3, 3) },
    .{ .str = "", .loc = .init(4, 4) },
  }, .{});
}

test "epsilon: findAll over pure-epsilon pattern on empty input" {
  try testFindAll("A{0,0}", "", &[_]Match{
    .{ .str = "", .loc = .init(0, 0) },
  }, .{});
}
