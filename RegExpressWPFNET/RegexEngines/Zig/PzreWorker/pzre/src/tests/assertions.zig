const t = @import("test.zig");

const testMatch = t.testMatch;
const testMatches = t.testMatches;
const testMatchExact = t.testMatchExact;
const testMatchStart = t.testMatchStart;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testMatchWithConfig = t.testMatchWithConfig;

const testMatchMany = t.testMatchMany;
const testMatchesMany = t.testMatchesMany;
const testMatchExactMany = t.testMatchExactMany;
const testMatchStartMany = t.testMatchStartMany;
const testMatchManyWithConfig = t.testMatchManyWithConfig;
const ExpectMatch = t.ExpectMatch;
const ExpectMatches = t.ExpectMatches;
const ExpectMatchExact = t.ExpectMatchExact;
const ExpectMatchStart = t.ExpectMatchStart;

const pzre = @import("../root.zig");
const Match = pzre.regex.Match;
const Config = pzre.compile.Config;

const ml: Config = .{ .semantics = .{ .multiline = true } };
const sl: Config = .{ .semantics = .{ .multiline = false } };

// ═══════════════════════════════════════════════════════════════════════════
// ^ and $ in NON-MULTILINE MODE (== \A and \z)
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: ^ in non-multiline equals \\A (start of text only)" {
  // ^ should only match at position 0.
  try testMatchExactMany("^a", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ba", .expected = false },
  });
  try testMatchWithConfig("^bar", "foo\nbar", null, sl);
}

test "anchor: $ in non-multiline equals \\z (end of text only)" {
  try testMatchExactMany("a$", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ab", .expected = false },
  });
  try testMatchWithConfig("foo$", "foo\nbar", null, sl);
}

test "anchor: ^ and $ together require full match" {
  try testMatchesMany("^$", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchesMany("^abc$", &.{
    .{ .str = "abc", .expected = true },
    .{ .str = "abcd", .expected = false },
    .{ .str = "xabc", .expected = false },
  });
}

// ═══════════════════════════════════════════════════════════════════════════
// \A and \z (text anchors — ignore multiline)
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: \\A only matches at position 0 regardless of multiline" {
  try testMatchExactMany("\\Aa", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ba", .expected = false },
  });
  // Even with multiline, \A only matches at position 0.
  try testMatchWithConfig("\\Abar", "foo\nbar", null, ml);
  try testMatchWithConfig("\\Afoo", "foo\nbar", Match{ .str = "foo", .loc = .init(0, 3) }, ml);
}

test "anchor: \\z only matches at end of input regardless of multiline" {
  try testMatchExactMany("a\\z", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ab", .expected = false },
  });
  try testMatchWithConfig("foo\\z", "foo\nbar", null, ml);
  try testMatchWithConfig("bar\\z", "foo\nbar", Match{ .str = "bar", .loc = .init(4, 7) }, ml);
}

test "anchor: \\A\\z together require full match (multiline-insensitive)" {
  try testMatchWithConfig("\\Abar\\z", "foo\nbar\nbaz", null, ml);
  try testMatchWithConfig("\\Afoo\\nbar\\nbaz\\z", "foo\nbar\nbaz", Match{ .str = "foo\nbar\nbaz", .loc = .init(0, 11) }, ml);
}

// ═══════════════════════════════════════════════════════════════════════════
// ^ and $ in MULTILINE MODE
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: ^ in multiline matches at start AND after \\n" {
  try testMatchManyWithConfig("^bar", &.{
    .{ .str = "foo\nbar", .expected = Match{ .str = "bar", .loc = .init(4, 7) } },
  }, ml);
  try testMatchManyWithConfig("^foo", &.{
    .{ .str = "foo\nbar", .expected = Match{ .str = "foo", .loc = .init(0, 3) } },
  }, ml);
}

test "anchor: $ in multiline matches at end AND before \\n" {
  try testMatchManyWithConfig("foo$", &.{
    .{ .str = "foo\nbar", .expected = Match{ .str = "foo", .loc = .init(0, 3) } },
  }, ml);
  try testMatchManyWithConfig("bar$", &.{
    .{ .str = "foo\nbar", .expected = Match{ .str = "bar", .loc = .init(4, 7) } },
  }, ml);
}

