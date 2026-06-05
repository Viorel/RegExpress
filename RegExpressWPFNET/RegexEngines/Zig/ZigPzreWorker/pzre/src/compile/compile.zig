//! pragmatic zig regex (pzre)
//! Evaluation implemented using the thompson nfa method (no bad cases)
const std = @import("std");
const builtin = @import("builtin");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const lens = pzre.lens;
const debug = lens.debug;
const meta = pzre.meta;
const arch = pzre.arch;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const MemoryModel = pzre.MemoryModel;
pub const search = pzre.arch.search;

pub const strategy = @import("strategy.zig");
pub const Formulation = strategy.Formulation;
const Optimization = pzre.ast.optimize.Optimization;

const _ast = pzre.ast;
pub const Ast = _ast.Ast;
const ArchResolved = pzre.arch.ArchResolved;
const Arch = pzre.arch.Arch;
const optimize = _ast.optimize;
const Manifest = _ast.Manifest;
const ManifestField = _ast.ManifestField;

pub const parse = @import("parse.zig");
pub const parse_node = @import("parse_node.zig");
pub const Parser = parse.Parser;
pub const ParseNode = parse_node.ParseNode;
pub const ParseError = parse.ParseError;
pub const ParseResult = parse.ParseResult;

pub const lexer = @import("lexer.zig");
pub const Lexer = lexer.Lexer;

pub const Error = ParseError || dispatch.DispatchError;

pub const dispatch = @import("dispatch.zig");

comptime {
  if (@import("builtin").is_test) {
    _ = @import("lexer.zig");
    _ = @import("parse.zig");
    _ = @import("dispatch.zig");
    _ = @import("parse_node.zig");
    _ = @import("strategy.zig");
  }
}

/// Base parsing/compile configuration
/// 
pub const Config = struct {
  /// Language builtin set definitions
  ///
  semantics: Semantics = .{},
  /// Language feature definitions
  ///
  sets: Sets = .{},
  /// Resource usage limits.
  ///
  limits: Limits = .{},
  /// What AST optimizations to use
  /// 
  /// Make sure to turn this off when analyzing syntax
  /// 
  ast_optimizations: std.EnumSet(Optimization) = .initFull(),
  /// The features you are explicitly requesting
  features: Features = .{},
  /// search problem override. The system will forcefully pick some available architecture that supports this
  /// 
  /// For compile APIs that pick the architecture dynamically:
  ///   It is treated as an additional compilation constraint.
  ///   The input set of selected architectures are filtered using this strategy
  ///   error.FormulationImpossible is returned if none of the architectures support this
  /// 
  /// Picking the wrong strategy will cause silent semantic problems.
  /// For example, picking .start_anchor_pass as the strategy for the minimal_nfa
  ///   will make the system treat a pattern such as "abc" identically to "^abc"
  ///   which is fine as long as it is intended
  /// 
  /// The primary purpose for this is to choose between general-purpose strategies
  /// For example, .start_set_pass will always result in the smallest number of states being built;
  ///   [az]+q   ->   3 states (3*4 bytes + sets size)
  /// It is general purpose, and will never fail due to semantics. However, it is not ReDoS immune. Patterns
  /// with pathological start sets such as  .*abc  will make it perform very poorly. This is never picked 
  /// dynamically, instead .bi_directional_pass is picked. Which requires the same machine as 
  /// .start_set_pass, but also reversed, doubling the memory usage.
  /// 
  /// null (default) means that the search problem is picked dynamically
  /// You can trust the search problem picking process. This should rarely be set
  /// 
  strategy: ?strategy.Name = null,
  /// Global architecture definitions
  /// 
  /// Meant for advanced packing algorithms where collections of varying architectures need homogenized 
  /// subtypes for shared components
  /// 
  /// There is rarely a reason to modify these
  global: Global = .{},

  const Self = @This();
};

