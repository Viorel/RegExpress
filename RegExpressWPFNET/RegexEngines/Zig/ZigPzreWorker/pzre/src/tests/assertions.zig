const t = @import("test.zig");

const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testMatch = t.testMatch;
const testMatchWithConfig = t.testMatchWithConfig;
const testMatchStart = t.testMatchStart;
const testMatches = t.testMatches;

const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;
const Config = pzre.compile.Config;

test "pzre findAll and multiline assertions" {
  const str = "name job id note\nmark sysadmin 123 jabroni\nsebastian sysadmin 333\ncole cook 592";

  { // basic
    const pattern = "\\d+";
    const expected: []const Match = comptime &.{
      Match{.str = "123", .loc = .init(31, 34)},
      Match{.str = "333", .loc = .init(62, 65)},
      Match{.str = "592", .loc = .init(76, 79)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  { // multiline dollar
    const pattern = "\\d+$";
    const expected: []const Match = comptime &.{
      Match{.str = "333", .loc = .init(62, 65)},
      Match{.str = "592", .loc = .init(76, 79)},
    };
    try testFindAllMultiline(pattern, str, expected);
  }

  { // end of text
    const pattern = "\\d+\\z";
    const expected: []const Match = comptime &.{
      Match{.str = "592", .loc = .init(76, 79)},
    };
    try testFindAllMultiline(pattern, str, expected);
    try testFindAll(pattern, str, expected, .{});
  }

  { // dollar
    const pattern = "\\d+$";
    const expected: []const Match = comptime &.{
      Match{.str = "592", .loc = .init(76, 79)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  { // multiline caret
    const pattern = "^\\w+";
    const expected: []const Match = comptime &.{
      Match{.str = "name", .loc = .init(0, 4)},
      Match{.str = "mark", .loc = .init(17, 21)},
      Match{.str = "sebastian", .loc = .init(43, 43 + 9)},
      Match{.str = "cole", .loc = .init(66, 70)},
    };
    try testFindAllMultiline(pattern, str, expected);
  }

  { // caret
    const pattern = "^\\w+";
    const expected: []const Match = comptime &.{
      Match{.str = "name", .loc = .init(0, 4)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  { // start of text
    const pattern = "\\A\\w+";
    const expected: []const Match = comptime &.{
      Match{.str = "name", .loc = .init(0, 4)},
    };
    try testFindAll(pattern, str, expected, .{});
    try testFindAllMultiline(pattern, str, expected);
  }
}

test "pzre word delimiters" {
  const str = "ab abz zab zabz ab ab";
  {
    const pattern = "ab\\b";
    const expected: []const Match = comptime &.{
      Match{.str = "ab", .loc = .init(0, 2)},
      Match{.str = "ab", .loc = .init(8, 10)},
      Match{.str = "ab", .loc = .init(16, 18)},
      Match{.str = "ab", .loc = .init(19, 21)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const pattern = "ab\\B";
    const expected: []const Match = comptime &.{
      Match{.str = "ab", .loc = .init(3, 5)},
      Match{.str = "ab", .loc = .init(12, 14)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const pattern = "\\bab";
    const expected: []const Match = comptime &.{
      Match{.str = "ab", .loc = .init(0, 2)},
      Match{.str = "ab", .loc = .init(3, 5)},
      Match{.str = "ab", .loc = .init(16, 18)},
      Match{.str = "ab", .loc = .init(19, 21)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const pattern = "\\Bab";
    const expected: []const Match = comptime &.{
      Match{.str = "ab", .loc = .init(8, 10)},
      Match{.str = "ab", .loc = .init(12, 14)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const pattern = "\\bab\\b";
    const expected: []const Match = comptime &.{
      Match{.str = "ab", .loc = .init(0, 2)},
      Match{.str = "ab", .loc = .init(16, 18)},
      Match{.str = "ab", .loc = .init(19, 21)},
    };
    try testFindAll(pattern, str, expected, .{});
  }

  {
    const s = "a";
    const pattern = "\\ba\\b";
    const expected: []const Match = comptime &.{
      Match{.str = "a", .loc = .init(0, 1)},
    };
    try testFindAll(pattern, s, expected, .{});
  }

  {
    const s = "a";
    const pattern = "\\Ba";
    const expected: []const Match = comptime &.{};
    try testFindAll(pattern, s, expected, .{});
  }

  {
    const s = "a";
    const pattern = "a\\B";
    const expected: []const Match = comptime &.{};
    try testFindAll(pattern, s, expected, .{});
  }
}

// Test all assertions that can jump
test "pzre jumping assertions" {
  try testMatch("ab\\b|mn", "ab", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("ab\\b|mn", "mn", .{ .loc = .init(0, 2), .str = "mn" });
  try testMatch("ab\\b|mn", "z", null);
  try testMatch("ab\\b|mn", "abb", null);
  try testMatch("ab\\b|mn", "ab b", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("ab\\b|mn", "b ab b", .{ .loc = .init(2, 4), .str = "ab" });

  try testMatch("ab\\B|mn", "ab", null);
  try testMatch("ab\\B|mn", "mn", .{ .loc = .init(0, 2), .str = "mn" });
  try testMatch("ab\\B|mn", "z", null);
  try testMatch("ab\\B|mn", "abb", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("ab\\B|mn", "ab b", null);
  try testMatch("ab\\B|mn", "b ab b", null);

  try testMatch("ab$|^qp|mn", "ab", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("ab$|^qp|mn", "aab", .{ .loc = .init(1, 3), .str = "ab" });
  try testMatch("ab$|^qp|mn", "aaba", null);
  try testMatch("ab$|^qp|mn", "qp", .{ .loc = .init(0, 2), .str = "qp" });
  try testMatch("ab$|^qp|mn", "aqp", null);
  try testMatch("ab$|^qp|mn", "qpa", .{ .loc = .init(0, 2), .str = "qp" });
  try testMatch("ab$|^qp|mn", "amna", .{ .loc = .init(1, 3), .str = "mn" });

  try testMatch("ab\\z|\\Aqp|mn", "ab", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("ab\\z|\\Aqp|mn", "aab", .{ .loc = .init(1, 3), .str = "ab" });
  try testMatch("ab\\z|\\Aqp|mn", "aaba", null);
  try testMatch("ab\\z|\\Aqp|mn", "qp", .{ .loc = .init(0, 2), .str = "qp" });
  try testMatch("ab\\z|\\Aqp|mn", "aqp", null);
  try testMatch("ab\\z|\\Aqp|mn", "qpa", .{ .loc = .init(0, 2), .str = "qp" });
  try testMatch("ab\\z|\\Aqp|mn", "amna", .{ .loc = .init(1, 3), .str = "mn" });
}

test "pzre nonsensical assertions" {
  try testMatch("\\b\\B", "a", null);
  try testMatch("\\b\\B", "", null);
  try testMatch("\\B\\b", "a", null);

  try testMatch("a^", "a", null);
  try testMatch("a\\A", "a", null);

  try testMatch("$a", "a", null);
  try testMatch("\\za", "a", null);

  try testMatch("$^", "", Match{.loc = .init(0, 0), .str = ""});
  try testMatchWithConfig("$^", "\n", Match{.loc = .init(0, 0), .str = ""}, .{ .semantics = .{ .multiline = true } });
  try testMatch("\\z\\A", "", Match{.loc = .init(0, 0), .str = ""});
}

test "pzre chained and redundant assertions" {
  try testMatch("^^a", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("a$$", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("\\A\\Aab\\z\\z", "ab", .{ .loc = .init(0, 2), .str = "ab" });

  try testMatch("\\b\\ba\\b\\b", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("\\b\\B\\ba", "a", null);

  try testMatch("^\\Aab$\\z", "ab", .{ .loc = .init(0, 2), .str = "ab" });
  try testMatch("^\\ba\\b$", "a", .{ .loc = .init(0, 1), .str = "a" });

  try testMatch("^^^", "", .{ .loc = .init(0, 0), .str = "" });
  try testMatch("$$$", "", .{ .loc = .init(0, 0), .str = "" });
}

test "pzre matchStart" {
  try testMatchStart("^abc", "abc", "abc");
  try testMatchStart("^abc", "mabc", null);
  try testMatchStart("^abc", "abcn", "abc");
  try testMatchStart("^abc", "mabcn", null);
  try testMatchStart("^abc", "", null);
}

test "pzre matches" {
  try testMatches("^abc", "abc", true);
  try testMatches("^abc", "mabc", false);
  try testMatches("^abc", "abcn", true);
  try testMatches("^abc", "mabcn", false);
  try testMatches("^abc", "", false);
}

test "pzre word boundary bug fix" {
  try testMatch("\\b\\.", " .", null);
  try testMatch("\\B\\.", " .", .{ .loc = .init(1, 2), .str = "." });

  try testMatch("a\\bb", "ab", null);
  try testMatch("a\\Bb", "ab", .{ .loc = .init(0, 2), .str = "ab" });

  try testMatch("a\\b\\.", "a.", .{ .loc = .init(0, 2), .str = "a." });
  try testMatch("a\\B\\.", "a.", null);

  try testMatch("^\\b\\.$", ".", null);
  try testMatch("^\\B\\.$", ".", .{ .loc = .init(0, 1), .str = "." });

  try testMatch("^\\ba$", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("^\\Ba$", "a", null);
}

test "pzre multiline exhaustive assertions" {
  const ml = pzre.compile.Config{ .semantics = .{ .multiline = true } };
  const sl = pzre.compile.Config{ .semantics = .{ .multiline = false } };

  // 1. ^ and $ matching middle lines (Multiline Mode)
  try testMatchWithConfig("^bar$", "foo\nbar\nbaz", .{ .loc = .init(4, 7), .str = "bar" }, ml);
  try testMatchWithConfig("foo$", "foo\nbar", .{ .loc = .init(0, 3), .str = "foo" }, ml);
  try testMatchWithConfig("^bar", "foo\nbar", .{ .loc = .init(4, 7), .str = "bar" }, ml);

  // 2. ^ and $ FAILING to match middle lines (Single-line Mode)
  try testMatchWithConfig("^bar$", "foo\nbar\nbaz", null, sl);
  try testMatchWithConfig("foo$", "foo\nbar", null, sl);
  try testMatchWithConfig("^bar", "foo\nbar", null, sl);

  // 3. \A and \z strictly ignoring the multiline config
  try testMatchWithConfig("\\Abar\\z", "foo\nbar\nbaz", null, ml);
  try testMatchWithConfig("\\Afoo", "foo\nbar", .{ .loc = .init(0, 3), .str = "foo" }, ml);
  try testMatchWithConfig("bar\\z", "foo\nbar", .{ .loc = .init(4, 7), .str = "bar" }, ml);
  try testMatchWithConfig("foo\\z", "foo\nbar", null, ml);
  try testMatchWithConfig("\\Abar", "foo\nbar", null, ml);

  // 4. Empty line matching (\n\n)
  try testMatchWithConfig("^$", "a\n\nb", Match{.loc = .init(2, 2), .str = ""}, ml);
  try testMatchWithConfig("^$", "\n", Match{.loc = .init(0, 0), .str = ""}, ml);
  
  // 5. Mixed anchor interactions
  try testMatchWithConfig("\\Afoo$", "foo\nbar", .{ .loc = .init(0, 3), .str = "foo" }, ml);
  try testMatchWithConfig("^bar\\z", "foo\nbar", .{ .loc = .init(4, 7), .str = "bar" }, ml);

  // 6. Trailing newline behaviors
  try testMatchWithConfig("^$", "foo\n", Match{.loc = .init(4, 4), .str = ""}, ml);
  try testMatchWithConfig("foo$", "foo\n", .{ .loc = .init(0, 3), .str = "foo" }, ml);
  
  // 7. Word boundaries (\b) adjacent to newlines 
  try testMatchWithConfig("foo\\b", "foo\nbar", .{ .loc = .init(0, 3), .str = "foo" }, ml);
  try testMatchWithConfig("\\bbar", "foo\nbar", .{ .loc = .init(4, 7), .str = "bar" }, ml);
  try testMatchWithConfig("foo\\B", "foo\nbar", null, ml);
}

test "pzre right anchoring" {
  try testMatches("a$", "a", true);
  try testMatches("a$", "ba", true);
  try testMatches("a$", "ab", false);
  try testMatches("a$", "bab", false);

  try testMatches("^$", "", true);
  try testMatches("^$", "a", false);

  try testMatches("a+$", "baaa", true);
  try testMatches("a+$", "baab", false);

  try testMatchStart("a$", "a", "a");
  try testMatchStart("a$", "ba", null);
  try testMatchStart("a$", "ab", null);

  try testMatchStart("a+$", "aaa", "aaa");
  try testMatchStart("a+$", "bbaaa", null);

  try testMatchStart(".*a$", "bba", "bba");
  try testMatchStart(".*a$", "bbac", null);

  try testMatchStart("[a-z]+$", "abc", "abc");
  try testMatchStart("[a-z]+$", "123abc", null);

  try testMatchStart("^$", "", "");
  try testMatchStart("^a$", "a", "a");
  try testMatchStart(".*$", "aaaa", "aaaa");
  try testMatchStart(".*$", "", "");

  try testMatch(".*$", "", Match{.str = "", .loc = .init(0, 0)});
  try testMatch(".*$", "a", Match{.str = "a", .loc = .init(0, 1)});
  try testMatch(".*$", "aaaa", Match{.str = "aaaa", .loc = .init(0, 4)});
}

test "pzre quantified assertions" {
  // 1. Starred assertions (should not infinite loop, should act as single assertion)
  try testMatch("^*a", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("\\b*a", "a", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("a$+", "a", .{ .loc = .init(0, 1), .str = "a" });
  // 2. Sandwiched invalidating assertions 
  try testMatch("a+^b*", "b", null);
  try testMatch("a+^b*", "ab", null); 

  try testMatch("a*^b*", "ab", Match{.loc = .init(0, 0), .str = ""}); 
  try testMatch("a*$b*", "aaa", .{ .loc = .init(0, 3), .str = "aaa" });
  try testMatch("a*$b*", "aaa", .{ .loc = .init(0, 3), .str = "aaa" });
  
  // 3. Alternation loop unwinding
  try testMatch("(a|^b)+", "ab", .{ .loc = .init(0, 1), .str = "a" });
  try testMatch("(a|b$)+", "aba", .{ .loc = .init(0, 1), .str = "a" });
}

test "pzre windows newlines support" {
  const config_default: Config = .{};
  const config_multiline: Config = .{ .semantics = .{ .multiline = true } };
  const config_no_implicit: Config = .{ .semantics = .{ .never_implicit_newline = true } };

  // 1. Dot operator excludes both carriage return and line feed
  try testMatchWithConfig(".", "\r", null, config_default);
  try testMatchWithConfig(".", "\n", null, config_default);
  try testMatchWithConfig(".\r\n.", "a\r\nb", Match{ .str = "a\r\nb", .loc = .init(0, 4) }, config_default);

  // 2. Multiline ^ and $ boundaries respect \r\n sequences
  try testMatchWithConfig("^a$", "a\r\nb\r\nc", Match{ .str = "a", .loc = .init(0, 1) }, config_multiline);
  try testMatchWithConfig("^b$", "a\r\nb\r\nc", Match{ .str = "b", .loc = .init(3, 4) }, config_multiline);
  try testMatchWithConfig("^c$", "a\r\nb\r\nc", Match{ .str = "c", .loc = .init(6, 7) }, config_multiline);
  try testMatchWithConfig("^$", "a\r\n\r\nb", Match{ .str = "", .loc = .init(3, 3) }, config_multiline);

  // 3. Inverted sets and implicit newlines
  try testMatchWithConfig("[^a]+", "b\r\nc", Match{ .str = "b\r\nc", .loc = .init(0, 4) }, config_default);
  try testMatchWithConfig("[^a]+", "b\r\nc", Match{ .str = "b", .loc = .init(0, 1) }, config_no_implicit);
}
