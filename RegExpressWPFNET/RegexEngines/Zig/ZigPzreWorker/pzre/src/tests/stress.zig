const t = @import("test.zig");
const std = @import("std");
const Config = pzre.compile.Config;
const compile = pzre.compile;
const lexer = pzre.lexer;

const testMatchExact = t.testMatchExact;
const testFindAll = t.testFindAll;
const testFindAllMultiline = t.testFindAllMultiline;
const testMatch = t.testMatch;
const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;
const assert = std.debug.assert;

test "pzre crash attempt" {
  const gpa = std.testing.allocator;
  const config: Config = .{ .semantics = .{} };

  const malicious_patterns = &[_][]const u8{
    // Structural imbalances
    "(", ")", "(()", "())", "a(b", "b)a",
    "[", "]", "[]", "[^]", "[a-", "[-a]", "[a-b-c]",
    "{", "}", "a{", "a}", "a{1", "a{1,", "a{1,2", "a{,2}",

    // Dangling and compounded modifiers
    "*", "+", "?", "a**", "a+*", "a?+", "(*)", "(|)*", 
    
    // Inverted bounds
    "a{2,1}",

    // Escaping bounds
    "\\", "a\\", "[\\", "\\q", "\\x", "\\u",

    // Alternation edge cases
    "|", "a|", "|a", "a||b", "(|)",

    // Integer boundary attacks
    "a{256}",
    "a{99999999999999999999999999}",

    // Syntactic gibberish
    "([{\\+*?^$|",
    "*+?{}[]()|\\",
  };

  for (malicious_patterns) |pattern| {
    if (compile.nfa(config, gpa, pattern)) |*nfa_obj| {
      @constCast(nfa_obj).deinit(gpa);
    } else |_| {}
  }
}

test "pzre crash attempt (encoding)" {
  const gpa = std.testing.allocator;
  const config: Config = .{ .semantics = .{} };

  const encoding_patterns = &[_][]const u8{
    // Embedded null bytes
    "\x00",
    "a\x00b",
    "\x00" ** 10000,
    
    // Truncated escape sequences at the end of the slice
    "\\x",
    "\\u",
    "\\u{",
    "\\u{10FFFF",
    
    // Invalid UTF-8 bytes (e.g. lone continuation bytes or invalid headers)
    "\xFF",
    "\x80\x80",
    "[\xFF-\xFF]",
  };

  for (encoding_patterns) |pattern| {
    if (compile.nfa(config, gpa, pattern)) |*nfa_obj| {
      @constCast(nfa_obj).deinit(gpa);
    } else |_| {}
  }
}

test "pzre crash attempt (sets)" {
  const gpa = std.testing.allocator;
  const config: Config = .{ .semantics = .{} };

  const set_patterns = &[_][]const u8{
    // Reversed ranges (should fail cleanly, not panic)
    "[z-a]",
    
    // Unescaped brackets and dashes in boundary positions
    "[]]",
    "[-a]",
    "[a-]",
    "[---]",
    "[^]]",
    
    // Overlapping or redundant ranges
    "[a-zA-Z0-9a-z]",
    
    // Empty inversions
    "[^]",
  };

  for (set_patterns) |pattern| {
    if (compile.nfa(config, gpa, pattern)) |*nfa_obj| {
      @constCast(nfa_obj).deinit(gpa);
    } else |_| {}
  }
}

test "pzre oom and resource exhaustion attempt" {
  const gpa = std.testing.allocator;
  
  // We provide a generous but finite upper bound to test if the engine respects it
  const config: Config = .{ .semantics = .{} };

  const exhaustion_patterns = &[_][]const u8{
    // 1. Massive Exact Quantifiers (Forces NFA unrolling)
    "a{50000}",
    "(a|b){50000}",
    
    // 2. Exponential State Explosion (Nested quantifiers)
    "((a{10}){10}){10}",
    "(((a+)+)+)+",
    "(((a*)*)*)*",
    
    // 3. AST Depth / Call Stack Exhaustion
    "((((((((((((((((((((((((((((((((((((((((a))))))))))))))))))))))))))))))))))))))))",
    
    // 4. Catastrophic Alternation Broadening
    "a" ** 10000,
    "a" ++ "|a" ** 10000,

    // 5. High-density Character Classes
    "[" ++ "a-z" ** 1000 ++ "]",
    "()" ** 10000,
    "(a)" ** 10000,
    "^" ** 10000,
    "\\b" ** 10000,
    "(" ** 10000,
    "[" ** 10000,
    "(a|" ** 5000 ++ "b" ++ ")" ** 5000,
    "a{18446744073709551615}",
    "a{0,18446744073709551615}",
    "[" ++ "\\w\\W\\d\\D\\s\\S" ** 1000 ++ "]",
  };

  for (exhaustion_patterns) |pattern| {
    // std.debug.print("testing pattern: {s}\n", .{pattern});
    // We expect the engine to gracefully return an error (like OutOfMemory or AllocationUpperbound)
    // A test failure here means the engine panicked, segfaulted, or hung indefinitely.
    if (compile.nfa(config, gpa, pattern)) |*nfa_obj| {
      // std.debug.print("success!\n", .{});
      @constCast(nfa_obj).deinit(gpa);
    } else |_| {
      // std.debug.print("err: {any}\n", .{err});
    } 
  }
}

