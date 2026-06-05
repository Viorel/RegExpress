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
const meta = pzre.meta;

const misc = pzre.misc;
const regex = pzre.regex;
const Regex = regex.Regex;
pub const Match = regex.Match;
pub const ManyReplacements = regex.ManyReplacements;
pub const Replacement = regex.Replacement;
pub const Global = pzre.compile.Global;

const ArchResolved = pzre.arch.ArchResolved;
const Arch = pzre.arch.Arch;
const AbsoluteBreakpoint = pzre.arch.AbsoluteBreakpoint;
const strategy = pzre.compile.strategy;
const context = pzre.arch.context;

const Config = pzre.compile.Config;
const dispatch = pzre.compile.dispatch;
const CountingAllocator = pzre.CountingAllocator;
const MemoryModel = pzre.MemoryModel;

const optimize = pzre.ast.optimize;
/// A predefined general purpose Regex object with a fixed dynamic context 
/// For when you do not wish to manually define one
/// 
/// - Compiled machine sizes cannot exceed context_size.
/// - Context always consumes the same fixed amount of memory
/// - Supports matching at comptime
/// 
pub fn FixedRegex(comptime context_size: comptime_int) type {
  return AnyRegex(&.{
    .{ .minimal_nfa = .{ .context = .{ .fixed = context_size } } },
  }, .{});
}

/// A predefined general purpose Regex object with an allocator managed context
/// For when you do not wish to manually define one
/// 
/// - Context starts small, can be extended or shrank
/// - Supports any sized machines
/// - Matching cannot be performed at comptime
///
pub const DynamicRegex = AnyRegex(&.{
  .{ .minimal_nfa = .{ .offset_bp = .i8, .context = .{ .dynamic = .u16 } } },
  .{ .minimal_nfa = .{ .offset_bp = .i16, .context = .{ .dynamic = .u16 } } },
}, .{});

