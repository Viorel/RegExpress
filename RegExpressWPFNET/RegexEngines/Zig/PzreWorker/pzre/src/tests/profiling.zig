const std = @import("std");
const pzre = @import("../root.zig");
const compile = pzre.compile;
const assert = std.debug.assert;

test "pzre memory profile comparison" {
  const gpa = std.testing.allocator;
  const patterns = .{
    "^[a-zA-Z0-9_]+@[a-zA-Z0-9_]+\\.[a-zA-Z]{2,}$",
    "https?://(www\\.)?[-a-zA-Z0-9@:%._\\+~#=]{1,256}\\.[a-zA-Z0-9()]{1,6}\\b",
    "\\b[0-9a-fA-F]{8}\\b-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}",
    "^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$",
    "[A-Za-z_]+[-A-Za-z_0-9]*",
  };

  std.debug.print("\n", .{});

  inline for (patterns) |pattern| {
    var rt_nfa = try compile.nfa(.{}, gpa, pattern);
    defer rt_nfa.deinit(gpa);

    var c_nfa = comptime compile.nfaComptime(.{.with_dyn_context = null}, pattern);
    var cr_nfa = comptime compile.nfaComptime(.{}, pattern);

    var rt_ctx = try rt_nfa.initContext(gpa);
    defer rt_ctx.deinit(gpa);

    var c_ctx = try c_nfa.initContext(gpa);
    defer c_ctx.deinit(gpa);

    var cr_ctx = try cr_nfa.initContext(gpa);
    defer cr_ctx.deinit(gpa);

    std.debug.print("Pattern: {s}\n", .{pattern});
    std.debug.print("  States amount: {d}\n", .{rt_nfa.states.len});
    std.debug.print("  Machine size (Runtime): {d}\n", .{rt_nfa.machineSize()});
    std.debug.print("  State size: {d}\n", .{@sizeOf(@TypeOf(rt_nfa).State)});
    std.debug.print("  Sets size (Runtime): {d}\n", .{rt_nfa.setsSize()});
    std.debug.print("  Dynamic Context size (Runtime): {d}\n", .{rt_ctx.sizeOf()});
    std.debug.print("  Combined size / states count: {d}\n", .{
      @divTrunc(rt_nfa.machineSize() + rt_ctx.sizeOf() + rt_nfa.setsSize(), rt_nfa.states.len),
    });
    std.debug.print("\n", .{});
    std.debug.print("  Machine size (Comptime): {d}\n", .{cr_nfa.machineSize()});
    std.debug.print("  State size: {d}\n", .{@sizeOf(@TypeOf(cr_nfa).State)});
    std.debug.print("  Sets size (Runtime): {d}\n", .{cr_nfa.setsSize()});
    std.debug.print("  Dynamic Context size (Comptime): {d}\n", .{cr_ctx.sizeOf()});
    std.debug.print("  Combined size / states count: {d} (dynamic)\n\n", .{
      @divTrunc(cr_nfa.machineSize() + cr_ctx.sizeOf() + cr_nfa.setsSize(), cr_nfa.states.len),
    });
    std.debug.print("  Fixed Context size (Comptime): {d}\n", .{c_ctx.sizeOf()});
    std.debug.print("  Combined size / states count: {d} (fixed)\n\n", .{
      @divTrunc(c_nfa.machineSize() + c_ctx.sizeOf() + c_nfa.setsSize(), c_nfa.states.len),
    });

    assert(c_nfa.machineSize()
      == cr_nfa.machineSize());
    assert(@sizeOf(@TypeOf(cr_nfa).State) == @sizeOf(@TypeOf(c_nfa).State));
  }
}
