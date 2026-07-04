const std = @import("std");
const assert = std.debug.assert;

const pzre = @import("../root.zig");

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

const Semantics = pzre.compile.Semantics;

const lens = pzre.lens;
const expectDeeplyEqual = lens.testing.expectDeeplyEqual;

/// Precedence does not matter. Other than treating char as the default case (any non matched printable)
pub const TokenType = enum {
  // Operators
  perl_set,                // \d \D \s \S \w \W
  assert_escape_sequence,  // \b \B \A \z
  escape_sequence,         // \a \f \t \n \r \v \\ \* \+ \? \{ \} \| \. \[ \( \) \] \- \^ \$ \0 \e
  repeat_sym,              // * + ?
  hex_sequence,            // \xNN         exactly 2 integers

  hyphen,                  // -
  pipe,                    // |
  any,                     // .
  caret,                   // ^
  dollar,                  // $
  comma,                   // ,
  set_start, set_end,      // [ ]
  lpar, rpar,              // ( )
  lbrace, rbrace,          // { }

  digit,                   // 0-9

  unexpected_eof,          // Error type
  not_recognized,          // Error type

  char,                    // Default case
};

pub const perl_set_letters_string = "dDsSwW";
const perl_set_letters = Set.fromSliceComptime(u8, perl_set_letters_string);
const ass_escape_sequence_letters = Set.fromSliceComptime(u8, "bBAz");
pub const escape_sequence_letters_string = "aftnrv\\*+?{}|.[()]-^$0e ";
const escape_sequence_letters = Set.fromSliceComptime(u8, escape_sequence_letters_string);

const digits = ascii.Set.DIGIT;
const default_set = ascii.Set.PRINTABLE;
const repeat_symbols = Set.fromSliceComptime(u8, "+*?");

fn decodeEscapeSequence(lexeme: *const [2]u8) TokenType {
  assert(lexeme[0] == '\\');
  const c = lexeme[1];
  return if (escape_sequence_letters.contains(u8, c)) .escape_sequence
  else if (ass_escape_sequence_letters.contains(u8, c)) .assert_escape_sequence
  else if (perl_set_letters.contains(u8, c)) .perl_set
  else if ('x' == c) .hex_sequence
  else .not_recognized;
}

fn makeToken(location: usize, lexeme: []const u8, t: TokenType) Token {
  return .{
    .location = location,
    .lexeme = lexeme,
    .type = t,
  };
}

pub const Token = struct {
  lexeme: []const u8,
  type: TokenType,
  location: usize,
};

