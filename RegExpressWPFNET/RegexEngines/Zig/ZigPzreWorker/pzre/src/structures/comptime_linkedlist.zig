//! comptime arraylist (O(1) Chunk-Linked Implementation)
const std = @import("std");

pub fn ComptimeLinkedList(comptime T: type) type {
  return struct {
    pub const Node = struct {
      slice: []const T,
      next: ?*const Node = null,
    };

    // Stored in reverse order of appends (O(1) insertion)
    back_list: ?*const Node = null,
    // Stored in forward order of pushFronts (O(1) insertion)
    front_list: ?*const Node = null,
    
    len: usize = 0,

    const Self = @This();
    pub const empty: Self = .{};

    /// Dedicated initializer to replace direct struct initialization
    pub fn initUsing(comptime items: []const T) Self {
      var list: Self = .empty;
      list.appendSlice(items);
      return list;
    }

    /// Flattens the linked chunks into a single contiguous slice and caches it.
    pub fn getItems(comptime self: *Self) []const T {
      comptime {
        if (self.len == 0) return &[_]T{};
        
        // Fast path: If it's already flattened into a single node, just return it.
        if (self.front_list == null and self.back_list != null and self.back_list.?.next == null) {
          return self.back_list.?.slice;
        }

        var flat: [self.len]T = undefined;
        var offset: usize = 0;

        // (forward order)
        var current = self.front_list;
        while (current) |node| {
          for (node.slice) |item| {
            flat[offset] = item;
            offset += 1;
          }
          current = node.next;
        }

        // (reverse order, so we write back-to-front)
        var back_offset: usize = self.len;
        current = self.back_list;
        while (current) |node| {
          back_offset -= node.slice.len;
          for (node.slice, 0..) |item, i| {
            flat[back_offset + i] = item;
          }
          current = node.next;
        }

        // Cache the flattened slice to make subsequent reads O(1)
        const final_slice = flat ++ [_]T{};
        self.front_list = null;
        self.back_list = &Node{ .slice = &final_slice, .next = null };
        
        return &final_slice;
      }
    }

    /// Extends the list by 1 element. O(1)
    pub fn append(comptime self: *Self, comptime item: T) void {
      comptime {
        const new_node = &Node{ .slice = &[_]T{item}, .next = self.back_list };
        self.back_list = new_node;
        self.len += 1;
      }
    }

    /// Extends the list by 1 element.
    pub fn dupeInplace(comptime self: *Self) void {
      comptime {
        const items = self.getItems();
        self.appendSlice(items);
      }
    }

    pub fn set(comptime self: *Self, comptime idx: usize, comptime item: T) void {
      comptime {
        const items = self.getItems();
        var new_items: [items.len]T = undefined;
        @memcpy(&new_items, items);
        new_items[idx] = item;
        
        const final_slice = new_items ++ [_]T{}; // FREEZE IT
        self.front_list = null;
        self.back_list = &Node{ .slice = &final_slice, .next = null };
      }
    }

    /// Prepends the list with one element. O(1)
    pub fn pushFront(comptime self: *Self, comptime item: T) void {
      comptime {
        const new_node = &Node{ .slice = &[_]T{item}, .next = self.front_list };
        self.front_list = new_node;
        self.len += 1;
      }
    }

    pub fn pop(comptime self: *Self) T {
      comptime {
        const items = self.getItems();
        const item = items[items.len - 1];
        
        const final_slice = items[0..items.len - 1];
        self.front_list = null;
        self.back_list = &Node{ .slice = final_slice, .next = null };
        self.len -= 1;
        
        return item;
      }
    }

    /// Append the slice of items to the list. O(1)
    pub fn appendSlice(comptime self: *Self, comptime items: []const T) void {
      comptime {
        if (items.len == 0) return;
        const new_node = &Node{ .slice = items, .next = self.back_list };
        self.back_list = new_node;
        self.len += items.len;
      }
    }

    pub fn clone(comptime self: Self) Self {
      comptime {
        // Because nodes and slices are strictly const at comptime, 
        // we can safely shallow-copy the struct and share the tree.
        return self;
      }
    }

    pub fn clearRetainingCapacity(comptime self: *Self) void {
      comptime {
        self.front_list = null;
        self.back_list = null;
        self.len = 0;
      }
    }
  };
}
