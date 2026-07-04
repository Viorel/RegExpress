//! The heavyweight mutable core of the NFA
const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const assert = std.debug.assert;
const Mutex = std.Io.Mutex;
const Io = std.Io;

const pzre = @import("../root.zig");
const arch = @import("arch.zig");
const search = @import("search.zig");

const misc = pzre.misc;
const debug = pzre.lens.debug;

/// The context of an architecture
/// 
/// Contexts can be freely shared between architectures that implement the exact same context architecture 
/// and define the same mode. E.g. two minimal_nfa's using different offset breakpoints can share contexts as 
/// long as the mode is the same.
/// 
/// On single threaded systems, ideally only one context object is defined and created and shared between all 
/// machines.
/// 
/// On multithreaded systems, a Cache should be defined instead
/// 
/// context-shareability means that different Regex (type-)instances can use the same single context instance
/// 
pub const Mode = union (enum) {
  /// Defines a dynamic list as the context type. Managed by gpa
  /// 
  /// Useful for maximum flexibility, the context can freely size up and down in order to match new
  ///   runtime requirements.
  /// 
  /// The list is compact, and sized exactly to fit the set of machines
  /// The context length is bounded by the breakpoint
  /// 
  dynamic: arch.AbsoluteBreakpoint,
  /// Defines a static array as the context type.
  /// 
  /// Requires no allocator; allocators can be passed as 'undefined', and errors ignored
  /// Use this for a gpa-free stack context
  /// 
  /// The value defines the upper bound (the maximum length), of the context. And will immediately consume 
  /// that amount of memory when created, regardless of what NFA uses it. It cannot adjust to new machines, 
  /// any update methods are no-op.
  /// 
  /// Useful when you want a context that never resizes. Its management requires less instructions, and takes 
  /// up less memory than .dynamic when the size is fully utilized. Comptime generation leverages this by 
  /// calculating the exact context size required before compilation.
  /// 
  /// Example:
  ///   Consider you wish to use a single shared context with fixed size 300 -> context.breakpoint = u8
  ///   You have some large 300-state machine (offset i16)
  ///   but then many small ones  (offset i8)
  /// 
  fixed: usize,
  /// 'fixed' but maximally compact. Asserts compilation is being done at comptime
  /// 
  /// This is achieved as follows
  /// 1. the pattern is parsed and optimized, then analyzed for state_count
  /// 2. the .compact_fixed field is set to .fixed with this concrete state_count
  /// 
  /// Very hostile for context-shareability. Multiple machines compiled using this flag will be defined for 
  /// different .fixed context types, making it impossible for them to share contexts (caught at comptime: 
  /// type error).
  ///
  compact_fixed,

  const Self = @This();

  pub fn eql(self: Self, other: Self) bool {
    const a = std.meta.activeTag(self);
    const b = std.meta.activeTag(other);
    if (a != b) return false;
    return switch (self) {
      .dynamic => |d| d == other.dynamic,
      .compact_fixed => self == other,
      .fixed => |c| c == other.fixed,
    };
  }
};

/// See 'Mode'
pub const ModeResolved = union (enum) {
  dynamic: arch.AbsoluteBreakpoint,
  fixed: usize,

  const Self = @This();

  /// Returns the implied context breakpoint
  pub fn breakpoint(comptime self: Self) arch.AbsoluteBreakpoint {
    comptime return switch (self) {
      .dynamic => |d| d,
      .fixed => |c| arch.AbsoluteBreakpoint.define(c),
    };
  }

  pub fn eql(self: Self, other: Self) bool {
    const a = std.meta.activeTag(self);
    const b = std.meta.activeTag(other);
    if (a != b) return false;
    return switch (self) {
      .dynamic => |d| d == other.dynamic,
      .fixed => |c| c == other.fixed,
    };
  }
};

