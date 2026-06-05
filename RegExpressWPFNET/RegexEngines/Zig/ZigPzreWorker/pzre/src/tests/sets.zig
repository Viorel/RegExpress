const std = @import("std");
const t = @import("test.zig");

const testMatch = t.testMatch;
const testMatchExact = t.testMatchExact;
const testParseError = t.testParseError;
const testMatchExactWithConfig = t.testMatchExactWithConfig;
const testMatchMany = t.testMatchMany;
const testMatchExactMany = t.testMatchExactMany;
const testMatchExactManyWithConfig = t.testMatchExactManyWithConfig;

const pzre = @import("../root.zig");
const Match = pzre.regex.Match;
const E = pzre.compile.parse.ParseError;
const ascii = pzre.encoding.ascii;

// ── Basic set semantics ────────────────────────────────────────────────────

test "set: single character in set matches that character" {
  try testMatchExactMany("[a]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
  try testMatchExactMany("[z]", &.{
    .{ .str = "z", .expected = true },
    .{ .str = "A", .expected = false },
  });
}

test "set: multiple characters in set match any one" {
  try testMatchExactMany("[abc]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "c", .expected = true },
    .{ .str = "d", .expected = false },
    .{ .str = "", .expected = false },
    .{ .str = "ab", .expected = false }, // single char only
  });
}

test "set: set quantified consumes multiple matching chars" {
  try testMatchExactMany("[abc]+", &.{
    .{ .str = "abc", .expected = true },
    .{ .str = "cba", .expected = true },
    .{ .str = "aaa", .expected = true },
    .{ .str = "abcabc", .expected = true },
    .{ .str = "abd", .expected = false },
  });
}

test "set: simple perl sets within sets" {
  try testMatchExactMany("[\\s]", &.{
    .{ .str = " ", .expected = true },
    .{ .str = "\t", .expected = true },
    .{ .str = "s", .expected = false },
  });
  try testMatchExactMany("[\\S]", &.{
    .{ .str = " ", .expected = false },
    .{ .str = "\t", .expected = false },
    .{ .str = "s", .expected = true },
  });
  try testMatchExactMany("[\\d]", &.{
    .{ .str = "1", .expected = true },
    .{ .str = "5", .expected = true },
    .{ .str = "d", .expected = false },
  });
  try testMatchExactMany("[\\D]", &.{
    .{ .str = "1", .expected = false },
    .{ .str = "5", .expected = false },
    .{ .str = "D", .expected = true },
  });
 
  try testMatchExactMany("[\\w]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "w", .expected = true },
  });
  try testMatchExactMany("[\\W]", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = false },
    .{ .str = "w", .expected = false },
    .{ .str = "W", .expected = false },
  });
}

test "set: perl sets within sets mixed with ranges" {
  try testMatchExactMany("[r-s\\da-b\\s]", &.{
    .{ .str = "r", .expected = true },
    .{ .str = "s", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "7", .expected = true },
    .{ .str = " ", .expected = true },
    .{ .str = "d", .expected = false },
  });
  try testParseError("[r-s\\da-\\s]", E.IllegalHyphenOperand);
  try testParseError("[r-s\\d-b\\s]", E.IllegalHyphenOperand);
}

// ── Empty set behavior ─────────────────────────────────────────────────────

test "set: empty set [] errors at parse time" {
  try testParseError("[]", E.UnexpectedEof);
  try testParseError("abc[]def", E.UnexpectedEof);
  try testParseError("([])", E.UnexpectedEof);
}

test "set: complement-of-universe [^\\d\\D] errors as EmptySet" {
  // \d matches digits, \D matches everything else, so [^\d\D] is empty.
  try testParseError("[^\\d\\D]", E.EmptySet);
  try testParseError("[^\\s\\S]", E.EmptySet);
  try testParseError("[^\\w\\W]", E.EmptySet);
}

// ── Universe and complement ────────────────────────────────────────────────

test "set: [^] universe matches any byte except maxInt" {
  // This is not valid anymore due to the ] being treated as literal
 
  // Sample various bytes including control characters and high bytes.
  // inline for ([_]u8{ 0x00, 0x09, 0x0A, 0x20, 0x41, 0x7F, 0x80, 0xFE }) |b|
  // {
  //   try testMatchExact("[^]", &.{b}, true);
  // }
}

