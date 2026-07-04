const std = @import("std");

const pzre = @import("../root.zig");
const Arch = pzre.Arch;
const ArchResolved = pzre.ArchResolved;
const regex = pzre.regex;
const Allocator = std.mem.Allocator;
const Io = std.Io;
const Match = pzre.Match;
const Context = pzre.arch.Context;
const Replacement = pzre.Replacement;
const Config = pzre.compile.Config;
const ManyReplacements = pzre.ManyReplacements;
const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;
const expectDeeplyEqual = pzre.lens.testing.expectDeeplyEqual;
const expectEqualStrings = std.testing.expectEqualStrings;
const expectError = std.testing.expectError;
const anyregex = pzre.anyregex;

test "Showcase: README" {
  // Compiling for a specific architecture
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{ .strategy = .bi_directional_pass }, "[A-Z][a-z_]+");
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
 
  var re2 = try regex.compile(arch, .{ .limits = .{
    .gpa_upper_bound = 1<<13,
  } }, gpa, "\\s*\\.field_name");
  defer re2.deinit(gpa);

  try ctx.update(gpa, &.{re2});

  try expectEqual(@TypeOf(re), @TypeOf(re2));
  if (re2.match(&ctx, "struct { .field_name }")) |match| {
    try expectEqualStrings(" .field_name", match.str);
  }
}

test "Showcase: RegexGeneric" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: compile comptime" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: compile and match comptime" {
  comptime {
    const arch = Arch{ .minimal_nfa = .{ .context = .compact_fixed } };

    const re = regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");
   
    var ctx = re.initContextFixed();
   
    if (re.match(&ctx, "camelCase")) |match| {
      try expectEqualStrings("Case", match.str);
    }
  }
}

test "Showcase: compiled types dont differ" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 128 }, .offset_bp = .i8 } };

  const comptime_re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");

  var re = try regex.compile(arch, .{.ast_optimizations = .initEmpty()}, gpa, "Santa Claus");
  defer re.deinit(gpa);
 
  var re2 = try regex.compile(arch, .{.semantics = .{ .multiline = true }}, gpa, "^\\s*\\[\\w+\\]");
  defer re2.deinit(gpa);
 
  try expectEqual(@TypeOf(comptime_re), @TypeOf(re));
  try expectEqual(@TypeOf(re), @TypeOf(re2));
}

test "Showcase: strategies" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{
    .strategy = .start_set_pass,
  }, "[A-Z][a-z_]+");
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: strategies 2" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{
    .strategy = .end_anchor_reverse_pass,
  }, "[A-Z][a-z_]+");
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: strategies 3" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+$");
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: optimization" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{.ast_optimizations = .initEmpty()}, "[A-Z][a-z_]+$");
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: limits" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  const re = comptime regex.compileComptime(arch, .{
    .limits = .{
      .gpa_upper_bound = 1 << 20, // how much memory can be allocated using the passed allocator
      .max_depth =  1 << 10,      // how deep the AST can be
      .max_states = 1 << 20       // how many total states the automata can require
    }
  }, "[A-Z][a-z_]+$");
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  }
}

test "Showcase: semantics" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  { // multiline
    const re = comptime regex.compileComptime(arch, .{
      .semantics = .{ .multiline = true }
    }, "^\\s*\\.\\w+");
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
   
    var field_it = re.matchIter(&ctx, 
      \\struct {
      \\  .name = ".mark",
      \\.word = "true",
      \\}
    );
   
    try expectEqualStrings("  .name", field_it.next().?.str);
    try expectEqualStrings(".word", field_it.next().?.str);
    try expectEqual(null, field_it.next());
  }
 
  { // ignore whitespace
    const re = comptime regex.compileComptime(arch, .{
      .semantics = .{ .pat_ignore_whitespace = true }
    },
      \\ ^
      \\   [A-Z]{3,9}
      \\   \s+
      \\   / [a-zA-Z0-9_./-]*
      \\   \s+
      \\   HTTP / [0-9] \. [0-9]
      \\ $
    );
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
   
    try expect(re.matches(&ctx, "POST /api/v1/users HTTP/1.1"));
    try expect(re.matches(&ctx, "GET / HTTP/1.0"));
    try expect(!re.matches(&ctx, "UPDATE / HTTP/2"));
  }
 
  { // ignore all whitespace
    const re = comptime regex.compileComptime(arch, .{
      .semantics = .{ .pat_ignore_all_whitespace = true }
    },
      \\ ^
      \\   [ A - Z ]{3,9}
      \\   \s+
      \\   / [ a-z A-Z 0-9 _ . /-]*
      \\   \s+
      \\   HTTP / [ 0-9 ] \. [ 0-9 ]
      \\ $
    );
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
   
    try expect(re.matches(&ctx, "POST /api/v1/users HTTP/1.1"));
    try expect(re.matches(&ctx, "GET / HTTP/1.0"));
    try expect(!re.matches(&ctx, "G E T / HTTP/1.0"));
    try expect(!re.matches(&ctx, "UPDATE / HTTP/2"));
  }
 
  { // ignore case
    const re = comptime regex.compileComptime(arch, .{
      .semantics = .{ .ignore_case = true }
    }, "any-?case");
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
   
    try expect(re.matchesExact(&ctx, "anyCase"));
    try expect(re.matchesExact(&ctx, "AnyCase"));
    try expect(re.matchesExact(&ctx, "any-case"));
  }

  { // dotall
    const re = comptime regex.compileComptime(arch, .{
      .semantics = .{ .dotall = true }
    }, "yeah.*fits");
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
   
    try expect(re.matchesExact(&ctx, "yeah\nthat\nfits"));
  }
 
  { // never implicit newline
    const re = comptime regex.compileComptime(arch, .{
      .semantics = .{ .never_implicit_newline = true }
    }, "[^abc]+");
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);
   
    try expect(re.matchesExact(&ctx, "Shrek"));
    try expect(!re.matchesExact(&ctx, "\nShrek"));
  }
}

