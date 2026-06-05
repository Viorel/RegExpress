const std = @import("std");
const t = @import("test.zig");

// This is a WIP file for collecting all testing data for phase 2 testing
// The approach is:
// 1. collect all pattern-match combinations what were manually written in the individual test files. This is 
//    done by injections placed in the harness. This is collected on-disk
// 2. wait for all test to finish, then run the next testing phase (modify build.zig)
// 3. test collective compilation and packing algorithms. This hand-written data makes it easy to test 
//    semantics, which is a hard problem in regex testing, since we are not generating any pattern-match 
//    combinations

var telemetry_file: ?std.fs.File = null;
var telemetry_mutex = std.Thread.Mutex{};

fn logTelemetry(
  comptime Re: type,
  comptime path: t.ExecutionPath,
  comptime pattern: []const u8,
  comptime fn_name: []const u8,
  comptime config: t.Config,
) void {
 
  telemetry_mutex.lock();
  defer telemetry_mutex.unlock();

  if (telemetry_file == null) {
    telemetry_file = std.fs.cwd().createFile("pzre_telemetry.jsonl", .{ .truncate = false }) catch return;
    telemetry_file.?.seekFromEnd(0) catch {}; 
  }

  const writer = telemetry_file.?.writer();

  // use the zig builtin json shit
  
  writer.print(
    \\{{"regex_type": "{s}", "path": "{s}", "pattern": "{s}", "fn": "{s}", "strategy": "{s}"}}
    \\
    , .{
      @typeName(Re),
      @tagName(path),
      pattern,
      fn_name,
      if (config.strategy) |s| @tagName(s) else "null",
    }
  ) catch {};
}
