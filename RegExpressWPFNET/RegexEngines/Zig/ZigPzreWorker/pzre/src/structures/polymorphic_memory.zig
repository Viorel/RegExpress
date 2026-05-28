//! Abstraction for handling linear memory structures memory model agnostically
//! This will be deleted in the future when zig supports comptime allocators
const std = @import("std");
const Allocator = std.mem.Allocator;
const testing = std.testing;
const ArrayList = std.ArrayList;
const pzre = @import("../root.zig");
const structures = pzre.structures;
const meta = pzre.meta;

const Deque = structures.deque.Deque;
const range = structures.range;
const BoundedArray = structures.bounded_array.BoundedArray;
const ComptimeArrayList = structures.comptime_arraylist.ComptimeArrayList;
const BoundedArrayError = structures.bounded_array.Error;
const IntegerSet = structures.integer_set.IntegerSet;

pub const List = enum {
  comptime_array_list,
  bounded_array,
  array_list,
  deque,

  pub fn Fn(list: List, comptime T: type, comptime size: ?usize) type {
    return switch (list) {
      .comptime_array_list => ComptimeArrayList(T),
      .array_list => ArrayList(T),
      .deque => Deque(T),
      .bounded_array => BoundedArray(T, size.?),
    };
  }
};

/// The memory model for lists that hold state indices or states
pub const MemoryModel = enum {
  /// Allocator interface, e.g. heap
  dynamic,
  /// Fixed comptime defined, stack .bss .rodata etc
  // fixed,
  /// Dynamically comptime computed for stack .bss .rodata etc
  comptime_dynamic,

  const Self = @This();
};

const ListMapping = fn (comptime MemoryModel) List;
const ErrorMapping = fn (comptime MemoryModel) type;

pub const presets = struct {
  /// Standard configuration for Append-Only usage (Stacks, Queues, Lists).
  /// Dynamic -> ArrayList
  /// Stack   -> BoundedArray
  pub const single_ended = struct {
    pub fn listMapping(comptime memory_model: MemoryModel) List {
      return switch (memory_model) {
        .dynamic => .array_list,
        // .fixed => .bounded_array,
        .comptime_dynamic => .comptime_array_list,
      };
    }

    pub fn Error(comptime memory_model: MemoryModel) type {
      return switch (memory_model) {
        .dynamic => Allocator.Error,
        // .fixed => BoundedArrayError,
        .comptime_dynamic => error{},
      };
    }

    pub fn Create(comptime model: MemoryModel, comptime buf_size: ?usize, comptime T: type) type {
      return PolymorphicList(model, buf_size, listMapping, Error, T);
    }
  };

  /// Configuration for Front & Back usage (Deques).
  /// Dynamic -> Deque
  /// Stack   -> BoundedArray (supports pushFront via shift)
  pub const double_ended = struct {
    pub fn listMapping(comptime memory_model: MemoryModel) List {
      return switch (memory_model) {
        .dynamic => .deque,
        // .fixed => .bounded_array,
        .comptime_dynamic => .comptime_array_list,
      };
    }

    pub fn Error(comptime memory_model: MemoryModel) type {
      return switch (memory_model) {
        .dynamic => Allocator.Error,
        // .fixed => BoundedArrayError,
        .comptime_dynamic => error{},
      };
    }

    pub fn Create(comptime model: MemoryModel, comptime buf_size: ?usize, comptime T: type) type {
      return PolymorphicList(model, buf_size, listMapping, Error, T);
    }
  };
};

