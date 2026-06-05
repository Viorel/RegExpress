const std = @import("std");
const assert = std.debug.assert;
const testing = std.testing;

pub const Error = error{Overflow};

/// A fixed-size array with a length counter.
/// Allocation is static (part of the struct layout).
/// Suitable for stack allocation or embedding in other structs.
/// Works at comptime due to the api not expecting any allocators. Once zig has comptime allocators this is deprecated in favor of std arraylist with fixedbuffer allocator
pub fn BoundedArray(comptime T: type, comptime max_capacity: usize) type {
  return struct {
    /// The backing storage. Content beyond `len` is undefined.
    buffer: [max_capacity]T = undefined,
    /// Current number of active elements.
    len: usize = 0,

    const Self = @This();
    
    pub const capacity = max_capacity;

    /// Initialize empty. 
    /// Note: You can also just use `.{}` if you don't need to set an initial length.
    pub const empty: Self = .{};

    /// Initializes with length max_capacity and zero values
    pub const zeroed: Self = .{.len = max_capacity, .buffer = [_]T{0} ** max_capacity};

    /// Initialize from an existing slice. 
    /// Returns error.Overflow if slice is too large.
    pub fn fromSlice(slc: []const T) Error!Self {
      if (slc.len > max_capacity) return error.Overflow;
      var self = Self{ .len = slc.len };
      @memcpy(self.buffer[0..slc.len], slc);
      return self;
    }

    /// Initialize from an existing slice. 
    /// Assumes slc.len <= max_capacity
    pub fn fromSliceAssumeCapacity(slc: []const T) Self {
      var self = Self{ .len = slc.len };
      @memcpy(self.buffer[0..slc.len], slc);
      return self;
    }

    /// Initialize with a specific length (contents undefined).
    pub fn initLen(length: usize) Error!Self {
      if (length > max_capacity) return error.Overflow;
      return Self{ .len = length };
    }

    /// Reset length to 0. Does not clear memory.
    pub fn clear(self: *Self) void {
      self.len = 0;
    }

    /// Returns a slice of the valid data.
    pub inline fn slice(self: *Self) []T {
      return self.buffer[0..self.len];
    }

    /// Returns a const slice of the valid data.
    pub inline fn constSlice(self: *const Self) []const T {
      return self.buffer[0..self.len];
    }

    /// Access element at index. Asserts bounds in Debug/ReleaseSafe.
    pub fn get(self: *const Self, index: usize) T {
      assert(index < self.len);
      return self.buffer[index];
    }

    /// Access element at index. Asserts bounds in Debug/ReleaseSafe.
    pub fn getPtr(self: *const Self, index: usize) *T {
      assert(index < self.len);
      return &self.buffer[index];
    }

    /// Set element at index. Asserts bounds in Debug/ReleaseSafe.
    pub fn set(self: *Self, index: usize, value: T) void {
      assert(index < self.len);
      self.buffer[index] = value;
    }

    /// Append a value. Returns error if full.
    pub fn append(self: *Self, item: T) Error!void {
      if (self.len >= max_capacity) return error.Overflow;
      self.buffer[self.len] = item;
      self.len += 1;
    }

    /// Append a value, asserting there is space.
    /// Use this when you have checked capacity logic externally.
    pub fn appendAssumeCapacity(self: *Self, item: T) void {
      assert(self.len < max_capacity);
      self.buffer[self.len] = item;
      self.len += 1;
    }

    /// Append a slice of values. Returns error if it won't fit.
    pub fn appendSlice(self: *Self, items: []const T) Error!void {
      if (self.len + items.len > max_capacity) return error.Overflow;
      @memcpy(self.buffer[self.len .. self.len + items.len], items);
      self.len += items.len;
    }

    /// Append a slice, asserting space exists.
    pub fn appendSliceAssumeCapacity(self: *Self, items: []const T) void {
      assert(self.len + items.len <= max_capacity);
      @memcpy(self.buffer[self.len .. self.len + items.len], items);
      self.len += items.len;
    }

    /// Remove and return the last element. Crashes if empty.
    pub fn pop(self: *Self) T {
      assert(self.len > 0);
      self.len -= 1;
      return self.buffer[self.len];
    }

    /// Remove and return the last element, or null if empty.
    pub fn popOrNull(self: *Self) ?T {
      if (self.len == 0) return null;
      self.len -= 1;
      return self.buffer[self.len];
    }

    /// Resize the array. 
    /// If growing, new elements are undefined.
    /// If shrinking, elements past new len are effectively discarded.
    pub fn resize(self: *Self, new_len: usize) Error!void {
      if (new_len > max_capacity) return error.Overflow;
      self.len = new_len;
    }

    /// Returns a slice of unusued capacity for 'n' elements
    /// Assumes the full slice is populated, extending the len of self by 'n' amount
    pub fn addManyAsSlice(self: *Self, n: usize) Error![]T {
      if (self.len + n > max_capacity) return error.Overflow;
      defer self.len += n;
      return self.buffer[self.len .. self.len + n];
    }

    /// Insert an item at index, shifting subsequent items to the right.
    /// O(N).
    pub fn insert(self: *Self, index: usize, item: T) Error!void {
      if (self.len >= max_capacity) return error.Overflow;
      if (index > self.len) return error.OutOfBounds;
      
      // Shift elements
      var i = self.len;
      while (i > index) : (i -= 1) {
        self.buffer[i] = self.buffer[i - 1];
      }
      
      self.buffer[index] = item;
      self.len += 1;
    }

    /// Remove item at index, shifting subsequent items left.
    /// O(N). Preserves order.
    pub fn orderedRemove(self: *Self, index: usize) T {
      assert(index < self.len);
      const item = self.buffer[index];
      
      var i = index;
      while (i < self.len - 1) : (i += 1) {
        self.buffer[i] = self.buffer[i + 1];
      }
      
      self.len -= 1;
      return item;
    }

    /// Remove item at index by swapping with the last item.
    /// O(1). Does NOT preserve order.
    pub fn swapRemove(self: *Self, index: usize) T {
      assert(index < self.len);
      const item = self.buffer[index];
      self.buffer[index] = self.buffer[self.len - 1];
      self.len -= 1;
      return item;
    }

    /// Insert an item at the beginning (index 0), shifting all other items right.
    /// O(N).
    pub fn pushFront(self: *Self, item: T) Error!void {
      if (self.len >= max_capacity) return error.Overflow;

      const src = self.buffer[0..self.len];
      const dest = self.buffer[1 .. self.len + 1];
      std.mem.copyBackwards(T, dest, src);

      self.buffer[0] = item;
      self.len += 1;
    }

    /// Insert a slice of items at the beginning, shifting all other items right.
    /// O(N + M).
    pub fn pushFrontSlice(self: *Self, items: []const T) Error!void {
      if (self.len + items.len > max_capacity) return error.Overflow;
      if (items.len == 0) return;

      const shift_amt = items.len;
      const src = self.buffer[0..self.len];
      const dest = self.buffer[shift_amt .. self.len + shift_amt];
      std.mem.copyBackwards(T, dest, src);

      @memcpy(self.buffer[0..shift_amt], items);
      self.len += shift_amt;
    }
  };
}

