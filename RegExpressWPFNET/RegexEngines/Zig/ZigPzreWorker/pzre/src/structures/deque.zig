//! An extension the std.Deque due to it lacking basic functionality
//! The base is copy pasted from the zig standard library
//! All modifications are explicitly stated

const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;

/// A contiguous, growable, double-ended queue.
///
/// Pushing/popping items from either end of the queue is O(1).
pub fn Deque(comptime T: type) type {
  return struct {
    /// A ring buffer.
    buffer: []T,
    /// The index in buffer where the first item in the logical deque is stored.
    head: usize,
    /// The number of items stored in the logical deque.
    len: usize,

    const Self = @This();

    /// A Deque containing no elements.
    pub const empty: Self = .{
      .buffer = &.{},
      .head = 0,
      .len = 0,
    };

    /// Initialize with capacity to hold `capacity` elements.
    /// The resulting capacity will equal `capacity` exactly.
    /// Deinitialize with `deinit`.
    pub fn initCapacity(gpa: Allocator, capacity: usize) Allocator.Error!Self {
      var deque: Self = .empty;
      try deque.ensureTotalCapacityPrecise(gpa, capacity);
      return deque;
    }

    /// Initialize with externally-managed memory. The buffer determines the
    /// capacity and the deque is initially empty.
    pub fn initBuffer(buffer: []T) Self {
      return .{
        .buffer = buffer,
        .head = 0,
        .len = 0,
      };
    }
   
    /// Initialize with externally-managed memory. The buffer determines the
    /// capacity and the deque is initially empty.
    pub fn initUsing(buffer: []T) Self {
      return .{
        .buffer = buffer,
        .head = 0,
        .len = buffer.len,
      };
    }

    /// Release all allocated memory.
    pub fn deinit(deque: *Self, gpa: Allocator) void {
      gpa.free(deque.buffer);
      deque.* = .empty;
    }

    /// Modify the deque so that it can hold at least `new_capacity` items.
    /// Implements super-linear growth to achieve amortized O(1) push/pop operations.
    /// Invalidates element pointers if additional memory is needed.
    pub fn ensureTotalCapacity(deque: *Self, gpa: Allocator, new_capacity: usize) Allocator.Error!void {
      if (deque.buffer.len >= new_capacity) return;
      return deque.ensureTotalCapacityPrecise(gpa, std.ArrayList(T).growCapacity(new_capacity));
    }

    /// If the current capacity is less than `new_capacity`, this function will
    /// modify the deque so that it can hold exactly `new_capacity` items.
    /// Invalidates element pointers if additional memory is needed.
    pub fn ensureTotalCapacityPrecise(deque: *Self, gpa: Allocator, new_capacity: usize) Allocator.Error!void {
      if (deque.buffer.len >= new_capacity) return;
      const old_buffer = deque.buffer;
      if (gpa.remap(old_buffer, new_capacity)) |new_buffer| {
        // If the items wrap around the end of the buffer we need to do
        // a memcpy to prevent a gap after resizing the buffer.
        if (deque.head > old_buffer.len - deque.len) {
          // The gap splits the items in the deque into head and tail parts.
          // Choose the shorter part to copy.
          const head = new_buffer[deque.head..old_buffer.len];
          const tail = new_buffer[0 .. deque.len - head.len];
          if (head.len > tail.len and new_buffer.len - old_buffer.len > tail.len) {
            @memcpy(new_buffer[old_buffer.len..][0..tail.len], tail);
          } else {
            // In this case overlap is possible if e.g. the capacity increase is 1
            // and head.len is greater than 1.
            deque.head = new_buffer.len - head.len;
            @memmove(new_buffer[deque.head..][0..head.len], head);
          }
        }
        deque.buffer = new_buffer;
      } else {
        const new_buffer = try gpa.alloc(T, new_capacity);
        if (deque.head < old_buffer.len - deque.len) {
          @memcpy(new_buffer[0..deque.len], old_buffer[deque.head..][0..deque.len]);
        } else {
          const head = old_buffer[deque.head..];
          const tail = old_buffer[0 .. deque.len - head.len];
          @memcpy(new_buffer[0..head.len], head);
          @memcpy(new_buffer[head.len..][0..tail.len], tail);
        }
        deque.head = 0;
        deque.buffer = new_buffer;
        gpa.free(old_buffer);
      }
    }

    /// Modify the deque so that it can hold at least `additional_count` **more** items.
    /// Invalidates element pointers if additional memory is needed.
    pub fn ensureUnusedCapacity(
      deque: *Self,
      gpa: Allocator,
      additional_count: usize,
    ) Allocator.Error!void {
      return deque.ensureTotalCapacity(gpa, try addOrOom(deque.len, additional_count));
    }

    /// Add one item to the front of the deque.
    ///
    /// Invalidates element pointers if additional memory is needed.
    pub fn pushFront(deque: *Self, gpa: Allocator, item: T) error{OutOfMemory}!void {
      try deque.ensureUnusedCapacity(gpa, 1);
      deque.pushFrontAssumeCapacity(item);
    }

    /// Add one item to the front of the deque.
    ///
    /// Never invalidates element pointers.
    ///
    /// If the deque lacks unused capacity for the additional item, returns
    /// `error.OutOfMemory`.
    pub fn pushFrontBounded(deque: *Self, item: T) error{OutOfMemory}!void {
      if (deque.buffer.len - deque.len == 0) return error.OutOfMemory;
      return deque.pushFrontAssumeCapacity(item);
    }

    /// Add one item to the front of the deque.
    ///
    /// Never invalidates element pointers.
    ///
    /// Asserts that the deque can hold one additional item.
    pub fn pushFrontAssumeCapacity(deque: *Self, item: T) void {
      assert(deque.len < deque.buffer.len);
      if (deque.head == 0) {
        deque.head = deque.buffer.len;
      }
      deque.head -= 1;
      deque.buffer[deque.head] = item;
      deque.len += 1;
    }

    /// Add one item to the back of the deque.
    ///
    /// Invalidates element pointers if additional memory is needed.
    pub fn pushBack(deque: *Self, gpa: Allocator, item: T) error{OutOfMemory}!void {
      try deque.ensureUnusedCapacity(gpa, 1);
      deque.pushBackAssumeCapacity(item);
    }

    /// Add one item to the back of the deque.
    ///
    /// Never invalidates element pointers.
    ///
    /// If the deque lacks unused capacity for the additional item, returns
    /// `error.OutOfMemory`.
    pub fn pushBackBounded(deque: *Self, item: T) error{OutOfMemory}!void {
      if (deque.buffer.len - deque.len == 0) return error.OutOfMemory;
      deque.pushBackAssumeCapacity(item);
    }

    /// Add one item to the back of the deque.
    ///
    /// Never invalidates element pointers.
    ///
    /// Asserts that the deque can hold one additional item.
    pub fn pushBackAssumeCapacity(deque: *Self, item: T) void {
      assert(deque.len < deque.buffer.len);
      const buffer_index = deque.bufferIndex(deque.len);
      deque.buffer[buffer_index] = item;
      deque.len += 1;
    }

    /// Return the first item in the deque or null if empty.
    pub fn front(deque: *const Self) ?T {
      if (deque.len == 0) return null;
      return deque.buffer[deque.head];
    }

    /// Return the last item in the deque or null if empty.
    pub fn back(deque: *const Self) ?T {
      if (deque.len == 0) return null;
      return deque.buffer[deque.bufferIndex(deque.len - 1)];
    }

    /// Return the item at the given index in the deque.
    /// The first item in the queue is at index 0.
    /// Asserts that the index is in-bounds.
    pub fn at(deque: *const Self, index: usize) T {
      assert(index < deque.len);
      return deque.buffer[deque.bufferIndex(index)];
    }

    /// Return the item ptr at the given index in the deque.
    /// The first item in the queue is at index 0.
    /// Asserts that the index is in-bounds.
    pub fn getPtr(deque: *const Self, index: usize) *T {
      assert(index < deque.len);
      return &deque.buffer[deque.bufferIndex(index)];
    }

    /// Sets the item at index
    /// The first item in the queue is at index 0.
    /// Asserts that the index is in-bounds.
    pub fn set(deque: *const Self, index: usize, item: T) void {
      assert(index < deque.len);
      deque.buffer[deque.bufferIndex(index)] = item;
    }

    /// Remove and return the first item in the deque or null if empty.
    pub fn popFront(deque: *Self) ?T {
      if (deque.len == 0) return null;
      const pop_index = deque.head;
      deque.head = deque.bufferIndex(1);
      deque.len -= 1;
      return deque.buffer[pop_index];
    }

    /// Remove and return the last item in the deque or null if empty.
    pub fn popBack(deque: *Self) ?T {
      if (deque.len == 0) return null;
      deque.len -= 1;
      return deque.buffer[deque.bufferIndex(deque.len)];
    }

    pub const Iterator = struct {
      deque: *const Self,
      index: usize,

      pub fn next(it: *Iterator) ?T {
        if (it.index < it.deque.len) {
          defer it.index += 1;
          return it.deque.at(it.index);
        } else {
          return null;
        }
      }
    };

    /// Iterates over all items in the deque in order from front to back.
    pub fn iterator(deque: *const Self) Iterator {
      return .{ .deque = deque, .index = 0 };
    }

    /// Returns the index in `buffer` where the element at the given
    /// index in the logical deque is stored.
    pub fn bufferIndex(deque: *const Self, index: usize) usize {
      // This function is written in this way to avoid overflow and
      // expensive division.
      const head_len = deque.buffer.len - deque.head;
      if (index < head_len) {
        return deque.head + index;
      } else {
        return index - head_len;
      }
    }

    /// Appends the slice contents to the queue
    pub fn appendSliceAssume(self: *Self, gpa: Allocator, slice: []const T) Allocator.Error!void {
      try self.ensureUnusedCapacity(gpa, slice.len);
      self.appendSliceAssumeCapacity(slice);
    }

    pub fn appendSliceAssumeCapacity(self: *Self, slice: []const T) void {
      if (self.head + self.len < self.buffer.len) { // end of buffer has space
        const space_left = self.buffer.len - (self.head + self.len);
        const empty_head = self.buffer[self.head + self.len .. ];
        const added = @min(space_left, slice.len);
        @memcpy(empty_head[0..added], slice[0..added]);
        if (added < slice.len) { // 
          const amount_left = slice.len - added;
          @memcpy(self.buffer[0..amount_left], slice[added .. ]);
        } 
      } else { // head fills end of buffer
        const head_len = self.buffer.len - self.head;
        const tail_len = self.len - head_len;
        assert(tail_len + slice.len <= self.head);
        @memcpy(self.buffer[tail_len .. ][0..slice.len], slice);
      }
      self.len += slice.len;
    }

    /// Appends the contents of another queue into this
    pub fn appendOther(self: *Self, gpa: Allocator, other: Self) Allocator.Error!void {
      try self.ensureUnusedCapacity(gpa, other.len);
      if (other.head < other.buffer.len - other.len) {
        const items = other.buffer[other.head..][0..other.len];
        self.appendSliceAssumeCapacity(items);
      } else {
        const head = other.buffer[other.head..];
        const tail = other.buffer[0 .. other.len - head.len];
        self.appendSliceAssumeCapacity(head);
        self.appendSliceAssumeCapacity(tail);
      }
    }

    /// Translates the internal logical list into an owned slice
    pub fn toOwnedSlice(self: *Self, gpa: Allocator) Allocator.Error![]T {
      if (self.head + self.len <= self.buffer.len) {
        if (self.head == 0) {
          // We attempt a remap when the items start at index 0. Essentially, we treat the structure as a simple ArrayList
          // This is benefitial for the case when the user calls clearRetainingCapacity, appends some elements and calls this
          const old_memory = self.buffer;
          if (gpa.remap(old_memory, self.len)) |new_items| {
            self.* = .empty;
            return new_items;
          }
        }
      }
      defer self.deinit(gpa);
      return self.copyToOwnedSlice(gpa);
    }

    /// Copies the structure without destroying it into an owned slice
    pub fn copyToOwnedSlice(self: *const Self, gpa: Allocator) Allocator.Error![]T {
      if (self.head + self.len <= self.buffer.len) {
        const items = self.buffer[self.head..][0..self.len];
        const duped = try gpa.dupe(T, items);
        return duped;
      } else {
        var r = try gpa.alloc(T, self.len);
        const head = self.buffer[self.head..];
        const tail = self.buffer[0 .. self.len - head.len];
        assert(r.len == head.len + tail.len);
        @memcpy(r[0..head.len], head);
        @memcpy(r[head.len ..], tail);
        return r;
      }
    }

    /// Clones the structure and returns a new owned copy
    /// The elements are not deeply copied
    pub fn clone(self: *const Self, gpa: Allocator) Allocator.Error!Self {
      const duped = try gpa.dupe(T, self.buffer);
      return Self{.buffer = duped, .len = self.len, .head = self.head};
    }

    /// Dupes the structure inplace optimally
    pub fn dupeInplace(self: *Self, gpa: Allocator) Allocator.Error!void {
      try self.appendOther(gpa, self.*);
    }

    pub fn clearRetainingCapacity(self: *Self) void {
      self.head = 0;
      self.len = 0;
    }
  };
}