test "anchor: ^...$ in multiline matches one complete line" {
  try testMatchManyWithConfig("^bar$", &.{
    .{ .str = "foo\nbar\nbaz", .expected = Match{ .str = "bar", .loc = .init(4, 7) } },
  }, ml);
  try testMatchManyWithConfig("^foo$", &.{
    .{ .str = "foo\nbar\nbaz", .expected = Match{ .str = "foo", .loc = .init(0, 3) } },
  }, ml);
  try testMatchManyWithConfig("^baz$", &.{
    .{ .str = "foo\nbar\nbaz", .expected = Match{ .str = "baz", .loc = .init(8, 11) } },
  }, ml);
}

test "anchor: ^$ matches at empty lines in multiline" {
  // Same pattern, same config — share the compile across all three inputs.
  try testMatchManyWithConfig("^$", &.{
    .{ .str = "a\n\nb", .expected = Match{ .str = "", .loc = .init(2, 2) } },
    .{ .str = "\n", .expected = Match{ .str = "", .loc = .init(0, 0) } },
    .{ .str = "foo\n", .expected = Match{ .str = "", .loc = .init(4, 4) } },
  }, ml);
}

test "anchor: $ matches at end-of-text with trailing newline (multiline)" {
  try testMatchManyWithConfig("foo$", &.{
    .{ .str = "foo\n", .expected = Match{ .str = "foo", .loc = .init(0, 3) } },
  }, ml);
}

test "anchor: ^ and $ NOT matching mid-line in non-multiline" {
  try testMatchWithConfig("^bar$", "foo\nbar\nbaz", null, sl);
  try testMatchWithConfig("foo$", "foo\nbar", null, sl);
  try testMatchWithConfig("^bar", "foo\nbar", null, sl);
}

// ═══════════════════════════════════════════════════════════════════════════
// findAll with anchors
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: findAll with $ and multiline yields all line ends" {
  const str = "name job id note\nmark sysadmin 123 jabroni\nsebastian sysadmin 333\ncole cook 592";
  try testFindAllMultiline("\\d+$", str, &.{
    .{ .str = "333", .loc = .init(62, 65) },
    .{ .str = "592", .loc = .init(76, 79) },
  });
}

test "anchor: findAll with \\z only yields the end-of-text match" {
  const str = "name job id note\nmark sysadmin 123 jabroni\nsebastian sysadmin 333\ncole cook 592";
  const expected: []const Match = comptime &.{
    .{ .str = "592", .loc = .init(76, 79) },
  };
  try testFindAll("\\d+\\z", str, expected, .{});
  try testFindAllMultiline("\\d+\\z", str, expected);
}

test "anchor: findAll with ^ and multiline yields all line starts" {
  const str = "name job id note\nmark sysadmin 123 jabroni\nsebastian sysadmin 333\ncole cook 592";
  try testFindAllMultiline("^\\w+", str, &.{
    .{ .str = "name", .loc = .init(0, 4) },
    .{ .str = "mark", .loc = .init(17, 21) },
    .{ .str = "sebastian", .loc = .init(43, 52) },
    .{ .str = "cole", .loc = .init(66, 70) },
  });
}

test "anchor: findAll with \\A yields only start-of-text match" {
  const str = "name job id note\nmark sysadmin 123 jabroni";
  const expected: []const Match = comptime &.{
    .{ .str = "name", .loc = .init(0, 4) },
  };
  try testFindAll("\\A\\w+", str, expected, .{});
  try testFindAllMultiline("\\A\\w+", str, expected);
}

// ═══════════════════════════════════════════════════════════════════════════
// Word boundary \b and \B
// ═══════════════════════════════════════════════════════════════════════════

test "wordbound: \\b at trailing position finds words at word/non-word transitions" {
  const str = "ab abz zab zabz ab ab";
  try testFindAll("ab\\b", str, &.{
    .{ .str = "ab", .loc = .init(0, 2) },
    .{ .str = "ab", .loc = .init(8, 10) },
    .{ .str = "ab", .loc = .init(16, 18) },
    .{ .str = "ab", .loc = .init(19, 21) },
  }, .{});
}

test "wordbound: \\B at trailing position requires word continuation" {
  const str = "ab abz zab zabz ab ab";
  try testFindAll("ab\\B", str, &.{
    .{ .str = "ab", .loc = .init(3, 5) },
    .{ .str = "ab", .loc = .init(12, 14) },
  }, .{});
}