/// Take not that comptime_array_list is extremely inefficient in many many operations
/// buf_size is only respected for fixed memory models
pub fn PolymorphicList(comptime model: MemoryModel, comptime buf_size: ?usize, comptime listMapping: ListMapping, comptime error_mapping: ErrorMapping, comptime Element: type) type {
  // if (model == .fixed and buf_size == null) @compileError("Buf size not given for fixed memory model");
  return struct {
    const Self = @This();

    pub const list: List = @call(.auto, listMapping, .{model});
    pub const Data = list.Fn(Element, buf_size);
    pub const Error: type = @call(.auto, error_mapping, .{model});

    pub const empty: Self = .{.data = .empty};

    data: Data,

    pub inline fn append(self: *Self, gpa: Allocator, item: Element) Error!void {
      switch (comptime list) {
        .comptime_array_list => self.data.append(item),
        .bounded_array => try self.data.append(item),
        .array_list => try self.data.append(gpa, item),
        .deque => try self.data.pushBack(gpa, item),
      }
    }

    pub inline fn dupeInplace(self: *Self, gpa: Allocator) Error!void {
      switch (comptime list) {
        .comptime_array_list => self.data.dupeInplace(),
        .deque => try self.data.dupeInplace(gpa),
        else => @compileError("Unimplemented"),
      }
    }

    pub inline fn pushFront(self: *Self, gpa: Allocator, item: Element) Error!void {
      switch (comptime list) {
        .comptime_array_list => self.data.pushFront(item),
        .bounded_array => try self.data.pushFront(item),
        .array_list => try self.data.insert(gpa, 0, item),
        .deque => try self.data.pushFront(gpa, item),
      }
    }

    pub inline fn appendSlice(self: *Self, gpa: Allocator, items: []const Element) Error!void {
      switch (comptime list) {
        .comptime_array_list => self.data.appendSlice(items),
        .bounded_array => try self.data.appendSlice(items),
        .array_list => try self.data.appendSlice(gpa, items),
        .deque => try self.data.appendSliceAssume(gpa, items),
      }
    }

    pub inline fn initUsing(items: []const Element) Self {
      const data = switch (comptime list) {
        .comptime_array_list => ComptimeArrayList(Element){.items = items},
        .array_list => ArrayList(Element){.items = @constCast(items), .capacity = items.len},
        else => @compileError("UNIMPLEMENTEIUDNA SM;!"),
      };
      return Self{.data = data};
    }

    pub inline fn addManyAsSlice(self: *Self, gpa: Allocator, n: usize) Error![]Element {
      switch (comptime list) {
        .array_list => return try self.data.addManyAsSlice(gpa, n),
        else => @compileError("Unimplemented"),
      }
    }

    pub inline fn len(self: *const Self) usize {
      return switch (comptime list) {
        .comptime_array_list => self.data.items.len,
        .bounded_array => self.data.len,
        .array_list => self.data.items.len,
        .deque => self.data.len,
      };
    }

    pub inline fn deinit(self: *Self, gpa: Allocator) void {
      switch (comptime list) {
        .comptime_array_list => {},
        .bounded_array => {},
        .array_list => self.data.deinit(gpa),
        .deque => self.data.deinit(gpa),
      }
    }

    pub inline fn freeOwnedSlice(self: *Self, gpa: Allocator, slice: []const Element) void {
      _ = self;
      switch (comptime list) {
        .comptime_array_list => {},
        else => gpa.free(slice),
      }
    }

    /// Access element by index (Read-Only).
    /// Asserts bounds in Debug/ReleaseSafe modes.
    pub inline fn get(self: *const Self, index: usize) Element {
      return switch (comptime list) {
        .comptime_array_list => self.data.items[index],
        .bounded_array => self.data.get(index),
        .array_list => self.data.items[index],
        // Assuming Deque has random access via get()
        .deque => self.data.at(index),
      };
    }

    /// Returns a constant slice of the underlying allocated elements
    pub inline fn getConstSlice(self: *const Self) []const Element {
      return switch (comptime list) {
        .comptime_array_list => self.data.items,
        .bounded_array => self.data.buffer[0..self.data.len],
        .array_list => self.data.items,
        // Assuming Deque has random access via get()
        .deque => @compileError("Not possible"),
      };
    }

    /// Returns a slice of the underlying allocated elements
    pub inline fn getSlice(self: *Self) []Element {
      return switch (comptime list) {
        .comptime_array_list => @compileError("Not possible"),
        .bounded_array => self.data.buffer[0..self.data.len],
        .array_list => self.data.items,
        // Assuming Deque has random access via get()
        .deque => @compileError("Not possible"),
      };
    }

    /// Returns a slice of the underlying allocated capacity
    pub inline fn getCapacity(self: *Self) []Element {
      return switch (comptime list) {
        .comptime_array_list => @compileError("Not possible"),
        .bounded_array => &self.data.buffer,
        .array_list => self.data.items.ptr[0..self.data.capacity],
        // Assuming Deque has random access via get()
        .deque => self.data.buffer,
      };
    }

    /// Access element pointer by index (Read/Write).
    /// Asserts bounds in Debug/ReleaseSafe modes.
    pub inline fn getPtr(self: *Self, index: usize) *Element {
      return switch (comptime list) {
        .comptime_array_list => @compileError("Unimplemented"),
        .bounded_array => self.data.getPtr(index),
        .array_list => &self.data.items[index],
        .deque => self.data.getPtr(index),
      };
    }

    /// Access element const pointer
    /// Asserts bounds in Debug/ReleaseSafe modes.
    pub inline fn getConstPtr(self: *Self, index: usize) *const Element {
      return switch (comptime list) {
        .comptime_array_list => &self.data.items[index],
        .bounded_array => self.data.getPtr(index),
        .array_list => &self.data.items[index],
        .deque => self.data.getPtr(index),
      };
    }

    /// Set element at index.
    pub inline fn set(self: *Self, index: usize, item: Element) void {
      switch (comptime list) {
        .comptime_array_list => self.data.set(index, item),
        .bounded_array => self.data.buffer[index] = item,
        .array_list => self.data.items[index] = item,
        .deque => self.data.set(index, item),
      }
    }

    pub inline fn appendOther(self: *Self, gpa: Allocator, other: *const Self) Error!void {
      switch (comptime list) {
        .comptime_array_list => self.data.appendSlice(other.data.items),
        .bounded_array => try self.data.appendSlice(other.data.constSlice()),
        .array_list => try self.data.appendSlice(gpa, other.data.items),
        .deque => try self.data.appendOther(gpa, other.data),
      }
    }

    pub inline fn toOwnedConstSlice(self: *Self, gpa: Allocator) Error![]const Element {
      return switch (comptime list) {
        .comptime_array_list => self.data.items,
        .bounded_array => {
          const slice = try gpa.alloc(Element, self.data.len);
          @memcpy(slice, self.data.constSlice());
          return slice;
        },
        .array_list => try self.data.toOwnedSlice(gpa),
        .deque => try self.data.toOwnedSlice(gpa),
      };
    }

    pub inline fn clearRetainingCapacity(self: *Self) void {
      switch (comptime list) {
        .comptime_array_list => self.data.clearRetainingCapacity(),
        .bounded_array => self.data.len = 0,
        .array_list => self.data.clearRetainingCapacity(),
        .deque => self.data.clearRetainingCapacity(),
      }
    }

    pub inline fn ensureCapacityPrecise(self: *Self, gpa: Allocator, n: usize) Error!void {
      return switch (comptime list) {
        .comptime_array_list => {},
        .bounded_array, .deque => @compileError("Unimplemented"),
        .array_list => try self.data.ensureTotalCapacityPrecise(gpa, n),
      };
    }

    pub inline fn clone(self: *const Self, gpa: Allocator) Error!Self {
      var new: Self = .empty;
      errdefer new.deinit(gpa);

      switch (comptime list) {
        .comptime_array_list => new.data = self.data.clone(),
        .bounded_array => new.data = try Data.fromSlice(self.data.buffer[0..self.data.len]),
        .array_list => new.data = try self.data.clone(gpa),
        .deque => new.data = try self.data.clone(gpa),
      }
      return new;
    }
  };
}

