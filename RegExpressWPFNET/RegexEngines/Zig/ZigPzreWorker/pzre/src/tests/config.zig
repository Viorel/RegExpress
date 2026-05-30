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

const Config = pzre.compile.Config;
const Set = pzre.Set;
const Range = pzre.Range;

const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;

// Standard baseline behavior to ensure proper isolation
const default_config: Config = .{};

test "pzre ignore_case" {
  const config: Config = .{
    .semantics = .{
      .ignore_case = true,
    },
  };

  // 1. Literal Matches
  try testMatchWithConfig("a", "a", Match{ .str = "a", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("a", "A", Match{ .str = "A", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("A", "a", Match{ .str = "a", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("A", "A", Match{ .str = "A", .loc = .init(0, 1) }, config);

  try testMatchWithConfig("hello", "HeLlO", Match{ .str = "HeLlO", .loc = .init(0, 5) }, config);
  try testMatchWithConfig("HeLlO", "hello", Match{ .str = "hello", .loc = .init(0, 5) }, config);
  try testMatchWithConfig("HeLlO", "HELLO", Match{ .str = "HELLO", .loc = .init(0, 5) }, config);

  try testMatchWithConfig("123a", "123A", Match{ .str = "123A", .loc = .init(0, 4) }, config);
  try testMatchWithConfig("-a-", "-A-", Match{ .str = "-A-", .loc = .init(0, 3) }, config);
  try testMatchWithConfig("\\x61", "A", Match{ .str = "A", .loc = .init(0, 1) }, config);

  // 2. Character Sets
  try testMatchWithConfig("[ab]", "A", Match{ .str = "A", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[ab]", "B", Match{ .str = "B", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[AB]", "a", Match{ .str = "a", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[aB]", "A", Match{ .str = "A", .loc = .init(0, 1) }, config);
  
  // 3. Ranges
  try testMatchWithConfig("[a-c]", "B", Match{ .str = "B", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[A-C]", "b", Match{ .str = "b", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[a-z]+", "aBcDeF", Match{ .str = "aBcDeF", .loc = .init(0, 6) }, config);
  try testMatchWithConfig("[A-Z]+", "AbCdEf", Match{ .str = "AbCdEf", .loc = .init(0, 6) }, config);

  // 4. Negated Sets (Crucial boundary)
  // [^a] must explicitly forbid 'A' from matching.
  try testMatchWithConfig("[^a]", "A", null, config);
  try testMatchWithConfig("[^a]", "a", null, config);
  try testMatchWithConfig("[^A]", "a", null, config);
  try testMatchWithConfig("[^a-c]", "B", null, config);
  try testMatchWithConfig("[^A-C]", "b", null, config);

  // 5. Hex Escapes inside Sets
  // \x61 is 'a', \x63 is 'c'
  try testMatchWithConfig("[\\x61-\\x63]", "B", Match{ .str = "B", .loc = .init(0, 1) }, config);

  // 6. Repetition Quantifiers
  try testMatchWithConfig("a+", "aAaA", Match{ .str = "aAaA", .loc = .init(0, 4) }, config);
  try testMatchWithConfig("(ab)+", "AbAbaB", Match{ .str = "AbAbaB", .loc = .init(0, 6) }, config);

  // 7. Non-Alphabetical ASCII Boundaries
  // Ensures the intersect masks didn't accidentally shift punctuation.
  // @ (64) is right before A. [ (91) is right after Z.
  // ` (96) is right before a. { (123) is right after z.
  try testMatchWithConfig("[@-[]", "@", Match{ .str = "@", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[@-[]", "`", null, config); 
  try testMatchWithConfig("[`-z]", "`", Match{ .str = "`", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[`-z]", "@", null, config); 

  try testMatchWithConfig("a", "A", null, default_config);
  try testMatchWithConfig("A", "a", null, default_config);
  try testMatchWithConfig("[a-z]", "Z", null, default_config);
}

test "pzre pat_ignore_whitespace semantics" {
  const config_all: Config = .{
    .semantics = .{
      .pat_ignore_all_whitespace = true,
    },
  };

  // Basic spaces and tabs
  try testMatchWithConfig("a b c", "abc", Match{ .str = "abc", .loc = .init(0, 3) }, config_all);
  try testMatchWithConfig("a\tb\tc", "abc", Match{ .str = "abc", .loc = .init(0, 3) }, config_all);

  // Multiline formatted patterns
  try testMatchWithConfig(
    \\a +
    \\b *
    \\c
    , "aabcc", Match{ .str = "aabc", .loc = .init(0, 4) }, config_all);

  // Escaped whitespace must still match literal whitespace
  try testMatchWithConfig("a \\  b", "a b", Match{ .str = "a b", .loc = .init(0, 3) }, config_all);
  try testMatchWithConfig("a \\n b", "a\nb", Match{ .str = "a\nb", .loc = .init(0, 3) }, config_all);

  // Standard whitespace sensitivity behavior to ensure isolation
  const strict_config: Config = .{};
  try testMatchWithConfig("a b", "a b", Match{ .str = "a b", .loc = .init(0, 3) }, strict_config);
  try testMatchWithConfig("a b", "ab", null, strict_config);

  // Inside character sets, whitespace must remain literal even when pat_ignore_whitespace is true
  try testMatchWithConfig("[a b]", " ", null, config_all);
  try testMatchWithConfig("[a b]", "b", Match{ .str = "b", .loc = .init(0, 1) }, config_all);

  const config_basic: Config = .{
    .semantics = .{
      .pat_ignore_whitespace = true,
    },
  };

  try testMatchWithConfig("a b c", "abc", Match{ .str = "abc", .loc = .init(0, 3) }, config_basic);
  try testMatchWithConfig("[a b]", " ", Match{ .str = " ", .loc = .init(0, 1) }, config_basic);
  try testMatchWithConfig("[a b]", "b", Match{ .str = "b", .loc = .init(0, 1) }, config_basic);
  try testMatchWithConfig("a \\  b", "a b", Match{ .str = "a b", .loc = .init(0, 3) }, config_basic);
}

test "pzre dotall" {
  const config: Config = .{
    .semantics = .{
      .dotall = true
    },
  };

  try testMatchWithConfig("a.b", "a\nb", Match{.str = "a\nb", .loc = .init(0, 3)}, config);
  try testMatchWithConfig("a.b", "a\nb", null, default_config);
}

test "pzre never_implicit_newline semantics" {
  const config: Config = .{
    .semantics = .{
      .never_implicit_newline = true,
    },
  };

  // 1. Inverted sets must automatically remove the newline
  try testMatchWithConfig("[^a]", "\n", null, config);
  // Should match 'b' but stop before the newline
  try testMatchWithConfig("[^a]+", "b\nc", Match{ .str = "b", .loc = .init(0, 1) }, config);

  // 2. Builtin sets must lose the newline 
  // \s defaults to [ \t\n\r\f\v]
  try testMatchWithConfig("\\s", "\n", null, config);
  // Ensure other whitespace still matches
  try testMatchWithConfig("\\s", " ", Match{ .str = " ", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("\\s", "\t", Match{ .str = "\t", .loc = .init(0, 1) }, config);

  // 3. Explicit newlines must bypass the restriction
  try testMatchWithConfig("\\n", "\n", Match{ .str = "\n", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[\n]", "\n", Match{ .str = "\n", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("[a\\n]", "\n", Match{ .str = "\n", .loc = .init(0, 1) }, config);

  try testMatchWithConfig("[^a]", "\n", Match{ .str = "\n", .loc = .init(0, 1) }, default_config);
  try testMatchWithConfig("\\s", "\n", Match{ .str = "\n", .loc = .init(0, 1) }, default_config);
}

test "pzre multiline semantics" {
  const config: Config = .{
    .semantics = .{
      .multiline = true,
    },
  };

  // ^ matching immediately after a newline
  try testMatchWithConfig("^b", "a\nb", Match{ .str = "b", .loc = .init(2, 3) }, config);
  try testMatchWithConfig("^b", "a\nb", null, default_config);

  // $ matching immediately before a newline
  try testMatchWithConfig("a$", "a\nb", Match{ .str = "a", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("a$", "a\nb", null, default_config);

  // Strict line isolation matching exactly the middle line
  try testMatchWithConfig("^b$", "a\nb\nc", Match{ .str = "b", .loc = .init(2, 3) }, config);
  try testMatchWithConfig("^b$", "a\nb\nc", null, default_config);
}

test "pzre custom sets injection" {
  // Create a digit set that only matches 0 through 5
  const custom_digit = comptime Set.init(&.{ Range.init('0', '6') });
  
  // Create a word set that only matches a through c
  const custom_word = comptime Set.init(&.{ Range.init('a', 'd') });

  const config: Config = comptime .{
    .sets = .{
      .digit_set = custom_digit,
      .word_set = custom_word,
    },
  };

  // \d should match 5, but strictly fail on 9
  try testMatchWithConfig("\\d", "5", Match{ .str = "5", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("\\d", "9", null, config);

  // \w should match b, but strictly fail on z
  try testMatchWithConfig("\\w", "b", Match{ .str = "b", .loc = .init(0, 1) }, config);
  try testMatchWithConfig("\\w", "z", null, config);

  // \b relies on word_set transitions. c is a word char, d is not.
  // Therefore, a word boundary exists between c and d.
  try testMatchWithConfig("c\\bd", "cd", Match{ .str = "cd", .loc = .init(0, 2) }, config);
}