test "wordbound: \\b at leading position" {
  const str = "ab abz zab zabz ab ab";
  try testFindAll("\\bab", str, &.{
    .{ .str = "ab", .loc = .init(0, 2) },
    .{ .str = "ab", .loc = .init(3, 5) },
    .{ .str = "ab", .loc = .init(16, 18) },
    .{ .str = "ab", .loc = .init(19, 21) },
  }, .{});
}

test "wordbound: \\B at leading position requires word preceding" {
  const str = "ab abz zab zabz ab ab";
  try testFindAll("\\Bab", str, &.{
    .{ .str = "ab", .loc = .init(8, 10) },
    .{ .str = "ab", .loc = .init(12, 14) },
  }, .{});
}

test "wordbound: \\bword\\b matches whole word" {
  const str = "ab abz zab zabz ab ab";
  try testFindAll("\\bab\\b", str, &.{
    .{ .str = "ab", .loc = .init(0, 2) },
    .{ .str = "ab", .loc = .init(16, 18) },
    .{ .str = "ab", .loc = .init(19, 21) },
  }, .{});
}

test "wordbound: single-character word with \\b on both sides" {
  // LANGUAGE.md: "End of/start of input is interpreted as a word boundary"
  try testFindAll("\\ba\\b", "a", &.{
    .{ .str = "a", .loc = .init(0, 1) },
  }, .{});
}

test "wordbound: \\B fails at start of input when first char is word char" {
  // First word char has start-of-input on its left, which is a non-word
  // boundary — so \B fails.
  try testFindAll("\\Ba", "a", &.{}, .{});
}

test "wordbound: \\B fails at end of input when last char is word char" {
  try testFindAll("a\\B", "a", &.{}, .{});
}

test "wordbound: \\b around non-word characters" {
  // \b. — needs word followed by non-word.
  try testMatch("\\b\\.", " .", null); // space . — no word before
  try testMatch("\\B\\.", " .", Match{ .str = ".", .loc = .init(1, 2) }); // non-word before non-word

  try testMatch("a\\bb", "ab", null);    // both word, no boundary between
  try testMatch("a\\Bb", "ab", Match{ .str = "ab", .loc = .init(0, 2) });

  try testMatch("a\\b\\.", "a.", Match{ .str = "a.", .loc = .init(0, 2) }); // a then .
  try testMatch("a\\B\\.", "a.", null); // word-to-non-word IS a boundary, \B fails
}

test "wordbound: \\b with anchors" {
  try testMatch("^\\b\\.$", ".", null);
  try testMatch("^\\B\\.$", ".", Match{ .str = ".", .loc = .init(0, 1) });
  try testMatch("^\\ba$", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("^\\Ba$", "a", null);
}

test "wordbound: \\b adjacent to newlines (multiline)" {
  try testMatchWithConfig("foo\\b", "foo\nbar", Match{ .str = "foo", .loc = .init(0, 3) }, ml);
  try testMatchWithConfig("\\bbar", "foo\nbar", Match{ .str = "bar", .loc = .init(4, 7) }, ml);
  try testMatchWithConfig("foo\\B", "foo\nbar", null, ml);
}

// ═══════════════════════════════════════════════════════════════════════════
// Nonsensical and self-contradicting combinations
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: \\b\\B and \\B\\b can never match (contradictory)" {
  try testMatchMany("\\b\\B", &.{
    .{ .str = "a", .expected = null },
    .{ .str = "", .expected = null },
  });
  try testMatchMany("\\B\\b", &.{
    .{ .str = "a", .expected = null },
    .{ .str = "", .expected = null },
  });
}

test "anchor: text-following-end and text-preceding-start can never match" {
  try testMatch("a^", "a", null);   // 'a' followed by start-of-input
  try testMatch("a\\A", "a", null);
  try testMatch("$a", "a", null);   // end-of-input followed by 'a'
  try testMatch("\\za", "a", null);
}

