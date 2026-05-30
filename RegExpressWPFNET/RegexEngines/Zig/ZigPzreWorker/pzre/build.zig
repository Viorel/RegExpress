const std = @import("std");

pub fn build(b: *std.Build) void {
  const target = b.standardTargetOptions(.{});
  const optimize = b.standardOptimizeOption(.{});

  // -Ddev=true 
  const is_dev = b.option(bool, "dev", "Enable dev dependencies (lens) inside the core module") orelse false;

  const pzre_mod = b.addModule("pzre", .{
    .root_source_file = b.path("src/root.zig"),
    .target = target,
    .optimize = optimize,
  });

  const test_filter = b.option([]const []const u8, "filter", "filter unit tests");
  const all_tests = b.addTest(.{
    .root_module = pzre_mod,
    .filters = if (test_filter) |filter| filter else &.{},
  });

  // Unit testing and data inspection library; Not a hard dependancy
  // 
  // Always injected to tests
  // Only injected to main if -Ddev=true
  if (b.lazyDependency("lens", .{
    .target = target,
    .optimize = optimize,
  })) |lens_dep| {
    const lens_mod = lens_dep.module("lens");

    all_tests.root_module.addImport("lens", lens_mod);

    if (is_dev) {
      pzre_mod.addImport("lens", lens_mod);
    }
  }

  const run_mod_tests = b.addRunArtifact(all_tests);
  const test_step = b.step("test", "Run tests");
  test_step.dependOn(&run_mod_tests.step);
}