/// Integer addition returning `error.OutOfMemory` on overflow.
fn addOrOom(a: usize, b: usize) error{OutOfMemory}!usize {
  const result, const overflow = @addWithOverflow(a, b);
  if (overflow != 0) return error.OutOfMemory;
  return result;
}

test "basic" {
  const testing = std.testing;
  const gpa = testing.allocator;

  var q: Deque(u32) = .empty;
  defer q.deinit(gpa);

  try testing.expectEqual(null, q.popFront());
  try testing.expectEqual(null, q.popBack());

  try q.pushBack(gpa, 1);
  try q.pushBack(gpa, 2);
  try q.pushBack(gpa, 3);
  try q.pushFront(gpa, 0);

  try testing.expectEqual(0, q.popFront());
  try testing.expectEqual(1, q.popFront());
  try testing.expectEqual(3, q.popBack());
  try testing.expectEqual(2, q.popFront());
  try testing.expectEqual(null, q.popFront());
  try testing.expectEqual(null, q.popBack());
}

test "buffer" {
  const testing = std.testing;

  var buffer: [4]u32 = undefined;
  var q: Deque(u32) = .initBuffer(&buffer);

  try testing.expectEqual(null, q.popFront());
  try testing.expectEqual(null, q.popBack());

  try q.pushBackBounded(1);
  try q.pushBackBounded(2);
  try q.pushBackBounded(3);
  try q.pushFrontBounded(0);
  try testing.expectError(error.OutOfMemory, q.pushBackBounded(4));

  try testing.expectEqual(0, q.popFront());
  try testing.expectEqual(1, q.popFront());
  try testing.expectEqual(3, q.popBack());
  try testing.expectEqual(2, q.popFront());
  try testing.expectEqual(null, q.popFront());
  try testing.expectEqual(null, q.popBack());
}