/// The mutable core of the Nfa.
/// Separated from the immutable part for better multithreading
///
/// The compiled machine is fully immutable, and with zero error signatures (for the core match api)
/// This is achieved by ensuring that the context is properly warmed up before usage.
///
/// An important job of this structure is to collapse the generic switching between lists implementations,
///   in order to make the codebase more lsp friendly
///
pub fn Context(
  comptime Data: type,
) type {
  return struct {
    data: Data,

    const Self = @This();

    pub const ListId = Data.ListId;
    pub const Idx = Data.Idx;

    pub const empty = Self{.data = .empty};
    pub const is_many_context = false;

    /// states_len: runtime states length
    pub inline fn init(gpa: Allocator, states_len: usize) Allocator.Error!Self {
      return Self{ .data = try Data.init(gpa, states_len) };
    }

    /// states_len: runtime states length
    pub inline fn initFixed() Self {
      return Self{ .data = Data.initFixed() };
    }

    /// Creates a context compatible for a single Nfa
    pub inline fn create(gpa: Allocator, states_len: usize) Allocator.Error!*Self {
      const r: *Self = try gpa.create(Self);
      errdefer gpa.destroy(r);

      r.* = try Self.init(gpa, states_len);
      return r;
    }

    /// Creates a single context compatible for a system of Nfas: &.{nfa} ++ including
    pub inline fn initForMany(
      comptime ib: arch.AbsoluteBreakpoint,
      gpa: Allocator,
      one: search.Formulation(ib),
      including: []const search.Formulation(ib),
    ) Allocator.Error!Self {
      var m: usize = one.requiredContextLen();
      for (including) |n| m = @max(m, n.requiredContextLen());

      return Self.init(gpa, m);
    }

    /// Updates the context in order to also be compatible with new nfa's
    ///
    /// The context remains compatible with any previously compatible automatas
    ///
    /// Retains existing capacity if the new nfas are smaller than the current capacity.
    ///
    /// 'including' is a single nfa, or an iterable collection (tuple, slice etc)
    pub fn update(self: *Self, gpa: Allocator, including: anytype) Allocator.Error!void {
      const m = getCollectiveContextLengthRequirement(self.len(), including);
      try self.updateLength(gpa, m);
    }

    /// Brings the context down to a completely new set of nfa's,
    ///
    /// discarding any old compatibilities if they are not present in the including list.
    ///
    /// Potentially shrinks the allocations to exactly match the new maximum requirement.
    ///
    /// 'including' is a single nfa, or an iterable collection (tuple, slice etc)
    pub fn updateExact(self: *Self, gpa: Allocator, including: anytype) Allocator.Error!void {
      const m = getCollectiveContextLengthRequirement(self.len(), including);
      try self.updateLengthExact(gpa, m);
    }

    /// Updates the context in order to also be compatible with new nfa's of new length
    pub fn updateLength(self: *Self, gpa: Allocator, new_len: usize) Allocator.Error!void {
      try self.data.update(gpa, new_len);
    }

    /// Brings the context down to a completely new set of nfa's of new length
    pub fn updateLengthExact(self: *Self, gpa: Allocator, new_len: usize) Allocator.Error!void {
      try self.data.updateExact(gpa, new_len);
    }

    pub inline fn deinit(self: *Self, gpa: Allocator) void {
      self.data.deinit(gpa);
    }

    /// Returns the length of the context
    /// e.g. the maximum supported submachine length
    pub inline fn len(self: *Self) usize {
      return self.data.len();
    }

    pub fn sizeOf(self: Self) usize {
      return @sizeOf(Self) + self.data.heapSize();
    }

    pub inline fn reset(self: *Self) void {
      self.data.reset();
    }
  };
}

