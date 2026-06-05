//! The object with the matching api
//! 
const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const pzre = @import("root.zig");
const debug = pzre.lens.debug;
const Range = pzre.structures.range.Range;
const IdxRange = Range(usize);
const Global = pzre.compile.Global;

const ArchResolved = pzre.arch.ArchResolved;
const Arch = pzre.arch.Arch;
const strategy = pzre.compile.strategy;
const context = pzre.arch.context;

const Config = pzre.compile.Config;
const MemoryModel = pzre.MemoryModel;

const regex_type = @import("regex_type.zig");
pub const Regex = regex_type.Regex;

pub const Match = struct {
  /// Most likely aliases with input but not guaranteed (static memory)
  str: []const u8,
  loc: IdxRange,
};

pub const ManyReplacements = struct {
  /// The range encompassing all replacements, mapped to the original input string's indices.
  span: Range(usize),
  /// The number of replacements that were performed
  count: usize,
  /// Newly allocated string
  new: []const u8,

  const Self = @This();
  pub fn deinit(r: Self, gpa: Allocator) void {
    gpa.free(r.new);
  }
};

pub const Replacement = struct {
  /// The range encompassing all replacements, mapped to the original input string's indices.
  span: Range(usize),
  /// Newly allocated string
  new: []const u8,

  const Self = @This();
  pub fn deinit(r: Self, gpa: Allocator) void {
    gpa.free(r.new);
  }
};

//WARNING: Be extra careful when refactoring this; Ensure implementation is not left in a state where ZLS stops giving matching API autocompletions for returned Regex objects!

/// Compiles a single, explicitly chosen architecture at runtime into a Regex.
/// 
/// 'arch' cannot contain fields that require comptime type resolution, e.g. compact_fixed context
///
/// Untrusted input safe
pub fn compile(
  comptime arch: Arch,
  comptime config: Config,
  gpa: Allocator,
  pattern: []const u8,
) pzre.compile.Error!RegexResolved(arch, config.global) {
  const resolved = comptime arch.resolve();
  const Re = Regex(resolved, config.global);
  return try Re.compile(config, gpa, pattern);
}

/// Compiles a single, explicitly chosen architecture at comptime into a Regex.
///
/// Errors are intercepted as @compileError
pub fn compileComptime(
  comptime arch: Arch,
  comptime config: Config,
  comptime pattern: []const u8,
) RegexResolvedWithPattern(arch, config, pattern) {
  comptime {
    const decision, const manifest, var artifacts = pzre.compile.prepareCompilation(@as([]const Arch, &.{arch}), config, .comptime_dynamic, undefined, pattern) catch |err| pzre.compile.compileError(err);
    assert(decision.idx == 0);
   
    const resolved = arch.resolveWithManifest(config, manifest);
    const Re = Regex(resolved, config.global);
   
    return Re.fromArtifacts(config, .comptime_dynamic, undefined, &artifacts, decision.bid.strategy) catch |err| pzre.compile.compileError(err);
  }
}

/// Compiles a single, explicitly chosen architecture at comptime into a Regex.
///
/// Errors are not intercepted
pub fn compileComptimeNonIntercepting(
  comptime arch: Arch,
  comptime config: Config,
  comptime pattern: []const u8,
) pzre.compile.Error!RegexResolvedWithPattern(arch, config, pattern) {
  comptime {
    const decision, const manifest, var artifacts = try pzre.compile.prepareCompilation(@as([]const Arch, &.{arch}), config, .comptime_dynamic, undefined, pattern);
   
    assert(decision.idx == 0);
    const resolved = arch.resolveWithManifest(config, manifest);
    const Re = Regex(resolved, config.global);
   
    return try Re.fromArtifacts(config, .comptime_dynamic, undefined, &artifacts, decision.bid.strategy);
  }
}

