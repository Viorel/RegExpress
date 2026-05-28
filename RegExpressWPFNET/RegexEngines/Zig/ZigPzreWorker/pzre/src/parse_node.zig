//! Unified object construction logic and error validation
//!
//! All object generators use this API for proper construction (whether PARSER or AST or OTHER). This ensures that errors and edgecases are consistent across pipelines. Allowing us to have a truly modular pipeline
//!
const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const pzre = @import("root.zig");

const Mode = pzre.misc.Mode;
const ast = pzre.ast;
const nfa = pzre.nfa;
const parse = pzre.parse;
const Action = parse.Action;

const lens = pzre.lens;
const debug = lens.debug;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const ParseError = pzre.ParseError;
const Semantics = pzre.Semantics;
const Limits = pzre.Limits;

const misc = pzre.misc;
const Repeat = misc.Repeat;
const Assertion = misc.Assertion;

/// Accumulated data of a pattern during a parse. 
/// Always computed regardless of compilation mode.
/// The single source of truth for predicting future state counts
pub const MetaData = struct {
  is_epsilon: bool = false,
  /// Currently unused and does not correctly show variable lengthness. Fails for
  /// abc|ab
  is_variable_len: bool = false,
  /// The number of states the pattern requires to be allocated (excluding final accept state)
  states_count: usize = 0,
  /// The deepest execution path of the resulting AST
  ast_depth: usize = 0,

  const Self = @This();

  pub fn validate(self: Self, comptime limits: Limits) ParseError!void {

    const max_submachine_len = std.math.maxInt(limits.max_submachine_states.Offset());
    if (max_submachine_len < self.states_count) return error.TooManyStates;
    if (limits.max_states < self.states_count) return error.TooManyStates;
    if (limits.max_depth < self.ast_depth) return error.TooDeep;
  }

  pub fn accept(self: Self) Self {
    var new = self;
    new.states_count = if (self.is_epsilon) 1 else new.states_count + 1;
    return new;
  }

  pub fn initEpsilon() Self {
    return .{ .is_epsilon = true, .ast_depth = 1 };
  }

  pub fn initTerm() Self {
    return .{ .states_count = 1, .ast_depth = 1 };
  }

  pub fn concat(lhs: Self, rhs: Self) Self {
    const max: usize = @max(lhs.ast_depth, rhs.ast_depth);
    const new_depth: usize = max + 1;
    
    if (lhs.is_epsilon and rhs.is_epsilon) {
      return .{
        .is_epsilon = true,
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .ast_depth = new_depth,
      };
    } else if (lhs.is_epsilon) {
      var res = rhs;
      res.is_variable_len = res.is_variable_len or lhs.is_variable_len;
      res.ast_depth = new_depth;
      return res;
    } else if (rhs.is_epsilon) {
      var res = lhs;
      res.is_variable_len = res.is_variable_len or rhs.is_variable_len;
      res.ast_depth = new_depth;
      return res;
    } else {
      return .{
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .states_count = lhs.states_count + rhs.states_count,
        .ast_depth = new_depth,
      };
    }
  }

  pub fn @"union"(lhs: Self, rhs: Self) Self {
    const max: usize = @max(lhs.ast_depth, rhs.ast_depth);
    const new_depth: usize = max + 1;
    
    if (lhs.is_epsilon and rhs.is_epsilon) {
      return .{
        .is_epsilon = true,
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .ast_depth = new_depth,
      };
    } else if (lhs.is_epsilon) {
      var res = rhs.quantify(.{ .min = 0, .max = 1 });
      res.is_variable_len = true;
      res.ast_depth = new_depth;
      return res;
    } else if (rhs.is_epsilon) {
      var res = lhs.quantify(.{ .min = 0, .max = 1 });
      res.is_variable_len = true;
      res.ast_depth = new_depth;
      return res;
    } else {
      return .{
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .states_count = lhs.states_count + rhs.states_count + 1,
        .ast_depth = new_depth,
      };
    }
  }

  pub fn quantify(child: Self, repeat: Repeat) Self {
    if (child.is_epsilon) return child;

    var new_data = child;
    new_data.ast_depth += 1;
    
    if (repeat.max) |max| {
      if (repeat.min != max) new_data.is_variable_len = true;
    } else {
      new_data.is_variable_len = true;
    }

    if (repeat.max != null and repeat.max.? == 0 and repeat.min == 0) {
      new_data.is_epsilon = true;
      new_data.states_count = 0;
    } else if (repeat.max) |max| {
      const exact_copies = repeat.min;
      const optional_copies = max - repeat.min;
      new_data.states_count = (exact_copies * child.states_count) + (optional_copies * (child.states_count + 1));
    } else {
      if (repeat.min == 0) {
        new_data.states_count = child.states_count + 1;
      } else {
        new_data.states_count = (repeat.min * child.states_count) + 1;
      }
    }
    return new_data;
  }
};

