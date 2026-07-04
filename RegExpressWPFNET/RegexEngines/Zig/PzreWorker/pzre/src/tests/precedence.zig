const t = @import("test.zig");

const testMatch = t.testMatch;
const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testMatchMany = t.testMatchMany;
const testMatchExactMany = t.testMatchExactMany;

const pzre = @import("../root.zig");
const Match = pzre.regex.Match;

test "quantifier: a* matches zero or more" {
  try testMatchExactMany("a*", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaaaaaaa", .expected = true },
    .{ .str = "b", .expected = false },
    .{ .str = "ab", .expected = false },
  });
}

test "quantifier: a+ matches one or more" {
  try testMatchExactMany("a+", &.{
    .{ .str = "", .expected = false },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaaa", .expected = true },
    .{ .str = "b", .expected = false },
    .{ .str = "ab", .expected = false },
  });
}

test "quantifier: a? matches zero or one" {
  try testMatchExactMany("a?", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = false },
    .{ .str = "b", .expected = false },
  });
}

test "quantifier: a{n} matches exactly n" {
  try testMatchExactMany("a{3}", &.{
    .{ .str = "", .expected = false },
    .{ .str = "aa", .expected = false },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaa", .expected = false },
  });
  try testMatchExactMany("a{1}", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "", .expected = false },
  });
  try testMatchExactMany("a{0}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
}

test "quantifier: a{n,} matches at least n" {
  try testMatchExactMany("a{2,}", &.{
    .{ .str = "", .expected = false },
    .{ .str = "a", .expected = false },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaaaaa", .expected = true },
  });

  try testMatchExactMany("a{1,}", &.{
    .{ .str = "", .expected = false },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaaaaa", .expected = true },
  });
 
  try testMatchExactMany("a{0,}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaaaaa", .expected = true },
  });

  try testMatchExactMany("a{0,}|a{1,}|a{2,}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaa", .expected = true },
  });
 
  try testMatchExactMany("a{1,}|a{0,}|a{2,}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaa", .expected = true },
  });
 
  try testMatchExactMany("a{1,}a{0,}a{2,}", &.{
    .{ .str = "", .expected = false },
    .{ .str = "a", .expected = false },
    .{ .str = "aa", .expected = false },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaa", .expected = true },
  });
 
  try testMatchExactMany("a{1,}a{2,}", &.{
    .{ .str = "", .expected = false },
    .{ .str = "a", .expected = false },
    .{ .str = "aa", .expected = false },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaa", .expected = true },
  });
  
  try testMatchExactMany("a{2,}a{3,}", &.{
    .{ .str = "aaaa", .expected = false },
    .{ .str = "aaaaa", .expected = true },
  });
}

test "quantifier: a{n,m} matches between n and m inclusive" {
  try testMatchExactMany("a{2,4}", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "aa", .expected = true },
    .{ .str = "aaa", .expected = true },
    .{ .str = "aaaa", .expected = true },
    .{ .str = "aaaaa", .expected = false },
  });
  // Exact: a{n,n} == a{n}
  try testMatchExactMany("a{3,3}", &.{
    .{ .str = "aaa", .expected = true },
    .{ .str = "aa", .expected = false },
    .{ .str = "aaaa", .expected = false },
  });
}

test "quantifier: a* is greedy (consumes as much as possible)" {
  try testMatchMany("a*", &.{
    .{ .str = "aaab", .expected = Match{ .str = "aaa", .loc = .init(0, 3) } },
    .{ .str = "aaaa", .expected = Match{ .str = "aaaa", .loc = .init(0, 4) } },
  });
}

test "quantifier: a+ is greedy" {
  try testMatchMany("a+", &.{
    .{ .str = "aaab", .expected = Match{ .str = "aaa", .loc = .init(0, 3) } },
    .{ .str = "aaaa", .expected = Match{ .str = "aaaa", .loc = .init(0, 4) } },
  });
}

test "quantifier: a{n,m} is greedy (takes max possible up to m)" {
  try testMatchMany("a{2,5}", &.{
    .{ .str = "aaaaaaaa", .expected = Match{ .str = "aaaaa", .loc = .init(0, 5) } },
    .{ .str = "aaa", .expected = Match{ .str = "aaa", .loc = .init(0, 3) } },
  });
}

test "quantifier: greediness with trailing context" {
  try testMatchMany(".*b", &.{
    .{ .str = "aaab", .expected = Match{ .str = "aaab", .loc = .init(0, 4) } },
    .{ .str = "abbb", .expected = Match{ .str = "abbb", .loc = .init(0, 4) } },
    .{ .str = "ababab", .expected = Match{ .str = "ababab", .loc = .init(0, 6) } },
  });
}

test "quantifier: greediness in alternation chooses longest at same position" {
  try testMatch("a|aa|aaa", "aaa", Match{ .str = "aaa", .loc = .init(0, 3) });
  try testMatch("aaa|aa|a", "aaa", Match{ .str = "aaa", .loc = .init(0, 3) });
  try testMatch("ab|abc", "abc", Match{ .str = "abc", .loc = .init(0, 3) });
  try testMatch("abc|ab", "abc", Match{ .str = "abc", .loc = .init(0, 3) });
}

test "concatenation: sequential matching" {
  try testMatchExactMany("ab", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "ba", .expected = false },
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = false },
  });
  try testMatchExactMany("abcdef", &.{
    .{ .str = "abcdef", .expected = true },
    .{ .str = "abcdeg", .expected = false },
  });
}

