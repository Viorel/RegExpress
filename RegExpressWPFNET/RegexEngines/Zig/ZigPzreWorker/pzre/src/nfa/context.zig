//! The heavyweight mutable core of the NFA
const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const assert = std.debug.assert;
const Mutex = std.Io.Mutex;
const Io = std.Io;

const pzre = @import("../root.zig");
const nfa = pzre.nfa;
const misc = pzre.misc;

const lists = @import("lists.zig");

const lens = pzre.lens;
const debug = lens.debug;

pub const Mode = union (enum) {
  /// Defines a dynamic list as the context type. Managed by user passed allocator
  /// 
  /// Useful for maximum flexibility, the context can freely size up and down in order to match new
  ///   runtime requirements.
  /// 
  /// The context can be shared between any nfa's that define the exact context type
  /// 
  /// The list is compact, and sized exactly to fit the set of machines
  /// 
  /// The context has no upper bound; the context will scale up to fit the exact length 
  ///   of the largest NFA that depends on it. Therefore, it is bound indirectly by the Nfa limits
  /// 
  /// On single threaded systems, ideally only one context object is created and shared between all machines
  /// 
  dynamic,
  /// Defines a static array as the context type.
  /// 
  /// Requires no allocator; allocators can be passed as 'undefined', and errors ignored
  /// 
  /// The context can be shared between any nfa's that define the exact context type
  /// 
  /// The value defines the upper bound (the maximum length), of the context. And will immediately consume that amount of memory when created, regardless of what NFA uses it.
  /// 
  /// Useful when you want a context that never resizes. Its management requires less instructions, and takes up less memory than .dynamic when the size is fully utilized. Comptime generation leverages this by calculating the exact context size required before compilation.
  /// 
  /// Context mode is part of the definition of the NFA, and can only be shared between Nfa's if they use the 
  /// exact same context mode. This means that if you compile a comptime pattern with maximally compact 
  /// context, you cannot share the same context between machines, requiring you to use a different context 
  /// between each such Nfa, wasting memory. The most rigid and optimal approach for comptime patterns, is to 
  /// define all patterns, and compile them together, so that the system can define the perfect context for 
  /// the family of machines. This however means that new patterns cannot be introduced later for the context 
  /// unless shown that the size of the new machine is less or equal to an original machine
  /// 
  /// On single threaded systems, ideally only one context object is created and shared between all machines
  /// 
  fixed: comptime_int,
  /// 'fixed' but maximally compact. Asserts compilation is being done at comptime
  /// 
  compact_fixed,
};