/// Global system definitions
pub const Global = struct {
  /// The index type used when indexing state machines (see strategy.zig)
  /// 
  /// The system compiles submachines into a single states array, which is then indexed as defined by the 
  /// search problem formulation. This has to be larger or equal to the sum of all compiled submachines
  /// 
  /// This has no noticable impact for singular compilations, and is meant to be used only for more advanced 
  /// packing algorithms
  /// 
  problem_bp: arch.AbsoluteBreakpoint = .u32,
  /// The index used for indexing the sets array. upper bound on the maximum number of allowed sets
  /// See config.max_unique_sets in compile.zig for more info
  /// 
  /// For 99.99% of cases, this should be kept at u8
  sets_bp: arch.AbsoluteBreakpoint = .u8,
};


/// Resource usage limits respected during compilation
/// Relevant fields are also respected when comptime compiled
pub const Limits = struct {
  /// Determines how much memory the passed allocator is allowed to hold onto at once
  /// 
  /// returns error.AllocationUpperbound when violated
  ///
  gpa_upper_bound: usize = 1 << 14,
  /// The maximum number of states the arch is allowed to be
  /// 
  /// The compiled arch is often composed of multiple submachines that exist 
  ///   in the same contiguous memory region. This is the maximum length of that region
  /// 
  /// If breakpoint i8, then the max size of the region is 4 * max_states
  /// 
  /// Returns error.TooManyStates on violation
  ///
  max_states: usize = 1 << 12,
  /// Prevents recursive algorithm stack overflow
  /// 
  /// Defines maximum parenthesis nesting count (((((a)))))
  ///   and maximum AST depth
  /// 
  /// default max depth is max u8
  /// 
  /// Returns error.TooDeep on violation
  ///
  max_depth: usize = 255,
  /// Maximum number of arbitrary repetitions
  /// a{3000} expands to 3000 literal states when unbound
  ///
  /// null for unbound
  /// 
  /// If patterns do not contain arbitary repetition a{n,m}, then state count is much more close to
  ///   pattern length. Estimating size directly from pattern length is effective in most practical cases.
  /// For a simple forward NFA automaton, pattern length is bounded above by k=pattern_len*2. Some search 
  ///   strategies will compile multiple machines. The most expensive strategy requires 2k+2 total states (3 
  ///   separate submachines). So guaranteed state_count <= 4 * pattern_len + 2
  /// Realistically, k is never close to double the pattern length. on average I would estimate roughly 1.2 
  /// times the pattern length assuming it is perfectly AST optimized and it contains no sets.
  /// Sets compile to a single state, e.g. the machine for [a-zA-Z_]+ is only a 2 state long machine
  /// 
  /// Returns error.TooHighArbitraryRepeat on too high repeat
  ///
  max_arbitrary_repetition: ?usize = null,
  /// A DFA will be preferred over an NFA only if its memory usage overhead is at worst N times as much
  /// 
  /// NOT IMPLEMENTED
  ///
  dfa_memory_scalar_threshold: usize = 10,
 
  /// The maximum number of unique sets allowed to be in the final compilation
  /// 
  /// Note that AST optimizations merge sets and unions together when possible
  ///   (a|b|c)+  ->  [abc]+
  ///   [ab]|[mn]  ->  [abmn]
  /// This is checked at the last stage of compilation. Once all optimizations have passed, but before linking.
  /// 
  /// Sets such as [a-zA-Z] are represented as integer-set datastructures
  /// Every sets are stored in an immutable array and indexed from the machines
  /// The larger the sets array is, the larger the indexing integer has to be, increasing the size of a 
  /// singular State. For maximum performance keep this strictly under 128
  /// 
  /// The memory footprint of sets can be significant if many patterns are compiled standalone
  /// When bulk-compiling (packing), sets memory usage impact lowers as the number of machines increase due 
  ///   to set-reuse; a single array is used for all sets, machines index this same set
  /// 
  /// Returns error.TooManySets on violation
  /// 
  max_unique_sets: usize = 64,
};

pub const Sets = struct {
  /// definition of \s  default: [ \t\n\r\f\v]
  /// 
  whitespace_set: Set = ascii.Set.WHITESPACE,
  /// definition of .  default: [^\r\n]
  /// 
  dot_set: Set = ascii.Set.DOT_SET,
  /// universe set the .dotall uses
  /// 
  dotall_set: Set = ascii.Set.ALL,
  /// definition of \d  default: [0-9]
  /// 
  digit_set: Set = ascii.Set.DIGIT,
  /// definition of \w  default: [0-9A-Za-z_]
  /// This defines behavior for \b (word boundary) assertions.
  /// 
  word_set: Set = ascii.Set.WORD,
};

