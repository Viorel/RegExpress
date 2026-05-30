const std = @import("std");
const assert = std.debug.assert;
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;

const pzre = @import("../root.zig");
const BoundedArray = pzre.structures.bounded_array.BoundedArray;
const nfa = pzre.nfa;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;

const lens = pzre.lens;
const debug = lens.debug;

/// A fixed size list. The context 
/// 
/// On single threaded systems with many comptime known patterns,
/// 
/// 
pub fn BoundedLists(comptime ListId: type, comptime states_len: comptime_int) type {
  const max_concurrency = nfa.analysis.determineMaxConcurrency(states_len);
  const breakpoint = nfa.state.getBreakpoint(states_len);
  const State = nfa.state.State(breakpoint);
  const Idx = State.Idx;

  return struct {
    /// Two alternating lists. After each step, we swap them
    /// A list contains .states indices. These are the next valid indices the machine can potentially move to
    lists: [2]BoundedArray(Idx, max_concurrency) = .{.empty, .empty},
    /// Each state stores the self.list_id when they were appended to lists. 
    /// This allows for a check to make sure that no states are added multiple times.
    last_list_idxs: BoundedArray(ListId, states_len) = .zeroed,

    const Self = @This();

    pub const empty: Self = .{};

    /// 'last_list_idxs' do not need to reset due to the list_id being a forever incrementor
    pub fn reset(self: *Self) void {
      self.lists[0].len = 0;
      self.lists[1].len = 0;
    }

    // In lists.zig -> BoundedLists
    pub fn heapSize(self: Self) usize {
      _ = self;
      return 0; // Comptime bounded arrays have no heap allocations
    }
  };
}

/// A list with dynamic length. Can be extended at runtime to match new requirements
/// 
/// Ideal for runtime compiled patterns
///
pub fn DynamicLists(comptime ListId: type, comptime breakpoint: nfa.state.Breakpoint) type {
  const State = nfa.state.State(breakpoint);
  const Idx = State.Idx;

  return struct {
    /// Two alternating lists. After each step, we swap them
    /// A list contains .states indices. These are the next valid indices the machine can potentially move to
    lists: [2]ArrayList(Idx) = .{.empty, .empty},
    /// Each state stores the self.list_id when they were appended to lists. 
    /// This allows for a check to make sure that no states are added multiple times.
    last_list_idxs: ArrayList(ListId) = .empty,

    const Self = @This();

    pub fn assertSupportedLen(new_len: usize) void {
      assert(new_len <= std.math.maxInt(breakpoint.Offset()));
    }

    pub fn sanityCheck(self: Self) void {
      const cap = self.last_list_idxs.capacity;
      assert(cap == self.last_list_idxs.items.len);
      const max_concurrency = nfa.analysis.determineMaxConcurrency(cap);
      assert(self.lists[0].capacity == max_concurrency);
      assert(self.lists[1].capacity == max_concurrency);
    }

    pub fn len(self: Self) usize {
      return self.last_list_idxs.capacity;
    }

    pub fn init(gpa: Allocator, states_len: usize) Allocator.Error!Self {
      assertSupportedLen(states_len);
      const max_concurrency = nfa.analysis.determineMaxConcurrency(states_len);

      var list_idxs: ArrayList(ListId) = try .initCapacity(gpa, states_len);
      list_idxs.items.len = states_len;
      @memset(list_idxs.items[0..], 0);
      errdefer list_idxs.deinit(gpa);

      var list_a: ArrayList(Idx) = try .initCapacity(gpa, max_concurrency);
      errdefer list_a.deinit(gpa);
      var list_b: ArrayList(Idx) = try .initCapacity(gpa, max_concurrency);
      errdefer list_b.deinit(gpa);

      const s = Self{.lists = .{list_a, list_b}, .last_list_idxs = list_idxs};
      s.sanityCheck();
      return s;
    }

    /// Updates an existing context to match new requirements
    pub fn update(self: *Self, gpa: Allocator, states_len: usize) Allocator.Error!void {
      assertSupportedLen(states_len);
      self.sanityCheck();

      const old_len = self.len();
      if (states_len > old_len) {
        try self.last_list_idxs.ensureTotalCapacityPrecise(gpa, states_len);
        self.last_list_idxs.items.len = states_len;
        @memset(self.last_list_idxs.items[old_len..], 0);
      }

      const max_concurrency = nfa.analysis.determineMaxConcurrency(states_len);
      try self.lists[0].ensureTotalCapacityPrecise(gpa, max_concurrency);
      try self.lists[1].ensureTotalCapacityPrecise(gpa, max_concurrency);

      self.sanityCheck();
    }

    /// Updates an existing context to match new requirements
    /// Brings the size down, and frees any unused capacity if the new requirement is lower than old
    pub fn updateExact(self: *Self, gpa: Allocator, states_len: usize) Allocator.Error!void {
      assertSupportedLen(states_len);
      self.sanityCheck();
      if (states_len == self.len()) return;

      const old_len = self.len();
      if (states_len > old_len) {
        try self.update(gpa, states_len);
      } else {
        const max_concurrency = nfa.analysis.determineMaxConcurrency(states_len);
        self.lists[0].items.len = self.lists[0].capacity;
        self.lists[0].shrinkAndFree(gpa, max_concurrency);
        self.lists[0].items.len = 0;

        self.lists[1].items.len = self.lists[0].capacity;
        self.lists[1].shrinkAndFree(gpa, max_concurrency);
        self.lists[1].items.len = 0;

        self.last_list_idxs.shrinkAndFree(gpa, states_len);
        self.last_list_idxs.items.len = states_len;

        // Due to how shrinkAndFree is implememented, the ONLY way it does not change the capacity to
        // the input value, is when it encounters a memory error. In that case we error aswell
        if (self.last_list_idxs.capacity != states_len 
          or self.lists[0].capacity != max_concurrency
          or self.lists[1].capacity != max_concurrency) return error.OutOfMemory;
      }

      self.sanityCheck();
    }

    pub fn heapSize(self: Self) usize {
      return (self.last_list_idxs.capacity * @sizeOf(ListId))
        + (self.lists[0].capacity * @sizeOf(Idx))
        + (self.lists[1].capacity * @sizeOf(Idx));
    }

    /// 'last_list_idxs' do not need to reset due to the list_id being a forever incrementor
    pub fn reset(self: *Self) void {
      self.lists[0].clearRetainingCapacity();
      self.lists[1].clearRetainingCapacity();
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      self.lists[0].deinit(gpa);
      self.lists[1].deinit(gpa);
      self.last_list_idxs.deinit(gpa);
    }
  };
}
