const std = @import("std");
pub fn build(b: *std.Build) void {
  const target = b.standardTargetOptions(.{});
  const optimize = b.standardOptimizeOption(.{});
  const pzre_mod = b.addModule("pzre", .{
    .root_source_file = b.path("src/root.zig"),
    .target = target,
    .optimize = optimize,
  });

  const is_dev = b.option(bool, "pzre_dev", "Enable dev dependencies (lens)") orelse false;
  if (!is_dev) return; // consumers only need the module; stop here so lens is never reached

  // Everything below is dev-only: tests + lens.
  if (b.lazyDependency("lens", .{ .target = target, .optimize = optimize })) |dep| {
    pzre_mod.addImport("lens", dep.module("lens"));
  }

  const test_filter = b.option([]const []const u8, "filter", "filter unit tests");
  const all_tests = b.addTest(.{
    .root_module = pzre_mod,
    .filters = if (test_filter) |filter| filter else &.{},
  });
 
  if (b.lazyDependency("lens", .{ .target = target, .optimize = optimize })) |dep| {
    all_tests.root_module.addImport("lens", dep.module("lens"));
  }
 
  const run_mod_tests = b.addRunArtifact(all_tests);
  const test_step = b.step("test", "Run tests");
  test_step.dependOn(&run_mod_tests.step);
}
