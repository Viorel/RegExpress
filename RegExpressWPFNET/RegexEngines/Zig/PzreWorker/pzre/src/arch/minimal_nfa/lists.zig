const std = @import("std");
const assert = std.debug.assert;
const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;

const pzre = @import("../../root.zig");
const arch = pzre.arch;
const nfa = arch.minimal_nfa;

const BoundedArray = pzre.structures.bounded_array.BoundedArray;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;

const lens = pzre.lens;
const debug = lens.debug;

// ListId has to be large enough, to ensure that it can be accumulated indefinitely without overflowing
// Practically, it will never overflow.
// if it does, then it wraps back to 0, which is only a problem, if there are sections of the automata, 
// that execute in **extremely** rare circumstances
// TODO: move this to globals
pub const ListId = usize;

/// Selects the appropriate Data type for a minimal_nfa ConfigResolved.
/// This is what gets passed to arch.Context(...) for this architecture.
/// The architecture needs to be normalized: No compact_fixed
pub fn Data(comptime config: nfa.ConfigResolved) type {
  comptime switch (config.context) {
    .dynamic => |bp| {
      return DynamicLists(bp);
    },
    .fixed => |len| {
      return BoundedLists(len);
    },
  };
}

/// A fixed, no-heap list
/// 
/// The breakpoint is determined directly by states_len as it makes no sense to ever complicate this type
///   to use any other breakpoint. DO NOT ADD BREAKPOINT SIGNATURE
/// 
pub fn BoundedLists(comptime states_len: comptime_int) type {
  const max_concurrency = nfa.analysis.determineMaxConcurrency(states_len);

  return struct {
    /// Two alternating lists. After each step, we swap them
    /// A list contains .states indices. These are the next valid indices the machine can potentially move to
    lists: [2]BoundedArray(Idx, max_concurrency) = .{.empty, .empty},
    /// Each state stores the self.list_id when they were appended to lists. 
    /// This allows for a check to make sure that no states are added multiple times.
    last_list_idxs: BoundedArray(ListId, states_len) = .zeroed,
    /// The currently active list index in .lists
    /// The active list: states we are currently at
    /// The inactive list: the next active states being accumulated
    ///
    /// After a single step, the lists are 
    /// swapped, and previously active list is reset
    current_list_idx: u1 = 0,
    /// A unique identifier incremented after each step
    /// 
    /// This is never reset between matches using this same context.
    ///   If the machine is ran multiple times, it will start at some value k.
    ///
    /// current_list_id - k = number_of_chars_consumed
    list_id: ListId = 0,
    /// When the accept state was previously encountered
    /// accept does not exist in the topology, instead we detect for end_of_states as being accept
    previous_accept_append_list_id: ?ListId = null,

    const Self = @This();

    pub const empty: Self = .{};
    pub const Idx = arch.AbsoluteBreakpoint.define(states_len).Index();

    /// states_len: runtime states length
    pub fn init(gpa: Allocator, runtime_states_len: usize) Allocator.Error!Self {
      _ = gpa;
      assert(runtime_states_len <= states_len);
      return .empty;
    }

    /// states_len: runtime states length
    pub fn initFixed() Self {
      return .empty;
    }

    pub fn len(self: Self) usize {
      _ = self;
      return states_len;
    }

    pub fn update(self: *Self, gpa: Allocator, new_len: usize) Allocator.Error!void {
      _ = self;
      _ = gpa;
      assert(new_len <= states_len);
    }

    pub fn updateExact(self: *Self, gpa: Allocator, new_len: usize) Allocator.Error!void {
      _ = self;
      _ = gpa;
      assert(new_len <= states_len);
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      _ = self;
      _ = gpa;
    }

    /// 'last_list_idxs' do not need to reset due to the list_id being a forever incrementor
    pub fn reset(self: *Self) void {
      self.lists[0].len = 0;
      self.lists[1].len = 0;
      self.current_list_idx = 0;
      self.previous_accept_append_list_id = null;
    }

    pub fn heapSize(self: Self) usize {
      _ = self;
      return 0;
    }

    pub inline fn setStateLastlist(self: *Self, state_idx: Idx, list_id: ListId) void {
      self.last_list_idxs.set(state_idx, list_id);
    }

    pub inline fn stateLastlist(self: *Self, state_idx: Idx) ListId {
      return self.last_list_idxs.get(state_idx);
    }

    pub inline fn clearNext(self: *Self) void {
      const idx = self.nextlistIdx();
      self.lists[idx].len = 0;
    }

    pub inline fn currentlist(self: *Self) []const Idx {
      const idx = self.current_list_idx;
      return self.lists[idx].constSlice();
    }

    /// Appends a state index to one of the two lists
    pub inline fn appendList(self: *Self, list_idx: u1, state_idx: Idx) void {
      self.lists[list_idx].appendAssumeCapacity(state_idx);
    }

    pub inline fn incrementListId(self: *Self) void {
      self.list_id +%= 1;
    }

    pub inline fn nextlistIdx(self: *Self) u1 {
      return self.current_list_idx +% 1;
    }

    pub inline fn swaplists(self: *Self) void {
      self.current_list_idx +%= 1;
    }

    pub inline fn hasCurrent(self: *Self) bool {
      return self.currentlist().len > 0;
    }
  };
}