pub const Semantics = struct {
  /// Interpret '^' and '$' as start/end of line
  ///
  multiline: bool = false,
  /// All letters [a-zA-Z] are interpreted as sets, e.g. a = A = [aA]
  /// 
  /// This is respected on all levels, even in hex sequences \xNN
  ///
  ignore_case: bool = false,
  /// All unescaped whitespace outside of sets in the pattern is ignored by the lexer
  /// Sets are parsed normally
  /// 
  /// Allows complex patterns to be defined over multiple lines
  ///
  pat_ignore_whitespace: bool = false,
  /// All unescaped whitespace in the pattern is ignored by the lexer
  /// Including unescaped whitespace in sets
  /// 
  /// Allows complex patterns to be defined over multiple lines
  ///
  pat_ignore_all_whitespace: bool = false,
  /// Disables all forms of implicit newlines, meaning:
  /// 
  /// 1. Disables newline from all builtin sets (including the dot operator set)
  /// 2. Newlines are automatically removed from inverted sets [^a]
  /// 
  /// The only way to match a newline is if it is explicitly present in the pattern
  ///
  never_implicit_newline: bool = false,
  /// dot_set '.' is ignored and universe [^] is used in its place
  /// Equivalent to defining the dot_set manually to []
  /// 
  dotall: bool = false,
};

/// Required features
pub const Features = struct {
  /// Whether to enable capture group extraction
  /// Has a significant performance penalty for arbitrary expressions
  /// Certain simple expressions have better performing capture group extractions via machine segmentation
  capture_groups: bool = false,
  /// enables \b \B assertions
  /// Currenly does nothing and this field might be removed in the future
  /// It's removal depends on how the DFA architecture will be designed
  word_boundary: bool = true,
};

/// A wrapper for generating parse objects
/// Action decides what to generate
pub fn parseObjects(
  comptime config: Config,
  comptime actions: std.EnumSet(parse.Action),
  comptime rbp: ?arch.RelativeBreakpoint,
  gpa: Allocator,
  pattern: []const u8,
) Error!parse.ParseResult(config.limits, actions, .dynamic, rbp, config.global.sets_bp) {
  return try parseObjectsWithModel(config, .dynamic, actions, rbp, gpa, pattern);
}

/// A wrapper for generating parse objects
/// Action decides what to generate
pub fn parseObjectsComptime(
  comptime config: Config,
  comptime actions: std.EnumSet(parse.Action),
  comptime rbp: ?arch.RelativeBreakpoint,
  pattern: []const u8,
) parse.ParseResult(config.limits, actions, .comptime_dynamic, rbp, config.global.sets_bp) {
  return parseObjectsWithModel(config, .comptime_dynamic, actions, rbp, undefined, pattern) catch |err|
    @compileError("Object parsing errored at comptime with: " ++ @errorName(err));
}

/// A wrapper for generating parse objects
/// Action decides what to generate
/// Does not compile error
pub fn parseObjectsComptimeNonIntercepting(
  comptime config: Config,
  comptime actions: std.EnumSet(parse.Action),
  comptime rbp: ?arch.RelativeBreakpoint,
  pattern: []const u8,
) Error!parse.ParseResult(config.limits, actions, .comptime_dynamic, rbp, config.global.sets_bp) {
  return try parseObjectsWithModel(config, .comptime_dynamic, actions, rbp, undefined, pattern);
}

pub fn parseObjectsWithModel(
  comptime config: Config,
  comptime model: MemoryModel,
  comptime actions: std.EnumSet(parse.Action),
  comptime rbp: ?arch.RelativeBreakpoint,
  gpa: Allocator,
  pattern: []const u8,
) Error!parse.ParseResult(config.limits, actions, model, rbp, config.global.sets_bp) {

  const P = pzre.compile.Parser(
    config.sets,
    config.semantics,
    config.limits,
    config.features,
    actions,
    model,
    rbp,
    config.global.sets_bp,
  );

  var parser: P = .new;
  defer parser.deinit(gpa);
  if (comptime model == .comptime_dynamic) {
    return try parser.parseComptime(pattern);
  } else {
    const result = try parser.parseUnbounded(gpa, pattern);
   
    if (comptime builtin.mode == .Debug and actions.contains(.metadata) and actions.contains(.ast)) {
      const metadata_report = result.meta_data.states_count;
      const ast_report = try result.ast.calculateNfaStateCount(.initEmpty());
      assert(metadata_report == ast_report);
    }
   
    return result;
  }
}