test "anchor: $^ matches empty input (both anchors satisfied)" {
  try testMatch("$^", "", Match{ .str = "", .loc = .init(0, 0) });
  try testMatch("\\z\\A", "", Match{ .str = "", .loc = .init(0, 0) });
  // In multiline, $^ across a newline boundary
  try testMatchWithConfig("$^", "\n", Match{ .str = "", .loc = .init(0, 0) }, ml);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chained and quantified assertions (must not loop)
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: repeated anchors collapse without effect" {
  try testMatch("^^a", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("a$$", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("\\A\\Aab\\z\\z", "ab", Match{ .str = "ab", .loc = .init(0, 2) });
  try testMatch("^^^", "", Match{ .str = "", .loc = .init(0, 0) });
  try testMatch("$$$", "", Match{ .str = "", .loc = .init(0, 0) });
}

test "anchor: repeated word boundaries collapse" {
  try testMatch("\\b\\ba\\b\\b", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("\\b\\B\\ba", "a", null); // contains \B between \b's
}

test "anchor: mixed text and line anchors" {
  try testMatch("^\\Aab$\\z", "ab", Match{ .str = "ab", .loc = .init(0, 2) });
  try testMatch("^\\ba\\b$", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatchWithConfig("\\Afoo$", "foo\nbar", Match{ .str = "foo", .loc = .init(0, 3) }, ml);
  try testMatchWithConfig("^bar\\z", "foo\nbar", Match{ .str = "bar", .loc = .init(4, 7) }, ml);
}

test "anchor: assertions under quantifiers must not infinite-loop" {
  // Quantified anchors behave as a single instance.
  try testMatch("^*a", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("\\b*a", "a", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("a$+", "a", Match{ .str = "a", .loc = .init(0, 1) });
}

test "anchor: sandwiched assertions invalidate the surrounding pattern" {
  // a+^b* — there's no way to have 'a's followed by start-of-input.
  try testMatchMany("a+^b*", &.{
    .{ .str = "b", .expected = null },
    .{ .str = "ab", .expected = null },
  });
  // a*^b* — only valid when a* matches zero a's and we're at start-of-input.
  try testMatch("a*^b*", "ab", Match{ .str = "", .loc = .init(0, 0) });
  // a*$b* — valid when b* matches zero b's at end-of-input.
  try testMatch("a*$b*", "aaa", Match{ .str = "aaa", .loc = .init(0, 3) });
}

test "anchor: assertions inside alternation" {
  // The alternation should let the non-anchored branch win when the anchor branch can't fire.
  try testMatch("(a|^b)+", "ab", Match{ .str = "a", .loc = .init(0, 1) });
  try testMatch("(a|b$)+", "aba", Match{ .str = "a", .loc = .init(0, 1) });
}

test "anchor: assertions in alternations with word boundaries" {
  try testMatchMany("ab\\b|mn", &.{
    .{ .str = "ab", .expected = Match{ .str = "ab", .loc = .init(0, 2) } },
    .{ .str = "mn", .expected = Match{ .str = "mn", .loc = .init(0, 2) } },
    .{ .str = "z", .expected = null },
    .{ .str = "abb", .expected = null },
    .{ .str = "ab b", .expected = Match{ .str = "ab", .loc = .init(0, 2) } },
    .{ .str = "b ab b", .expected = Match{ .str = "ab", .loc = .init(2, 4) } },
  });

  try testMatchMany("ab\\B|mn", &.{
    .{ .str = "ab", .expected = null },
    .{ .str = "mn", .expected = Match{ .str = "mn", .loc = .init(0, 2) } },
    .{ .str = "abb", .expected = Match{ .str = "ab", .loc = .init(0, 2) } },
    .{ .str = "ab b", .expected = null },
  });
}

test "anchor: ^ and $ in alternations" {
  try testMatchMany("ab$|^qp|mn", &.{
    .{ .str = "ab", .expected = Match{ .str = "ab", .loc = .init(0, 2) } },
    .{ .str = "aab", .expected = Match{ .str = "ab", .loc = .init(1, 3) } },
    .{ .str = "aaba", .expected = null },
    .{ .str = "qp", .expected = Match{ .str = "qp", .loc = .init(0, 2) } },
    .{ .str = "aqp", .expected = null },
    .{ .str = "qpa", .expected = Match{ .str = "qp", .loc = .init(0, 2) } },
    .{ .str = "amna", .expected = Match{ .str = "mn", .loc = .init(1, 3) } },
  });
}

test "anchor: \\A and \\z in alternations behave like text anchors" {
  try testMatchMany("ab\\z|\\Aqp|mn", &.{
    .{ .str = "ab", .expected = Match{ .str = "ab", .loc = .init(0, 2) } },
    .{ .str = "aab", .expected = Match{ .str = "ab", .loc = .init(1, 3) } },
    .{ .str = "aaba", .expected = null },
    .{ .str = "qp", .expected = Match{ .str = "qp", .loc = .init(0, 2) } },
    .{ .str = "aqp", .expected = null },
  });
}

// ═══════════════════════════════════════════════════════════════════════════
// Right anchoring with various trailing expressions
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: $ with simple trailing match" {
  try testMatchesMany("a$", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "ba", .expected = true },
    .{ .str = "ab", .expected = false },
    .{ .str = "bab", .expected = false },
  });
}

test "anchor: ^$ matches only empty input" {
  try testMatchesMany("^$", &.{
    .{ .str = "", .expected = true },
    .{ .str = "a", .expected = false },
  });
}

test "anchor: anchored quantifiers" {
  try testMatchesMany("a+$", &.{
    .{ .str = "baaa", .expected = true },
    .{ .str = "baab", .expected = false },
  });
  try testMatchStartMany("a+$", &.{
    .{ .str = "aaa", .expected = "aaa" },
    .{ .str = "bbaaa", .expected = null },
  });
}

test "anchor: greedy match leading to anchor" {
  try testMatchStartMany(".*a$", &.{
    .{ .str = "bba", .expected = "bba" },
    .{ .str = "bbac", .expected = null },
  });
  try testMatchStartMany("[a-z]+$", &.{
    .{ .str = "abc", .expected = "abc" },
    .{ .str = "123abc", .expected = null },
  });
}

test "anchor: anchored .* and empty matches" {
  try testMatchStartMany(".*$", &.{
    .{ .str = "aaaa", .expected = "aaaa" },
    .{ .str = "", .expected = "" },
  });
  try testMatchMany(".*$", &.{
    .{ .str = "", .expected = Match{ .str = "", .loc = .init(0, 0) } },
    .{ .str = "a", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
    .{ .str = "aaaa", .expected = Match{ .str = "aaaa", .loc = .init(0, 4) } },
  });
}

// ═══════════════════════════════════════════════════════════════════════════
// Windows newlines (\r\n)
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: dot excludes both \\r and \\n by default" {
  try testMatchManyWithConfig(".", &.{
    .{ .str = "\r", .expected = null },
    .{ .str = "\n", .expected = null },
  }, sl);
  try testMatchWithConfig(".\r\n.", "a\r\nb", Match{ .str = "a\r\nb", .loc = .init(0, 4) }, sl);
}

test "anchor: multiline ^ and $ respect \\r\\n line breaks" {
  try testMatchManyWithConfig("^a$", &.{
    .{ .str = "a\r\nb\r\nc", .expected = Match{ .str = "a", .loc = .init(0, 1) } },
  }, ml);
  try testMatchManyWithConfig("^b$", &.{
    .{ .str = "a\r\nb\r\nc", .expected = Match{ .str = "b", .loc = .init(3, 4) } },
  }, ml);
  try testMatchManyWithConfig("^c$", &.{
    .{ .str = "a\r\nb\r\nc", .expected = Match{ .str = "c", .loc = .init(6, 7) } },
  }, ml);
  // Empty line between \r\n's
  try testMatchWithConfig("^$", "a\r\n\r\nb", Match{ .str = "", .loc = .init(3, 3) }, ml);
}

test "anchor: never_implicit_newline disables implicit newline behavior" {
  const no_implicit: Config = .{ .semantics = .{ .never_implicit_newline = true } };
  const default_cfg: Config = .{};

  // With default, complement sets implicitly exclude newlines.
  try testMatchWithConfig("[^a]+", "b\r\nc", Match{ .str = "b\r\nc", .loc = .init(0, 4) }, default_cfg);
  // With never_implicit_newline, complement sets do NOT implicitly exclude newlines.
  try testMatchWithConfig("[^a]+", "b\r\nc", Match{ .str = "b", .loc = .init(0, 1) }, no_implicit);
}

// ═══════════════════════════════════════════════════════════════════════════
// matchStart and matches API semantics
// ═══════════════════════════════════════════════════════════════════════════

test "anchor: matchStart requires match at position 0" {
  try testMatchStartMany("^abc", &.{
    .{ .str = "abc", .expected = "abc" },
    .{ .str = "mabc", .expected = null },
    .{ .str = "abcn", .expected = "abc" },
    .{ .str = "mabcn", .expected = null },
    .{ .str = "", .expected = null },
  });
}

test "anchor: matches with ^ requires position 0" {
  try testMatchesMany("^abc", &.{
    .{ .str = "abc", .expected = true },
    .{ .str = "mabc", .expected = false },
    .{ .str = "abcn", .expected = true },
    .{ .str = "mabcn", .expected = false },
    .{ .str = "", .expected = false },
  });
}