test "Showcase: fixed context" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

  var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
 
  var ctx = re.initContext(undefined) catch unreachable;
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;
}

test "Showcase: fixed context compile error" {
  const gpa = std.testing.allocator;
 
  const small = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 8 } } };
  const large = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

  var re_small = try regex.compile(small, .{}, gpa, "[A-Z][a-z_]+");
  defer re_small.deinit(gpa);
  var re_large = try regex.compile(large, .{}, gpa, "[A-Z][a-z_]+");
  defer re_large.deinit(gpa);
  var ctx = re_small.initContextFixed();
 
  try expect(re_small.matches(&ctx, "Aa"));
  // try expect(re_large.matches(&ctx, "Aa")); // compile error
}

test "Showcase: fixed context runtime error" {
  const gpa = std.testing.allocator;
 
  const large = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

  const re_large = regex.compile(large, .{}, gpa, "a{65}");
  try expectError(error.ContextTooSmall, re_large);
}

test "Showcase: fixed context 2" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

  var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
 
  var ctx = re.initContextFixed();
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;
}

test "Showcase: fixed context comptime" {
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };

  comptime {
    const re = regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");
   
    var ctx = re.initContextFixed();
   
    if (re.match(&ctx, "camelCase")) |match| {
      try expectEqualStrings("Case", match.str);
    } else unreachable;
  }
}

test "Showcase: fixed context zero heap usage" {
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };
  const re = comptime regex.compileComptime(arch, .{}, "[A-Z][a-z_]+");
 
  var ctx = re.initContextFixed();
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;
}

test "Showcase: compact fixed context" {
  const arch = Arch{ .minimal_nfa = .{ .context = .compact_fixed }};
  const re = comptime regex.compileComptime(arch, .{.strategy = .start_set_pass}, "[A-Z][a-z_]+");
 
  var ctx = re.initContextFixed();

  try expectEqual(
    @TypeOf(re),
    regex.Regex(.{ .minimal_nfa = .{ .context = .{ .fixed = 3 }, .offset_bp = .i8 } }, .{})
  );
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;
}

test "Showcase: fixed context breakpoints" {
  const arch_u8_1 = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 254 } }};
  const arch_u8_2 = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 255 } }};
 
  const arch_u16 = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 256 } }};

  const re_u8_1 = comptime regex.compileComptime(arch_u8_1, .{}, "A");
  const re_u8_2 = comptime regex.compileComptime(arch_u8_2, .{}, "B");
  const re_u16 = comptime regex.compileComptime(arch_u16, .{}, "C");
 
  const ctx_u8_1 = comptime re_u8_1.initContextFixed();
  const ctx_u8_2 = comptime re_u8_2.initContextFixed();
  const ctx_u16 = comptime re_u16.initContextFixed();
 
  const incr = ctx_u8_2.sizeOf() - ctx_u8_1.sizeOf();
  try expect(ctx_u16.sizeOf() - ctx_u8_2.sizeOf() > 50 * incr);
}

test "Showcase: dynamic context" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;
}