test "set: complement excludes its members" {
  try testMatchExactMany("[^a]", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = true },
    .{ .str = "A", .expected = true },
  });
  try testMatchExactMany("[^abc]", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "b", .expected = false },
    .{ .str = "c", .expected = false },
    .{ .str = "d", .expected = true },
  });
}

// ── Dot operator ───────────────────────────────────────────────────────────

test "set: dot excludes both \\r and \\n" {
  // LANGUAGE.md: ". equivalent to [^\r\n]"
  try testMatchExactMany(".", &.{
    .{ .str = "\n", .expected = false },
    .{ .str = "\r", .expected = false },
    .{ .str = "a", .expected = true },
    .{ .str = " ", .expected = true },
    .{ .str = "\t", .expected = true },
    .{ .str = &.{0x00}, .expected = true },
    .{ .str = &.{0xFE}, .expected = true },
  });
}

test "set: dot+ stops at newline" {
  try testMatchMany(".+", &.{
    .{ .str = "abc\nxyz", .expected = Match{ .str = "abc", .loc = .init(0, 3) } },
    .{ .str = "abc\rxyz", .expected = Match{ .str = "abc", .loc = .init(0, 3) } },
  });
}

test "set: dot universe spans whole DOT_SET" {
  // Validate against the engine's own DOT_SET definition for correctness
  // independent of the documentation.
  const seq = comptime ascii.Set.DOT_SET.toSequenceComptime(u8);
  try testMatchExact(".+", seq, true);
}

test "set: dot_set is configurable" {
  // LANGUAGE.md: "character classes are comptime configurable"
  const DIGIT = ascii.Set.DIGIT;
  try testMatchExactManyWithConfig(".+", &.{
    .{ .str = "1234567890", .expected = true },
    .{ .str = "123abc123", .expected = false },
  }, .{ .sets = .{ .dot_set = DIGIT } });
}

// ── Range syntax ───────────────────────────────────────────────────────────

test "set: simple byte range" {
  try testMatchExactMany("[a-m]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "g", .expected = true },
    .{ .str = "m", .expected = true },
    .{ .str = "n", .expected = false },
    .{ .str = "A", .expected = false }, // case-sensitive
    .{ .str = "-", .expected = false }, // hyphen is operator, not literal
    .{ .str = "1", .expected = false },
  });
}

test "set: range complement" {
  try testMatchExactMany("[^a-m]", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "m", .expected = false },
    .{ .str = "n", .expected = true },
    .{ .str = "A", .expected = true },
    .{ .str = "1", .expected = true },
  });
}

test "set: multiple ranges in one set" {
  try testMatchExactMany("[a-zA-Z]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "Z", .expected = true },
    .{ .str = "1", .expected = false },
  });
  try testMatchExactMany("[a-zA-Z0-9]", &.{
    .{ .str = "5", .expected = true },
    .{ .str = " ", .expected = false },
  });
}

// ── Hyphen position rules ──────────────────────────────────────────────────