/// A list with dynamic length. Can be extended at runtime to match new requirements
/// 
/// Ideal for runtime compiled patterns
///
pub fn DynamicLists(comptime bp: arch.AbsoluteBreakpoint) type {
  return struct {
    /// Two alternating lists. After each step, we swap them
    /// A list contains .states indices. These are the next valid indices the machine can potentially move to
    lists: [2]ArrayList(Idx) = .{.empty, .empty},
    /// Each state stores the self.list_id when they were appended to lists. 
    /// This allows for a check to make sure that no states are added multiple times.
    last_list_idxs: ArrayList(ListId) = .empty,
    /// The currently active list index in .lists
    /// The active list: states we are currently at
    /// The inactive list: the next active states being accumulated
    ///
    /// After a single step, the lists are 
    /// swapped, and previously active list is reset
    current_list_idx: u1 = 0,
    /// A unique identifier incremented after each step
    /// 
    /// This is never reset between matches using this same context.
    ///   If the machine is ran multiple times, it will start at some value k.
    ///
    /// current_list_id - k = number_of_chars_consumed
    list_id: ListId = 0,
    /// When the accept state was previously encountered
    /// accept does not exist in the topology, instead we detect for end_of_states as being accept
    previous_accept_append_list_id: ?ListId = null,

    const Self = @This();
   
    pub const Idx = bp.Index();

    pub const empty: Self = .{};

    pub fn assertSupportedLen(new_len: usize) void {
      assert(new_len <= std.math.maxInt(bp.Index()));
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
      if (@inComptime()) @compileError("Dynamic context cannot be generated at comptime!");

      if (states_len == 0) return .empty;

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

    pub fn initFixed() Self {
      @panic("Not a fixed context!");
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
      self.current_list_idx = 0;
      self.previous_accept_append_list_id = null;
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      self.lists[0].deinit(gpa);
      self.lists[1].deinit(gpa);
      self.last_list_idxs.deinit(gpa);
    }

    pub inline fn setStateLastlist(self: *Self, state_idx: Idx, list_id: ListId) void {
      self.last_list_idxs.items[state_idx] = list_id;
    }

    pub inline fn stateLastlist(self: *Self, state_idx: Idx) ListId {
      return self.last_list_idxs.items[state_idx];
    }

    pub inline fn clearNext(self: *Self) void {
      const idx = self.nextlistIdx();
      self.lists[idx].clearRetainingCapacity();
    }

    pub inline fn currentlist(self: *Self) []const Idx {
      const idx = self.current_list_idx;
      return self.lists[idx].items;
    }

    /// Appends a state index to one of the two lists
    pub inline fn appendList(self: *Self, list_idx: u1, state_idx: Idx) void {
      self.lists[list_idx].appendAssumeCapacity(state_idx);
    }

    pub inline fn incrementListId(self: *Self) void {
      self.list_id +%= 1;
    }

    pub inline fn nextlistIdx(self: *Self) u1 {
      return self.current_list_idx +% 1;
    }

    pub inline fn swaplists(self: *Self) void {
      self.current_list_idx +%= 1;
    }

    pub inline fn hasCurrent(self: *Self) bool {
      return self.currentlist().len > 0;
    }
  };
}