/// The mutable core of the Nfa. Separated from the immutable part for better multithreading
/// 
/// The compiled machine is fully immutable, and with zero error signatures (for the core match api)
/// This is achieved by ensuring that the context is properly warmed up before usage.
/// 
/// An important job of this structure is to collapse the generic switching between lists implementations,
///   in order to make the codebase more lsp friendly
///
pub fn Context(
  comptime mode: Mode,
  comptime breakpoint: nfa.state.Breakpoint,
) type {
  return struct {
    data: DataType,
    /// The currently active list index in .lists
    /// The active list: states we are currently at
    /// The inactive list: the next active states being accumulated
    ///
    /// After a single step, the lists are swapped, and previously active list is reset
    current_list_idx: u1 = 0,
    /// A unique identifier incremented after each step
    /// 
    /// This is never reset between matches using this same context. 
    ///   If the machine is ran multiple times, it will start at some value k.
    ///
    /// current_list_id - k = number_of_chars_consumed
    list_id: ListId = 0,

    /// When the accept state was previously added to the potential next states
    previous_accept_append_list_id: ?ListId = null,

    const Self = @This();

    // ListId has to be large enough, to ensure that it can be accumulated indefinitely without overflowing
    // Practically, it will never overflow. if it does, then it wraps back to 0, which is only a problem, if there are sections of the automata, that execute in **extremely** rare circumstances
    pub const ListId = u64;
    pub const Idx = nfa.state.DefineIdx(breakpoint);

    pub const DataType = switch (mode) {
      .dynamic => lists.DynamicLists(ListId, breakpoint),
      .fixed => |len| lists.BoundedLists(ListId, len),
      .compact_fixed => unreachable,
    };

    /// states_len: runtime states length
    pub inline fn init(gpa: Allocator, states_len: usize) Allocator.Error!Self {
      switch (comptime mode) {
        .fixed => |max_len| {
          assert(states_len <= max_len);
          return Self{ .data = DataType.empty };
        },
        .dynamic => {
          if (@inComptime()) @compileError("Dynamic context cannot be generated at comptime!");
          return Self{ .data = try DataType.init(gpa, states_len)};
        },
        .compact_fixed => unreachable,
      }
    }

    /// Creates a context compatible for a single Nfa
    pub inline fn create(gpa: Allocator, states_len: usize) Allocator.Error!*Self {
      const r: *Self = try gpa.create(Self);
      errdefer gpa.destroy(r);

      r.* = try Self.init(gpa, states_len);
      return r;
    }

    /// Creates a single context compatible for a system of Nfas: &.{nfa} ++ including
    pub inline fn initForMany(comptime Nfa: type, gpa: Allocator, one: Nfa, including: []const Nfa) Allocator.Error!Self {
      var m: usize = one.requiredContextLen();
      for (including) |n| m = @max(m, n.requiredContextLen());

      return Self.init(gpa, m);
    }

    /// Checks for an nfa ducktype
    pub fn assertNfaDucktype(comptime T: type) void {
      comptime {
        const is_nfa = @hasDecl(T, "requiredContextLen") and
          @hasField(T, "states") and
          @hasField(T, "sets") and
          @hasField(T, "formulation");

        if (!is_nfa) @compileError("The passed nfa(s) are not nfa(s)!");
      }
    }

    fn getCollectiveContextLengthRequirement(including: anytype) usize {
      const T = @TypeOf(including);

      const m = if (comptime pzre.meta.isForIterable(T)) b: {
        const Child = pzre.meta.GetChild(T).?;
        comptime assertNfaDucktype(Child);

        var m: usize = 0;
        for (including) |n| m = @max(m, n.requiredContextLen());
        break :b m;
      } else b: {
          comptime assertNfaDucktype(T);
        break :b including.requiredContextLen();
      };

      return m;
    }

    /// Updates the context in order to also be compatible with new nfa's
    /// 
    /// The context remains compatible with any previously compatible automatas
    /// 
    /// Retains existing capacity if the new nfas are smaller than the current capacity.
    ///
    /// 'including' is a single nfa, or an iterable collection (tuple, slice etc)
    pub fn update(self: *Self, gpa: Allocator, including: anytype) Allocator.Error!void {
      const m = getCollectiveContextLengthRequirement(including);

      switch (comptime mode) {
        .fixed => |max_len| {
          assert(m <= max_len);
          return;
        },
        .dynamic => try self.data.update(gpa, m),
        .compact_fixed => unreachable,
      }
    }

    /// Brings the context down to a completely new set of nfa's, 
    /// 
    /// discarding any old compatibilities if they are not present in the including list.
    /// 
    /// Potentially shrinks the allocations to exactly match the new maximum requirement.
    ///
    /// 'including' is a single nfa, or an iterable collection (tuple, slice etc)
    pub fn updateExact(self: *Self, gpa: Allocator, including: anytype) Allocator.Error!void {
      const m = getCollectiveContextLengthRequirement(including);

      switch (comptime mode) {
        .fixed => |max_len| {
          assert(m <= max_len);
          return;
        },
        .dynamic => try self.data.updateExact(gpa, m),
        .compact_fixed => unreachable,
      }
    }

    pub inline fn setStateLastlist(self: *Self, state_idx: Idx, list_id: ListId) void {
      switch (comptime mode) {
        .fixed => self.data.last_list_idxs.set(state_idx, list_id),
        .dynamic => self.data.last_list_idxs.items[state_idx] = list_id,
        .compact_fixed => unreachable,
      }
    }

    pub inline fn stateLastlist(self: *Self, state_idx: Idx) ListId {
      return switch (comptime mode) {
        .fixed => self.data.last_list_idxs.get(state_idx),
        .dynamic => self.data.last_list_idxs.items[state_idx],
        .compact_fixed => unreachable,
      };
    }

    pub inline fn clearNext(self: *Self) void {
      const idx = self.nextlistIdx();
      switch (comptime mode) {
        .fixed => self.data.lists[idx].len = 0,
        .dynamic => self.data.lists[idx].clearRetainingCapacity(),
        .compact_fixed => unreachable,
      }
    }

    pub inline fn currentlist(self: *Self) []const Idx {
      const idx = self.current_list_idx;
      return switch (comptime mode) {
        .fixed => self.data.lists[idx].constSlice(),
        .dynamic => self.data.lists[idx].items,
        .compact_fixed => unreachable,
      };
    }

    /// Appends a state index to one of the two lists
    pub inline fn appendList(self: *Self, list_idx: u1, state_idx: Idx) void {
      self.data.lists[list_idx].appendAssumeCapacity(state_idx);
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
      return self.currentSlice().len > 0;
    }

    pub inline fn deinit(self: *Self, gpa: Allocator) void {
      if (comptime mode == .dynamic) {
        self.data.deinit(gpa);
      }
    }

    // In context.zig -> Context
    pub fn sizeOf(self: Self) usize {
      // @sizeOf(Self) perfectly captures the active list index, the list ID,
      // the previous accept list ID, the data struct, and all memory padding.
      return @sizeOf(Self) + self.data.heapSize();
    }

    pub inline fn reset(self: *Self) void {
      self.current_list_idx = 0;
      self.data.reset();
      self.previous_accept_append_list_id = null;
    }

  };
}