test "slow growth" {
  const testing = std.testing;
  const gpa = testing.allocator;

  var q: Deque(i32) = .empty;
  defer q.deinit(gpa);

  try q.ensureTotalCapacityPrecise(gpa, 1);
  q.pushBackAssumeCapacity(1);
  try q.ensureTotalCapacityPrecise(gpa, 2);
  q.pushFrontAssumeCapacity(0);
  try q.ensureTotalCapacityPrecise(gpa, 3);
  q.pushBackAssumeCapacity(2);
  try q.ensureTotalCapacityPrecise(gpa, 5);
  q.pushBackAssumeCapacity(3);
  q.pushFrontAssumeCapacity(-1);
  try q.ensureTotalCapacityPrecise(gpa, 6);
  q.pushFrontAssumeCapacity(-2);

  try testing.expectEqual(-2, q.popFront());
  try testing.expectEqual(-1, q.popFront());
  try testing.expectEqual(3, q.popBack());
  try testing.expectEqual(0, q.popFront());
  try testing.expectEqual(2, q.popBack());
  try testing.expectEqual(1, q.popBack());
  try testing.expectEqual(null, q.popFront());
  try testing.expectEqual(null, q.popBack());
}
//
// test "fuzz against ArrayList oracle" {
//   try std.testing.fuzz({}, fuzzAgainstArrayList, .{});
// }