/// # AnyRegex: Completely type-erased Regex object
/// Public facing pattern-matching API
/// 
/// The API behaves exactly the same as if you were to use Regex (the explicit regex type).
/// Use this wrapper when you need to store machines with differing architectures within slices, arrays or 
/// reference them in function signatures.
/// It is also useful when you wish to support a large number of architectures for situations you cannot 
/// predict at comptime.
/// 
/// # Executable bloat
/// The architectures available are defined strictly by the slice passed to it. The executable code for every 
/// included architecture will be included in the final executable. There are cases when the Zig compiler 
/// will successfully eliminate any such code, but especially when compiling at runtime, it is impossible for 
/// the Zig compiler to deduce what code will or will not be used. It is best to curate the compiled 
/// architectures carefully.
/// 
/// # Performance Note
/// Matching via this object incurs a minor dynamic dispatch overhead due to 
/// the internal tagged union switch statement. For zero-overhead static routing, 
/// use Regex directly.
/// 
/// # Context usage
/// The Context type is a struct with a field for every single unique context type required. Therefore it is 
/// important to be mindful of how many unique context-dependent architectures are included. The system 
/// compile errors if architecture sub-definitions do not share the same context type, or if .compact_fixed 
/// is used. More exactly:
/// 
/// const these_compile_error = &.{
///  .{ .minimal_nfa = .{.context = .{ .fixed = 128 }} },
///  .{ .minimal_nfa = .{.offset_bp = .i16, .context = .{ .dynamic = .i16 }} },
/// };
/// const these_do_not = &.{
///  .{ .minimal_nfa = .{.context = .{ .fixed = 128 }} },
///  .{ .pike_vm = .{.offset_bp = .i16, .context = .{ .dynamic = .i16 }} },
/// };
/// Pike vm not implemented as of writing. It is redundant to include different variations of the same 
/// context architecture.
/// 
/// For comptime matching, all architectures are required to use fixed contexts. Never use compact_fixed as 
/// it would bloat up the type-erased AnyRegex wrapper. Context definitions do not alter whether 
/// runtime/comptime compilation is supported for AnyRegex
/// 
/// From a high level perspective, initContextIncluding and all other context methods work exactly the 
/// same as in Regex. You do not have to worry about what architectures are present for a given AnyRegex. 
/// Always assume a context has to be created, passed to the methods and then deinited in the end. 
/// 
/// Whenever a new AnyRegex instance is created you can update an existing context instance to also support it 
/// using the updateContext method, just as you can with Regex. Contexts can also be shared as long as 
/// the contexts are properly set to support eachother, and generated from the same AnyRegex definition. Even if 
/// you do not include architectures that require contexts at all, you should still call all of the context 
/// management functions following good practice, and let the system decide how to manage them internally.
/// 
/// # Example
/// const archs = &.{
///  .{ .minimal_nfa = .{.offset_bp = .i8, .context = .{ .dynamic = .i16 }} },
///  .{ .minimal_nfa = .{.offset_bp = .i16, .context = .{ .dynamic = .i16 }} },
/// };
/// 
/// Use DynamicRegex or FixedRegex in this same file for a sensible and general purpose definition
/// 
pub fn AnyRegex(
  /// The architecture universe this polymorphic regex is allowed to draw from.
  /// 
  /// compact_fixed is forbidden in the Arch context definition
  comptime archs: []const Arch,
  /// System configuration
  ///
  /// Meant for advanced packing algorithms where collections of varying architectures need homogenized 
  /// subtypes for shared components
  ///
  /// There is rarely a reason to modify these
  comptime global: Global,
) type {
  if (archs.len == 0) {
    @compileError("AnyRegex requires at least one architecture configuration provided in the array.");
  }

  // Config-only resolution. Lossless here because compact_fixed is rejected below.
  const resolved_archs: []const ArchResolved = comptime b: {
    var arr: [archs.len]ArchResolved = undefined;
    for (archs, 0..) |a, i| {
      if (a.resolutionRequiresManifest()) @compileError("Architecture passed to AnyRegex whose resolution depends on manifest!");
      arr[i] = a.resolve();
    }
    const Static = struct {
      const data = arr;
    };
    break :b Static.data[0..];
  };

  pzre.arch.assertUniqueContextsPerArch(resolved_archs);

  return struct {
    arch: ArchUnion,

    const Self = @This();
    pub const ArchUnion = DefineArchUnion();
    pub const IterUnion = DefineIterUnion(ArchUnion);

    const ctx_info = buildContextInfo(ArchUnion);
    pub const Context = context.ManyContext(ctx_info.unique_types);
    pub fn ContextCache() type {
      return context.Cache(Context);
    }

    /// Builds the iterator type by strictly reflecting the Engine union
    fn DefineIterUnion(comptime AU: type) type {
      const ti = @typeInfo(AU).@"union";
      const engine_fields = std.meta.fields(AU);
      const count = engine_fields.len;
      const TagEnum = ti.tag_type.?;
      const TagInt = @typeInfo(TagEnum).@"enum".tag_type;

      comptime var names: [count][]const u8 = undefined;
      comptime var enum_vals: [count]TagInt = undefined;
      comptime var types: [count]type = undefined;
      comptime var attrs: [count]std.builtin.Type.UnionField.Attributes = undefined;

      for (engine_fields, 0..) |field, i| {
        names[i] = field.name;
        enum_vals[i] = @as(TagInt, @intCast(i));
        types[i] = field.type.MatchIterator;
        attrs[i] = .{ .@"align" = @alignOf(types[i]) };
      }

      const Tag = @Enum(TagInt, .exhaustive, &names, &enum_vals);
      return @Union(.auto, Tag, &names, &types, &attrs);
    }

    pub const MatchIterator = struct {
      arch: IterUnion,

      pub fn next(it: *@This()) ?Match {
        return switch (it.arch) {
          inline else => |*typed_it| typed_it.next(),
        };
      }

      pub fn reset(it: *@This()) void {
        switch (it.arch) {
          inline else => |*typed_it| typed_it.reset(),
        }
      }
    };

    fn getTagType(comptime count: usize) type {
      if (count <= std.math.maxInt(u8)) return u8;
      if (count <= std.math.maxInt(u16)) return u16;
      if (count <= std.math.maxInt(u32)) return u32;
      return u64;
    }

    /// Creates a Regex object for each architecture in archs
    fn DefineArchUnion() type {
      @setEvalBranchQuota(1_000_000); // due to comptimePrint
      const count = resolved_archs.len;
      const TagInt = getTagType(count);

      comptime var names: [count][]const u8 = undefined;
      comptime var enum_vals: [count]TagInt = undefined;
      comptime var types: [count]type = undefined;
      comptime var attrs: [count]std.builtin.Type.UnionField.Attributes = undefined;

      for (resolved_archs, 0..) |a, i| {
        names[i] = std.fmt.comptimePrint("{d}", .{i});
        enum_vals[i] = @as(TagInt, @intCast(i));
        types[i] = Regex(a, global);
        attrs[i] = .{ .@"align" = @alignOf(types[i]) };
      }

      const Tag = @Enum(TagInt, .exhaustive, &names, &enum_vals);
      return @Union(.auto, Tag, &names, &types, &attrs);
    }

    const ContextInfo = struct {
      unique_types: []const type,
      map: [resolved_archs.len]usize,
    };

    /// Inspects all N unique architectures passed in order to determine C <= N number of
    /// unique context types. A struct is generated with C number of fields, each associated with
    /// a required Context type. 
    /// A 'map' structure is generated that maps each Regex union field to its associated Context field
    /// 
    fn buildContextInfo(comptime AU: type) ContextInfo {
      const engine_fields = std.meta.fields(AU);
      comptime var unique_types: [resolved_archs.len]type = undefined;
      comptime var map_arr: [resolved_archs.len]usize = undefined;
      comptime var curr = 0;

      for (engine_fields, 0..) |field, i| {
        // We grab the native Context type strictly exported from the Regex
        const CtxType = field.type.Context;
        var found = false;
        for (0..curr) |j| {
          if (unique_types[j] == CtxType) {
            map_arr[i] = j;
            found = true;
            break;
          }
        }
        if (!found) {
          unique_types[curr] = CtxType;
          map_arr[i] = curr;
          curr += 1;
        }
      }

      const trimmed: [curr]type = unique_types[0..curr].*;
      return .{
        .unique_types = &trimmed,
        .map = map_arr,
      };
    }

    /// Returns the length of the required context
    pub fn requiredContextLen(self: Self) usize {
      return switch (self.arch) {
        inline else => |*exec| exec.requiredContextLen(),
      };
    }

    /// Creates a single context for this Regex
    ///
    /// Contexts do not require manual reset
    /// 
    pub inline fn initContext(self: Self, gpa: Allocator) Allocator.Error!Context {
      return self.initContextIncluding(gpa, &.{});
    }

    /// Creates a single context for this Regex
    ///
    /// Contexts do not require manual reset
    /// 
    /// Asserts that the context is fixed across all architectures
    /// 
    pub fn initContextFixed(self: Self) Context {
      _ = self;
      comptime for (std.meta.fields(ArchUnion)) |field| {
        assert(field.type.non_allocator_context);
      };
      return Context.initFixed();
    }

    /// Creates a single context for a system of nfas, including this one
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn initContextIncluding(
      self: Self,
      gpa: Allocator,
      including: []const Self,
    ) Allocator.Error!Context {
      const lens = computeFieldLens(self, including);
      return Context.init(gpa, &lens);
    }

    /// Updates an existing context to also support any included machines
    ///
    /// Contexts do not require manual reset
    ///
    pub fn updateContext(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      including: []const Self,
    ) Allocator.Error!void {
      const lens = computeFieldLens(self, including);
      try ctx.updateLength(gpa, &lens);
    }

    /// Calculates the max per-field length of the machine set
    pub fn computeFieldLens(self: Self, including: []const Self) [Context.field_count]usize {
      var lens = [_]usize{0} ** Context.field_count;

      const self_arch_idx = @intFromEnum(self.arch);
      const self_ctx_idx = ctx_info.map[self_arch_idx];
      lens[self_ctx_idx] = @max(lens[self_ctx_idx], self.requiredContextLen());

      for (including) |re| {
        const re_arch_idx = @intFromEnum(re.arch);
        const re_ctx_idx = ctx_info.map[re_arch_idx];
        lens[re_ctx_idx] = @max(lens[re_ctx_idx], re.requiredContextLen());
      }
      return lens;
    }

    /// Creates an optimized context cache for multithreaded systems.
    /// 
    /// The cache is optimized for this + including architectures
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
      including: []const Self,
    ) context.WarmupError!ContextCache() {
      const lens = computeFieldLens(self, including);
      return try ContextCache().init(gpa, io, n_workers, &lens);
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
      const lens = computeFieldLens(self, including);
      return try cache.warmup(gpa, io, n_workers, &lens);
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
      const lens = computeFieldLens(self, including);
      return try cache.warmupExact(gpa, io, n_workers, &lens);
    }

    /// Internal helper
    inline fn callWithContext(
      self: Self,
      ctx: *Context,
      comptime fn_name: []const u8,
      args: anytype,
      comptime ReturnType: type,
    ) ReturnType {
      @setEvalBranchQuota(1_000_000); // due to comptimePrint
      return switch (self.arch) {
        inline else => |exec, tag| b: {
          const engine_idx = @intFromEnum(tag);
          const ctx_idx = comptime ctx_info.map[engine_idx];
          const field_name = comptime std.fmt.comptimePrint("{d}", .{ctx_idx});
          const typed_ctx = &@field(ctx.ctx, field_name);
 
          const EngineType = @TypeOf(exec);
          const func = @field(EngineType, fn_name);
 
          // Manual unroll: tuple `++` is comptime-only, so we dispatch on the
          // comptime-known args.len.  Bump the case list if a method ever needs
          // more than 6 trailing arguments.
          break :b switch (args.len) {
            0 => @call(.auto, func, .{exec, typed_ctx}),
            1 => @call(.auto, func, .{exec, typed_ctx, args[0]}),
            2 => @call(.auto, func, .{exec, typed_ctx, args[0], args[1]}),
            3 => @call(.auto, func, .{exec, typed_ctx, args[0], args[1], args[2]}),
            4 => @call(.auto, func, .{exec, typed_ctx, args[0], args[1], args[2], args[3]}),
            5 => @call(.auto, func, .{exec, typed_ctx, args[0], args[1], args[2], args[3], args[4]}),
            6 => @call(.auto, func, .{exec, typed_ctx, args[0], args[1], args[2], args[3], args[4], args[5]}),
            else => @compileError("callWithContext: argument count exceeds unroll limit (6)"),
          };
        }
      };
    }

    /// Returns true if the entire string matches
    pub inline fn matchesExact(self: Self, ctx: *Context, str: []const u8) bool {
      return self.callWithContext(ctx, "matchesExact", .{str}, bool);
    }

    /// Attempts to match the string
    /// Returns the head that matched;
    /// str[0..end <= str.len]
    pub inline fn matchStart(self: Self, ctx: *Context, str: []const u8) ?[]const u8 {
      return self.callWithContext(ctx, "matchStart", .{str}, ?[]const u8);
    }

    /// True if any substring matches
    pub inline fn matches(self: Self, ctx: *Context, str: []const u8) bool {
      return self.callWithContext(ctx, "matches", .{str}, bool);
    }

    /// Finds the first match
    pub inline fn match(self: Self, ctx: *Context, str: []const u8) ?Match {
      return self.callWithContext(ctx, "match", .{str}, ?Match);
    }

    /// Similar to 'match' but instead finds the first match that starts within range 
    ///   [start_idx, max_base]   end inclusive
    pub inline fn find(self: Self, ctx: *Context, str: []const u8, start_idx: usize, max_base: usize) ?Match {
      return self.callWithContext(ctx, "find", .{str, start_idx, max_base}, ?Match);
    }

    /// Finds all matches and stores them in a slice
    pub inline fn findAllAlloc(self: Self, ctx: *Context, gpa: Allocator, str: []const u8) Allocator.Error![]Match {
      return self.callWithContext(ctx, "findAllAlloc", .{ gpa, str }, Allocator.Error![]Match);
    }

    /// Finds all matches and stores them in a slice
    pub fn findAllComptime(comptime self: Self, comptime ctx: *Context, comptime str: []const u8) []const Match {
      return self.callWithContext(ctx, "findAllComptime", .{ str }, []const Match);
    }

    /// Finds all matches and replaces them with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    ///
    /// Returns the region where replacements occured and the number of replacements
    ///
    pub inline fn replaceAll(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
    ) Allocator.Error!?ManyReplacements {
      return self.callWithContext(
        ctx,
        "replaceAll",
        .{ gpa, str, replacement },
        Allocator.Error!?ManyReplacements
      );
    }

    /// Finds all matches starting within range [start_idx, max_base]   end inclusive
    ///   and replaces them with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    ///
    /// Returns the region where replacements occured and the number of replacements
    ///
    pub inline fn replaceAllWithin(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
      start_idx: usize,
      max_base: usize,
    ) Allocator.Error!?ManyReplacements {
      return self.callWithContext(
        ctx,
        "replaceAllWithin",
        .{ gpa, str, replacement, start_idx, max_base },
        Allocator.Error!?ManyReplacements,
      );
    }

    /// Finds the first match and replaces it with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    pub inline fn replaceFirst(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
    ) Allocator.Error!?Replacement {
      return self.callWithContext(
        ctx,
        "replaceFirst",
        .{ gpa, str, replacement },
        Allocator.Error!?Replacement,
      );
    }

    /// Finds the first match that starts within range [start_idx, max_base]   end inclusive
    ///   and replaces it with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    pub inline fn replaceFirstWithin(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
      start_idx: usize,
      max_base: usize,
    ) Allocator.Error!?Replacement {
      return self.callWithContext(
        ctx,
        "replaceFirstWithin",
        .{ gpa, str, replacement, start_idx, max_base },
        Allocator.Error!?Replacement,
      );
    }

    /// Returns an iterator for all matches
    pub fn matchIter(self: Self, ctx: *Context, str: []const u8) MatchIterator {
      @setEvalBranchQuota(1_000_000); // due to comptimePrint
      return switch (self.arch) {
        inline else => |*exec, tag| b: {
          const engine_idx = @intFromEnum(tag);
          const ctx_idx = comptime ctx_info.map[engine_idx];
          const field_name = comptime std.fmt.comptimePrint("{d}", .{ctx_idx});
          const typed_ctx = &@field(ctx.ctx, field_name);

          break :b MatchIterator{
            .arch = @unionInit(IterUnion, @tagName(tag), exec.matchIter(typed_ctx, str)),
          };
        }
      };
    }

    /// Compiles at runtime and returns a type erased Regex object
    /// 
    /// The system will pick the best available architecture
    /// 
    pub fn compile(comptime config: Config, child_allocator: Allocator, pattern: []const u8) pzre.compile.Error!Self {
      var gpa = CountingAllocator.init(child_allocator, config.limits.gpa_upper_bound);
      return compileWithModel(config, .dynamic, gpa.allocator(), pattern) catch |err| switch (err) {
        error.OutOfMemory => {
          // Differentiate between reaching the resource cap, over a system out of memory error
          return if (gpa.cap_reached) error.AllocationUpperbound else error.OutOfMemory;
        },
        else => return err,
      };
    }

    /// Compiles at comptime and returns a type erased Regex object
    /// 
    /// The system will pick the best available architecture
    /// Errors are intercepted as @compileError
    /// 
    pub fn compileComptime(comptime config: Config, comptime pattern: []const u8) Self {
      return compileComptimeNonIntercepting(config, pattern) catch |err|
        @compileError("compileComptime failed for '" ++ pattern ++ "': " ++ @errorName(err));
    }

    /// Compiles at comptime and returns a type erased Regex object
    /// 
    /// The system will pick the best available architecture
    /// Errors are not intercepted
    /// 
    pub fn compileComptimeNonIntercepting(comptime config: Config, comptime pattern: []const u8) pzre.compile.Error!Self {
      return compileWithModel(config, .comptime_dynamic, undefined, pattern);
    }

    /// Compiles memory-polymorphically
    ///
    fn compileWithModel(comptime config: Config, comptime model: MemoryModel, gpa: Allocator, pattern: []const u8) pzre.compile.Error!Self {
      @setEvalBranchQuota(1_000_000);
      comptime assert(std.meta.eql(config.global, global));

      const decision, const manifest, var artifacts = try pzre.compile.prepareCompilation(resolved_archs, config, model, gpa, pattern);
      _ = manifest;
      errdefer artifacts.deinit(gpa);

      // resolved_archs is the same config-only resolution used to build ArchUnion,
      // so the field name and type line up by construction. We index resolved_archs
      // directly rather than re-resolving.
      inline for (resolved_archs, 0..) |a, i| {
        if (i == decision.idx) {
          const Re = Regex(a, global);

          const generic = try Re.fromArtifacts(config, model, gpa, &artifacts, decision.bid.strategy);

          const field_name = comptime std.fmt.comptimePrint("{d}", .{i});
          artifacts.deinitAllButSets(gpa);
          return Self{ .arch = @unionInit(ArchUnion, field_name, generic) };
        }
      }

      unreachable; // decision.idx is always within archs bounds
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      switch (self.arch) {
        inline else => |*exec| exec.deinit(gpa),
      }
    }
  };
}
