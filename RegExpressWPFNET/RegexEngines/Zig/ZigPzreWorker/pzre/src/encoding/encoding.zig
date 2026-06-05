pub const ascii = @import("ascii.zig");

comptime {
  if (@import("builtin").is_test) {
    _ = @import("ascii.zig");
  }
}
