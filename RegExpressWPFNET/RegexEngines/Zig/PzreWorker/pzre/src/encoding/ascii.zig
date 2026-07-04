//! ASCII Windows-1252 standard (extension of ISO 8859-1)
//! Max u8 cannot be represented
const std = @import("std");
const pzre = @import("../root.zig");
const structures = pzre.structures;
const assert = std.debug.assert;
const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;
const expectError = std.testing.expectError;
const expectEqualStrings = std.testing.expectEqualStrings;

// Note u8 cannot represent the exclusive range [0, 256).
pub const Int = u8;
pub const IntegerSet = structures.integer_set.IntegerSet(Int);
pub const Range = structures.range.Range(Int);

const MAX = std.math.maxInt(Int); // end exclusive

/// Raw Byte Values
pub const V = struct {
  // Control Characters
  pub const NUL = 0x00;
  pub const TAB = 0x09;
  pub const LF  = 0x0A;
  pub const CR  = 0x0D;
  pub const ESC = 0x1B;
  pub const DEL = 0x7F;
  pub const SPACE = 0x20;

  // Punctuation & Symbols
  pub const BANG         = '!';
  pub const DQT          = '"';
  pub const HASH         = '#';
  pub const DOLLAR       = '$';
  pub const PERCENT      = '%';
  pub const AMPERSAND    = '&';
  pub const APOSTROPHE   = '\'';
  pub const LPAR         = '(';
  pub const RPAR         = ')';
  pub const ASTERISK     = '*';
  pub const PLUS         = '+';
  pub const COMMA        = ',';
  pub const MINUS        = '-';
  pub const DOT          = '.';
  pub const SLASH        = '/';
  pub const COLON        = ':';
  pub const SEMICOLON    = ';';
  pub const LESS         = '<';
  pub const EQUAL        = '=';
  pub const GREATER      = '>';
  pub const QUESTION     = '?';
  pub const AT           = '@';
  pub const LBRACKET     = '[';
  pub const BACKSLASH    = '\\';
  pub const RBRACKET     = ']';
  pub const CARET        = '^';
  pub const UNDERSCORE   = '_';
  pub const BACKTICK     = '`';
  pub const LBRACE       = '{';
  pub const PIPE         = '|';
  pub const RBRACE       = '}';
  pub const TILDE        = '~';

  // Ranges
  pub const ZERO  = '0';
  pub const NINE  = '9';
  pub const A_UPPER = 'A';
  pub const Z_UPPER = 'Z';
  pub const A_LOWER = 'a';
  pub const Z_LOWER = 'z';
};