test "set: hyphen at first position is literal" {
  try testMatchExactMany("[-]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("[-a]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
}

test "set: hyphen at last position is literal" {
  try testMatchExactMany("[a-]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
  try testMatchExactMany("[a-z-]", &.{
    .{ .str = "d", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = "1", .expected = false },
  });
}

test "set: hyphen at both first and last is literal" {
  try testMatchExact("[--]", "-", true);
  try testMatchExactMany("[-a-]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
}

test "set: hyphen in complement at first position is literal" {
  try testMatchExactMany("[^-]", &.{
    .{ .str = "-", .expected = false },
    .{ .str = "a", .expected = true },
  });
}

// ── Hyphen operand rules ───────────────────────────────────────────────────

test "set: range with non-concrete operand errors" {
  // LANGUAGE.md: "Hyphens require a concrete value for each operand"
  try testParseError("[a-\\s]", E.IllegalHyphenOperand);
  try testParseError("[a-\\d]", E.IllegalHyphenOperand);
  try testParseError("[a-\\w]", E.IllegalHyphenOperand);
  try testParseError("[\\s-z]", E.IllegalHyphenOperand);
  try testParseError("[m-na-\\s]", E.IllegalHyphenOperand);
  try testParseError("[m-na-\\d]", E.IllegalHyphenOperand);
  try testParseError("[m-na-\\w]", E.IllegalHyphenOperand);
  try testParseError("[m-n\\s-z]", E.IllegalHyphenOperand);
  try testParseError("[\\sm-na-\\s]", E.IllegalHyphenOperand);
  try testParseError("[\\sm-na-\\d]", E.IllegalHyphenOperand);
  try testParseError("[\\sm-na-\\w]", E.IllegalHyphenOperand);
  try testParseError("[\\sm-n\\s-z]", E.IllegalHyphenOperand);
}

test "set: range with first >= second errors" {
  // LANGUAGE.md: "Hyphens require the first operand to be strictly lower"
  try testParseError("[c-c]", E.RedundantRange);
  try testParseError("[c-b]", E.ReversedHyphenRange);
  try testParseError("[z-a]", E.ReversedHyphenRange);
  try testParseError("[9-0]", E.ReversedHyphenRange);
}

test "set: escaped hyphen is literal regardless of position" {
  try testMatchExactMany("[a\\-z]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "z", .expected = true },
    .{ .str = "b", .expected = false }, // not a range
  });
  try testMatchExact("[\\-a]", "-", true);
  try testMatchExact("[a\\-]", "-", true);
}

// ── Closing bracket as element ─────────────────────────────────────────────

test "set: first closing bracket interpreted as element" {
  // LANGUAGE.md: "The first closing bracket in set definition syntax
  // []] or []abc] is interpreted as an element"
  try testMatchExactMany("[]]", &.{
    .{ .str = "]", .expected = true },
    .{ .str = "[", .expected = false },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("[]a]", &.{
    .{ .str = "]", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "[", .expected = false },
  });
}

test "set: escaped closing bracket also matches ]" {
  // Both [\]] and []]should match a single ']' character.
  try testMatchExactMany("[\\]]", &.{
    .{ .str = "]", .expected = true },
    .{ .str = "a", .expected = false },
  });
}

test "set: complement with leading ] includes ] as literal" {
  try testMatchExactMany("[^]a]", &.{
    .{ .str = "]", .expected = false }, // ] excluded
    .{ .str = "a", .expected = false }, // a excluded
    .{ .str = "b", .expected = true },  // everything else
  });
 
  try testMatchExactMany("[^]]", &.{
    .{ .str = "b", .expected = true },
    .{ .str = "]", .expected = false },
  });
}

// ── Deduplication ──────────────────────────────────────────────────────────

test "set: duplicate characters deduplicate (no semantic difference)" {
  // LANGUAGE.md: "[aab] is the same as [ab]"
  // Verifies via match equivalence — if dedup didn't happen, behavior would
  // be unchanged anyway, but the doc claims this is a structural property.
  try testMatchExactMany("[aab]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "c", .expected = false },
  });
  try testMatchExactMany("[aaaaa]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
}

test "set: overlapping ranges merge (no semantic difference)" {
  // LANGUAGE.md: "[a-md-z] is the same as [a-z]"
  // Same approach — verify the resulting set is correct.
  inline for ("abcdefghijklmnopqrstuvwxyz") |c| {
    try testMatchExact("[a-md-z]", &.{c}, true);
  }
  try testMatchExactMany("[a-md-z]", &.{
    .{ .str = "A", .expected = false },
    .{ .str = "0", .expected = false },
  });

  // Heavily overlapping case
  try testMatchExactMany("[0-54-9]", &.{
    .{ .str = "0", .expected = true },
    .{ .str = "3", .expected = true },
    .{ .str = "4", .expected = true },
    .{ .str = "5", .expected = true },
    .{ .str = "9", .expected = true },
    .{ .str = "a", .expected = false },
  });
}

// ── Perl character classes ─────────────────────────────────────────────────

test "set: \\d matches digits 0-9 only" {
  inline for ("0123456789") |c|
  {
    try testMatchExact("\\d", &.{c}, true);
    try testMatchExact("[\\d]", &.{c}, true);
  }
  try testMatchExactMany("\\d", &.{
    .{ .str = "a", .expected = false },
    .{ .str = " ", .expected = false },
    .{ .str = "/", .expected = false }, // boundary char before '0'
    .{ .str = ":", .expected = false }, // boundary char after '9'
  });
}

test "set: \\D matches non-digits" {
  inline for ("0123456789") |c|
  {
    try testMatchExact("\\D", &.{c}, false);
  }
  try testMatchExactMany("\\D", &.{
    .{ .str = "a", .expected = true },
    .{ .str = " ", .expected = true },
    .{ .str = "\n", .expected = true },
  });
}

test "set: \\s matches default whitespace set" {
  // Default whitespace set is [\t\n\v\f\r ] (all standard ASCII whitespace).
  try testMatchExactMany("\\s", &.{
    .{ .str = " ", .expected = true },
    .{ .str = "\t", .expected = true },
    .{ .str = "\n", .expected = true },
    .{ .str = "\r", .expected = true },
    .{ .str = "\x0B", .expected = true },  // vertical tab
    .{ .str = "\x0C", .expected = true },  // form feed
    .{ .str = "a", .expected = false },
    .{ .str = "0", .expected = false },
  });
}

test "set: \\S matches non-whitespace" {
  try testMatchExactMany("\\S", &.{
    .{ .str = " ", .expected = false },
    .{ .str = "\t", .expected = false },
    .{ .str = "a", .expected = true },
    .{ .str = "0", .expected = true },
  });
}

test "set: \\w matches word characters [a-zA-Z0-9_]" {
  inline for ("abcdefghijklmnopqrstuvwxyz") |c|
  {
    try testMatchExact("\\w", &.{c}, true);
  }
  inline for ("ABCDEFGHIJKLMNOPQRSTUVWXYZ") |c|
  {
    try testMatchExact("\\w", &.{c}, true);
  }
  inline for ("0123456789") |c|
  {
    try testMatchExact("\\w", &.{c}, true);
  }
  try testMatchExactMany("\\w", &.{
    .{ .str = "_", .expected = true },
    .{ .str = "-", .expected = false },
    .{ .str = " ", .expected = false },
    .{ .str = ".", .expected = false },
  });
}

test "set: \\W matches non-word characters" {
  try testMatchExactMany("\\W", &.{
    .{ .str = "a", .expected = false },
    .{ .str = "Z", .expected = false },
    .{ .str = "5", .expected = false },
    .{ .str = "_", .expected = false },
    .{ .str = "-", .expected = true },
    .{ .str = " ", .expected = true },
    .{ .str = ".", .expected = true },
  });
}

test "set: perl classes inside set context" {
  // Mixed perl classes and literals in a single set
  try testMatchExactMany("[\\d_]", &.{
    .{ .str = "0", .expected = true },
    .{ .str = "_", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("[\\d\\w]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "0", .expected = true },
    .{ .str = " ", .expected = false },
  });
  try testMatchExactMany("[_\\d\\w]", &.{
    .{ .str = "_", .expected = true },
    .{ .str = "\t", .expected = false },
  });
}

test "set: complement of perl-class union" {
  try testMatchExactMany("[^_\\d\\w]", &.{
    .{ .str = "_", .expected = false },
    .{ .str = "a", .expected = false },
    .{ .str = "1", .expected = false },
    .{ .str = "\t", .expected = true },
    .{ .str = " ", .expected = true },
  });
}

// ── Magic removal inside sets ──────────────────────────────────────────────

test "set: magic symbols inside set are literal" {
  // LANGUAGE.md: "Magic is removed from characters in set context"
  inline for ("*+?.{}()$|]-") |c|
  {
    testMatchExact("[" ++ @as([]const u8, &.{c}) ++ "]", &.{c}, true) catch |err|
    {
      std.debug.print("Failed on: {c}\n", .{c});
      return err;
    };
  }
}

test "set: escaping magic in set is no-op" {
  // LANGUAGE.md: "Escaping a magic symbol that was already being treated
  // as a literal in set-context does nothing"
  try testMatchExactMany("[\\*]", &.{
    .{ .str = "*", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExact("[\\+]", "+", true);
  try testMatchExact("[\\?]", "?", true);
  try testMatchExact("[\\.]", ".", true);
  // The escaped form and unescaped form behave identically.
  try testMatchExact("[*]", "*", true);
  try testMatchExact("[+]", "+", true);
}

test "set: complement of all magic chars" {
  const meta_chars = "]*+?.{}()^$[|-";
  const meta_set = "[" ++ meta_chars ++ "]";
  
  inline for (meta_chars) |c|
  {
    testMatchExact(meta_set, &.{c}, true) catch |err| {
      std.debug.print("Failed on: {c}\n", .{c});
      return err;
    };
  }

  const comp_set = "[^" ++ meta_chars ++ "]";
  
  inline for (meta_chars) |c|
  {
    testMatchExact(comp_set, &.{c}, false) catch |err| {
      std.debug.print("Failed on: {c}\n", .{c});
      return err;
    };
  }

  try testMatchExactMany(comp_set, &.{
    .{ .str = "a", .expected = true },
    .{ .str = "2", .expected = true },
  });
}

test "set: complement of all escaped magic chars" {
  // SAME AS ABOVE TEST BUT INSTEAD
  // Build the escaped inner string at comptime: "\]\*\+\?\.\{\}\(\)\^\$\[\|\-"
  const meta_chars = "]*+?.{}()^$[|-";
  const escaped_inner = comptime b: {
    var res: [meta_chars.len * 2]u8 = undefined;
    for (meta_chars, 0..) |c, i| {
      res[i * 2] = '\\';
      res[i * 2 + 1] = c;
    }
    break :b res;
  };
  const meta_set = "[" ++ escaped_inner ++ "]";
  
  inline for (meta_chars) |c|
  {
    testMatchExact(meta_set, &.{c}, true) catch |err| {
      std.debug.print("Failed on (positive): {c}\n", .{c});
      return err;
    };
  }

  const comp_set = "[^" ++ escaped_inner ++ "]";
  
  inline for (meta_chars) |c|
  {
    testMatchExact(comp_set, &.{c}, false) catch |err| {
      std.debug.print("Failed on (negative): {c}\n", .{c});
      return err;
    };
  }

  try testMatchExactMany(comp_set, &.{
    .{ .str = "a", .expected = true },
    .{ .str = "2", .expected = true },
  });
}

test "set: RE2 backslash escape semantics" {
  try testMatchExact("\\\\", "\\", true);
  try testMatchExactMany("[\\\\]", &.{
    .{ .str = "\\", .expected = true },
    .{ .str = "a", .expected = false },
  });
  try testMatchExactMany("[\\]]", &.{
    .{ .str = "]", .expected = true },
    .{ .str = "\\", .expected = false },
  });

  try testMatchExactMany("[a\\-z]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = "z", .expected = true },
    .{ .str = "m", .expected = false },
  });

  try testMatchExactMany("[\\^a]", &.{
    .{ .str = "^", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
}

// ── Invalid escapes inside sets ────────────────────────────────────────────

test "set: assertion escapes (\\b, \\B, \\A, \\z) error inside set" {
  // Word boundaries and text anchors are not valid set members.
  try testParseError("[\\b]", E.UnexpectedToken);
  try testParseError("[\\B]", E.UnexpectedToken);
  try testParseError("[\\A]", E.UnexpectedToken);
  try testParseError("[\\z]", E.UnexpectedToken);
}

test "set: unknown escape errors inside set" {
  // Letters that don't correspond to a known escape produce a parse error.
  try testParseError("[\\o]", E.UnexpectedToken);
  try testParseError("[\\q]", E.UnexpectedToken);
  try testParseError("[\\y]", E.UnexpectedToken);
}

test "set: comma never needs escaping (and escape is invalid)" {
  // The comma is not a magic symbol in set context, so escaping it errors.
  try testParseError("[\\,]", E.UnexpectedToken);
}

test "set: incomplete escape at set end errors" {
  try testParseError("[\\\\", E.UnexpectedEof); // unclosed set after backslash
  try testParseError("[\\", E.UnexpectedEof);   // dangling escape
}

// ── Set with caret in non-leading position ─────────────────────────────────

test "set: caret outside leading position is literal" {
  // Only [^...] is complement;
  // [...^...] treats ^ as literal.
  try testMatchExactMany("[a^]", &.{
    .{ .str = "^", .expected = true },
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = false },
  });
  try testMatchExactMany("[\\^]", &.{
    .{ .str = "^", .expected = true },
    .{ .str = "a", .expected = false },
  });
}

// ── Real-world composite patterns ──────────────────────────────────────────

test "set: identifier pattern (composite of multiple set forms)" {
  const id_pattern = "[A-Za-z_]+[-A-Za-z_0-9]*";
  try testMatchExactMany(id_pattern, &.{
    .{ .str = "snake_case", .expected = true },
    .{ .str = "SCREAMING_CASE", .expected = true },
    .{ .str = "kebab-case", .expected = true },
    .{ .str = "camelCase", .expected = true },
    .{ .str = "PascalCase", .expected = true },
    .{ .str = "Pascal123Case", .expected = true },
    .{ .str = "__private_field", .expected = true },
    .{ .str = "1id", .expected = false }, // can't start with digit
  });
}

test "set: whitespace-followed-by-identifier composite" {
  const id_pattern = "[A-Za-z_]+[-A-Za-z_0-9]*";
  const pat = "\\s+\\." ++ id_pattern;
  try testMatchExactMany(pat, &.{
    .{ .str = "    .indented_field", .expected = true },
    .{ .str = "\t\t.indented_field", .expected = true },
    .{ .str = ".indented_field", .expected = false }, // needs leading \s
    .{ .str = "    indented_field", .expected = false }, // needs dot
  });

  const pat_optional_ws = "\\s*\\." ++ id_pattern;
  try testMatchExactMany(pat_optional_ws, &.{
    .{ .str = ".indented_field", .expected = true },
    .{ .str = "\t\t.indented_field", .expected = true },
  });
}

test "set: hyphen as a valid range boundary" {
  // Ascii 45 to 48
  try testMatchExactMany("[--0]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = ".", .expected = true },
    .{ .str = "0", .expected = true },
    .{ .str = "1", .expected = false },
  });
  // Ascii 43 to 45
  try testMatchExactMany("[+--]", &.{
    .{ .str = "+", .expected = true },
    .{ .str = ",", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = ".", .expected = false },
  });
}

test "set: set as a valid range boundary" {
  // Ascii 45 to 48
  try testMatchExactMany("[]-a]", &.{
    .{ .str = "]", .expected = true },
    .{ .str = "^", .expected = true },
    .{ .str = "[", .expected = false },
    .{ .str = "a", .expected = true },
    .{ .str = "\\", .expected = false },
    .{ .str = "b", .expected = false },
  });
  // Ascii 43 to 45
  try testMatchExactMany("[+--]", &.{
    .{ .str = "+", .expected = true },
    .{ .str = ",", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = ".", .expected = false },
  });
}

test "set: negated leading hyphen mixed with ranges" {
  try testMatchExactMany("[^-a-c]", &.{
    .{ .str = "-", .expected = false },
    .{ .str = "a", .expected = false },
    .{ .str = "c", .expected = false },
    .{ .str = "d", .expected = true },
    .{ .str = "+", .expected = true },
  });
}

test "set: escaped closing bracket in negated class" {
  try testMatchExactMany("[^\\]]", &.{
    .{ .str = "]", .expected = false },
    .{ .str = "a", .expected = true },
  });
}

test "set: unclosed and truncated sets error" {
  try testParseError("[", E.UnexpectedEof);
  try testParseError("[^", E.UnexpectedEof);
  try testParseError("[a-", E.UnexpectedEof);
}

test "set: exhaustive hyphen operand ambiguity and chaining" {
  // [a-b-c] is ambiguous (range a-b, then orphan -, then c) — engine errors.
  try testParseError("[a-b-c]", E.IllegalHyphenChain);
  try testParseError("[a-z-0-9]", E.IllegalHyphenChain); 
  try testParseError("[a--c]", E.ReversedHyphenRange); 

  try testMatchExactMany("[!--c]", &.{
    .{ .str = "!", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = "#", .expected = true },
    .{ .str = " ", .expected = false },
    .{ .str = "c", .expected = true },
    .{ .str = "b", .expected = false },
  });

  try testMatchExactMany("[a-bc-d]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "c", .expected = true },
    .{ .str = "d", .expected = true },
    .{ .str = "-", .expected = false },
  });

  try testMatchExactMany("[-a-c]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "b", .expected = true },
  });

  try testMatchExactMany("[a-c-]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = "b", .expected = true },
  });

  try testMatchExact("[-a-c-]", "-", true);

  try testParseError("[---]", error.RedundantRange); 

  try testMatchExactMany("[a-b\\-c]", &.{
    .{ .str = "a", .expected = true },
    .{ .str = "b", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = "c", .expected = true },
  });

  try testMatchExactMany("[ \\-\\-]", &.{
    .{ .str = " ", .expected = true },
    .{ .str = "-", .expected = true },
    .{ .str = "\\", .expected = false },
    .{ .str = "*", .expected = false },
    .{ .str = ".", .expected = false },
  });
  
  try testMatchExactMany("[\\--0]", &.{
    .{ .str = "-", .expected = true },
    .{ .str = ".", .expected = true },
    .{ .str = "0", .expected = true },
    .{ .str = "1", .expected = false },
  });
 
  try testParseError("[a-", error.UnexpectedEof);
}
