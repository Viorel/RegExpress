//! comptime arraylist
const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

pub fn ComptimeArrayList(comptime T: type) type {
  return struct {
    items: []const T = &[_]T{},

    const Self = @This();

    pub const empty: Self = .{};

    /// Extends the list by 1 element.
    pub fn append(comptime self: *Self, comptime item: T) void {
      comptime {
        self.items = self.items ++ [_]T{item};
      }
    }

    /// Extends the list by 1 element.
    pub fn dupeInplace(comptime self: *Self) void {
      comptime {
        self.items = self.items ++ self.items;
      }
    }

    pub fn set(comptime self: *Self, comptime idx: usize, comptime item: T) void {
      var new_items: [self.items.len]T = undefined;
      @memcpy(&new_items, self.items);
      new_items[idx] = item;
      const final_items = new_items; 
      self.items = &final_items;
    }

    /// Prepends the list with one element
    pub fn pushFront(comptime self: *Self, comptime item: T) void {
      comptime self.items = [_]T{item} ++ self.items;
    }

    pub fn pop(comptime self: *Self) T {
      comptime {
        const item = self.items[self.items.len - 1];
        self.items = self.items[0..self.items.len - 1];
        return item;
      }
    }

    /// Append the slice of items to the list.
    pub fn appendSlice(comptime self: *Self, comptime items: []const T) void {
      comptime self.items = self.items ++ items;
    }

    /// Append the slice of items to the list.
    pub fn clone(comptime self: Self) Self {
      comptime {
        var r: Self = .empty;
        r.appendSlice(self.items);
        return r;
      }
    }

    pub fn clearRetainingCapacity(comptime self: *Self) void {
      self.items.len = 0;
    }
  };
}
