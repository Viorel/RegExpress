//! The object with the matching api
//! 
const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const Io = std.Io;

const pzre = @import("root.zig");
const debug = pzre.lens.debug;
const Range = pzre.structures.range.Range;
const ComptimeArrayList = pzre.structures.comptime_arraylist.ComptimeArrayList;
const Global = pzre.compile.Global;

const ArchResolved = pzre.arch.ArchResolved;
const strategy = pzre.compile.strategy;
const context = pzre.arch.context;

const regex = pzre.regex;
const Match = regex.Match;
const ManyReplacements = regex.ManyReplacements;
const Replacement = regex.Replacement;

const Config = pzre.compile.Config;
const CountingAllocator = pzre.CountingAllocator;
const MemoryModel = pzre.MemoryModel;

/// # Strictly-Typed Regex Engine
/// Public facing pattern-matching API. Defined for a single architecture.
/// 
/// PZRE is designed to support various configurable architectures. Integer sizes, context types, and even 
/// their internal search algorithms are configurable. The goal is to give the user the power to either 
/// explicitly choose the exact architecture they wish to use, or let the system pick it for them.
/// 
/// This is the underlying Regex type providing the API definitions.
/// 
/// This object exists for users who prefer to painfully handle the friction of a 
/// non-type-erased Regex. Use this if you refuse to pay the dynamic dispatch overhead 
/// of the polymorphic wrapper, demand the smallest possible object size, or want zero 
/// risk of bloating the final binary with unused architectures.
/// 
/// It is also useful when you wish to target specific architectures
/// 
/// See AnyRegex for the type-erased polymorphic wrapper, or Pack for an efficient 
/// collection of compiled architectures.
///
pub fn Regex(
  /// The architecture configuration used by the solver
  comptime arch: ArchResolved,
  comptime global: Global,
) type {

  return struct {
    internals: Internals,

    const Self = @This();

    /// It's best to call this only once, unless you wish to trust that Zig will properly cache
    ///   comptime parsed pattern information
    pub const Internals = arch.Internals(global.problem_bp, global.sets_bp);
    pub const non_allocator_context = Internals.non_allocator_context;

    // The Context type has to be grabbed thoughtfully so that LSP autocompletions work for the returned
    // Context objects.
    pub const Context = context.Context(arch.ContextData());
    pub fn ContextCache() type {
      return context.Cache(Context);
    }

    /// Builds this Regex from already-prepared compilation artifacts.
    ///
    /// This is a pure linking step. It does NOT run the compilation pipeline:
    /// the arch is already resolved (it parameterizes this type) and the artifacts
    /// (optimized AST, sets) are produced upstream by pzre.compile.prepareCompilation.
    /// Orchestration of the pipeline lives in the top-level compile/compileComptime
    /// functions in this file.
    ///
    /// the sets inside 'artifacts' is referenced within the returned Internals:
    ///   errdefer: deinit artifacts fully
    ///   post: deinit artifacts partially (everything but sets), unless you wish to keep the AST
    pub fn fromArtifacts(
      comptime config: Config,
      comptime model: MemoryModel,
      gpa: Allocator,
      artifacts: *pzre.compile.Artifacts,
      strat: strategy.Name,
    ) pzre.compile.Error!Self {
      assert(artifacts.sets.len <= std.math.maxInt(global.sets_bp.Index()));

      const internals = try arch.build(config.limits, model, global.problem_bp, global.sets_bp, gpa, artifacts, config.sets.word_set, strat);
      // debug.prettyPrint(.{
      //   .compiled = internals
      // });

      return Self{.internals = internals};
    }
   
    /// Compiles at runtime
    /// 
    /// Untrusted input safe
    pub fn compile(
      comptime config: Config,
      child_allocator: Allocator,
      pattern: []const u8,
    ) pzre.compile.Error!Self {
      var gpa = CountingAllocator.init(child_allocator, config.limits.gpa_upper_bound);
      return Self.compileMemoryUnsafe(config, .dynamic, gpa.allocator(), pattern) catch |err| switch (err) {
        error.OutOfMemory => {
          // Differentiate between reaching the resource cap, over a system out of memory error
          return if (gpa.cap_reached) error.AllocationUpperbound else error.OutOfMemory;
        },
        else => return err,
      };
    }
   
    /// Compiles at comptime
    ///
    /// Errors are intercepted as @compileError
    pub fn compileComptime(
      comptime config: Config,
      comptime pattern: []const u8,
    ) Self {
      comptime return compileMemoryUnsafe(config, .comptime_dynamic, undefined, pattern) catch |err| pzre.compile.compileError(err);
    }
   
    /// Compiles at comptime
    ///
    /// Errors are not intercepted
    pub fn compileComptimeNonIntercepting(
      comptime config: Config,
      comptime pattern: []const u8,
    ) pzre.compile.Error!Self {
      comptime return try compileMemoryUnsafe(config, .comptime_dynamic, undefined, pattern);
    }

    fn compileMemoryUnsafe(
      comptime config: Config,
      comptime model: MemoryModel,
      gpa: Allocator,
      pattern: []const u8,
    ) pzre.compile.Error!Self {
      const decision, const manifest, var artifacts = try pzre.compile.prepareCompilation(@as([]const ArchResolved, &.{arch}), config, model, gpa, pattern);
      _ = manifest;
      errdefer artifacts.deinit(gpa);
      assert(decision.idx == 0);
     
      const built = try Self.fromArtifacts(config, model, gpa, &artifacts, decision.bid.strategy);
      artifacts.deinitAllButSets(gpa);
      return built;
    }

    /// Similar to 'match' but instead finds the first match that starts within range 
    ///   [start_idx, max_base]   end inclusive
    pub inline fn find(self: Self, ctx: *Context, str: []const u8, start_idx: usize, max_base: usize) ?Match {
      return self.internals.find(ctx, str, start_idx, max_base);
    }

    /// Returns true if the entire string matches
    pub fn matchesExact(self: Self, ctx: *Context, str: []const u8) bool {
      const is_match = self.matchStart(ctx, str);
      if (is_match) |m| {
        if (m.len == str.len) return true;
      }
      return false;
    }

    /// Attempts to match the string
    /// Returns the head that matched;
    /// str[0..end <= str.len]
    pub inline fn matchStart(self: Self, ctx: *Context, str: []const u8) ?[]const u8 {
      if (self.find(ctx, str, 0, 0)) |result| {
        return result.str;
      } else return null;
    }

    /// True if any substring matches
    pub inline fn matches(self: Self, ctx: *Context, str: []const u8) bool {
      return self.match(ctx, str) != null;
    }

    /// Finds the first match
    pub inline fn match(self: Self, ctx: *Context, str: []const u8) ?Match {
      return self.find(ctx, str, 0, str.len);
    }

    /// Finds all matches and stores them in a slice
    pub fn findAllAlloc(self: Self, ctx: *Context, gpa: Allocator, str: []const u8) Allocator.Error![]Match {
      var r: ArrayList(Match) = .empty;
      var it = self.matchIter(ctx, str);

      while (it.next()) |m| {
        try r.append(gpa, m);
      }

      return r.toOwnedSlice(gpa);
    }

    /// Finds all matches and stores them in a slice at comptime
    pub fn findAllComptime(comptime self: Self, comptime ctx: *Context, comptime str: []const u8) []const Match {
      comptime {
        var r: ComptimeArrayList(Match) = .empty;
        var it = self.matchIter(ctx, str);

        while (it.next()) |m| {
          r.append(m);
        }

        return r.items;
      }
    }

    pub const MatchIterator = struct {
      idx: usize = 0,
      self: Self,
      str: []const u8,
      ctx: *Context,

      const It = @This();
      pub fn init(self: Self, ctx: *Context, str: []const u8) It {
        return .{
          .self = self,
          .str = str,
          .ctx = ctx,
        };
      }

      pub fn next(it: *It) ?Match {
        // debug.prettyPrint(.{
        //   .idx = it.idx,
        //   .str = it.str,
        // });

        if (it.idx > it.str.len) return null;
        if (it.self.find(it.ctx, it.str, it.idx, it.str.len)) |m| {
          // debug.prettyPrint(.{.m = m});
          const start = m.loc.start;
          const end = m.loc.end;
          assert(start >= it.idx);
          it.idx = if (start == end) end + 1 else end;
          return m;
        } else {
          it.idx = it.str.len + 1;
          return null;
        }
      }

      pub fn reset(it: *It) void {
        it.idx = 0;
      }
    };

    /// Returns an iterator for all matches
    pub fn matchIter(self: Self, ctx: *Context, str: []const u8) MatchIterator {
      return .init(self, ctx, str);
    }

    /// Finds all matches and replaces them with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    ///
    /// Returns the region where replacements occured and the number of replacements
    ///
    pub fn replaceAll(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
    ) Allocator.Error!?ManyReplacements {
      return self.replaceAllWithin(ctx, gpa, str, replacement, 0, str.len);
    }

    /// Finds all matches starting within range [start_idx, max_base]   end inclusive
    ///   and replaces them with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    ///
    /// Returns the region where replacements occured and the number of replacements
    ///
    pub fn replaceAllWithin(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
      start_idx: usize,
      max_base: usize,
    ) Allocator.Error!?ManyReplacements {
      assert(start_idx <= str.len);
      assert(start_idx <= max_base);

      var span: Range(usize) = .init(0, 0);
      var count: usize = 0;

      var it = self.matchIter(ctx, str);
      it.idx = start_idx;
      var r: std.ArrayList(u8) = .empty;
      errdefer r.deinit(gpa);

      var previous_match_end: usize = 0;

      if (it.next()) |first| {
        count += 1;
        span = first.loc;
        previous_match_end = first.loc.end;
        const head = str[0 .. first.loc.start];
        const mem = try r.addManyAsSlice(gpa, head.len + replacement.len);
        @memcpy(mem[0 .. head.len], head);
        @memcpy(mem[head.len .. head.len + replacement.len], replacement);
      } else return null;

      while (it.idx <= max_base) {
        if (it.next()) |result| {
          count += 1;
          span.end = result.loc.end;
          const head = str[previous_match_end .. result.loc.start];
          const mem = try r.addManyAsSlice(gpa, head.len + replacement.len);
          @memcpy(mem[0 .. head.len], head);
          @memcpy(mem[head.len .. head.len + replacement.len], replacement);

          previous_match_end = result.loc.end;
        } else break;
      }

      try r.appendSlice(gpa, str[previous_match_end..]);

      const new = try r.toOwnedSlice(gpa);
      return ManyReplacements{.new = new, .count = count, .span = span};
    }

    /// Finds the first match and replaces it with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    pub fn replaceFirst(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
    ) Allocator.Error!?Replacement {
      return self.replaceFirstWithin(ctx, gpa, str, replacement, 0, str.len);
    }

    /// Finds the first match that starts within range [start_idx, max_base]   end inclusive
    ///   and replaces it with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    pub fn replaceFirstWithin(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
      start_idx: usize,
      max_base: usize,
    ) Allocator.Error!?Replacement {
      assert(start_idx <= str.len);
      assert(start_idx <= max_base);
      if (self.find(ctx, str, start_idx, max_base)) |result| {
        const head = str[0 .. result.loc.start];
        const tail = str[result.loc.end ..];
        const r = try gpa.alloc(u8, head.len + replacement.len + tail.len);
        @memcpy(r[0 .. head.len], head);
        @memcpy(r[head.len .. head.len + replacement.len], replacement);
        @memcpy(r[head.len + replacement.len .. ], tail);
        return Replacement{.new = r, .span = result.loc};
      }
      return null;
    }

    /// Returns the length of the required context
    pub inline fn requiredContextLen(self: Self) usize {
      return self.internals.requiredContextLen();
    }

    /// Creates a single context for this nfa.
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn initContext(self: Self, gpa: Allocator) Allocator.Error!Context {
      return Context.init(gpa, self.requiredContextLen());
    }

    /// Creates a single context for this nfa.
    ///
    /// Contexts do not require manual reset
    /// 
    /// Asserts (at comptime) the context is fixed
    /// 
    pub fn initContextFixed(self: Self) Context {
      _ = self;
      assert(non_allocator_context);
      return Context.initFixed();
    }

    /// Creates a single context for a system of nfas, including this one
    /// 'including' need to be architectures that share the context type of Self
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn initContextIncluding(self: Self, gpa: Allocator, including: anytype) Allocator.Error!Context {
      const self_len = self.internals.requiredContextLen();
      const len = context.getCollectiveContextLengthRequirement(self_len, including);
      return try Context.init(gpa, len);
    }

    /// Updates an existing context to also support any included machines
    /// 'including' need to be architectures that share the context type of Self
    /// 
    /// Contexts do not require manual reset
    ///
    pub fn updateContext(self: Self, ctx: *Context, gpa: Allocator, including: anytype) Allocator.Error!void {
      const self_len = self.internals.requiredContextLen();
      const len = context.getCollectiveContextLengthRequirement(self_len, including);
      try ctx.updateLength(gpa, len);
    }

    /// Creates an optimized context cache for multithreaded systems.
    /// 
    /// The cache is optimized for this + including architectures
    /// 'including' need to be architectures that share the context type of Self
    /// 
    /// Each thread should acquire a context, perform the match, and release it.
    ///
    /// Contexts do not require manual reset. See context.Cache for more details
    /// 
    pub fn initContextCache(
      self: Self,
      gpa: Allocator,
      io: Io,
      n_workers: usize,
      including: anytype,
    ) context.WarmupError!ContextCache() {
      const self_len = self.internals.requiredContextLen();
      const len = context.getCollectiveContextLengthRequirement(self_len, including);
      return try ContextCache().init(gpa, io, n_workers, &.{len});
    }

    /// Resizes the cache to `n_workers` and updates every context to satisfy `including`.
    ///
    /// Requires that no contexts are currently acquired, the caller must
    /// have drained all outstanding contexts back into the cache first.
    /// 
    /// The contexts are extended as needed; never shrunk. No need to pass the old lengths, the cache stays 
    /// valid for the old machines
    pub fn warmupContextCache(
      self: Self,
      cache: *ContextCache(),
      gpa: Allocator,
      io: Io,
      n_workers: usize,
      including: []const Self,
    ) context.WarmupError!void {
      const self_len = self.internals.requiredContextLen();
      const len = context.getCollectiveContextLengthRequirement(self_len, including);
      return try cache.warmup(gpa, io, n_workers, &.{len});
    }

    /// Resizes the cache to `n_workers` and updates every context to satisfy `including`.
    ///
    /// Requires that no contexts are currently acquired, the caller must
    /// have drained all outstanding contexts back into the cache first.
    /// 
    /// The contexts are set to the exact length. Any old machines not accounted for in including are 
    /// invalidated
    /// 
    pub fn warmupContextCacheExact(
      self: Self,
      cache: *ContextCache(),
      gpa: Allocator,
      io: Io,
      n_workers: usize,
      including: []const Self,
    ) context.WarmupError!void {
      const self_len = self.internals.requiredContextLen();
      const len = context.getCollectiveContextLengthRequirement(self_len, including);
      return try cache.warmupExact(gpa, io, n_workers, &.{len});
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      self.internals.deinit(gpa);
    }
  };
}