/// Performs all compilation stages except linking
/// 
/// This function is ALLOCATION UPPER BOUND UNSAFE.
/// Untrusted input safe compilation is performed through pzre.regex
/// For calling this on untrusted input you have to wrap the gpa in pzre.CountingAllocator
/// 
/// Returns all needed objects either for linking or type resolution
///   -> (chosen architecture, manifest, build artifacts)
/// 
/// The returned arch index is runtime (unless called from comptime), this needs to be converted into
///   a comptime index with an inline for loop. The indexed arch needs to be resolved using the returned 
///   manifest (in comptime), or with no manifest (in runtime)
/// 
pub fn prepareCompilation(
  /// Either []const Arch or []const ArchResolved
  comptime unfiltered_archs: anytype,
  comptime config: Config,
  comptime model: MemoryModel,
  gpa: Allocator,
  pattern: []const u8,
) Error!struct { dispatch.Decision, Manifest, Artifacts } {
  const archs, const to_unfiltered = comptime filterSupportedArchs(unfiltered_archs, config.strategy);
  if (comptime archs.len == 0) return error.NoAvailableArchitecture;
 
  const T = meta.GetChild(@TypeOf(archs)).?;
  if (T != Arch and T != ArchResolved) @compileError("unfiltered_archs passed into prepareCompilation has to be either []const Arch or []const ArchResolved. Found: " ++ @typeName(@TypeOf(unfiltered_archs)));

  const interests = comptime unionInterest(config, archs);
  // Stages 1-5: architecture agnostic analysis
  const manifest, const artifacts = try archAgnosticAnalysis(config, model, interests, gpa, pattern);
 
  // Stage 6: arch normalization
  // Partially defined architectures are resolved
  const archs_resolved: []const ArchResolved = comptime if (T == Arch) b: {
    var resolved_archs_arr: [archs.len]ArchResolved = undefined;
    if (model == .comptime_dynamic) {
      for (archs, 0..) |a, i| resolved_archs_arr[i] = a.resolveWithManifest(config, manifest);
    } else {
      for (archs, 0..) |a, i| resolved_archs_arr[i] = a.resolve();
    }
    const r = resolved_archs_arr[0..] ++ &[_]ArchResolved{};
    break :b r;
  } else b: {
    assert(T == ArchResolved);
    break :b archs;
  };

  // Stage 7: dispatch
  // Choose the best architecture given the manifest out of many
  // Architectures need to be resolved because the bids contain architecture related memory information
  const choice = try dispatch.choose(archs_resolved, config.global.sets_bp, manifest, config.strategy);
  var decision = try dispatch.resolveChoice(choice, manifest);
  decision.idx = to_unfiltered[decision.idx]; // map filtered_idx -> original_list_idx
  return .{decision, manifest, artifacts};
}