test "pzre runtime execution stress" {
  const gpa = std.testing.allocator;
  const config: Config = .{ .semantics = .{} };

  // A pattern that forces heavy epsilon expansion and multiple active states
  const pattern = ".*(a|b|c)+.*$";
  
  var nfa_obj = compile.nfa(config, gpa, pattern) catch return;
  defer nfa_obj.deinit(gpa);

  // Generate a 1MB string of non-matching characters
  const massive_input = try gpa.alloc(u8, 1024 * 1024);
  defer gpa.free(massive_input);
  @memset(massive_input, 'd');

  // If the list pools or context trackers have capacity leaks, this will OOM or hang
  var ctx = try nfa_obj.initContext(gpa);
  defer ctx.deinit(gpa);
  const match = nfa_obj.find(&ctx, massive_input, 0, massive_input.len);
  try std.testing.expect(match == null);
}

test "pzre aggressive escape sequence fuzzing" {
  // Attempt to make the parser crash with random escape sequences
  // 1. Many patterns with length 16-32
  // 2. 50% chance a character is 'a'
  // 3. 50% chance its a random escape sequence using the entire ascii range

  const gpa = std.testing.allocator;

  var prng = std.Random.DefaultPrng.init(0xdeadbeef);
  const random = prng.random();

  var buf: [32]u8 = undefined;

  for (0..100_000) |_| {
    var len: usize = 0;
    for (0..16) |_| {
      if (random.boolean()) {
        buf[len] = 'a';
        len += 1;
      } else {
        buf[len] = '\\';
        buf[len + 1] = random.intRangeLessThan(u8, 0, 255);
        len += 2;
      }
    }

    const pattern = buf[0..len];

    if (random.boolean()) {
      if (pzre.compile.generate(.{}, .{ .ast = true, .sets = true }, gpa, pattern)) |*result| {
        pzre.misc.destroySets(gpa, result.sets);
        @constCast(result).ast.deinit(gpa);
      } else |_| {}
    } else {
      var result = pzre.compile.nfa(.{}, gpa, pattern);
      if (result) |*nfa| {
        nfa.deinit(gpa);
      } else |_| {}
    }
  }

}

test "pzre aggressive structural fuzzing" {
  // Random broken patterns

  const gpa = std.testing.allocator;

  var prng = std.Random.DefaultPrng.init(0x1337beef);
  const random = prng.random();

  const valid_pool = "a1P#*+?|()[]{}^$.,\\-";

  for (0..100_000) |_| {
    var str: std.ArrayList(u8) = .empty;
    var len: usize = 0;

    while (len < 32) {
      const char = valid_pool[random.intRangeLessThan(usize, 0, valid_pool.len)];
      const count = random.intRangeAtMost(usize, 1, 8);

      str.appendNTimes(gpa, char, count) catch unreachable;
      len += count;
    }

    const pattern = str.toOwnedSlice(gpa) catch unreachable;
    defer gpa.free(pattern);

    if (pzre.compile.generate(.{}, .{ .ast = true, .sets = true }, gpa, pattern)) |*result| {
      pzre.misc.destroySets(gpa, result.sets);
      @constCast(result).ast.deinit(gpa);
    } else |_| {}
  }
}