test "BoundedArray pushFront and pushFrontSlice" {
  const MyArray = BoundedArray(u32, 5);
  var arr = MyArray.empty;

  try arr.pushFront(10); // [10]
  try testing.expectEqual(@as(usize, 1), arr.len);
  try testing.expectEqual(@as(u32, 10), arr.buffer[0]);

  try arr.pushFront(20); // [20, 10]
  try testing.expectEqual(@as(usize, 2), arr.len);
  try testing.expectEqual(@as(u32, 20), arr.buffer[0]);
  try testing.expectEqual(@as(u32, 10), arr.buffer[1]);

  const slice = [_]u32{ 30, 40 };
  try arr.pushFrontSlice(&slice); // [30, 40, 20, 10]
  
  try testing.expectEqual(@as(usize, 4), arr.len);
  try testing.expectEqualSlices(u32, &[_]u32{ 30, 40, 20, 10 }, arr.buffer[0..4]);

  try arr.pushFront(50); // [50, 30, 40, 20, 10] (Full)
  try testing.expectEqual(@as(usize, 5), arr.len);
  try testing.expectError(error.Overflow, arr.pushFront(60));

  arr.len = 4; // Manually "pop" one to make space
  const big_slice = [_]u32{ 100, 200 };
  try testing.expectError(error.Overflow, arr.pushFrontSlice(&big_slice));

  try testing.expectEqual(@as(usize, 4), arr.len);
  try testing.expectEqual(@as(u32, 50), arr.buffer[0]);
}

test "BoundedArray basic usage" {
  var list: BoundedArray(u32, 10) = .empty;
  try list.append(1);
  try list.append(2);
  
  try std.testing.expectEqual(@as(usize, 2), list.len);
  try std.testing.expectEqual(@as(u32, 1), list.get(0));
  try std.testing.expectEqual(@as(u32, 2), list.pop());
  try std.testing.expectEqual(@as(usize, 1), list.len);
}
