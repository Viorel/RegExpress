const std = @import("std");
const Allocator = std.mem.Allocator;
const Alignment = std.mem.Alignment;
const testing = std.testing;

/// A wrapper allocator that tracks memory usage and enforces a limit.
/// Thread-safe if the underlying allocator is thread-safe (stats are atomic).
pub const CountingAllocator = struct {
  child_allocator: Allocator,
  bytes_allocated: usize = 0,
  max_bytes: usize = 0, // 0 means unlimited
  /// In order to distinguish cap reached from child_allocator out of memory error, this is set
  cap_reached: bool = false,

  pub fn init(child_allocator: Allocator, limit: usize) @This() {
    return .{
      .child_allocator = child_allocator,
      .max_bytes = limit,
    };
  }

  pub fn allocator(self: *@This()) Allocator {
    return .{
      .ptr = self,
      .vtable = &.{
        .alloc = alloc,
        .resize = resize,
        .free = free,
        .remap = remap,
      },
    };
  }

  fn alloc(ctx: *anyopaque, len: usize, ptr_align: Alignment, ret_addr: usize) ?[*]u8 {
    const self: *@This() = @ptrCast(@alignCast(ctx));

    if (self.max_bytes > 0 and (self.bytes_allocated + len) > self.max_bytes) {
      self.cap_reached = true;
      return null;
    }

    const ptr = self.child_allocator.rawAlloc(len, ptr_align, ret_addr) orelse return null;
    self.bytes_allocated += len;
    return ptr;
  }

  fn remap(ctx: *anyopaque, memory: []u8, alignment: Alignment, new_len: usize, ret_addr: usize) ?[*]u8 {
    const self: *@This() = @ptrCast(@alignCast(ctx));

    if (new_len > memory.len) {
      const growth = new_len - memory.len;
      if (self.max_bytes > 0 and (self.bytes_allocated + growth) > self.max_bytes) {
        self.cap_reached = true;
        return null;
      }
    }

    if (self.child_allocator.rawRemap(memory, alignment, new_len, ret_addr)) |new_ptr| {
      if (new_len > memory.len) {
        self.bytes_allocated += (new_len - memory.len);
      } else {
        self.bytes_allocated -= (memory.len - new_len);
      }
      return new_ptr;
    }

    return null;
  }

  fn resize(ctx: *anyopaque, buf: []u8, buf_align: Alignment, new_len: usize, ret_addr: usize) bool {
    const self: *@This() = @ptrCast(@alignCast(ctx));

    // Check limit only if growing
    if (new_len > buf.len) {
      const growth = new_len - buf.len;
      if (self.max_bytes > 0 and (self.bytes_allocated + growth) > self.max_bytes) {
        return false;
      }
    }

    if (self.child_allocator.rawResize(buf, buf_align, new_len, ret_addr)) {
      if (new_len > buf.len) {
        self.bytes_allocated += (new_len - buf.len);
      } else {
        self.bytes_allocated -= (buf.len - new_len);
      }
      return true;
    }
    return false;
  }

  fn free(ctx: *anyopaque, buf: []u8, buf_align: Alignment, ret_addr: usize) void {
    const self: *@This() = @ptrCast(@alignCast(ctx));
    self.child_allocator.rawFree(buf, buf_align, ret_addr);
    self.bytes_allocated -= buf.len;
  }
};

test "CountingAllocator limit" {
  var buffer: [1024]u8 = undefined;
  var fba = std.heap.FixedBufferAllocator.init(&buffer);
  
  var counter = CountingAllocator.init(fba.allocator(), 100);
  const allocator = counter.allocator();

  // 1. Successful alloc
  const ptr = try allocator.alloc(u8, 50);
  defer allocator.free(ptr);
  try std.testing.expectEqual(@as(usize, 50), counter.bytes_allocated);

  // 2. Failed alloc (exceeds limit)
  try std.testing.expectError(error.OutOfMemory, allocator.alloc(u8, 51));
}