pub const PoolError = Allocator.Error || Io.Cancelable;

pub const PoolConfig = struct {
  /// Setting initial_capacity == num_of_workers, 
  ///   completely removes memory allocations post nfa compilation
  ///
  /// It is pointless to set this higher than num_of_workers
  initial_capacity: usize = 0,
  /// On release, if the number of contexts in the pool is at max_capacity, 
  ///   the released context is destroyed
  ///
  max_capacity: ?usize = null,
  /// The pool is warmed up at init in order to remove all post-init allocations 
  ///
  /// Initial capacity is ignored if this is true
  perform_warmup_routine: bool = true,
};

/// A pool of contexts for a runtime multithreaded environment
///
/// Can be shared between nfa's if the comptime inputs to this function match
/// 
/// Sharing works regardless if the pattern was compiled at comptime or runtime
///
/// Each thread should acquire a context, perform the match, and release it.
pub fn Pool(
  comptime mode: Mode,
  comptime breakpoint: nfa.state.Breakpoint,
  comptime config: PoolConfig,
) type {

  return struct {
    mutex: Mutex = .init,
    pool: ArrayList(*Ctx) = .empty,
    total_contexts: std.atomic.Value(usize) = .init(0),

    const Self = @This();
    pub const Ctx = Context(mode, breakpoint);

    pub const empty: Self = .{};

    /// Inits a pool that can be shared between all nfas
    ///
    /// if mode == .fixed, then all nfa's are required to have the exact same length
    pub fn init(comptime Nfa: type, gpa: Allocator, io: Io, n_workers: usize, one: Nfa, including: []const Nfa) PoolError!Self {
      if (@inComptime()) @compileError("Multithreading is not legal in comptime.");

      var s: Self = .empty;
      errdefer s.deinit(gpa);

      if (config.perform_warmup_routine) {
        try s.warmup(Nfa, gpa, io, n_workers, one, including);
      }
      else {
        const amount = comptime if (config.max_capacity) |max| 
          @min(max, config.initial_capacity) else config.initial_capacity;

        try s.pool.ensureTotalCapacityPrecise(gpa, amount);
        const m = maxStates(Nfa, one, including);

        for (0 .. amount) |_| {
          const data = try Ctx.create(gpa, m);
          s.pool.appendAssumeCapacity(data);
        }

        s.total_contexts = .init(amount);
        assert(s.pool.items.len == amount);
      }

      return s;
    }

    /// Performs a warmup routine given a family of nfas such that warmup-phase allocations are removed
    /// Assumes existing workers have finished working before this is called
    ///
    /// if n_workers is the peak number of workers, the pool will never perform another allocation
    ///
    /// Can also be used for resizing an existing pool, to a new pool for a new set of workers and nfa's
    pub fn warmup(self: *Self, comptime Nfa: type, gpa: Allocator, io: Io, n_workers: usize, one: Nfa, including: []const Nfa) PoolError!void {

      if (comptime mode == .fixed) {
        const max_len = mode.fixed;
        assert(one.requiredContextLen() <= max_len);
        for (including) |n| assert(n.requiredContextLen() <= max_len);
      }
      assert(self.pool.items.len == self.total_contexts.load(.acquire)); // contexts were not returned

      try self.mutex.lock(io);
      defer self.mutex.unlock(io);

      const m = maxStates(Nfa, one, including);
      const start_len = self.pool.items.len;

      if (n_workers <= start_len) {
        for (n_workers..start_len) |i| {
          const ctx = self.pool.items[i];
          destroyCtx(gpa, ctx);
        }
        self.pool.shrinkAndFree(gpa, n_workers);
        self.total_contexts = .init(n_workers);
        // Due to how shrinkAndFree is implememented, the ONLY way it does not change the capacity to
        // the input value, is when it encounters a memory error. In that case we error aswell
        if (self.pool.capacity != n_workers) return error.OutOfMemory;

      } else {
        try self.pool.ensureTotalCapacityPrecise(gpa, n_workers);
        for (start_len..n_workers) |_| {
          const data = try Ctx.create(gpa, m);
          self.pool.appendAssumeCapacity(data);
          _ = self.total_contexts.fetchAdd(1, .release);
        }
      }

      for (0..self.pool.items.len) |i| {
        var ctx = self.pool.items[i];
        if (mode == .dynamic) try ctx.data.update(gpa, m);
      }

      assert(self.pool.items.len == n_workers);
    }

    /// Performs allocations when mode == .dynamic, and:
    /// 1. the system is warming up:
    ///    - not enough contexts: if initial_capacity <= peak_capacity
    ///    - contexts too low capacity: largest nfa has not used each context
    /// 
    /// 2. max_capacity <= worker_count
    ///     a sudden peak of workers will allocate new temporary contexts
    ///   
    /// The returned context has to be released back into the pool or destroyed
    pub fn acquire(self: *Self, gpa: Allocator, io: Io, states_len: usize) PoolError!*Ctx {

      try self.mutex.lock(io);
      if (self.pool.pop()) |ctx| {
        defer self.mutex.unlock(io);
        if (comptime mode == .dynamic) {
          ctx.data.update(gpa, states_len) catch |err| {
            self.release(gpa, io, ctx);
            return err;
          };
        }
        return ctx;
      }
      // Guarantee space in the array for when the context is returned
      {
        defer self.mutex.unlock(io);
        try self.pool.ensureTotalCapacity(gpa, self.total_contexts.load(.acquire) + 1);
        _ = self.total_contexts.fetchAdd(1, .release);
      }

      const data = Ctx.create(gpa, states_len) catch |err| {
        // If context creation fails, revert the counter so we don't leak capacity requirements
        _ = self.total_contexts.fetchSub(1, .release);
        return err;
      };

      return data;
    }

    /// Returns a context back to the pool or destroys it if its full
    pub fn release(self: *Self, gpa: Allocator, io: Io, ctx: *Ctx) void {
      self.mutex.lock(io) catch { // The OS sent a teardown signal
        ctx.deinit(gpa);
        _ = self.total_contexts.fetchSub(1, .release);
        return;
      };
      defer self.mutex.unlock(io);

      if (config.max_capacity) |max| {
        if (self.pool.items.len >= max) {
          _ = self.total_contexts.fetchSub(1, .release);
          destroyCtx(gpa, ctx);
          return;
        }
      }
      // Pool is guaranteed to hold enough capacity
      assert(self.pool.capacity >= self.pool.items.len);
      self.pool.appendAssumeCapacity(ctx);
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      assert(self.total_contexts.load(.acquire) == self.pool.items.len);
      for (self.pool.items) |ctx| destroyCtx(gpa, ctx);
      self.pool.deinit(gpa);
      self.total_contexts = .init(0);
    }

    fn destroyCtx(gpa: Allocator, ctx: *Ctx) void {
      ctx.deinit(gpa);
      gpa.destroy(ctx);
    }
  };
}

pub fn maxStates(comptime Nfa: type, one: Nfa, including: []const Nfa) usize {
  var m: usize = one.requiredContextLen();
  for (including) |n| m = @max(m, n.requiredContextLen());
  return m;
}