/// Generic test function that exercises the entire Uniform API.
/// It doesn't know what underlying structure it is testing.
fn testPolyListOperations(list: anytype, gpa: Allocator) !void {
  try testing.expectEqual(@as(usize, 0), list.len());

  try list.append(gpa, 10);
  try list.append(gpa, 20);
  try testing.expectEqual(@as(usize, 2), list.len());
  try testing.expectEqual(@as(u32, 10), list.get(0));
  try testing.expectEqual(@as(u32, 20), list.get(1));
  list.set(1, 5);
  try testing.expectEqual(@as(u32, 5), list.get(1));
  list.set(1, 20);
  try testing.expectEqual(@as(u32, 20), list.get(1));

  try list.pushFront(gpa, 5); // [5, 10, 20]
  try testing.expectEqual(@as(usize, 3), list.len());
  try testing.expectEqual(@as(u32, 5), list.get(0));
  try testing.expectEqual(@as(u32, 10), list.get(1));

  if (comptime @TypeOf(list.*).list != .deque) {
    try testing.expectEqualSlices(u32, &.{5, 10, 20}, list.getConstSlice());
    if (comptime @TypeOf(list.*).list != .comptime_array_list) {
      try testing.expectEqualSlices(u32, &.{5, 10, 20}, list.getSlice());
    }
  }

  if (comptime @TypeOf(list.*).list != .comptime_array_list) {
    if (comptime @TypeOf(list.*).list != .deque) {
      try testing.expectEqualSlices(u32, &.{5, 10, 20}, list.getCapacity()[0..list.len()]);
    }
  }

  const slice = [_]u32{ 30, 40 };
  try list.appendSlice(gpa, &slice); // [5, 10, 20, 30, 40]
  try testing.expectEqual(@as(usize, 5), list.len());
  try testing.expectEqual(@as(u32, 40), list.get(4));

  var cloned = try list.clone(gpa);
  defer cloned.deinit(gpa);
  try testing.expectEqual(list.len(), cloned.len());
  try testing.expectEqual(list.get(0), cloned.get(0));

  try list.appendOther(gpa, &cloned); // [5...40, 5...40]
  try testing.expectEqual(@as(usize, 10), list.len());

  // Note: BoundedArrays allocate a new slice here, dynamic lists pass ownership.
  // In all cases, we get a dynamic slice we must free.
  if (!@inComptime()) {
    const owned = try list.toOwnedConstSlice(gpa);
    defer list.freeOwnedSlice(gpa, owned);
    try testing.expectEqual(@as(usize, 10), owned.len);
    try testing.expectEqual(@as(u32, 5), owned[0]);
  }

  list.clearRetainingCapacity();
  try testing.expectEqual(@as(usize, 0), list.len());
}