test "dumb fuzz against ArrayList oracle" {
  const testing = std.testing;
  const gpa = testing.allocator;

  const input = try gpa.alloc(u8, 1024);
  defer gpa.free(input);

  var prng = std.Random.DefaultPrng.init(testing.random_seed);
  prng.random().bytes(input);

  try fuzzAgainstArrayList({}, input);
}

fn generateRandomList(comptime T: type, gpa: Allocator, rand_id: u8) Allocator.Error!Deque(T) {
  const bits = @typeInfo(T).int.bits;
  assert(bits % 8 == 0);
  const testing = std.testing;
  var prng = std.Random.DefaultPrng.init(testing.random_seed);
  const random = prng.random();
  const QueueType = enum (u8) {
    head_full_exact = 0,
    head_full,
    head_partial,
    full,
    empty,
  };
  const len = 8;
  var r: Deque(T) = try .initCapacity(gpa, len);
  
  const c = bits / 8;
  var buf: [c * len]u8 = undefined;
  random.bytes(&buf);
  const casted: [len]T = @bitCast(buf);
  @memcpy(r.buffer[0..], casted[0..]);
  const t: QueueType = @enumFromInt(rand_id % 4);
  switch (t) {
    .head_full_exact => {
      r.head = random.intRangeAtMost(usize, 2, len - 1);
      const space_left = len - r.head;
      r.len = space_left;
    },
    .head_full => {
      r.head = random.intRangeAtMost(usize, 2, len - 1);
      const space_left = len - r.head;
      const elements = random.intRangeAtMost(usize, space_left + 1, len - 1);
      r.len = elements;
    },
    .head_partial => {
      r.head = random.intRangeAtMost(usize, 1, len - 3);
      const space_left = len - r.head;
      const elements = @divTrunc(space_left, 2);
      r.len = elements;
    },
    .full => {
      r.len = len;
      r.head = random.intRangeAtMost(usize, 0, len - 1);
    },
    .empty => {
      r.len = 0;
      r.head = random.intRangeAtMost(usize, 0, len - 1);
    },
  }
  return r;
}