/// ASCII Integer Sets
pub const Set = struct {
  fn r(s: u8, e: u8) Range { return .{ .start = s, .end = e }; }

  /// [0, 256)
  pub const ALL = IntegerSet.init(&.{
    r(0, MAX),
  });

  /// [0-9]
  pub const DIGIT = IntegerSet.init(&.{
    r(V.ZERO, V.NINE + 1),
  });

  /// [A-Z]
  pub const HEX_DIGIT = IntegerSet.canonizeComptime(.init(&.{
    r(V.ZERO, V.NINE + 1),
    r(V.A_UPPER, 'F' + 1),
    r(V.A_LOWER, 'f' + 1),
  }));

  /// [A-Z]
  pub const UPPER = IntegerSet.init(&.{
    r(V.A_UPPER, V.Z_UPPER + 1),
  });

  /// [a-z]
  pub const LOWER = IntegerSet.init(&.{
    r(V.A_LOWER, V.Z_LOWER + 1),
  });

  /// [a-zA-Z]
  pub const ALPHA = IntegerSet.init(&.{
    r(V.A_UPPER, V.Z_UPPER + 1),
    r(V.A_LOWER, V.Z_LOWER + 1),
  });

  /// [a-zA-Z0-9]
  pub const ALPHANUMERIC = IntegerSet.canonizeComptime(.init(&.{
    r(V.ZERO, V.NINE + 1),
    r(V.A_UPPER, V.Z_UPPER + 1),
    r(V.A_LOWER, V.Z_LOWER + 1),
  }));

  /// [a-zA-Z0-9_]
  pub const WORD = ALPHANUMERIC.unionComptime(.init(&.{
    r(V.UNDERSCORE, V.UNDERSCORE + 1)
  }));

  /// [ \t]
  pub const BLANK = IntegerSet.init(&.{
    r(V.TAB, V.TAB + 1),
    r(V.SPACE, V.SPACE + 1),
  });

  /// Including windows newlines
  /// regex '.' e.g. [^\r\n]
  pub const DOT_SET = IntegerSet.init(&.{
    r(0, 10),
    r(11, 13),
    r(14, MAX),
  });

  /// [ \t\n\v\f\r]
  pub const WHITESPACE = IntegerSet.init(&.{
    r(9, 14),
    r(V.SPACE, V.SPACE + 1),
  });

  /// [ \t\v\f]
  pub const WHITESPACE_NO_LINE_ENDINGS = WHITESPACE.subtractComptime(.init(&.{
    r(V.LF, V.LF + 1),
    r(V.CR, V.CR + 1),
  }));

  /// Control characters (0-31 and 127)
  pub const CONTROL = IntegerSet.init(&.{
    r(0, 32),
    r(127, 128),
  });

  /// Control characters and special symbols before numbers: ! " # ... /
  pub const CONTROL_SPECIAL_HEAD = IntegerSet.init(&.{
    r(0, 48),
  });

  /// Control characters and all special symbols: ! " # ... /
  /// E.g. everything but numbers and letters
  pub const CONTROL_SPECIAL_ALL = IntegerSet.init(&.{
    r(0, 48),
    r(58, 65),
    r(91, 97),
    r(123, 128),
  });

  /// all special printable symbols: ! " # ... /
  /// no whitespace
  pub const SPECIAL_SYMBOLS = IntegerSet.init(&.{
    r(33, 48),
    r(58, 65),
    r(91, 97),
    r(123, 127),
  });

  /// Visible characters (33-126)
  pub const GRAPH = IntegerSet.init(&.{
    r(33, 127),
  });

  /// Visible characters + whitespace
  pub const PRINTABLE = IntegerSet.init(&.{
    r(10, 12),
    r(32, 127),
  });

  /// Visible characters - whitespace
  pub const PRINTABLE_NOS = PRINTABLE.subtractComptime(WHITESPACE);

  /// Full 7-bit ASCII (0-127)
  pub const ASCII = IntegerSet.init(&.{
    r(0, 128),
  });

  /// Extended ASCII (128-255).
  /// Represented cleanly in u8 as [128, 255).
  pub const EXTENDED = IntegerSet.init(&.{
    r(128, 256),
  });

  /// C/Zig Identifier Start: [a-zA-Z_]
  pub const ID_START = IntegerSet.init(&.{
    r(V.A_UPPER, V.Z_UPPER + 1),
    r(V.UNDERSCORE, V.UNDERSCORE + 1),
    r(V.A_LOWER, V.Z_LOWER + 1),
  });

  /// C/Zig Identifier Body: [a-zA-Z0-9_]
  pub const ID_BODY = IntegerSet.init(&.{
    r(V.ZERO, V.NINE + 1),
    r(V.A_UPPER, V.Z_UPPER + 1),
    r(V.UNDERSCORE, V.UNDERSCORE + 1),
    r(V.A_LOWER, V.Z_LOWER + 1),
  });
};

/// Check if a string matches Zig's atomic type naming convention (u8, i32, f64, etc)
pub fn isZigAtomicTypeName(str: []const u8) bool {
  for (comptime std.meta.builtinTypeNames()) |t| {
    if (std.mem.eql(u8, t, str)) return true;
  }

  if (str.len < 2) return false;

  const prefix = str[0];
  if (prefix != 'u' and prefix != 'i') return false;

  const remainder = str[1..];

  for (remainder) |c| {
    if (!Set.DIGIT.contains(u8, c)) return false;
  }

  return true;
}

// Formats a char into its visual representation
pub fn formatChar(c: u8, buf: *[2]u8) ?[]u8 {
  // Handle standard single-character escape sequences first
  switch (c) {
    '\n' => {
      @memcpy(buf[0..2], "\\n");
      return buf[0..2];
    },
    '\r' => {
      @memcpy(buf[0..2], "\\r");
      return buf[0..2];
    },
    '\t' => {
      @memcpy(buf[0..2], "\\t");
      return buf[0..2];
    },
    0x00 => { // null
      @memcpy(buf[0..2], "\\0");
      return buf[0..2];
    },
    0x08 => { // backspace
      @memcpy(buf[0..2], "\\b");
      return buf[0..2];
    },
    0x0B => {
      @memcpy(buf[0..2], "\\v");
      return buf[0..2];
    },
    0x0C => { // form feed
      @memcpy(buf[0..2], "\\f");
      return buf[0..2];
    },
    0x1B => { // escape
      @memcpy(buf[0..2], "\\e");
      return buf[0..2];
    },
    0x07 => { // bell
      @memcpy(buf[0..2], "\\a");
      return buf[0..2];
    },
    else => {
      if (Set.PRINTABLE.contains(u8, c)) {
        buf[0] = c;
        return buf[0..1];
      }
      return null;
    },
  }
}
