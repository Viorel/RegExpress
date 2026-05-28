//! pragmatic zig regex (pzre)
//! Evaluation implemented using the thompson nfa method (no bad cases)
const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("root.zig");
const lens = pzre.lens;
const debug = lens.debug;
const meta = pzre.meta;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const MemoryModel = pzre.MemoryModel;
const Limits = pzre.Limits;
const context = _nfa.context;
const resolution = _nfa.resolution;

const _nfa = pzre.nfa;
const state = _nfa.state;

pub const Config = pzre.language.Config;
pub const BaseConfig = pzre.language.BaseConfig;

pub const search_problem = pzre.nfa.search_problem;

pub const RuntimeNfa = _nfa.resolution.RuntimeNfa;
pub const ComptimeNfa = _nfa.resolution.ComptimeNfa;
const requiresAst = _nfa.resolution.requiresAst;
const deriveNfaResolutionObjects = _nfa.resolution.deriveNfaResolutionObjects;

pub const MetaData = pzre.parse_node.MetaData;
const inferProblem = _nfa.search_problem.inferProblem;

const _ast = pzre.ast;
pub const Ast = _ast.Ast;

const parse = pzre.parse;
pub const Parser = parse.Parser;
const ParseError = parse.ParseError;

const misc = pzre.misc;
const CountingAllocator = pzre.CountingAllocator;
const optimizeDestructively = pzre.ast.optimize.optimizeDestructively;

// ============================================================================
// Pipeline 1: Basic Generation (No optimizations, config stripped)
// ============================================================================

pub const Request = struct {
  metadata: bool = false,
  ast: bool = false,
  sets: bool = false,

  pub fn empty(req: Request) bool {
    return !(req.sets or req.ast or req.metadata);
  }
};

pub fn GenerateResult(comptime req: Request) type {
  return struct {
    metadata: if (req.metadata) MetaData else void,
    ast: if (req.ast) Ast else void,
    sets: if (req.sets) []const Set else void,

    const Self = @This();

    pub fn deinit(self: *Self, gpa: Allocator) void {
      if (@inComptime()) return;
      if (comptime req.ast) self.ast.deinit(gpa);
      if (comptime req.sets) self.sets.deinit(gpa);
    }
  };
}

/// Generate basic preliminary objects through the parser at runtime, any permutation of:
///   1. unoptimized ast   2. pattern metadata  3. sets present in the pattern
/// can be compiled on a single parser pass.
pub fn generate(
  comptime base: BaseConfig,
  comptime req: Request,
  gpa: Allocator,
  pattern: []const u8,
) ParseError!GenerateResult(req) {
  assert(!req.empty());

  const user_config = Config{ .semantics = base.semantics, .sets = base.sets, .limits = base.limits };
  const config = comptime user_config.normalize();
  const action = if (comptime req.ast) parse.Action.make_ast else parse.Action.dry;
  
  var ca = CountingAllocator.init(gpa, config.limits.gpa_upper_bound);

  const P = Parser(config.sets, config.semantics, config.limits, req.sets, action, .dynamic, base.limits.max_submachine_states);
  var parser: P = .new;
  defer parser.deinit(ca.allocator());

  const result = parser.parseUnbounded(ca.allocator(), pattern) catch |err| return switch (err) {
    error.OutOfMemory => error.AllocationUpperbound,
    else => err
  };
  const ast = if (comptime req.ast) Ast.init(result.ast_root, result.ast_nodes) else {};

  return .{
    .metadata = if (req.metadata) result.meta_data else {},
    .sets = result.sets,
    .ast = ast,
  };
}

/// Generate basic preliminary objects through the parser at comptime, any permutation of:
///   1. unoptimized ast   2. pattern metadata   3. sets present in the pattern
/// can be compiled on a single parser pass.
pub fn generateComptime(
  comptime base: BaseConfig,
  comptime req: Request,
  comptime pattern: []const u8,
) GenerateResult(req) {
  comptime {
    assert(!req.empty());

    const user_config = Config{ .semantics = base.semantics, .sets = base.sets, .limits = base.limits };
    const config = user_config.normalize();
    const action = if (req.ast) parse.Action.make_ast else parse.Action.dry;
    
    const P = Parser(config.sets, config.semantics, config.limits, req.sets, action, .comptime_dynamic, null);
    var parser: P = .new;

    const result = parser.parseComptime(pattern) catch |err| compileError(pattern, err);

    const ast = if (req.ast) Ast.init(result.ast_root, result.ast_nodes) else {};
    return .{
      .metadata = if (req.metadata) result.meta_data,
      .sets = result.sets,
      .ast = ast,
    };
  }
}