///
/// A heterogeneous collection of Context values, one per unique Context type
/// required by a Regex over multiple architectures.  Field naming is numeric
/// ("0", "1", ...), with the type of each field being one of the Context types
/// passed at type construction.
///
/// The API is duck-typed against single Context
///
/// Updates differ in shape because they're length-per-field rather than a
/// single length:
///   updateLength(lens: [field_count]usize)
///   updateLengthExact(lens: [field_count]usize)
///
/// A Regex acts on Context while Regex acts on ManyContext
/// Cache/Pool acts on either of the two uniformly due to ducktyping
///
/// Every passed Context needs to have a public .empty decl
/// 
/// --- Example ---
/// 
/// Consider Regex is defined using:
/// 
/// const archs = &.{
///  .{ .minimal_nfa = .{.offset_bp = .i8, .context = .{ .dynamic = .i16 }} },
///  .{ .minimal_nfa = .{.offset_bp = .i16, .context = .{ .dynamic = .i16 }} },
///  .{ .pike_vm = .{ ..., .context = .{ .fixed = 128 }} },
/// };           // pike_vm not implemented as of writing
/// 
/// We have 3 machine architectures and 2 different context architectures A and B. 
/// This Regex type will generate the context type:
/// 
/// ManyContext{
///   0: A,
///   1: B,
/// }
/// 
/// Consider now that the user compiles 100 machines. 10 pike vm's and 90 minimal nfas
/// The largest minimal nfa submachine size is 32. The largest pike vm submachine size is 93
/// 
/// The user is running a single-threaded system, and he decides to generate a Context for the Regex
/// He has a slice of compiled regexes `const re: [100]Regex = ...`
/// He initiates a context `var ctx = try re[0].initContextIncluding(gpa, re[0..]);`
/// The 0 field will be inited with length 32 (dynamic)
/// The 1 field will be inited with length 128 (fixed); because its always the same size. Updating it is 
///   essentially a no-op. The memory of the "1" field exists fully on the stack. "0" is partially fragmented 
///   on the heap.
/// 
pub fn ManyContext(comptime context_types: []const type) type {
  if (context_types.len == 0) {
    @compileError("ManyContext requires at least one context type");
  }

  return struct {
    ctx: ContextStruct,

    const Self = @This();
    pub const is_many_context = true;

    pub const ContextStruct = b: {
      const Attr = std.builtin.Type.StructField.Attributes;
      var field_names: [context_types.len][]const u8 = undefined;
      var field_attrs: [context_types.len]Attr = undefined;

      for (context_types, 0..) |T, i| {
        field_names[i] = std.fmt.comptimePrint("{d}", .{i});
        field_attrs[i] = Attr{
          .@"comptime" = false,
          .@"align" = @alignOf(T),
          .default_value_ptr = null,
        };
      }
      break :b @Struct(.auto, null, &field_names, @ptrCast(context_types), &field_attrs);
    };
    pub const field_count = context_types.len;

    pub const empty: Self = b: {
      var s: Self = undefined;
      for (std.meta.fields(ContextStruct)) |field| {
        @field(s.ctx, field.name) = .empty;
      }
      break :b s;
    };

    pub fn init(gpa: Allocator, lens: *const [field_count]usize) Allocator.Error!Self {
      var s: Self = undefined;
      var initialized: usize = 0;
      errdefer {
        // Tear down any fields we successfully initialized before failure.
        inline for (std.meta.fields(ContextStruct), 0..) |field, i| {
          if (i < initialized) @field(s.ctx, field.name).deinit(gpa);
        }
      }
      inline for (std.meta.fields(ContextStruct), 0..) |field, i| {
        @field(s.ctx, field.name) = try @TypeOf(@field(s.ctx, field.name)).init(gpa, lens[i]);
        initialized = i + 1;
      }
      return s;
    }

    pub fn initFixed() Self {
      var s: Self = undefined;
      inline for (std.meta.fields(ContextStruct)) |field| {
        @field(s.ctx, field.name) = @TypeOf(@field(s.ctx, field.name)).initFixed();
      }
      return s;
    }

    pub fn create(gpa: Allocator, lens: *const [field_count]usize) Allocator.Error!*Self {
      const r: *Self = try gpa.create(Self);
      errdefer gpa.destroy(r);
      r.* = try Self.init(gpa, lens);
      return r;
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      inline for (std.meta.fields(ContextStruct)) |field| {
        @field(self.ctx, field.name).deinit(gpa);
      }
    }

    /// Heap size + self size
    pub fn sizeOf(self: Self) usize {
      var sum: usize = @sizeOf(Self);
      inline for (std.meta.fields(ContextStruct)) |field| {
        sum += @field(self.ctx, field.name).heapSize();
      }
      return sum;
    }

    pub fn reset(self: *Self) void {
      inline for (std.meta.fields(ContextStruct)) |field| {
        @field(self.ctx, field.name).reset();
      }
    }

    /// Per-field length update.  Lengths of 0 leave the corresponding field
    /// untouched (no shrink, no grow).  Used by the Regex wrapper to update
    /// only the fields its included archs actually need.
    pub fn updateLength(self: *Self, gpa: Allocator, lens: *const [field_count]usize) Allocator.Error!void {
      inline for (std.meta.fields(ContextStruct), 0..) |field, i| {
        if (lens[i] > 0) {
          try @field(self.ctx, field.name).updateLength(gpa, lens[i]);
        }
      }
    }

    /// Per-field exact update.  Lengths of 0 leave the corresponding field
    /// untouched.  Pass nonzero values for every field you want to resize
    /// (including shrinking).
    pub fn updateLengthExact(self: *Self, gpa: Allocator, lens: *const [field_count]usize) Allocator.Error!void {
      inline for (std.meta.fields(ContextStruct), 0..) |field, i| {
        if (lens[i] > 0) {
          try @field(self.ctx, field.name).updateLengthExact(gpa, lens[i]);
        }
      }
    }
  };
}

