//! Unified object construction logic and error validation
//!
//! All object generators use this API for proper construction (whether PARSER or AST or OTHER). This ensures that errors and edgecases are consistent across pipelines. Allowing us to have a truly modular pipeline
//!
const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const pzre = @import("../root.zig");

const arch = pzre.arch;
const ast = pzre.ast;
const nfa = pzre.arch.minimal_nfa;

const compile = pzre.compile;
const parse = compile.parse;
const Action = parse.Action;
const ParseError = compile.ParseError;
const Semantics = compile.Semantics;
const Limits = compile.Limits;
const addWithOverflow = pzre.misc.addWithOverflow;
const mulWithOverflow = pzre.misc.mulWithOverflow;

const lens = pzre.lens;
const debug = lens.debug;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

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
  /// The number of (nfa) states the pattern requires to be allocated
  states_count: usize = 0,
  /// The deepest execution path of the resulting AST
  ast_depth: usize = 0,
  // Mirrors Fragment.unpatchable_fallthrough to predict .jump states
  unpatchable_fallthrough: bool = false,

  const Self = @This();

  pub fn validate(self: Self, comptime limits: Limits) ParseError!void {
    if (limits.max_states < self.states_count) return error.TooManyStates;
    if (limits.max_depth < self.ast_depth) return error.TooDeep;
  }

  // Called when parsing finishes
  // Currently no-op after the removal of the accept state
  pub fn accept(self: Self) Self {
    return self;
  }

  pub fn initEpsilon() Self {
    // Epsilons correctly allocate 0 states in Fragment.empty
    return .{ .is_epsilon = true, .ast_depth = 1, .states_count = 0, .unpatchable_fallthrough = false };
  }

  pub fn initTerm() Self {
    return .{ .states_count = 1, .ast_depth = 1, .unpatchable_fallthrough = false };
  }

  pub fn concat(lhs: Self, rhs: Self) error{Overflow}!Self {
    const max: usize = @max(lhs.ast_depth, rhs.ast_depth);
    const new_depth: usize = try addWithOverflow(max, 1);
    
    if (lhs.is_epsilon and rhs.is_epsilon) {
      return .{
        .is_epsilon = true,
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .ast_depth = new_depth,
        .unpatchable_fallthrough = false,
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
        .states_count = try addWithOverflow(lhs.states_count, rhs.states_count),
        .ast_depth = new_depth,
        // Concat overwrites LHS fallthrough with RHS fallthrough without resolving
        .unpatchable_fallthrough = rhs.unpatchable_fallthrough,
      };
    }
  }

  pub fn @"union"(lhs: Self, rhs: Self) error{Overflow}!Self {
    const max: usize = @max(lhs.ast_depth, rhs.ast_depth);
    const new_depth: usize = try addWithOverflow(max, 1);
    
    if (lhs.is_epsilon and rhs.is_epsilon) {
      return .{
        .is_epsilon = true,
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .ast_depth = new_depth,
        .unpatchable_fallthrough = false,
      };
    } else if (lhs.is_epsilon) {
      var res = try rhs.quantify(.{ .min = 0, .max = 1 });
      res.is_variable_len = true;
      res.ast_depth = new_depth;
      return res;
    } else if (rhs.is_epsilon) {
      var res = try lhs.quantify(.{ .min = 0, .max = 1 });
      res.is_variable_len = true;
      res.ast_depth = new_depth;
      return res;
    } else {
      // If LHS has an unpatchable fallthrough, Fragment.union resolves it by adding 1 jump state
      const lhs_jump_state = if (lhs.unpatchable_fallthrough) @as(usize, 1) else 0;
      return .{
        .is_variable_len = lhs.is_variable_len or rhs.is_variable_len,
        .states_count = try addWithOverflow(try addWithOverflow(try addWithOverflow(lhs.states_count, rhs.states_count),  1), lhs_jump_state),
        .ast_depth = new_depth,
        .unpatchable_fallthrough = rhs.unpatchable_fallthrough,
      };
    }
  }

  pub fn capture(self: Self) Self {
    _ = self;
    return .{ .states_count = 1, .ast_depth = 1 };
  }

  pub fn quantify(child: Self, repeat: Repeat) error{Overflow}!Self {
    if (child.is_epsilon) return child;
    var new_data = child;
    new_data.ast_depth = try addWithOverflow(new_data.ast_depth, 1);
    
    if (repeat.max) |max| {
      if (repeat.min != max) new_data.is_variable_len = true;
    } else {
      new_data.is_variable_len = true;
    }

    if (repeat.max != null and repeat.max.? == 0 and repeat.min == 0) {
      new_data.is_epsilon = true;
      new_data.states_count = 0;
      new_data.unpatchable_fallthrough = false;
    } else if (repeat.max) |max| {
      const concrete_count = repeat.min;
      var optional_count = max - repeat.min;
      if (repeat.min == 0 and optional_count > 0) optional_count -= 1;

      var cloned_states = child.states_count;
      if (optional_count > 0) {
        cloned_states = try addWithOverflow(cloned_states, 1); // optional() split
      }

      var cur_states = child.states_count;
      if (repeat.min == 0) {
        cur_states = try addWithOverflow(cur_states, 1); // optional() split
      }

      if (concrete_count > 0) {
        cur_states = try mulWithOverflow(cur_states, concrete_count);
      }

      if (optional_count > 0) {
        const optional_states = try mulWithOverflow(cloned_states, optional_count);
        cur_states = try addWithOverflow(cur_states, optional_states);
      }
      
      new_data.states_count = cur_states;
      // Duplication carries over the child's fallthrough state natively
      new_data.unpatchable_fallthrough = child.unpatchable_fallthrough;
      
    } else {
      if (repeat.min > 0) {
        // plus() adds 1 split state and SETS fallthrough
       
        new_data.states_count = try mulWithOverflow(child.states_count, repeat.min);
        new_data.states_count = try addWithOverflow(new_data.states_count, 1);
       
        new_data.unpatchable_fallthrough = true;
      } else {
        // star() resolves fallthrough if present, then adds 1 split state
        var cur_states = child.states_count;
        if (child.unpatchable_fallthrough) {
          cur_states = try addWithOverflow(cur_states, 2);  // jump + split
        } else {
          cur_states = try addWithOverflow(cur_states, 1);  // just split
        }
        
        new_data.states_count = cur_states;
        new_data.unpatchable_fallthrough = false;
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
  comptime actions: std.EnumSet(Action),
  comptime model: MemoryModel,
  comptime rbp: ?arch.RelativeBreakpoint,
  comptime sets_bp: ?arch.AbsoluteBreakpoint,
) type {

  const has_ast = actions.contains(.ast);
  const has_nfa = actions.contains(.nfa);

  return struct {
    nfa: if (has_nfa) Fragment    else void,
    ast: if (has_ast) usize else void,
    data: MetaData = .{},

    const Self = @This();
    pub const Fragment = if (has_nfa) nfa.fragment.Fragment(model, rbp.?, sets_bp.?) else {};
    pub const State = if (rbp != null) nfa.state.State(rbp.?, sets_bp.?);
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
    pub fn createFromState(gpa: Allocator, state: State) ParseError!Self {
      assert(has_nfa);
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
            assert(i <= std.math.maxInt(sets_bp.?.Index()));
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
      const new_data = try lhs.data.@"union"(rhs.data);
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
      const new_data = try lhs.data.concat(rhs.data);
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
      const new_data = try node.data.quantify(repeat);
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