pub fn nfa(comptime config: Config, gpa: Allocator, pattern: []const u8) 
  ParseError!RuntimeNfa(config.normalize()) 
{
  _, const m = try nfaInternal(config, false, gpa, pattern);
  return m;
}

pub fn astAndNfa(comptime config: Config, gpa: Allocator, pattern: []const u8) 
  ParseError!struct {Ast, RuntimeNfa(config.normalize())} 
{
  return nfaInternal(config, true, gpa, pattern);
}

pub fn nfaComptime(comptime config: Config, comptime pattern: []const u8) 
  ComptimeNfa(config.normalize(), pattern) 
{
  comptime {
    _, const m = nfaComptimeInternal(config, false, pattern) catch |err| compileError(pattern, err);
    return m;
  }
}

pub fn astAndNfaComptime(comptime config: Config, comptime pattern: []const u8) 
  struct {Ast, ComptimeNfa(config.normalize(), pattern)} 
{
  comptime return nfaComptimeInternal(config, true, pattern) catch |err| compileError(pattern, err);
}

// ============================================================================
// Pipeline 2: Main NFA Generation (Config aware, optimization aware)
// ============================================================================

/// Runtime nfa builder
/// Decides how the parser should be executed for the configuration
///
/// So why not comptime? In order to perform type resolution on the comptime Nfa, 80% of the compilation 
/// pipeline has to be executed. This is because Nfa depends on the integer breakpoint. This function 
/// attempts to construct the nfa immediately when ast is not needed, which is not possible without knowing 
/// the breakpoint.
/// 
fn nfaInternal(
  comptime user_config: Config,
  /// Whether AST is returned
  /// If the implied problem does not strictly require one, then it is generated regardless
  comptime return_ast: bool,
  /// Undefined for comptime model
  gpa: Allocator,
  /// Only one of the patterns can be active at once
  pattern: []const u8,
) ParseError!struct {
  if (return_ast) Ast else void,
  RuntimeNfa(user_config.normalize()),
} {
  const config = comptime user_config.normalize();

  // Check the required problem and route to low-latency compilation if possible
  const requires_ast = return_ast or requiresAst(config);
  const breakpoint = config.limits.max_submachine_states;
  
  if (return_ast and !requiresAst(config)) {
    // A request was given to return the ast, but it is not strictly needed
    //  -> parse both at the same time
    const generate_sets = true;
    const P = Parser(config.sets, config.semantics, config.limits, generate_sets, .make_nfa_and_ast, .dynamic, breakpoint);
    var parser: P = .new;
    defer parser.deinit(gpa);

    const result = try parser.parseUnbounded(gpa, pattern);
    var ast = Ast.init(result.ast_root, result.ast_nodes);
    errdefer ast.deinit(gpa);

    const problem = if (config.problem) |p| p else search_problem.inferProblem(ast);
    const machine = try RuntimeNfa(config).solveWith(gpa, result.sets, &.{
      .ast = ast,
      .nfa = result.nfa_states,
    }, problem);
    return .{ast, machine};

  } else if (requires_ast) { // An AST is strictly required

    if (comptime !config.optimize) {
      if (comptime config.problem) |user_problem| {
        if (comptime user_problem.hasRequirement(.nfa)) {

          const generate_sets = true;
          const P = Parser(config.sets, config.semantics, config.limits, generate_sets, .make_nfa_and_ast, .dynamic, breakpoint);
          var parser: P = .new;
          defer parser.deinit(gpa);

          const result = try parser.parseUnbounded(gpa, pattern);
          var ast = Ast.init(result.ast_root, result.ast_nodes);
          errdefer ast.deinit(gpa);

          const machine = try RuntimeNfa(config).solveWith(gpa, result.sets, &.{
            .ast = ast,
            .nfa = result.nfa_states,
          }, user_problem);
          if (return_ast) {
            return .{ast, machine};
          } else {
            ast.deinit(gpa);
            return .{{}, machine};
          }
        }
      }
    }

    const base = BaseConfig{ .semantics = config.semantics, .sets = config.sets, .limits = config.limits };
    const res = try generate(base, .{ .ast = true, .sets = true }, gpa, pattern);

    var ast = res.ast;
    var sets = res.sets;
    if (config.optimize) {
      sets, ast = try optimizeDestructively(config.limits.max_submachine_states, .dynamic, gpa, res.sets, ast);
    }
    
    errdefer ast.deinit(gpa);

    const problem = config.problem orelse inferProblem(ast);
    const machine = try RuntimeNfa(config).solveWith(gpa, sets, &.{ .ast = ast }, problem);

    errdefer pzre.misc.destroySets(gpa, sets);
    
    if (return_ast) {
      return .{ ast, machine };
    } else {
      ast.deinit(gpa);
      return .{ {}, machine };
    }
  } else { // low latence branch; no AST
    var ca = CountingAllocator.init(gpa, config.limits.gpa_upper_bound);

    const generate_sets = true;
    const P = Parser(config.sets, config.semantics, config.limits, generate_sets, .make_nfa, .dynamic, breakpoint);
    var parser: P = .new;
    defer parser.deinit(ca.allocator());

    const result = parser.parseUnbounded(ca.allocator(), pattern) catch |err| return switch (err) {
      error.OutOfMemory => error.AllocationUpperbound,
      else => err
    };

    const problem = if (config.problem) |p| p else search_problem.default_low_latency;
    const machine = try RuntimeNfa(config).solveWith(gpa, result.sets, &.{
      .nfa = result.nfa_states,
    }, problem);
    return .{ {}, machine };
  }
}