/// Resolves the Regex type given a comptime known pattern
/// Executes the compilation pipeline partially when required
///
/// Resolution never compile errors, instead it returns the type void when compilation fails
pub fn RegexResolvedWithPattern(
  comptime arch: Arch,
  comptime config: Config,
  comptime pattern: []const u8,
) type {
  const resolved: ArchResolved = if (arch.resolutionRequiresManifest()) b: {
    const decision, const manifest, _ = pzre.compile.prepareCompilation(@as([]const Arch, &.{arch}), config, .comptime_dynamic, undefined, pattern) catch return void;
   
    assert(decision.idx == 0);
    break :b arch.resolveWithManifest(config, manifest);
  } else arch.resolve();
 
  return Regex(resolved, config.global);
}

/// Resolves the Regex type with immediate resolution.
pub fn RegexResolved(
  comptime arch: Arch,
  comptime global: Global,
) type {
  assert(!arch.resolutionRequiresManifest());
  const resolved = arch.resolve();
  return Regex(resolved, global);
}

/// Resolves the Regex type by executing most of the compilation pipeline including dispatch
/// 
/// Resolution never compile errors, instead it returns the type void when compilation fails
pub fn RegexResolvedWithDispatch(
  comptime archs: []const Arch,
  comptime config: Config,
  comptime pattern: []const u8,
) type {
  const decision, const manifest, _ = pzre.compile.prepareCompilation(archs, config, .comptime_dynamic, undefined, pattern) catch return void;
 
  const chosen_arch = archs[decision.idx].resolveWithManifest(config, manifest);
  return Regex(chosen_arch, config.global);
}

/// Compiles the most optimal Regex object for the pattern at comptime. Meaning, it will grab all (*) 
/// available pattern matching architectures available in this library, and choose the architecture it deems 
/// the most optimal for this specific pattern. If the best architecture uses a context, then it will use 
/// 'ctx' as the context type. Otherwise 'ctx' is ignored.
///
/// Keep in mind that the returned type is resolved dynamically. If you compile many different patterns, you 
/// will most likely end up with many different Regex sub-variations and will not be able to refer to 
/// their type under a single unified name. 'AnyRegex' is a type-erased wrapper designed for that purpose.
/// 
/// Additionally, it is worth noting that the more unique Regex types you define, the more unique 
/// executable code you are forcing the Zig compiler to embed into your binary. It is best to minimize the 
/// number of Regex types in your application.
///
/// Useful for inspecting what architecture the system thinks is the most optimal for the pattern
/// 
pub fn compileOptimal(
  comptime config: Config,
  comptime pattern: []const u8,
  comptime ctx: context.Mode,
) RegexResolvedWithDispatch(pzre.arch.archUniverseSweep(ctx), config, pattern) {
  comptime {
    // In order to resolve, we need to run the optimization pipeline 80% there
    //  and then start from scratch in here again...
    // It is unclear how to do this in one pass without breaking ZLS
   
    const archs = pzre.arch.archUniverseSweep(ctx);
    const decision, const manifest, var artifacts = pzre.compile.prepareCompilation(archs, config, .comptime_dynamic, undefined, pattern) catch |err| pzre.compile.compileError(err);
   
    const chosen_arch = archs[decision.idx].resolveWithManifest(config, manifest);
    //
    // @compileLog(
    //   manifest.nfa_states_count,
    //   manifest.rnfa_states_count,
    //   pzre.minimal_nfa.maxSubmachineSize(manifest, decision.bid.strategy),  // context len
    //   pzre.minimal_nfa.statesLength(manifest, decision.bid.strategy),  // states len
    //   chosen_arch.minimal_nfa.offset_bp,        // derived offset
    //   chosen_arch.minimal_nfa.context,          // derived index width
    //   decision.bid,
    // );

    const Re = Regex(chosen_arch, config.global);
    return Re.fromArtifacts(config, .comptime_dynamic, undefined, &artifacts, decision.bid.strategy) catch |err| pzre.compile.compileError(err);
  }
}
