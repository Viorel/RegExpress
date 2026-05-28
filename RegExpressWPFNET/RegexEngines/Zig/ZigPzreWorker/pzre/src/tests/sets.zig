const std = @import("std");
const t = @import("test.zig");

const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testParseError = t.testParseError;
const testMatchExactWithConfig = t.testMatchExactWithConfig;
const expectEqual = std.testing.expectEqual;
const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;
const E = pzre.parse.ParseError;
const compile = pzre.compile;
const expect = std.testing.expect;

const ascii = pzre.encoding.ascii;

test "pzre sets" {
  try testParseError("[]", E.UnexpectedEof);
  try testParseError("abc[]def", E.UnexpectedEof);
  try testParseError("abc[]def", E.UnexpectedEof);
  try testParseError("[^\\d\\D]", E.EmptySet);

  try testMatchExact("[b]", "b", true);
  try testMatchExact("[b]", "a", false);
  try testMatchExact("[b]", "c", false);
  try testMatchExact("[abc]", "", false);
  try testMatchExact("[abc]", "a", true);
  try testMatchExact("[abc]", "b", true);
  try testMatchExact("[abc]", "c", true);
  try testMatchExact("[abc]", "d", false);
  try testMatchExact("[abc]", "ab", false);
  try testMatchExact("[abc]+", "cba", true);
  try testMatchExact("[abc]", "abc", false);
}

test "pzre set ranges" {
  try testMatchExact("[a-m]", "-", false);
  try testMatchExact("[a-m]", "a", true);
  try testMatchExact("[a-m]", "A", false);
  try testMatchExact("[a-m]", "c", true);
  try testMatchExact("[a-m]", "m", true);
  try testMatchExact("[a-m]", "n", false);
  try testMatchExact("[a-m]", "1", false);

  try testMatchExact("[^a-m]", "a", false);
  try testMatchExact("[^a-m]", "A", true);
  try testMatchExact("[^a-m]", "c", false);
  try testMatchExact("[^a-m]", "m", false);
  try testMatchExact("[^a-m]", "n", true);
  try testMatchExact("[^a-m]", "1", true);

  // hyphen edge cases
  try testMatchExact("[--]", "-", true);
  try testMatchExact("[-]", "-", true);
  try testMatchExact("[-]", "", false);
  try testMatchExact("[-a-]", "-", true);
  try testMatchExact("[-a-]", "a", true);
  try testMatchExact("[-a-]", "", false);
  try testMatchExact("[-a]", "-", true);
  try testMatchExact("[-a]", "a", true);
  try testMatchExact("[a-]", "-", true);
  try testMatchExact("[a-]", "a", true);
  try testMatchExact("[a-z-]", "d", true);
  try testMatchExact("[a-z-]", "-", true);

  try testParseError("[a-\\s]", error.UnexpectedToken);
  try testParseError("[a-b-c]", error.UnexpectedToken);
  try testParseError("[a--c]", error.UnexpectedToken);
  try testParseError("[c-b]", error.UnexpectedToken);
  try testParseError("[b-b]", error.UnexpectedToken);

  // overlapping
  try testMatchExact("[0-54-9]", "0", true);
  try testMatchExact("[0-54-9]", "9", true);
  try testMatchExact("[0-54-9]", "5", true);
  try testMatchExact("[0-54-9]", "4", true);
  try testMatchExact("[0-54-9]", "3", true);
  try testMatchExact("[0-54-9]", "8", true);
  try testMatchExact("[0-54-9]", "a", false);
}

test "pzre sets unescaped closing bracket" {
  try testMatchExact("[]]", "]", true);
  try testMatchExact("[]]", "[", false);
  try testMatchExact("[]a]", "]", true);
  try testMatchExact("[]a]", "[", false);
  try testMatchExact("[]a]", "a", true);
  try testParseError("[]", error.UnexpectedEof);
}

