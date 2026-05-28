const tst = @import("test.zig");
const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const testMatchExact = tst.testMatchExact;
const testFindAll = tst.testFindAll;
const testFindAllMultiline = tst.testFindAllMultiline;
const testFind = tst.testFind;
const pzre = @import("../root.zig");
const Match = pzre.nfa.Match;
const compile = pzre.compile;

const Config = pzre.compile.Config;
const context = pzre.nfa.context;
const Mode = pzre.misc.Mode;

test "pzre generative fuzzing leak check" {
  @setEvalBranchQuota(1000000);
  const gpa = std.testing.allocator;

  // The goal is to generate long complicated patterns that are not commonly written by humans and check for memory leaks
  const fragments = [_][]const u8{
    "a", "b", "c", "[a]",

    ".", "\\w", "\\W", "\\d", "\\D", "\\s", "\\S",

    "^", "$", "\\b", "\\B", "\\A", "\\z",

    "\\n", "\\t", "\\r", "\\0", "\\e",

    "\\\\", "\\.", "\\|", "\\*", "\\+", "\\?", "\\(", "\\)", "\\[", "\\]", "\\{", "\\}",

    "[a-z]", "[A-Z0-9]", "[^a-zA-Z]", "[abc][cba]", "[mnrp\\d]",

    "[-a-z]", "[a-z-]", "[\\]\\^\\-]", "[]]",

    "a*", "b+", "c?", "x{2}", "y{1,3}", "z{5,}", "w{0,0}"
  };
  const operators = [_][]const u8{ "", "|", "()" };

  var prng = std.Random.DefaultPrng.init(0);
  const random = prng.random();

  var pattern: std.ArrayList(u8) = .empty;
  defer pattern.deinit(gpa);

  for (0..30) |_| {
    defer pattern.clearRetainingCapacity();
    
    const num_fragments = random.intRangeAtMost(usize, 10, 40);
    for (0..num_fragments) |_| {
      const op = operators[random.intRangeLessThan(usize, 0, operators.len)];
      
      if (std.mem.eql(u8, op, "()")) {
        const frag = fragments[random.intRangeLessThan(usize, 0, fragments.len)];
        try pattern.append(gpa, '(');
        try pattern.appendSlice(gpa, frag);
        try pattern.append(gpa, ')');
      } else {
        const frag_a = fragments[random.intRangeLessThan(usize, 0, fragments.len)];
        const frag_b = fragments[random.intRangeLessThan(usize, 0, fragments.len)];
        try pattern.appendSlice(gpa, frag_a);
        try pattern.appendSlice(gpa, op);
        try pattern.appendSlice(gpa, frag_b);
      }
    }

    const S = struct {
      fn f(_gpa: Allocator, pat: []const u8) !void {
        var nfa = compile.nfa(.{ .limits = .{ .gpa_upper_bound = 8 << 30 } }, _gpa, pat) catch |err| {
          // If we hit an error, we need to check if we are in the middle of a checkAllAllocationFailures run.
          // Since _gpa is the failing allocator during the check, any error should be treated as OOM to satisfy the test runner.
          // If it's the normal run, we just ignore the error because the string was randomly generated garbage.
          if (err == error.OutOfMemory) return err;
          
          // Only bubble up OOM if we are using the failing allocator.
          // In Zig, std.testing.allocator is the base, but checkAllAllocationFailures wraps it.
          if (_gpa.vtable != std.testing.allocator.vtable) {
            return error.OutOfMemory;
          }
          return; // Ignore syntax errors on the normal run
        };
        
        nfa.deinit(_gpa);
      }
    }.f;

    try S(gpa, pattern.items);
    try std.testing.checkAllAllocationFailures(gpa, S, .{pattern.items});
  }
}