/// The first step of analysis: purely arch-agnostic manifest and artifact construction
/// The system has to provide an arch-agnostic pipeline that performs all steps up until manifest creation 
///   which acts as the primary object for arch-resolution
pub fn archAgnosticAnalysis(
  comptime config: Config,
  comptime model: MemoryModel,
  comptime manifest_fields: std.EnumSet(ManifestField),
  gpa: Allocator,
  pattern: []const u8,
) Error!struct { Manifest, Artifacts } {
 
  // Stage 1: syntax analysis
  // Standard single-pass parse-object creation
  const parse_result = try parseObjectsWithModel(config, model, .initMany(&.{.ast, .sets, .metadata}), null, gpa, pattern);

  // Stage 2: semantic perserving AST optimizations
  // Simple optimizations
  // It is important to not cover this with an errdefer over sets/ast
  const sets, var ast = try optimize.optimizeDestructively(
    model,
    gpa,
    config.ast_optimizations,
    parse_result.sets,
    parse_result.ast
  );
  if (config.limits.max_unique_sets < sets.len) return error.TooManySets;

  // Stage 3: routing
  // !! Extremely important !!
  // Routing is used for removing redundant information already present in the AST (routingLowering)
  // two situations:
  //  1. we already know the end-strategy due to it given by the user
  //    -> use its implied routing
  //  2. we do not know the end-strategy
  //    -> infer the routing from the AST
  // If we were to ignore the user's strategy here, we run the risk of:
  // - Inferring an anchored routing e.g. .prefix_match
  // - Removing all ^ anchors from the AST
  // - forcing dispatch to use a generic user strategy e.g. .bi_directional_pass
  // -> semantics corruption
  const routing = if (comptime config.strategy) |strat| strat.routing() else ast.determineRouting();

  // Stage 4: post-routing AST optimizations
  // It is important to not cover this with an errdefer over the ast
  ast = b: {
    errdefer pzre.misc.destroySets(gpa, sets);
    break :b try optimize.routingLowering(ast, model, gpa, config.ast_optimizations, routing);
  };
  var artifacts = Artifacts{
    .ast = ast,
    .sets = sets,
  };
  errdefer artifacts.deinit(gpa);
 
  // Stage 5: manifest creation
  // Create an artifact that represents the current situation
  // This HAS to run after ALL AST optimizations have taken place
  const manifest = try ast.buildManifest(config, manifest_fields, model, gpa, routing, &artifacts);
  return .{manifest, artifacts};
}

pub fn compileError(comptime err: anyerror) noreturn {
  @compileError(std.fmt.comptimePrint("Compilation errored with: {s}", .{err}));
}

pub const Artifact = enum {
  ast, reverse_ast, sets,
};

pub const Artifacts = struct {
  ast: Ast,
  sets: []const Set,
  reverse_ast: ?Ast = null,

  const Self = @This();

  pub fn deinit(self: *Self, gpa: Allocator) void {
    self.deinitAllButSets(gpa);
    pzre.misc.destroySets(gpa, self.sets);
  }

  pub fn deinitAllButSets(self: *Self, gpa: Allocator) void {
    self.ast.deinit(gpa);
    if (self.reverse_ast) |*n| n.deinit(gpa);
  }
};

/// Returns the subset of 'archs' that support the strategy, plus a mapping from
/// each filtered position back to its index in the original 'archs'.
///
/// mapping[filtered_idx] == original_idx
pub fn filterSupportedArchs(
  /// Either []const Arch or []const ArchResolved
  comptime archs: anytype,
  comptime forced_strat: ?strategy.Name,
) struct { @TypeOf(archs), []const usize } {
  comptime {
    if (forced_strat) |strat| {
      assert(@TypeOf(archs) == []const Arch
        or @TypeOf(archs) == []const ArchResolved);
      const T = meta.GetChild(@TypeOf(archs)).?;

      var j: usize = 0;
      for (archs) |a| {
        if (a.strategies().contains(strat)) j += 1;
      }

      var valid_archs: [j]T = undefined;
      var mapping: [j]usize = undefined;
      var curr: usize = 0;
      for (archs, 0..) |a, i| {
        if (a.strategies().contains(strat)) {
          valid_archs[curr] = a;
          mapping[curr] = i;
          curr += 1;
        }
      }

      const StaticStorage = struct {
        const archs_data = valid_archs;
        const map_data = mapping;
      };
      return .{ &StaticStorage.archs_data, &StaticStorage.map_data };
    } else {
      var id: [archs.len]usize = undefined;
      for (0..id.len) |i| id[i] = i;
      const S = struct { const data = id; };
      return .{ archs, S.data[0..] };
    }
  }
}

/// Determines what manifest fields should be populated. Reads UNRESOLVED archs;
/// interests are config-level and do not depend on resolution.
pub fn unionInterest(
  /// Either []const Arch or []const ArchResolved
  comptime config: Config,
  comptime archs: anytype,
) std.EnumSet(ManifestField) {
  comptime {
    var sys_interests: std.EnumSet(ManifestField) = .initEmpty();
    for (archs) |a| {
      sys_interests.setUnion(a.interests(config));
    }
    return sys_interests;
  }
}