/// A generalized Context Cache acting uniformly on ManyContext or Context.
/// Similar to a Pool but simpler and rigid
/// 
/// Mechanically, the cache is very simple. The recommended usage pattern is: 
///   1. compile N new machines in bulk
///   2. initiate the cache for M workers given the N machines
///   3. acquire/release contexts as needed
/// 
/// The cache will never perform any allocations when acquiring/releasing because the cache is warmed up on init
/// 
/// When you have to compile new machines, or you wish to update the number of workers you should:
///   1. compile as many machines in bulk as you can
///   2. wait for all outstanding contexts to be released back into the cache
///   3. warmup the cache for the new machines
///   4. do not use the cache while its being warmed up
///   5. resume using the cache as usual
/// 
/// The assumption of the cache always being properly warmed up makes the cache faster and simplifies its 
/// signatures. Because the regex matching API is allocator-free, working threads do not need allocators even 
/// when interacting with a cache.
/// 
pub const WarmupError = Allocator.Error || Io.Cancelable;
 
pub fn Cache(
  /// Either ManyContext or Context definition
  comptime T: type,
) type {
  return struct {
    mutex: Mutex = .init,
    cache: ArrayList(*Ctx) = .empty,
 
    const Self = @This();
    pub const empty: Self = .{};
    pub const Ctx = T;

    /// 1 for Context
    /// N for the width of ManyContext
    const using_manycontext = Ctx.is_many_context;
    const context_width = if (!using_manycontext) 1 else b: {
      const ctx_struct = std.meta.fields(Ctx)[0];
      break :b std.meta.fields(ctx_struct.type).len;
    };
 
    pub fn init(
      gpa: Allocator,
      io: Io,
      n_workers: usize,
      setup_lengths: *const [context_width]usize,
    ) WarmupError!Self {
      if (@inComptime()) @compileError("Multithreading is not legal in comptime.");
      var s: Self = .empty;
      errdefer s.deinit(gpa);
      try s.warmup(gpa, io, n_workers, setup_lengths);
      return s;
    }
 
    /// Resizes the cache to `n_workers` and updates every context to satisfy `setup_lengths`.
    ///
    /// Requires that no contexts are currently acquired, the caller must
    /// have drained all outstanding contexts back into the cache first.
    /// 
    /// The contexts are extended as needed; never shrunk. No need to pass the old lengths, the cache stays 
    /// valid for the old machines
    pub fn warmup(
      self: *Self,
      gpa: Allocator,
      io: Io,
      n_workers: usize,
      setup_lengths: *const [context_width]usize,
    ) WarmupError!void {
      try self.mutex.lock(io);
      defer self.mutex.unlock(io);
      try self.updateContextAmount(gpa, n_workers, setup_lengths);
 
      // Resize surviving contexts to the new profile.
      for (self.cache.items) |ctx| {
        const len = if (comptime using_manycontext) setup_lengths else setup_lengths[0];
        try ctx.updateLength(gpa, len);
      }
      assert(self.cache.items.len == n_workers);
    }

    /// Resizes the cache to `n_workers` and updates every context to satisfy `setup_lengths`.
    ///
    /// Requires that no contexts are currently acquired, the caller must
    /// have drained all outstanding contexts back into the cache first.
    /// 
    /// The contexts are set to the exact length. Any old machines not accounted for in setup_lengths are 
    /// invalidated
    /// 
    pub fn warmupExact(
      self: *Self,
      gpa: Allocator,
      io: Io,
      n_workers: usize,
      setup_lengths: *const [context_width]usize,
    ) WarmupError!void {
      try self.mutex.lock(io);
      defer self.mutex.unlock(io);
      try self.updateContextAmount(gpa, n_workers, setup_lengths);
 
      // Resize surviving contexts to the new profile.
      for (self.cache.items) |ctx| {
        const len = if (comptime using_manycontext) setup_lengths else setup_lengths[0];
        try ctx.updateLengthExact(gpa, len);
      }
      assert(self.cache.items.len == n_workers);
    }

    fn updateContextAmount(
      self: *Self,
      gpa: Allocator,
      n_workers: usize,
      setup_lengths: *const [context_width]usize,
    ) WarmupError!void {
      const start_len = self.cache.items.len;
      if (n_workers <= start_len) {
        // Shrink: destroy excess, then truncate the array.
        for (n_workers..start_len) |i| destroyCtx(gpa, self.cache.items[i]);
        self.cache.shrinkRetainingCapacity(n_workers);
        self.cache.shrinkAndFree(gpa, n_workers);
        if (self.cache.capacity != n_workers) return error.OutOfMemory;
      } else {
        // Grow: allocate the deficit at the new target lengths.
        try self.cache.ensureTotalCapacityPrecise(gpa, n_workers);
        for (start_len..n_workers) |_| {

          const len = if (comptime using_manycontext) setup_lengths else setup_lengths[0];
          const ctx = try Ctx.create(gpa, len);

          self.cache.appendAssumeCapacity(ctx);
        }
      }
    }
 
    /// Acquires a context from the cache. Blocks if the cache is empty until
    /// another thread releases one. Never allocates.
    ///
    /// The returned context is already sized to match the cache's most recent
    /// warmup profile.  Match operations on it are allocation-free.
    pub fn acquire(self: *Self, io: Io) Io.Cancelable!*Ctx {
      try self.mutex.lock(io);
      defer self.mutex.unlock(io);
      return self.cache.pop() orelse unreachable;
    }
 
    pub fn release(self: *Self, io: Io, ctx: *Ctx) void {
      self.mutex.lock(io) catch { // OS teardown signal; we drop the context.
        // Cannot reclaim; the cache may be deinit-ing concurrently.  In
        // practice this path only fires during process shutdown.
        return;
      };
      defer self.mutex.unlock(io);
 
      // The cache's capacity equals its initial worker count (set by warmup),
      // so the append cannot exceed capacity unless a caller is releasing
      // a context that was never acquired from this cache.
      assert(self.cache.capacity >= self.cache.items.len + 1);
      self.cache.appendAssumeCapacity(ctx);
    }
 
    pub fn deinit(self: *Self, gpa: Allocator) void {
      for (self.cache.items) |ctx| destroyCtx(gpa, ctx);
      self.cache.deinit(gpa);
    }
 
    fn destroyCtx(gpa: Allocator, ctx: *Ctx) void {
      ctx.deinit(gpa);
      gpa.destroy(ctx);
    }
  };
}
 