fn nfaComptimeInternal(
  comptime user_config: Config,
  comptime return_ast: bool,
  comptime pattern: []const u8,
) ParseError!struct {
  if (return_ast) Ast else void,
  ComptimeNfa(user_config.normalize(), pattern),
} {
  comptime {
    const config = user_config.normalize();

    const o = try deriveNfaResolutionObjects(config, return_ast, pattern);
    const Nfa = resolution.ComptimeNfaFromObjects(config, o);
    
    if (o.ast) |tree| {
      const machine = try Nfa.solveWith(undefined, o.sets, &.{ .ast = tree }, o.problem);
      if (return_ast) return .{ tree, machine } else return .{ {}, machine };
    } else {
      assert(!return_ast);
      const generate_sets = false;
      const P = Parser(o.sets, config.semantics, config.limits, generate_sets, .make_nfa, .comptime_dynamic, o.breakpoint);
      var parser: P = .new;
      const result = try parser.parseComptime(pattern);
      const machine = try Nfa.solveWith(undefined, result.sets, &.{ .nfa = result.nfa_states }, o.problem);
      return .{{}, machine};
    }
  }
}

// ============================================================================
// Internal Helpers
// ============================================================================

pub fn compileError(comptime pattern: []const u8, comptime err: ParseError) noreturn {
  comptime pzre.lens.debug.compileError("Compiling the pattern '{s}' failed at compiletime due to: {any}", .{pattern, err});
}

// ============================================================================
// Tests
// ============================================================================

test "Runtime API Wrappers" {
  const gpa = std.testing.allocator;
  const config: Config = .{};
  const base: BaseConfig = .{};
  const pattern = "a{2,5}|b*";

  // 1. Basic Generation (AST only)
  var gen1 = try generate(base, .{ .ast = true }, gpa, pattern);
  gen1.ast.deinit(gpa);

  // 2. Basic Generation (AST + Sets)
  var gen2 = try generate(base, .{ .ast = true, .sets = true }, gpa, pattern);
  pzre.misc.destroySets(gpa, gen2.sets);
  gen2.ast.deinit(gpa);

  // 3. NFA only
  var machine = try nfa(config, gpa, pattern);
  machine.deinit(gpa);

  // 4. AST and NFA
  const ast_and_nfa_tuple = try astAndNfa(config, gpa, pattern);
  var ast3 = ast_and_nfa_tuple[0];
  var nfa2 = ast_and_nfa_tuple[1];
  ast3.deinit(gpa);
  nfa2.deinit(gpa);

  // 5. Basic Generation (Dry Run metadata only)
  _ = try generate(base, .{ .metadata = true }, gpa, pattern);
}

