const t = @import("test.zig");

const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testMatch = t.testMatch;
const testMatchWithConfig = t.testMatchWithConfig;
const testMatchStart = t.testMatchStart;
const testMatches = t.testMatches;
const testParseError = t.testParseError;

const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;

test "pzre hex \\xNN" {
  // Valid exact matches
  try testMatch("\\x00", "\x00", Match{ .str = "\x00", .loc = .init(0, 1) });
  try testMatch("\\x09", "\x09", Match{ .str = "\x09", .loc = .init(0, 1) });
  try testMatch("\\x0a", "\x0a", Match{ .str = "\x0a", .loc = .init(0, 1) });
  try testMatch("\\x0A", "\x0a", Match{ .str = "\x0a", .loc = .init(0, 1) });
  try testMatch("\\x7f", "\x7f", Match{ .str = "\x7f", .loc = .init(0, 1) });
  try testMatch("\\x7F", "\x7f", Match{ .str = "\x7f", .loc = .init(0, 1) });
  try testMatch("\\xfe", "\xfe", Match{ .str = "\xfe", .loc = .init(0, 1) });
  try testMatch("\\xFE", "\xfe", Match{ .str = "\xfe", .loc = .init(0, 1) });

  // Valid sequences embedded in larger patterns
  try testMatch("a\\x0Ab", "a\x0Ab", Match{ .str = "a\x0Ab", .loc = .init(0, 3) });
  try testMatch("\\xfe+", "\xfe\xfe", Match{ .str = "\xfe\xfe", .loc = .init(0, 2) });

  // Valid sequences failing to match the input string
  try testMatch("\\x00", "\x01", null);
  try testMatch("\\x0A", "\r", null);
  try testMatch("\\xfe", "\x7f", null);

  // Invalid syntax triggering parser errors
  try testParseError("\\x", error.UnexpectedEof);
  try testParseError("\\xA", error.UnexpectedEof);
  try testParseError("\\xAG", error.UnexpectedToken);
  try testParseError("\\xG1", error.UnexpectedToken);
  try testParseError("\\xx", error.UnexpectedEof);
  try testParseError("\\x-1", error.UnexpectedToken);
  try testParseError("\\x 1", error.UnexpectedToken);
}