fn fuzzAgainstArrayList(_: void, input: []const u8) anyerror!void {
  const testing = std.testing;
  const gpa = testing.allocator;

  var q: Deque(u32) = .empty;
  defer q.deinit(gpa);
  var l: std.ArrayList(u32) = .empty;
  defer l.deinit(gpa);

  if (input.len < 2) return;

  var prng = std.Random.DefaultPrng.init(input[0]);
  const random = prng.random();

  const Action = enum {
    push_back,
    push_front,
    pop_back,
    pop_front,
    append_other,
    grow,
    /// Sentinel to avoid hardcoding the cast below
    max,
  };
  for (input[1..]) |byte| {
    const action = @as(Action, @enumFromInt(byte % (@intFromEnum(Action.max))));
    switch (action) {
      .push_back => {
        const item = random.int(u8);
        try testing.expectEqual(
          l.appendBounded(item),
          q.pushBackBounded(item),
        );
      },
      .push_front => {
        const item = random.int(u8);
        try testing.expectEqual(
          l.insertBounded(0, item),
          q.pushFrontBounded(item),
        );
      },
      .pop_back => {
        try testing.expectEqual(l.pop(), q.popBack());
      },
      .pop_front => {
        try testing.expectEqual(
          if (l.items.len > 0) l.orderedRemove(0) else null,
          q.popFront(),
        );
      },
      // Growing by small, random, linear amounts seems to better test
      // ensureTotalCapacityPrecise(), which is the most complex part
      // of the Deque implementation.
      .grow => {
        const growth = random.int(u3);
        try l.ensureTotalCapacityPrecise(gpa, l.items.len + growth);
        try q.ensureTotalCapacityPrecise(gpa, q.len + growth);
      },
      .append_other => {
        const item = random.int(u8);
        var other = try generateRandomList(u32, gpa, item);
        try q.appendOther(gpa, other);
        const owned = try other.toOwnedSlice(gpa);
        defer gpa.free(owned);
        try l.appendSlice(gpa, owned);
      },
      .max => unreachable,
    }
    try testing.expectEqual(l.getLastOrNull(), q.back());
    try testing.expectEqual(
      if (l.items.len > 0) l.items[0] else null,
      q.front(),
    );
    try testing.expectEqual(l.items.len, q.len);
    try testing.expectEqual(l.capacity, q.buffer.len);
    {
      var it = q.iterator();
      for (l.items) |item| {
        try testing.expectEqual(item, it.next());
      }
      try testing.expectEqual(null, it.next());
    }
  }
}