/// The output of the parser. Represents an NFA fragment, an AST node, or purely metadata.
/// 
/// This structure encapsulates the actual object generation. The parser does not concern itself 
/// with whatever objects are being built. It simply calls the correct methods at the correct time.
pub fn ParseNode(
  comptime limits: Limits,
  comptime action: Action,
  comptime model: MemoryModel,
  comptime breakpoint: ?nfa.state.Breakpoint,
) type {

  const has_ast = action == .make_ast or action == .make_nfa_and_ast;
  const has_nfa = action == .make_nfa or action == .make_nfa_and_ast;
  const dry_run = action == .dry;

  return struct {
    nfa: if (has_nfa) Fragment    else void,
    ast: if (has_ast) usize else void,
    data: MetaData = .{},

    const Self = @This();
    pub const Fragment = if (has_nfa) nfa.fragment.Fragment(model, breakpoint.?) else {};
    pub const State = if (has_nfa) nfa.state.State(breakpoint.?) else {};
    pub const AstList = polymorphic_memory.presets.single_ended.Create(model, null, ast.Node);

    pub fn createAstNode(gpa: Allocator, ast_list: *AstList, node: ast.Node) Allocator.Error!usize {
      try ast_list.append(gpa, node);
      return ast_list.len() - 1;
    }

    pub fn createEpsilon(gpa: Allocator, ast_list: *AstList) ParseError!Self {
      const new_data = MetaData.initEpsilon();
      try new_data.validate(limits);
      
      const new_ast = if (comptime has_ast) 
        try createAstNode(gpa, ast_list, .{.epsilon = {}}) 
      else {};
      
      const new_nfa = if (comptime has_nfa) Fragment.empty else {};

      return Self{
        .nfa = new_nfa,
        .ast = new_ast,
        .data = new_data,
      };
    }

    /// Creates a parse node directly from a state
    /// Asserts that only an nfa is being built
    pub fn createFromState(gpa: Allocator, state: State) ParseError!Self {
      assert(has_nfa and !has_ast and !dry_run);
      const new_data = MetaData.initTerm();
      try new_data.validate(limits);
      return Self{
        .nfa = try Fragment.create(gpa, state),
        .ast = {},
        .data = new_data,
      };
    }

    // NOTE:
    // Ensure each api call follows the explicit structure Metadata -> AST -> NFA -> Return
    // No early returns

    pub fn create(gpa: Allocator, ast_list: *AstList, atom: misc.Atom) ParseError!Self {
      // 1. Metadata
      const new_data = MetaData.initTerm();
      try new_data.validate(limits);

      // 2. AST
      const new_ast = if (comptime has_ast) b: {
        const node = switch (atom) {
          .assertion => |ass| ast.Node{.assertion = ass},
          .char => |c| ast.Node{.term = .{ .char = c} },
          .set_idx => |i| ast.Node{.term = .{ .set_idx = i} },
        };
        break :b try createAstNode(gpa, ast_list, node);
      } else {};

      // 3. NFA
      const new_nfa = if (comptime has_nfa) b: {
        const state: State = switch (atom) {
          .assertion => |ass| State{.tag = nfa.state.Tag.fromAssertion(ass)},
          .char => |c| State{ .tag = .term_char, .term = .{ .char = .{ .value = c } } },
          .set_idx => |i| i: {
            assert(i <= std.math.maxInt(State.Idx));
            break :i State{ .tag = .term_set, .term = .{ .set_idx = @intCast(i) } };
          },
        };
        break :b try Fragment.create(gpa, state);
      } else {};

      return Self{
        .nfa = new_nfa,
        .ast = new_ast,
        .data = new_data,
      };
    }

    pub fn @"union"(lhs: *Self, gpa: Allocator, rhs: *Self, ast_list: *AstList) ParseError!Self {
      errdefer lhs.destroy(gpa);
      errdefer rhs.destroy(gpa);

      lhs.epsilonSanityCheck();
      rhs.epsilonSanityCheck();

      // 1. Metadata
      const new_data = lhs.data.@"union"(rhs.data);
      try new_data.validate(limits);

      // 2. AST
      const new_ast = if (comptime has_ast) b: {
        break :b try createAstNode(gpa, ast_list, ast.Node{.@"union" = .{.lhs = lhs.ast, .rhs = rhs.ast}});
      } else {};

      // 3. NFA
      const new_nfa = if (comptime has_nfa) b: {
        if (lhs.data.is_epsilon and rhs.data.is_epsilon) {
          rhs.nfa.destroy(gpa);
          break :b lhs.nfa;
        } else if (lhs.data.is_epsilon) {
          try rhs.repeatExact(gpa, .{ .min = 0, .max = 1 }, ast_list);
          lhs.nfa.destroy(gpa);
          break :b rhs.nfa;
        } else if (rhs.data.is_epsilon) {
          try lhs.repeatExact(gpa, .{ .min = 0, .max = 1 }, ast_list);
          rhs.nfa.destroy(gpa);
          break :b lhs.nfa;
        } else {
          try lhs.nfa.@"union"(gpa, rhs.nfa);
          rhs.nfa.destroy(gpa);
          break :b lhs.nfa;
        }
      } else {};

      return Self{ .nfa = new_nfa, .ast = new_ast, .data = new_data };
    }

    pub fn concat(lhs: *Self, gpa: Allocator, rhs: *Self, ast_list: *AstList) ParseError!Self {
      errdefer lhs.destroy(gpa);
      errdefer rhs.destroy(gpa);

      lhs.epsilonSanityCheck();
      rhs.epsilonSanityCheck();
      
      // 1. Metadata
      const new_data = lhs.data.concat(rhs.data);
      try new_data.validate(limits);

      // 2. AST
      const new_ast = if (comptime has_ast) b: {
        break :b try createAstNode(gpa, ast_list, ast.Node{.concat = .{.lhs = lhs.ast, .rhs = rhs.ast}});
      } else {};
      
      // 3. NFA
      const new_nfa = if (comptime has_nfa) b: {
        if (lhs.data.is_epsilon and rhs.data.is_epsilon) {
          rhs.nfa.destroy(gpa);
          break :b lhs.nfa;
        } else if (lhs.data.is_epsilon) {
          lhs.nfa.destroy(gpa);
          break :b rhs.nfa;
        } else if (rhs.data.is_epsilon) {
          rhs.nfa.destroy(gpa);
          break :b lhs.nfa;
        } else {
          try lhs.nfa.concat(gpa, rhs.nfa);
          rhs.nfa.destroy(gpa);
          break :b lhs.nfa;
        }
      } else {};

      return Self{ .nfa = new_nfa, .ast = new_ast, .data = new_data };
    }

    pub fn repeatExact(node: *Self, gpa: Allocator, repeat: Repeat, ast_list: *AstList) ParseError!void {
      errdefer node.destroy(gpa);
      node.epsilonSanityCheck();

      if (repeat.max) |max| {
        if (max < repeat.min) return error.InvalidRepeat;
      }

      if (limits.max_arbitrary_repetition) |upper_bound| {
        if (repeat.max) |max| {
          if (max > upper_bound and max > 1) return error.TooHighArbitraryRepeat;
        }
      }

      // 1. Metadata
      const new_data = node.data.quantify(repeat);
      try new_data.validate(limits);

      // 2. AST
      const new_ast = if (comptime has_ast) b: {
        break :b try createAstNode(gpa, ast_list, ast.Node{.quantifier = .{.node = node.ast, .repeat = repeat}});
      } else {};

      // 3. NFA
      if (comptime has_nfa) {
        if (!node.data.is_epsilon) {
          if (repeat.max != null and repeat.max.? == 0 and repeat.min == 0) {
            node.nfa.destroy(gpa);
            node.nfa = .empty;
          } else {
            try node.nfa.repeatExact(gpa, repeat);
          }
        }
      }

      // 4. Update Node
      node.data = new_data;
      if (comptime has_ast) node.ast = new_ast;
    }

    pub fn destroy(self: *Self, gpa: Allocator) void {
      if (has_nfa) self.nfa.destroy(gpa);
    }

    pub fn epsilonSanityCheck(self: Self) void {
      if (comptime has_nfa) {
        assert(if (self.data.is_epsilon) self.nfa.states.len() == 0 else true);
      }
    }
  };
}