test "union: alternative branches" {
  try testMatchExactMany("a|b", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "c", .expected = false },
  });
  try testMatchExactMany("a|b|c|d", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "d", .expected = true },
    .{ .str = "e", .expected = false },
  });
}

test "grouping: parentheses contain subpatterns" {
  try testMatchExactMany("(ab)", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "ba", .expected = false },
  });
  try testMatchExact("((ab))", "ab", true);
  try testMatchExact("(((ab)))", "ab", true);
}

test "precedence: repetition binds tighter than concatenation" {
  try testMatchExactMany("ab+", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "abb", .expected = true },
    .{ .str = "abbbbb", .expected = true },
    .{ .str = "abab", .expected = false },
  });

  try testMatchExactMany("(ab)+", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "abab", .expected = true },
    .{ .str = "abb", .expected = false },
  });

  try testMatchExactMany("ab*", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ab", .expected = true },
    .{ .str = "abbb", .expected = true },
  });
  
  try testMatchExactMany("ab?", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ab", .expected = true },
    .{ .str = "abb", .expected = false },
  });
  
  try testMatchExactMany("ab{3}", &.{
    .{ .str = "abbb", .expected = true },
    .{ .str = "ababab", .expected = false },
  });
}

test "precedence: concatenation binds tighter than union" {
  try testMatchExactMany("ab|cd", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "cd", .expected = true },
    .{ .str = "ad", .expected = false },
    .{ .str = "ac", .expected = false },
    .{ .str = "abcd", .expected = false },
  });

  try testMatchExactMany("a(b|c)d", &.{
    .{ .str = "abd", .expected = true },
    .{ .str = "acd", .expected = true },
    .{ .str = "ab", .expected = false },
    .{ .str = "cd", .expected = false },
  });
}

test "precedence: combined rep > concat > union with no grouping" {
  try testMatchExactMany("ab+|cd*", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "abbb", .expected = true },
    .{ .str = "c", .expected = true },
    .{ .str = "cd", .expected = true },
    .{ .str = "cdddd", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "d", .expected = false },
  });

  try testMatchExactMany("(ab+|cd)*", &.{
    .{ .str = "abcd", .expected = true },
    .{ .str = "abbbcdab", .expected = true },
  });
  
  try testMatchExactMany("((ab)+|cd*)", &.{
    .{ .str = "abab", .expected = true },
    .{ .str = "ab", .expected = true },
  });
}

test "quantifier: grouped subpatterns quantify as a unit" {
  try testMatchExactMany("(abc|123)+", &.{
    .{ .str = "abc", .expected = true },
    .{ .str = "abcabc", .expected = true },
    .{ .str = "abc123", .expected = true },
    .{ .str = "123abc", .expected = true },
    .{ .str = "", .expected = false },
    .{ .str = "ab", .expected = false },
    .{ .str = "bc", .expected = false },
    .{ .str = "abc12", .expected = false },
  });
}

test "quantifier: nested quantified groups" {
  try testMatchExactMany("(ab+)+", &.{
    .{ .str = "ab", .expected = true },
    .{ .str = "abb", .expected = true },
    .{ .str = "abab", .expected = true },
    .{ .str = "abbabbb", .expected = true },
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = false },
  });

  try testMatchExactMany("((a|b)+c)+", &.{
    .{ .str = "ac", .expected = true },
    .{ .str = "abc", .expected = true },
    .{ .str = "abcabc", .expected = true },
    .{ .str = "abbac", .expected = true },
    .{ .str = "c", .expected = false },
  });
}

test "quantifier: large but valid repeat counts" {
  try testMatchExactMany("(aa){5,7}", &.{
    .{ .str = "", .expected = false },
    .{ .str = "aaaaaaaa", .expected = false },
    .{ .str = "aaaaaaaaaa", .expected = true },
    .{ .str = "aaaaaaaaaaaa", .expected = true },
    .{ .str = "aaaaaaaaaaaaaa", .expected = true },
    .{ .str = "aaaaaaaaaaaaaaaa", .expected = false },
    .{ .str = "aaaaaaaaaaa", .expected = false },
    .{ .str = "aaaaaaaaaaaaa", .expected = false },
  });
}

test "quantifier: identity cases" {
  try testMatchExactMany("a{1,1}", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "", .expected = false },
    .{ .str = "aa", .expected = false },
  });
  
  try testMatchExactMany("a{1}", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "", .expected = false },
  });
}

test "precedence: quantifier on group with union inside" {
  try testMatchExactMany("(abc|123){0,}", &.{
    .{ .str = "", .expected = true },
    .{ .str = "abc", .expected = true },
    .{ .str = "123abc123", .expected = true },
    .{ .str = "abc1", .expected = false },
    .{ .str = "12c", .expected = false },
  });

  try testMatchExactMany("(abc|123){2,3}", &.{
    .{ .str = "abcabc", .expected = true },
    .{ .str = "123123", .expected = true },
    .{ .str = "abc123abc", .expected = true },
    .{ .str = "abcabcabcabc", .expected = false },
    .{ .str = "abcabc12", .expected = false },
  });
}

test "quantifier: bounded greediness with trailing match requirement" {
  try testMatchMany("a{2,5}b", &.{
    .{ .str = "aab", .expected = Match{ .str = "aab", .loc = .init(0, 3) } },
    .{ .str = "aaaaab", .expected = Match{ .str = "aaaaab", .loc = .init(0, 6) } },
    .{ .str = "aaaaaaab", .expected = Match{ .str = "aaaaab", .loc = .init(2, 8) } },
  });
}