test "pzre full configuration matrix fuzzing" {
  const gpa = std.testing.allocator;
  
  const num_configs = 32;
  const configs = comptime b: {
    @setEvalBranchQuota(100_000);
    var res: [num_configs]pzre.compile.Config = undefined;
    var comp_prng = std.Random.DefaultPrng.init(0xdeadbeef);
    const comp_random = comp_prng.random();

    for (&res) |*cfg| {
      cfg.* = .{
        .optimize = comp_random.boolean(),
        .context = if (comp_random.boolean()) .dynamic else .{ .fixed = 10000 },
        // Randomize problem or leave null for inference
        .problem = if (comp_random.boolean()) null else comp_random.enumValue(pzre.nfa.search_problem.Name),
        .semantics = .{
          .multiline = comp_random.boolean(),
          .ignore_case = comp_random.boolean(),
          .pat_ignore_whitespace = comp_random.boolean(),
          .pat_ignore_all_whitespace = comp_random.boolean(),
          .never_implicit_newline = comp_random.boolean(),
          .dotall = comp_random.boolean(),
        },
        .limits = .{
          // Stress the memory and depth limits
          .gpa_upper_bound = if (comp_random.boolean()) 1 << 10 else 1 << 20,
          .max_states = if (comp_random.boolean()) 10 else 10000,
          .max_depth = if (comp_random.boolean()) 5 else 255,
          .max_arbitrary_repetition = if (comp_random.boolean()) comp_random.intRangeAtMost(usize, 0, 100) else null,
          .max_submachine_states = if (comp_random.boolean()) .i8 else .i16,
        },
      };
    }
    break :b res;
  };

  var prng = std.Random.DefaultPrng.init(0xbadc0de);
  const random = prng.random();
  // Include quantifiers, groups, sets, and anchors
  const valid_pool = "aAmM*+?|()[]{}^$.*+?|()[]{}^$.\\- \r\t\n";

  for (0..10_000) |_| {
    var str: std.ArrayList(u8) = .empty;
    var len: usize = 0;

    // Generate arbitrary chaotic patterns up to 64 chars
    while (len < 64) {
      const char = valid_pool[random.intRangeLessThan(usize, 0, valid_pool.len)];
      const count = random.intRangeAtMost(usize, 1, 4);
      
      str.appendNTimes(gpa, char, count) catch unreachable;
      len += count;
    }

    const pattern = str.toOwnedSlice(gpa) catch unreachable;
    defer gpa.free(pattern);

    // Pick a random config from our comptime generated matrix
    const config_idx = random.intRangeLessThan(usize, 0, num_configs);

    inline for (configs, 0..) |cfg, i| {
      if (i == config_idx) {
        // Test the main optimized runtime pipeline
        if (pzre.compile.nfa(cfg, gpa, pattern)) |*machine| {
          // If the compilation succeeded, clean up the artifact
          @constCast(machine).deinit(gpa);
        } else |_| {
          // Expected to fail frequently due to random syntax or tight configuration limits
        }
      }
    }
  }
}

test "pzre aggressive set malformation fuzzing" {
  // Test malformed ranges 

  const gpa = std.testing.allocator;

  var prng = std.Random.DefaultPrng.init(0x5e7bad);
  const random = prng.random();

  // reduce chance of failure by invalid escape sequence
  const escape_chars: []const u8 = lexer.perl_set_letters_string ++ lexer.escape_sequence_letters_string;
  const weight = 5;
  const structural_chars = "-^[]";
  const escape_symbols = "\\" ** @divTrunc(escape_chars.len, weight);
  const set_chaos_pool = escape_symbols ++ escape_chars ++ structural_chars;

  for (0..100_000) |_| {
    var str: std.ArrayList(u8) = .empty;
    
    str.append(gpa, '[') catch unreachable;

    if (random.boolean()) {
      str.append(gpa, '^') catch unreachable;
    }

    const inner_len = random.intRangeAtMost(usize, 0, 16);
    for (0..inner_len) |_| {
      const char = set_chaos_pool[random.intRangeLessThan(usize, 0, set_chaos_pool.len)];
      str.append(gpa, char) catch unreachable;
    }

    if (random.boolean()) {
      str.append(gpa, ']') catch unreachable;
    }

    const pattern = str.toOwnedSlice(gpa) catch unreachable;
    defer gpa.free(pattern);

    if (pzre.compile.generate(.{}, .{ .ast = true, .sets = true }, gpa, pattern)) |*result| {
      pzre.misc.destroySets(gpa, result.sets);
      @constCast(result).ast.deinit(gpa);
    } else |_| {}
  }
}

test "pzre aggressive quantifier fuzzing" {
  const gpa = std.testing.allocator;

  var prng = std.Random.DefaultPrng.init(0xdeadbeef);
  const random = prng.random();

  const bounds_pool = &[_][]const u8{
    "", "0", "1", "5", "256", "1024", "65535", "9999999999", "18446744073709551615", "-1", "a",
  };
  
  const atom_pool = "a.(?:[a-z])\\d";
  const simple_quantifiers = "*+?";

  for (0..100_000) |_| {
    var str: std.ArrayList(u8) = .empty;
    
    str.append(gpa, atom_pool[random.intRangeLessThan(usize, 0, atom_pool.len)]) catch unreachable;

    const q_count = random.intRangeAtMost(usize, 1, 4);
    for (0..q_count) |_| {
      if (random.boolean()) {
        const q = simple_quantifiers[random.intRangeLessThan(usize, 0, simple_quantifiers.len)];
        str.append(gpa, q) catch unreachable;
      } else {
        str.append(gpa, '{') catch unreachable;
        
        const left = bounds_pool[random.intRangeLessThan(usize, 0, bounds_pool.len)];
        str.appendSlice(gpa, left) catch unreachable;

        if (random.boolean()) {
          str.append(gpa, ',') catch unreachable;
          const right = bounds_pool[random.intRangeLessThan(usize, 0, bounds_pool.len)];
          str.appendSlice(gpa, right) catch unreachable;
        }

        str.append(gpa, '}') catch unreachable;
      }
    }

    const pattern = str.toOwnedSlice(gpa, ) catch unreachable;
    defer gpa.free(pattern);

    if (pzre.compile.generate(.{}, .{ .ast = true, .sets = true }, gpa, pattern)) |*result| {
      pzre.misc.destroySets(gpa, result.sets);
      @constCast(result).ast.deinit(gpa);
    } else |_| {}
  }
}
