//! Functions for deriving the comptime compiled Nfa type
const std = @import("std");
const pzre = @import("../root.zig");
const nfa = pzre.nfa;
const language = pzre.language;
const Config = language.Config;
const parse = pzre.parse;
const Parser = parse.Parser;
const ParseError = parse.ParseError;
const compile = pzre.compile;
const context = nfa.context;

const inferProblem = nfa.search_problem.inferProblem;
const search_problem = nfa.search_problem;
const optimizeDestructively = pzre.ast.optimize.optimizeDestructively;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

const _ast = pzre.ast;
pub const Ast = _ast.Ast;

pub fn requiresAst(comptime config: Config) bool {
  if (config.optimize) return true;
  if (config.problem == null) return true;
  return switch (config.problem.?) {
    .bi_directional_pass, .strict_end_anchor => true,
    else => false,
  };
}

pub fn RuntimeNfa(comptime config: Config) type {
  const max = config.limits.max_submachine_states;
  const context_max = if(config.limits.context_breakpoint) |m| m else max;
  return nfa.Nfa(config.limits, config.sets, .dynamic, max, context_max, config.context);
}

pub const ComptimeNfaResolutionObjects = struct {
  sets: []const Set,
  ast: ?Ast = null,
  problem: search_problem.Name,
  breakpoint: nfa.state.Breakpoint,
  /// The implied size by the AST
  /// Does not account for total states due to multiple submachines
  base_states: comptime_int,
};

/// Returns the required objects for performing NFA type resolution
pub fn deriveNfaResolutionObjects(
  comptime config: Config,
  /// If the ast has to be generated, then its returned anyway
  comptime return_ast: bool,
  comptime pattern: []const u8,
) ParseError!ComptimeNfaResolutionObjects {
  comptime {
    // An NFA cannot be compiled directly, for type resolution we require:
    // - a dry run, or
    // - an ast
    // Comptime parsing performance is trash

    const requires_ast = return_ast or requiresAst(config);

    if (requires_ast) {
      var parser: Parser(config.sets, config.semantics, config.limits, true, .make_ast, .comptime_dynamic, null) = .new;
      const result = try parser.parseComptime(pattern);

      var tree = Ast.init(result.ast_root, result.ast_nodes);
      var sets = result.sets;
      if (config.optimize) {
        sets, tree  = try optimizeDestructively(config.limits.max_submachine_states, .comptime_dynamic, undefined, result.sets, tree);
      }

      const problem = config.problem orelse inferProblem(tree);

      const base_states = if (config.optimize) tree.calculateNfaStateCountAssumeOptimized()
        else tree.calculateNfaStateCount();
      const breakpoint = nfa.state.getBreakpoint(base_states);
      
      return .{
        .sets = sets,
        .ast = tree,
        .problem = problem,
        .breakpoint = breakpoint,
        .base_states = base_states,
      };

    } else { // not clear how much better this truly is
      var dry_parser: Parser(config.sets, config.semantics, config.limits, true, .dry, .comptime_dynamic, null) = .new;
      const dry_result = try dry_parser.parseComptime(pattern);

      const problem = config.problem.?;
      const base_states = dry_result.meta_data.states_count;
      const breakpoint = nfa.state.getBreakpoint(base_states);

      return .{
        .sets = dry_result.sets,
        .ast = null,
        .problem = problem,
        .breakpoint = breakpoint,
        .base_states = base_states,
      };
    }
  }
}

pub fn ComptimeNfa(
  comptime config: Config,
  comptime pattern: []const u8,
) type {
  const o = deriveNfaResolutionObjects(config, false, pattern) catch |err| compile.compileError(pattern, err);
  return ComptimeNfaFromObjects(config, o);
}

pub fn ComptimeNfaFromObjects(
  comptime config: Config,
  comptime o: ComptimeNfaResolutionObjects,
) type {
  const req_context_size = search_problem.calculateMaximumSubmachineSize(o.base_states, o.problem);
  const context_mode = if (config.context == .compact_fixed) context.Mode{.fixed = req_context_size} else config.context;
  const context_breakpoint = if (config.limits.context_breakpoint) |b| b else o.breakpoint;

  return nfa.Nfa(config.limits, config.sets, .comptime_dynamic, o.breakpoint, context_breakpoint, context_mode);
}