test "CountingAllocator: basic accounting" {
  var buf: [1024]u8 = undefined;
  var fba = std.heap.FixedBufferAllocator.init(&buf);

  // Unlimited (0)
  var ca = CountingAllocator.init(fba.allocator(), 0);
  const a = ca.allocator();

  const ptr = try a.alloc(u8, 100);
  try testing.expectEqual(@as(usize, 100), ca.bytes_allocated);

  a.free(ptr);
  try testing.expectEqual(@as(usize, 0), ca.bytes_allocated);
}

test "CountingAllocator: enforces hard limit" {
  var buf: [1024]u8 = undefined;
  var fba = std.heap.FixedBufferAllocator.init(&buf);

  // Strict limit of 50 bytes
  var ca = CountingAllocator.init(fba.allocator(), 50);
  const a = ca.allocator();

  // 1. Allocation within limit succeeds
  const ptr1 = try a.alloc(u8, 30);
  try testing.expectEqual(@as(usize, 30), ca.bytes_allocated);

  // 2. Allocation exceeding limit fails
  try testing.expectError(error.OutOfMemory, a.alloc(u8, 21));

  try testing.expectEqual(@as(usize, 30), ca.bytes_allocated);

  // 3. Exact limit fill succeeds
  const ptr2 = try a.alloc(u8, 20);
  try testing.expectEqual(@as(usize, 50), ca.bytes_allocated);

  a.free(ptr1);
  a.free(ptr2);
}

test "CountingAllocator: resizing (grow and shrink)" {
  var buf: [1024]u8 = undefined;
  var fba = std.heap.FixedBufferAllocator.init(&buf);

  var ca = CountingAllocator.init(fba.allocator(), 100);
  const a = ca.allocator();

  var slice = try a.alloc(u8, 10);
  try testing.expectEqual(@as(usize, 10), ca.bytes_allocated);

  // 1. Grow within limit
  if (a.resize(slice, 20)) {
    slice = slice.ptr[0..20];
  } else @panic("Resize failed");
  try testing.expectEqual(@as(usize, 20), ca.bytes_allocated);

  // 2. Grow exceeding limit
  const success = a.resize(slice, 110);
  try testing.expect(!success);
  try testing.expectEqual(@as(usize, 20), ca.bytes_allocated);

  // 3. Shrink
  if (a.resize(slice, 5)) {
    slice = slice.ptr[0..5];
  } else @panic("Shrink failed");
  try testing.expectEqual(@as(usize, 5), ca.bytes_allocated);

  a.free(slice);
  try testing.expectEqual(@as(usize, 0), ca.bytes_allocated);
}

test "CountingAllocator: handles backing allocator failure" {
  var buf: [10]u8 = undefined;
  var fba = std.heap.FixedBufferAllocator.init(&buf);

  var ca = CountingAllocator.init(fba.allocator(), 100);
  const a = ca.allocator();

  const ptr = try a.alloc(u8, 5);
  try testing.expectEqual(@as(usize, 5), ca.bytes_allocated);

  try testing.expectError(error.OutOfMemory, a.alloc(u8, 10));

  try testing.expectEqual(@as(usize, 5), ca.bytes_allocated);
  a.free(ptr);
}

test "CountingAllocator: distinguish user limit from system OOM" {
  var buf: [1000]u8 = undefined;
  var fba = std.heap.FixedBufferAllocator.init(&buf);

  // Case 1: Hit User Limit (cap_reached = true)
  {
    var ca = CountingAllocator.init(fba.allocator(), 10);
    const a = ca.allocator();

    // Try to alloc 20 (exceeds 10)
    try testing.expectError(error.OutOfMemory, a.alloc(u8, 20));
    try testing.expect(ca.cap_reached); 
  }

  // Case 2: Hit System Limit (cap_reached = false)
  {
    var ca = CountingAllocator.init(fba.allocator(), 2000); // Limit > Buffer
    const a = ca.allocator();

    // Try to alloc 1100 (exceeds FBA buffer of 1000, but < user limit 2000)
    try testing.expectError(error.OutOfMemory, a.alloc(u8, 1100));
    try testing.expect(!ca.cap_reached); // It was the BACKING allocator that failed
  }
}