test "pzre sets escaping" {
  const gpa = std.testing.allocator;
  // magic escape
  const meta_chars = "-*+?.{}()^$[|";
  const meta_set_escaped = "\\-\\*\\+\\?\\.\\{\\}\\(\\)\\^\\$\\[\\|\\\\"; // added backslash to the end

  var re_meta_chars = try compile.nfa(.{}, gpa, "[" ++ meta_chars ++ "]");
  defer re_meta_chars.deinit(gpa);

  var re_meta_chars_escaped = try compile.nfa(.{}, gpa, "[" ++ meta_set_escaped ++ "]");
  defer re_meta_chars_escaped.deinit(gpa);

  inline for (meta_chars) |c| {
    var ctx = try re_meta_chars.initContextIncluding(gpa, &.{re_meta_chars_escaped});
    defer ctx.deinit(gpa);

    try expect(re_meta_chars.matchesExact(&ctx, &.{c}));
    try expect(re_meta_chars_escaped.matchesExact(&ctx, &.{c}));
  }

  // complement should work even for complex sets
  const meta_set_escaped_complement = "[^" ++ meta_set_escaped ++ "]";
  try testMatchExact(meta_set_escaped_complement, "+", false);
  try testMatchExact(meta_set_escaped_complement, "2", true);

  // set specific invalid escape sequences
  try testParseError("[\\b]", error.UnexpectedToken);
  try testParseError("[\\B]", error.UnexpectedToken);
  try testParseError("[\\A]", error.UnexpectedToken);
  try testParseError("[\\z]", error.UnexpectedToken);
  try testParseError("[\\o]", error.UnexpectedToken); // random letter

  try testParseError("[\\,]", error.UnexpectedToken); // comma never escaped
  try testParseError("[\\\\", error.UnexpectedEof); // no end
  try testParseError("[\\", error.UnexpectedEof); // end escaped
  try testParseError("\\", error.UnexpectedEof); // end escaped

  try testMatchExact("\\[", "[", true); // start escaped

  // perl sets
  try testMatchExact("[\\d]", "a", false);
  try testMatchExact("[\\d]", "2", true);
  try testMatchExact("[\\D]", "a", true);
  try testMatchExact("[\\D]", "2", false);

  try testMatchExact("[\\s]", "a", false);
  try testMatchExact("[\\s]", " ", true);
  try testMatchExact("[\\S]", "a", true);
  try testMatchExact("[\\S]", " ", false);

  try testMatchExact("[\\w]", "a", true);
  try testMatchExact("[\\w]", " ", false);
  try testMatchExact("[\\W]", "a", false);
  try testMatchExact("[\\W]", " ", true);

  // range escape
  try testMatchExact("[a\\-z]", "-", true);
  try testMatchExact("[a\\-z]", "a", true);
  try testMatchExact("[a\\-z]", "z", true);
  try testMatchExact("[a\\-z]", "b", false);

  // complement escape
  try testMatchExact("[\\^]", "^", true);
  try testMatchExact("[\\^]", "1", false);

  // immediate escaped set end
  try testMatchExact("[\\]]", "]", true);

  // unionized multiclass
  const mixed_class = "[_\\d\\w]";
  try testMatchExact(mixed_class, "1", true);
  try testMatchExact(mixed_class, "a", true);
  try testMatchExact(mixed_class, "_", true);
  try testMatchExact(mixed_class, "\t", false);

  // complement multiclass
  const mixed_class_complement = "[^_\\d\\w]";
  try testMatchExact(mixed_class_complement, "1", false);
  try testMatchExact(mixed_class_complement, "a", false);
  try testMatchExact(mixed_class_complement, "_", false);
  try testMatchExact(mixed_class_complement, "\t", true);
  
  // hyphen negation
  try testMatchExact("[^-]", "-", false);
  try testMatchExact("[^-]", "a", true);
}

// Not possible atm
// 
// test "pzre byte boundaries" {
//   // byte boundaries. data type is a byte, due to end exclusivity, "\xff" not possible
//   try testMatchExact("[\x00\xfe]", "\x00", true);
//   try testMatchExact("[\x00\xff]", "\xff", true);
//   try testMatchExact("[\x00\xfe]", "\xaa", false);
//   try testMatchExact("[\x00-\xfe]", "\x00", true);
//   try testMatchExact("[\x00-\xfe]", "\xaa", true);
//   try testMatchExact("[\x00-\xfe]", "\xfe", true);
//   try testMatchExact("[^\x00]", "\x00", false);
//   try testMatchExact("[^\x00]", "\xaa", true);
// }

test "pzre sets id pattern" {
  const id_pattern = "[A-Za-z_]+[-A-Za-z_0-9]*";

  // identifiers
  try testMatchExact(id_pattern, "snake_case", true);
  try testMatchExact(id_pattern, "SCREAMING_CASE", true);
  try testMatchExact(id_pattern, "kebab-case", true);
  try testMatchExact(id_pattern, "camelCase", true);
  try testMatchExact(id_pattern, "PascalCase", true);
  try testMatchExact(id_pattern, "Pascal123Case", true);
  try testMatchExact(id_pattern, "__private_field", true);
  try testMatchExact(id_pattern, "1id", false);

  // whitespace
  try testMatchExact("\\s+\\." ++ id_pattern, "    .indented_field", true);
  try testMatchExact("\\s+\\." ++ id_pattern, "    indented_field", false);
  try testMatchExact("\\s+\\." ++ id_pattern, ".indented_field", false);
  try testMatchExact("\\s+\\." ++ id_pattern, "indented_field", false);
  // by default, the whitespace set contains only tabulators and empty spaces
  try testMatchExact("\\s+\\." ++ id_pattern, " \n\r  .indented_field", true);
  try testMatchExact("\\s+\\." ++ id_pattern, "\t\t.indented_field", true);
  try testMatchExact("\\s*\\." ++ id_pattern, "\t\t.indented_field", true);
  try testMatchExact("\\s*\\." ++ id_pattern, ".indented_field", true);
}

test "pzre dot (universe)" {
  // NOTE: dont allow this exhaustive check for unicode

  const seq = comptime ascii.Set.DOT_SET.toSequenceComptime(u8);
  try testMatchExact(".+", seq, true);
  try testMatchExact("[^]+", seq, true);

  try testMatchExact("1+", "1", true);
  try testMatchExact(".+", "\n", false);
  try testMatchExact(".+", "10", true);

  // see if dot_set change is respected
  const DIGIT = ascii.Set.DIGIT;

  try testMatchExactWithConfig(".+", "123abc123", false, .{ .sets = .{ .dot_set = DIGIT } });
  try testMatchExactWithConfig(".+", "1234567890", true, .{ .sets = .{ .dot_set = DIGIT } });
}