test "PolymorphicList: Verify all backends" {
  const gpa = std.testing.allocator;

  // 1. Dynamic Heap (ArrayList)
  const ListDynamic = PolymorphicList(
    .dynamic,
    null,
    presets.single_ended.listMapping,
    presets.single_ended.Error,
    u32,
  );
  var l1: ListDynamic = .empty;
  defer l1.deinit(gpa);
  try testPolyListOperations(&l1, gpa);

  // 2. Dynamic Heap (Deque)
  const ListDeque = PolymorphicList(
    .dynamic,
    null,
    presets.double_ended.listMapping, 
    presets.double_ended.Error, 
    u32
  );
  var l2: ListDeque = .empty;
  defer l2.deinit(gpa);
  try testPolyListOperations(&l2, gpa);

  // // 3. Stack Fixed (BoundedArray)
  // // We give it enough capacity (32) to handle the test steps
  // const ListStack = PolymorphicList(
  //   .fixed,
  //   32,
  //   presets.single_ended.listMapping, 
  //   presets.single_ended.Error, 
  //   u32
  // );
  // var l3: ListStack = .empty;   // Allocator ignored for fixed
  // defer l3.deinit(gpa);         // No-op for fixed
  // try testPolyListOperations(&l3, gpa);
  //
  // // 4. Comptime BoundedArray
  // comptime {
  //   const ListComptime = PolymorphicList(
  //     .fixed,
  //     32,
  //     presets.single_ended.listMapping, 
  //     presets.single_ended.Error, 
  //     u32
  //   );
  //   var l4: ListComptime = .empty;
  //   defer l4.deinit(undefined);
  //   try testPolyListOperations(&l4, undefined);
  // }

  // 4. Comptime Dynamic
  comptime {
    const ListComptime = PolymorphicList(
      .comptime_dynamic,
      null,
      presets.single_ended.listMapping, 
      presets.single_ended.Error, 
      u32
    );
    var l4: ListComptime = .empty;
    defer l4.deinit(undefined);
    try testPolyListOperations(&l4, undefined);
  }
}

pub const SetOp = enum {unary, binary};

/// Consumes the 'a' set (deinit) and constructs a new set in its place memory-polymorphically
/// 'op' is a raw operation from the Set interface, e.g. @"union"
/// 'buf_size_request_fn' is a function from Set.buf_sizes
/// 'b' is null for unary operations
pub fn polymorphicSetOperationInplace(
  comptime mem_model: MemoryModel,
  comptime Int: type,
  self: *presets.single_ended.Create(mem_model, null, range.Range(Int)),
  b: ?IntegerSet(Int),
  gpa: Allocator,
  comptime mode: SetOp,
  comptime op: anytype,
  comptime buf_size_request_fn: anytype,
) meta.GetChild(@TypeOf(self)).?.Error!void {
  const Set = IntegerSet(Int);
  const Range = range.Range(Int);

  switch (mem_model) {
    .dynamic => {
      const a = Set{.ranges = self.getConstSlice()};

      const buf_size = if (comptime mode == .unary) 
        @call(.auto, buf_size_request_fn, .{a})
      else @call(.auto, buf_size_request_fn, .{a, b.?});

      const buf = try gpa.alloc(Range, buf_size);

      const new: Set = if (comptime mode == .unary) 
        @call(.auto, op, .{a, buf})
      else @call(.auto, op, .{a, b.?, buf});

      if (gpa.remap(buf, new.ranges.len)) |new_mem| {
        self.deinit(gpa);
        self.data.items = new_mem;
        self.data.capacity = new_mem.len;
        return;
      }
      defer gpa.free(buf);

      const new_mem = try gpa.alloc(Range, new.ranges.len);
      self.deinit(gpa);

      @memcpy(new_mem, buf[0..new.ranges.len]);
      self.data.items = new_mem;
      self.data.capacity = new_mem.len;
    },
    .comptime_dynamic => {
      comptime {
        const current_set = self.getConstSlice();

        const a_ranges = current_set ++ &[0]Range{};
        const a = Set{.ranges = a_ranges};

        const buf_size = if (mode == .unary) 
          @call(.auto, buf_size_request_fn, .{a})
        else @call(.auto, buf_size_request_fn, .{a, b.?});

        var out: [buf_size]Range = undefined;
        const buf = out[0..];

        const new: Set = if (mode == .unary) 
          @call(.auto, op, .{a, buf})
        else @call(.auto, op, .{a, b.?, buf});

        self.data.items = new.ranges ++ &[0]Range{};
      }
    }
  }
}

