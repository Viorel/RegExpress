const t = @import("test.zig");

const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testMatch = t.testMatch;
const pzre = @import("../root.zig");
const Match = pzre.regex.Match;

test "pzre leftmost-longest" {
  try testMatch("(abc|ab)", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  try testMatch("(ab|abc)", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  try testMatch("(a|aa)", "aaaaa", .{ .loc = .init(0, 2), .str = "aa" });
  try testMatch("(a*)a", "aaaa", .{ .loc = .init(0, 4), .str = "aaaa" });
}

test "pzre leftmost longest exhaustive" {
  // Alternation order independence (longest branch must win regardless of position)
  try testMatch("a|ab|abc", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  try testMatch("abc|ab|a", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  try testMatch("ab|a", "abc", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("a|ab", "abc", .{ .loc = .init(0, 2), .str = "ab" });

  // Leftmost priority over longest (earlier start always beats longer match)
  try testMatch("a|ba", "cba", .{ .loc = .init(1, 3), .str = "ba" });
  try testMatch("ba|a", "cba", .{ .loc = .init(1, 3), .str = "ba" });
  try testMatch("b|ab", "xabx", .{ .loc = .init(1, 3), .str = "ab" });

  // Greedy quantifiers expanding fully
  try testMatch("a+", "aaaa", .{ .loc = .init(0, 4), .str = "aaaa" });
  try testMatch("a{1,3}", "aaaa", .{ .loc = .init(0, 3), .str = "aaa" });
  try testMatch("(a|aa)+", "aaaaa", .{ .loc = .init(0, 5), .str = "aaaaa" });

  // Prefix and suffix greedy overlap
  try testMatch(".*a", "aabaa", .{ .loc = .init(0, 5), .str = "aabaa" });
  try testMatch("a.*", "aabaa", .{ .loc = .init(0, 5), .str = "aabaa" });
  try testMatch("a.*b", "abxab", .{ .loc = .init(0, 5), .str = "abxab" });

  // Cross-branch combinations (a + bcd is length 4, ab + c is length 3)
  try testMatch("(a|ab)(c|bcd)", "abcd", .{ .loc = .init(0, 4), .str = "abcd" });
  
  // Empty string and epsilon edge cases
  try testMatch("a?|b?", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("a*|b*", "bb", .{ .loc = .init(0, 2), .str = "bb" });
  try testMatch("(a|)|b", "a", .{ .loc = .init(0, 1), .str = "a" });
  
  // Pathological nesting
  try testMatch("(a+|a)", "aaaa", .{ .loc = .init(0, 4), .str = "aaaa" });
  try testMatch("((a|ab)c|abc)", "abc", .{ .loc = .init(0, 3), .str = "abc" });
  
  // Later matches in strings
  try testMatch("apple|orange", "my orange tree", .{ .loc = .init(3, 9), .str = "orange" });
}
