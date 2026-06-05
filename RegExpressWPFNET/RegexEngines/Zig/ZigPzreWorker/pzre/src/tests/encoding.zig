const std = @import("std");
const t = @import("test.zig");

const testMatch = t.testMatch;
const testMatches = t.testMatches;
const testMatchExact = t.testMatchExact;
const testParseError = t.testParseError;

const pzre = @import("../root.zig");
const Match = pzre.regex.Match;

test "literal: documented escape sequences produce the claimed bytes" {
  const Pair = struct { pattern: []const u8, byte: u8 };
  const pairs = [_]Pair{
    .{ .pattern = "\\0", .byte = 0x00 },
    .{ .pattern = "\\a", .byte = 0x07 },
    .{ .pattern = "\\e", .byte = 0x1B },
    .{ .pattern = "\\f", .byte = 0x0C },
    .{ .pattern = "\\t", .byte = 0x09 },
    .{ .pattern = "\\n", .byte = 0x0A },
    .{ .pattern = "\\r", .byte = 0x0D },
    .{ .pattern = "\\v", .byte = 0x0B },
  };

  inline for (pairs) |p| {
    try testMatchExact(p.pattern, &.{p.byte}, true);
    try testMatchExact(p.pattern, &.{p.byte +% 1}, false);
    try testMatchExact(p.pattern, &.{p.byte -% 1}, false);
  }
}

test "literal: \\xNN accepts the full byte range below maxInt" {
  inline for ([_]u8{ 0x00, 0x01, 0x09, 0x0A, 0x0D, 0x1B, 0x20, 0x7F, 0x80, 0xA0, 0xC0, 0xFE }) |b| {
    const pat = comptime std.fmt.comptimePrint("\\x{X:0>2}", .{b});
    try testMatchExact(pat, &.{b}, true);
  }
}

test "literal: \\xNN is case-insensitive in hex digits" {
  try testMatch("\\x0a", "\x0a", Match{ .str = "\x0a", .loc = .init(0, 1) });
  try testMatch("\\x0A", "\x0a", Match{ .str = "\x0a", .loc = .init(0, 1) });
  try testMatch("\\xfe", "\xfe", Match{ .str = "\xfe", .loc = .init(0, 1) });
  try testMatch("\\xFE", "\xfe", Match{ .str = "\xfe", .loc = .init(0, 1) });
}

test "literal: \\xNN does not bleed into adjacent byte values" {
  try testMatch("\\x00", "\x01", null);
  try testMatch("\\x00", "\xFF", null);
  try testMatch("\\x0A", "\r", null);
  try testMatch("\\x0A", "\x0B", null);
  try testMatch("\\xfe", "\x7f", null);
  try testMatch("\\xfe", "\xfd", null);
}

test "literal: maxInt byte (0xFF) rejected by parser across all surfaces" {
  try testParseError("\\xff", error.MaxInt);
  try testParseError("\\xFF", error.MaxInt);
  try testParseError("\xff", error.MaxInt);
  try testParseError("[\xff]", error.MaxInt);
  try testParseError("[\\xff]", error.MaxInt);
  try testParseError("[^\xff]", error.MaxInt);
  try testParseError("[^\\xff]", error.MaxInt);
  try testParseError("[a-\xff]", error.MaxInt);
  try testParseError("[a-\\xff]", error.MaxInt);
  try testParseError("\\xff]", error.MaxInt);
  try testParseError("a\\xff", error.MaxInt);
  try testParseError("\\xffa", error.MaxInt);
}

test "literal: maxInt byte in input silently never matches" {
  try testMatch("\\xfa", "\xFF", null);
  try testMatch("\xfa", "\xFF", null);
  try testMatch(".", "\xFF", null);
  try testMatch("[^a]", "\xFF", null);
  try testMatch("[\\x00-\\xfe]", "\xFF", null);
  try testMatch("[^a]|a", "\xFF", null);
}

// ── Bytes near boundaries work as literals ─────────────────────────────────

test "literal: control characters can be used as raw pattern bytes" {
  // LANGUAGE.md: "This includes the zero-byte \0, as well as any other
  // control value."
  inline for ([_]u8{ 0x00, 0x01, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1B, 0x1F }) |b| {
    try testMatchExact(&.{b}, &.{b}, true);
  }
}

test "literal: high-bit-set bytes can be used as raw pattern bytes" {
  // High-byte values (0x80-0xFE) are valid pattern literals.
  inline for ([_]u8{ 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0xFE }) |b| {
    try testMatchExact(&.{b}, &.{b}, true);
  }
}

test "literal: escape sequences compose with other operators" {
  try testMatch("a\\x0Ab", "a\nb", Match{ .str = "a\nb", .loc = .init(0, 3) });
  try testMatch("\\xfe+", "\xfe\xfe\xfe", Match{ .str = "\xfe\xfe\xfe", .loc = .init(0, 3) });
  try testMatch("(\\n|\\t)+", "\n\t\n", Match{ .str = "\n\t\n", .loc = .init(0, 3) });
  try testMatch("\\0\\0", "\x00\x00", Match{ .str = "\x00\x00", .loc = .init(0, 2) });
}

test "literal: \\xNN with missing or invalid hex digits errors" {
  try testParseError("\\x", error.UnexpectedEof);
  try testParseError("\\xA", error.UnexpectedEof);
  try testParseError("\\xx", error.UnexpectedEof);
  try testParseError("\\xG1", error.UnexpectedToken);
  try testParseError("\\xAG", error.UnexpectedToken);
  try testParseError("\\x-1", error.UnexpectedToken);
  try testParseError("\\x 1", error.UnexpectedToken);
}

test "literal: quantifier operators without a preceding term error" {
  try testParseError("?", error.UnexpectedToken);
  try testParseError("+", error.UnexpectedToken);
  try testParseError("*", error.UnexpectedToken);
  try testParseError("{1}", error.UnexpectedToken);
  try testParseError("{1,2}", error.UnexpectedToken);
}

test "literal: structural delimiters in invalid positions error" {
  try testParseError("(", error.UnmatchedParenthesis);
  try testParseError(")", error.UnexpectedToken);
  try testParseError("[", error.UnexpectedEof);
  try testParseError("]", error.UnexpectedToken);
  try testParseError("{", error.UnexpectedToken);
  try testParseError("}", error.UnexpectedToken);
}

test "literal: incomplete repeat-exact braces error" {
  try testParseError("a{", error.UnexpectedEof);
  try testParseError("a{1", error.UnexpectedEof);
  try testParseError("a{1,", error.UnexpectedEof);
  try testParseError("a{,2}", error.UnexpectedToken);
  try testParseError("a{2,1}", error.InvalidRepeat);
}
