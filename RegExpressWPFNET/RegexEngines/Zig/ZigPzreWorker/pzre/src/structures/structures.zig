pub const comptime_arraylist = @import("comptime_arraylist.zig");
/// Similar to comptime_arraylist but slightly less dogshit
pub const comptime_linkedlist = @import("comptime_linkedlist.zig");
pub const deque = @import("deque.zig");
pub const integer_set = @import("integer_set.zig");
pub const range = @import("range.zig");
pub const polymorphic_memory = @import("polymorphic_memory.zig");
pub const bounded_array = @import("bounded_array.zig");

comptime {
  if (@import("builtin").is_test) {
    _ = @import("bounded_array.zig");
    _ = @import("comptime_arraylist.zig");
    _ = @import("comptime_linkedlist.zig");
    _ = @import("deque.zig");
    _ = @import("integer_set.zig");
    _ = @import("range.zig");
    _ = @import("polymorphic_memory.zig");
  }
}