pub fn maxStates(
  comptime ib: arch.AbsoluteBreakpoint,
  one: search.Formulation(ib),
  including: []const search.Formulation(ib),
) usize {
  var m: usize = one.requiredContextLen();
  for (including) |n| m = @max(m, n.requiredContextLen());
  return m;
}

/// Calculates the context length including self_len and any other contexts
/// if self_len is nonexistent, pass 0
/// 'including' is any ducktyped architecture structure with the requiredContextLen() method
/// 
/// e.g. from contexts: 
///   getCollectiveContextLengthRequirement(self.len(), including)
/// 
/// e.g. from machines: 
///   context.getCollectiveContextLengthRequirement(self.formulation.requiredContextLen(), including)
///
pub fn getCollectiveContextLengthRequirement(self_len: usize, including: anytype) usize {
  const T = @TypeOf(including);
  const E = @TypeOf(.{});
  const is_empty = T == E or (if (pzre.meta.GetChild(T)) |Child| Child == E);
  
  if (comptime is_empty) {
    return self_len;
  } else {
    comptime assert(pzre.meta.hasDeclAll(T, "requiredContextLen"));
    
    var m: usize = 0;
    
    if (comptime pzre.meta.isForIterableTuple(T)) {
      inline for (including) |n| m = @max(m, n.requiredContextLen());
    } else if (comptime pzre.meta.isForIterable(T)) {
      for (including) |n| m = @max(m, n.requiredContextLen());
    } else {
      m = including.requiredContextLen();
    }
    
    return @max(self_len, m);
  }
}