test "Comptime API Wrappers" {
  const config: Config = .{};
  const base: BaseConfig = .{};
  const pattern = "a{2,5}|b*";

  comptime {
    const gen1 = generateComptime(base, .{ .ast = true }, pattern);
    _ = gen1;

    const gen2 = generateComptime(base, .{ .metadata = true, .sets = true }, pattern);
    _ = gen2;

    const machine = nfaComptime(config, pattern);
    _ = machine;

    const ast_and_nfa_tuple = astAndNfaComptime(config, pattern);
    _ = ast_and_nfa_tuple;
  }
}

test "pzre memory safety for top level compile functions" {
  const gpa = std.testing.allocator;
  const config: Config = .{
    .limits = .{ .max_states = 1000, .gpa_upper_bound = 1024 * 5 },
  };
  const base = BaseConfig{ .limits = config.limits };

  const patterns = &[_][]const u8{
    "a" ** 40000,
    "a{4000}" ** 40000,
    "((a|b)|(c|d)|(e|f)|(g|h)){100,}",
    "(" ** 500 ++ "a" ++ ")" ** 500,
    "(a" ** 1000,
    "a|" ** 500 ++ "a",
    "a{300000}"
  };

  for (patterns) |pattern| {
    if (nfa(config, gpa, pattern)) |*machine| {
      @constCast(machine).deinit(gpa);
      return error.TestExpectedError;
    } else |_| {}

    if (generate(base, .{ .ast = true }, gpa, pattern)) |*gen| {
      @constCast(gen).ast.deinit(gpa);
      return error.TestExpectedError;
    } else |_| {}

    const unopt_config = Config{ .optimize = false, .limits = config.limits };
    if (nfa(unopt_config, gpa, pattern)) |*machine| {
      @constCast(machine).deinit(gpa);
      return error.TestExpectedError;
    } else |_| {}

    if (astAndNfa(config, gpa, pattern)) |*tuple| {
      var tree = tuple[0];
      var machine = tuple[1];
      tree.deinit(gpa);
      machine.deinit(gpa);
      return error.TestExpectedError;
    } else |_| {}
  }
}

test "allocation failure resistance across public facing interface" {
  const patterns = .{
    .{ "a+b*c?", "aaabbbc" },
    .{ "(foo|bar)", "bar" },
    .{ "[a-zA-Z0-9]+", "Z1g" },
    .{ "^begin.*end$", "begin testing end" },
    .{ "\\d{2,4}\\s\\w+", "123 word" },
  };

  const test_fn = struct {
    fn runInner(gpa: std.mem.Allocator, pattern: []const u8, input: []const u8) !void {
      const config: pzre.compile.Config = .{};
      const unopt_config: pzre.compile.Config = .{ .optimize = false };
      const base: BaseConfig = .{};

      var opt_machine = try pzre.compile.nfa(config, gpa, pattern);
      defer opt_machine.deinit(gpa);
      var opt_ctx = try opt_machine.initContext(gpa);
      defer opt_ctx.deinit(gpa);
      try std.testing.expect(opt_machine.matches(&opt_ctx, input));

      var unopt_machine = try pzre.compile.nfa(unopt_config, gpa, pattern);
      defer unopt_machine.deinit(gpa);
      var unopt_ctx = try unopt_machine.initContext(gpa);
      defer unopt_ctx.deinit(gpa);
      try std.testing.expect(unopt_machine.matches(&unopt_ctx, input));

      var gen1 = try pzre.compile.generate(base, .{ .ast = true }, gpa, pattern);
      gen1.ast.deinit(gpa);

      var gen2 = try pzre.compile.generate(base, .{ .ast = true, .sets = true }, gpa, pattern);
      defer pzre.misc.destroySets(gpa, gen2.sets);
      gen2.ast.deinit(gpa);

      var tree, var machine = try pzre.compile.astAndNfa(config, gpa, pattern);
      tree.deinit(gpa);
      machine.deinit(gpa);

      _ = try pzre.compile.generate(base, .{ .metadata = true }, gpa, pattern);
    }

    fn run(gpa: std.mem.Allocator, pattern: []const u8, input: []const u8) !void {
      runInner(gpa, pattern, input) catch |err| {
        if (err == error.AllocationUpperbound) {
          return error.OutOfMemory;
        }
        return err;
      };
    }
  }.run;

  inline for (patterns) |p| {
    try std.testing.checkAllAllocationFailures(std.testing.allocator, test_fn, .{ p[0], p[1] });
  }
}