pub fn Lexer(comptime semantics: Semantics) type {
  return struct {
    pattern: []const u8 = "",
    head: usize = 0,
    inside_set: bool = false,

    const Self = @This();
    pub const empty: Self = .{};

    pub inline fn reset(self: *Self) void {
      self.head = 0;
    }

    pub fn init(pattern: []const u8) Self {
      return .{
        .pattern = pattern,
      };
    }

    pub fn peek(self: *Self) ?Token {
      const loc = self.head;
      const was_inside = self.inside_set;
      defer {
        self.head = loc;
        self.inside_set = was_inside;
      }
      return self.next();
    }

    pub fn next(self: *Self) ?Token {
      @setEvalBranchQuota(1_000_000);
      
      if (comptime semantics.pat_ignore_all_whitespace) {
        while (self.head < self.pattern.len and std.ascii.isWhitespace(self.pattern[self.head])) {
          self.head += 1;
        }
      } else if (comptime semantics.pat_ignore_whitespace) {
        if (!self.inside_set) {
          while (self.head < self.pattern.len and std.ascii.isWhitespace(self.pattern[self.head])) {
            self.head += 1;
          }
        }
      }

      if (self.head >= self.pattern.len) return null;
      
      const remaining = self.pattern.len - self.head;
      const c = self.pattern[self.head];
      const cptr: [*]const u8 = @ptrCast(&self.pattern[self.head]);
      
      if (c == '\\') {
        if (remaining == 1) {
          defer self.head += 1;
          return makeToken(self.head, cptr[0..1], .unexpected_eof);
        }
        const lexeme = cptr[0..2];
        const t = decodeEscapeSequence(lexeme);

        if (t == .hex_sequence) {
          if (remaining < 4) {
            defer self.head += remaining;
            return makeToken(self.head, cptr[0..remaining], .unexpected_eof);
          }
          if (!std.ascii.isHex(cptr[2]) or !std.ascii.isHex(cptr[3])) {
            defer self.head += 4;
            return makeToken(self.head, cptr[0..4], .not_recognized);
          }
          defer self.head += 4;
          return makeToken(self.head, cptr[0..4], t);
        } else {
          defer self.head += 2;
          return makeToken(self.head, lexeme, t);
        }
      } else {
        defer self.head += 1;
        const t: TokenType = if (repeat_symbols.contains(u8, c)) .repeat_sym
          else if (c == '-') .hyphen
          else if (c == '|') .pipe
          else if (c == '.') .any
          else if (c == ',') .comma
          else if (c == '^') .caret
          else if (c == '$') .dollar
          else if (c == '[') .set_start
          else if (c == ']') .set_end
          else if (c == '(') .lpar
          else if (c == ')') .rpar
          else if (c == '{') .lbrace
          else if (c == '}') .rbrace
          else if (digits.contains(u8, c)) .digit
          else .char;
        const char = cptr[0..1];

        if (comptime semantics.pat_ignore_whitespace) {
          if (t == .set_start) self.inside_set = true;
          if (t == .set_end) self.inside_set = false;
        }

        return makeToken(self.head, char, t);
      }
    }
  };
}

const L = Lexer(.{});

test "regex lex" {
  const pattern = "\\d\\n*-.|^,[](){0}a\\q\\a$\\b\\B\\A\\z\\x0A";
    
  const expected = [_]Token{
    makeToken(0, "\\d", .perl_set),
    makeToken(2, "\\n", .escape_sequence),
    makeToken(4, "*", .repeat_sym),
    makeToken(5, "-", .hyphen),
    makeToken(6, ".", .any),
    makeToken(7, "|", .pipe),
    makeToken(8, "^", .caret),
    makeToken(9, ",", .comma),
    makeToken(10, "[", .set_start),
    makeToken(11, "]", .set_end),
    makeToken(12, "(", .lpar),
    makeToken(13, ")", .rpar),
    makeToken(14, "{", .lbrace),
    makeToken(15, "0", .digit),
    makeToken(16, "}", .rbrace),
    makeToken(17, "a", .char),
    makeToken(18, "\\q", .not_recognized),
    makeToken(20, "\\a", .escape_sequence),
    makeToken(22, "$", .dollar),
    makeToken(23, "\\b", .assert_escape_sequence),
    makeToken(25, "\\B", .assert_escape_sequence),
    makeToken(27, "\\A", .assert_escape_sequence),
    makeToken(29, "\\z", .assert_escape_sequence),
    makeToken(31, "\\x0A", .hex_sequence),
  };
  var lexer = L.init(pattern);
  try expectDeeplyEqual(expected, &lexer);
}

test "lexer trailing backslash eof" {
  const pattern = "a\\";
  const expected = [_]Token{
    makeToken(0, "a", .char),
    makeToken(1, "\\", .unexpected_eof),
  };

  var lexer = L.init(pattern);
  try expectDeeplyEqual(expected, &lexer);
}

test "lexer invalid hex characters" {
  const pattern = "\\xG1\\x1G";
  const expected = [_]Token{
    makeToken(0, "\\xG1", .not_recognized),
    makeToken(4, "\\x1G", .not_recognized),
  };

  var lexer = L.init(pattern);
  try expectDeeplyEqual(expected, &lexer);
}

test "lexer premature eof on hex sequence" {
  const pattern = "\\x1";
  const expected = [_]Token{
    makeToken(0, "\\x1", .unexpected_eof),
  };

  var lexer = L.init(pattern);
  try expectDeeplyEqual(expected, &lexer);
}