test "Showcase: dynamic context for many" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
 
  var hex = try regex.compile(arch, .{}, gpa, "#[0-9a-fA-F]{6}");
  defer hex.deinit(gpa);
 
  var ctx = try re.initContextIncluding(gpa, &.{hex});
  defer ctx.deinit(gpa);
 
  try expect(re.matchesExact(&ctx, "Batman"));
  try expect(hex.matchesExact(&ctx, "#AAAFFF"));
}

test "Showcase: dynamic context update" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };

  var re = try regex.compile(arch, .{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  try expect(re.matchesExact(&ctx, "Batman"));

  var hex = try regex.compile(arch, .{}, gpa, "#[0-9a-fA-F]{6}");
  defer hex.deinit(gpa);
 
  try ctx.update(gpa, &.{hex});
 
  try expect(hex.matchesExact(&ctx, "#AAAFFF"));
}

test "Showcase: fixed context shareability" {
  const arch1 = Arch{.minimal_nfa = .{ .context = .{ .dynamic = .u8 } }};
  const arch2 = Arch{.minimal_nfa = .{ .context = .{ .dynamic = .u16 } }};
  const arch3 = Arch{.minimal_nfa = .{ .context = .{ .fixed = 17 } }};
  const arch4 = Arch{.minimal_nfa = .{ .context = .{ .fixed = 18 } }};
  _ = arch1;
  _ = arch2;
  _ = arch3;
  _ = arch4;
}

// -- Matching API: match and matches ------------------------------------------
 
test "Showcase: match and matches" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  // match: leftmost Match, with slice and location
  if (re.match(&ctx, "abc 123 def")) |m| {
    try expectEqualStrings("123", m.str);
    try expectEqual(@as(usize, 4), m.loc.start);
    try expectEqual(@as(usize, 7), m.loc.end);
  } else unreachable;
 
  // matches: the boolean form
  try expect(re.matches(&ctx, "abc 123 def"));
  try expect(!re.matches(&ctx, "no digits here"));
}
 
test "Showcase: matchesExact and matchStart" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[a-z]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  // matchesExact: whole string must match
  try expect(re.matchesExact(&ctx, "hello"));
  try expect(!re.matchesExact(&ctx, "hello world"));
 
  // matchStart: a match must begin at index 0; returns the matched head
  try expectEqualStrings("hello", re.matchStart(&ctx, "hello world").?);
  try expectEqual(null, re.matchStart(&ctx, " hello"));
}
 
test "Showcase: find with explicit range" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "a+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  const str = "aa bb aa";
  // only matches starting in [3, str.len]; skips the leading "aa"
  if (re.find(&ctx, str, 3, str.len)) |m| {
    try expectEqual(@as(usize, 6), m.loc.start);
    try expectEqualStrings("aa", m.str);
  } else unreachable;
}
 
// -- Matching API: iteration --------------------------------------------------
 
test "Showcase: matchIter" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[a-z_]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  var it = re.matchIter(&ctx, "123 snake_case 456 PascalCase");
  try expectEqualStrings("snake_case", it.next().?.str);
  try expectEqualStrings("ascal", it.next().?.str);
  try expectEqualStrings("ase", it.next().?.str);
  try expectEqual(null, it.next());
}
 
test "Showcase: iterator reset" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[a-z]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  var it = re.matchIter(&ctx, "ab cd ef");
  var first_pass: usize = 0;
  while (it.next()) |_| first_pass += 1;
 
  it.reset();
  var second_pass: usize = 0;
  while (it.next()) |_| second_pass += 1;
 
  try expectEqual(first_pass, second_pass);
}
 
// -- Matching API: findAll ----------------------------------------------------
 
test "Showcase: findAllAlloc" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  const matches = try re.findAllAlloc(&ctx, gpa, "1 22 333");
  defer gpa.free(matches);
 
  try expectEqual(@as(usize, 3), matches.len);
  try expectEqualStrings("1", matches[0].str);
  try expectEqualStrings("22", matches[1].str);
  try expectEqualStrings("333", matches[2].str);
}
 
test "Showcase: findAllComptime" {
  const fixed_arch = Arch{ .minimal_nfa = .{ .context = .{ .fixed = 64 } } };
 
  comptime {
    @setEvalBranchQuota(1_000_000);
    const re = regex.compileComptime(fixed_arch, .{}, "[0-9]+");
    var ctx = re.initContextFixed();
 
    const matches = re.findAllComptime(&ctx, "1 22 333");
    if (matches.len != 3) @compileError("expected three matches");
  }
}
 
// -- Matching API: replacement ------------------------------------------------
 
test "Showcase: replaceFirst" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (try re.replaceFirst(&ctx, gpa, "id 123 and 456", "N")) |result| {
    defer result.deinit(gpa);
    try expectEqualStrings("id N and 456", result.new);
  } else unreachable;
}
 
test "Showcase: replaceAll" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "[0-9]+");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  if (try re.replaceAll(&ctx, gpa, "a1 b22 c333", "#")) |result| {
    defer result.deinit(gpa);
    try expectEqualStrings("a# b# c#", result.new);
    try expectEqual(@as(usize, 3), result.count);
  } else unreachable;
}
 
test "Showcase: replaceAllWithin" {
  const gpa = std.testing.allocator;
  const arch = Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
 
  var re = try regex.compile(arch, .{}, gpa, "a");
  defer re.deinit(gpa);
 
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
 
  const str = "a a a";
  // only replace matches starting at index 2 or later
  if (try re.replaceAllWithin(&ctx, gpa, str, "X", 2, str.len)) |result| {
    defer result.deinit(gpa);
    try expectEqualStrings("a X X", result.new);
    try expectEqual(@as(usize, 2), result.count);
  } else unreachable;
}

test "Showcase: anyregex 1" {
  const gpa = std.testing.allocator;
 
  const Re = anyregex.AnyRegex(&.{
    .{ .minimal_nfa = .{ .offset_bp = .i8,  .context = .{ .dynamic = .u16 } } },
    .{ .minimal_nfa = .{ .offset_bp = .i16, .context = .{ .dynamic = .u16 } } },
  }, .{});
   
  var re = try Re.compile(.{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
}

test "Showcase: anyregex 2" {
  const gpa = std.testing.allocator;
  var fixed = try anyregex.FixedRegex(128).compile(.{}, gpa, "[A-Z][a-z_]+");
  defer fixed.deinit(gpa);
 
  var dynamic = try anyregex.DynamicRegex.compile(.{}, gpa, "[A-Z][a-z_]+");
  defer dynamic.deinit(gpa);
}

test "Showcase: anyregex 3" {
  const gpa = std.testing.allocator;
  var re = try anyregex.DynamicRegex.compile(.{}, gpa, "[A-Z][a-z_]+");
  defer re.deinit(gpa);
   
  var ctx = try re.initContext(gpa);
  defer ctx.deinit(gpa);
   
  if (re.match(&ctx, "camelCase")) |m| {
    try expectEqualStrings("Case", m.str);
  }
}

test "Showcase: anyregex 4" {
  const gpa = std.testing.allocator;
  var a = try anyregex.DynamicRegex.compile(.{}, gpa, "[A-Z][a-z_]+");
  defer a.deinit(gpa);
  var b = try anyregex.DynamicRegex.compile(.{}, gpa, "#[0-9a-fA-F]{6}");
  defer b.deinit(gpa);
   
  // a context sized to support both machines
  var ctx = try a.initContextIncluding(gpa, &.{b});
  defer ctx.deinit(gpa);
   
  try expect(a.matchesExact(&ctx, "Batman"));
  try expect(b.matchesExact(&ctx, "#AAAFFF"));
}

test "Showcase: compileOptimal" {
  const re = comptime regex.compileOptimal(.{}, "[A-Z][a-z_]+", .{ .fixed = 64 });

  var ctx = re.initContextFixed();

  if (re.match(&ctx, "camelCase")) |match| {
    try expectEqualStrings("Case", match.str);
  } else unreachable;

  const a = comptime regex.compileOptimal(.{}, "[A-Z][a-z_]+", .{ .fixed = 64 });
  const b = comptime regex.compileOptimal(.{}, "a|b|c|d|e|f", .{ .fixed = 64 });

  // a and b may or may not share a type; do not rely on either outcome
  _ = a;
  _ = b;
}

test "Showcase: Regex usage" {
  const gpa = std.testing.allocator;

  const arch = ArchResolved{
    .minimal_nfa = .{
      .context = .{ .fixed = 64 },
      .offset_bp = .i8,
    },
  };
 
  const Re = regex.Regex(arch, .{});

  {
    var re = try Re.compile(.{}, gpa, "^abc");
    defer re.deinit(gpa);
   
    try expectEqual(Re, @TypeOf(re));
   
    var ctx = try re.initContext(gpa);
    defer ctx.deinit(gpa);

    try expect(re.matches(&ctx, "abc"));
  }

  {
    const re = comptime regex.compileComptime(.{
      .minimal_nfa = .{ .context = .compact_fixed },
    }, .{}, "^abc");
   
    const Expected = regex.Regex(.{
      .minimal_nfa = .{
        .context = .{.fixed = 3},
        .offset_bp = .i8,
      },
    }, .{});
    try expectEqual(Expected, @TypeOf(re));
  }
}
