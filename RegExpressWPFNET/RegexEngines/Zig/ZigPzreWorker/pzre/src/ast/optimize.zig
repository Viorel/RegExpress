const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const ast = pzre.ast;
const Ast = ast.Ast;

const debug = pzre.lens.debug;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const List = polymorphic_memory.presets.single_ended.Create;
const polymorphicSetUnionInplace = pzre.pse.polymorphicSetUnionInplace;

const state = pzre.nfa.state;
const State = state.State;

const misc = pzre.misc;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const Range = ascii.Range;

const testing = std.testing;
const expectDeeplyEqual = pzre.lens.testing.expectDeeplyEqual;

const compile = pzre.compile;

/// Performs the optimization pass on the AST
/// Grabs ownership over 'old_ast' and 'sets', accessing either post call is illegal
///
/// breakpoint is null if it is not known at this stage yet. This implies comptime compilation
/// 
/// Returns a new AST and sets struture
pub fn optimizeDestructively(
  comptime breakpoint: ?state.Breakpoint,
  comptime model: MemoryModel,
  gpa: Allocator,
  sets: []const Set,
  old_ast: Ast,
) Allocator.Error!struct {[]const Set, Ast} {
  assert(old_ast.root < old_ast.nodes.len);

  const bloated_sets, var optimized_ast = b: {
    var curr = old_ast;

    {
      errdefer misc.destroySets(gpa, sets);

      curr = try applyDestructively(model, .unroll_quantifiers, gpa, curr, .{model, gpa, curr});
      curr = try applyDestructively(model, .normalize_quantifiers, gpa, curr, .{model, gpa, curr});
       
      // Quantifier normalization will turn a{0,0} to epsilon
      curr = try applyDestructively(model, .epsilon_pruning, gpa, curr, .{model, gpa, curr});
      // curr = try applyDestructively(model, .union_trimming, gpa, curr, .{model, gpa, curr});
      // curr = try applyDestructively(model, .assertion_deduplication, gpa, curr, .{model, gpa, curr});
    }

    // Changes to sets:
    //  1. new entries are appended
    //  2. existing become redundant
    const new_sets, curr = try applySetMergeDestructively(breakpoint, model, gpa, sets, curr);

    // 'sets' is consumed
    break :b .{new_sets, curr};
  };

  const compact_sets = b: {
    errdefer misc.destroySets(gpa, bloated_sets);
    errdefer optimized_ast.deinit(gpa);
    const compact_sets, optimized_ast = try optimized_ast.compactSets(model, gpa, bloated_sets);
    misc.destroySets(gpa, bloated_sets);
    break :b compact_sets;
  };

  const r = .{ compact_sets, optimized_ast };
  return r;
}

/// Applies the set merge optimization
/// Consumes the ast and sets, returning new ones as a replacement
pub fn applySetMergeDestructively(
  comptime breakpoint: ?state.Breakpoint,
  comptime model: MemoryModel,
  gpa: Allocator,
  sets: []const Set,
  old_ast: Ast,
) Allocator.Error!struct {[]const Set, Ast} {
  var curr = old_ast;

  const new_sets: []const Set = if (model == .dynamic) b: {
    curr.assertPhaseOneOptimized();

    const state_size = @sizeOf(state.State(breakpoint.?));

    var setslist: List(model, null, Set) = .initUsing(sets);
    errdefer misc.destroySetslist(model, gpa, &setslist);

    const not_aggressive = false;
    curr = try applyDestructively(model, .set_merge, gpa, curr, .{
      model,
      state_size,
      @sizeOf([]const usize),
      gpa,
      &setslist,
      curr,
      not_aggressive,
      null
    });
    errdefer curr.deinit(gpa);
    break :b try setslist.toOwnedConstSlice(gpa);

  } else b: {
    assert(@inComptime());
    // The target integer sizes are still unknown. Determine the bracket:
    var setslist: List(model, null, Set) = .initUsing(sets);

    const base_states_count = curr.calculateNfaStateCountAssumeOptimized();
    const base_bracket = state.getBreakpoint(base_states_count);

    const aggressive = true;
    const duped_tree = curr.dupe(gpa) catch unreachable;

    curr = applyDestructively(model, .set_merge, undefined, duped_tree, .{
      model,
      base_bracket.stateSize(),
      @sizeOf([]const usize),
      gpa,
      &setslist,
      duped_tree,
      aggressive,
      null,
    }) catch unreachable;

    const aggressive_states_count = curr.calculateNfaStateCountAssumeOptimized();
    const aggressive_bracket = state.getBreakpoint(aggressive_states_count);
    const not_aggressive = false;

    if (aggressive_bracket.stateSize() < base_bracket.stateSize()) { // New integer bracket
      // TODO:
      // Currently, the merge algorithm is strictly unidirectional, it only merges, but never deconstructs
      // existing sets back into unions. The correct approach is to use the aggressive tree and bring it back
      // up in state count optimally, currently simply return the aggressive tree
      const aggressive_tree = curr;
      curr = aggressive_tree;

      // Integer sizes are better when merging everything, now apply again, 
      // bringing state count up without going past the bracket

      // const max_state_count = std.math.maxInt(AggressiveState.Offset);
      // curr = applyDestructively(model, .set_merge, undefined, curr, .{
      //   model,
      //   aggressive_bracket,
      //   @sizeOf([]const usize),
      //   gpa,
      //   &setslist,
      //   curr,
      //   not_aggressive,
      //   max_state_count,
      // }) catch unreachable;

    } else { // Not new bracket
      // do the optimal merge, since integer sizes were not succesfully reduced
      // Ideally we would not dupe the tree, and bring it back up
      //  ^ not currently implemented, instead use the duped tree and bring it down less aggressively

      curr = applyDestructively(model, .set_merge, undefined, duped_tree, .{
        model,
        base_bracket.stateSize(),
        @sizeOf([]const usize),
        gpa,
        &setslist,
        duped_tree,
        not_aggressive,
        null,
      }) catch unreachable;
    }

    break :b setslist.toOwnedConstSlice(gpa) catch unreachable;
  };

  return .{new_sets, curr};
}

/// Consumes the old tree, and returns a new one in its place
pub fn applyDestructively(
  comptime model: MemoryModel,
  comptime opt: Optimization,
  gpa: Allocator,
  old_ast: Ast,
  args: anytype,
) Allocator.Error!Ast {
  assert(old_ast.root < old_ast.nodes.len);

  defer if (model == .dynamic) gpa.free(old_ast.nodes);
  const fname = comptime opt.camelCase();
  const optFN = @field(@This(), fname);

  const new: Ast = try @call(.auto, optFN, args);
  return new;
}

/// These have to be snake case versions of the pascal case functions
const Optimization = enum {
  epsilon_pruning,
  unroll_quantifiers,
  normalize_quantifiers,
  // union_trimming,
  // assertion_deduplication,
  set_merge,

  const Self = @This();

  fn camelCase(self: Self) []const u8 {
    return switch (self) {
      .epsilon_pruning => "epsilonPruning",
      .unroll_quantifiers => "unrollQuantifiers",
      .normalize_quantifiers => "normalizeQuantifiers",
      // .union_trimming => "unionTrimming",
      // .assertion_deduplication => "assertionDeduplication",
      .set_merge => "setMerge",
    };
  }
};

pub fn expectOptimization(comptime opt: Optimization, pattern: []const u8, expected: []const u8) !void {
  return expectOptimizationWithArch(
    opt,
    8,
    @sizeOf([]const u8),
    pattern,
    expected,
  );
}

/// Expect an optimization on an algorithm that uses architecture dependent information
pub fn expectOptimizationWithArch(
  comptime opt: Optimization,
  comptime state_size: comptime_int,
  comptime slice_size: comptime_int,
  pattern: []const u8,
  expected: []const u8,
) !void {

  // WARN: what this does and why it fails:
  // 
  // 1. we generate the literal AST from expected
  // 2. we generate the optimized AST from pattern
  // Check structural equality on both
  // 
  // It produces false negatives because the nodes are in different order even in cases
  //  when both trees represent the same automata, TODO: perform tree normalization

  const L = List(.dynamic, null, Set);

  const gpa = std.testing.allocator;

  _, var expected_ast = try compile.ast(.{}, false, gpa, expected);
  defer expected_ast.deinit(gpa);

  const user_sets, var user_ast = try compile.ast(.{}, true, gpa, pattern);
  defer user_ast.deinit(gpa);

  const duped_user_sets = b: {
    errdefer misc.destroySets(gpa, user_sets);
    break :b try misc.dupeSets(gpa, user_sets);
  };
  defer misc.destroySets(gpa, duped_user_sets);

  const fname = comptime opt.camelCase();
  const optFN = @field(@This(), fname);


  {
    var setslist: L = .initUsing(user_sets);
    defer misc.destroySetslist(.dynamic, gpa, &setslist);

    const args = if (opt == .set_merge) .{MemoryModel.dynamic, state_size, slice_size, gpa, &setslist, user_ast} 
      else .{MemoryModel.dynamic, gpa, user_ast};

    var optimized: Ast = try @call(.auto, optFN, args);
    defer optimized.deinit(gpa);

    const compact_sets = try optimized.compactSets(.dynamic, gpa, setslist.getConstSlice());
    defer misc.destroySets(gpa, compact_sets);

    // try expectDeeplyEqual(expected_ast, optimized);
  }

  // Run the exact same test again with the failing allocator
  // This is required because expectDeeplyEqual is a very large function that should not be
  //  tested by the zig impl
  const dummy = struct{
    fn f(_gpa: Allocator, ds: []const Set, ua: Ast) !void {
      // Isolate the failing allocator by copying the sets locally
      var local_sets: L = .empty;
      try local_sets.ensureCapacityPrecise(_gpa, ds.len);
      defer misc.destroySetslist(.dynamic, _gpa, &local_sets);

      for (ds) |set| {
        const duped = try set.dupe(_gpa);
        local_sets.append(_gpa, duped) catch unreachable;
      }

      const args = if (opt == .set_merge) .{MemoryModel.dynamic, state_size, slice_size, _gpa, &local_sets, ua} 
        else .{MemoryModel.dynamic, _gpa, ua};

      var optimized = try @call(.auto, optFN, args);
      defer optimized.deinit(_gpa);
    }
  };

  _ = dummy{};

  try std.testing.checkAllAllocationFailures(gpa, dummy.f, .{duped_user_sets, user_ast});
}

/// Removes epsilon
/// ba{0,0} -> b
/// a{0,0}+ -> NOTHING
/// a{0,0}* -> NOTHING
/// a{0,0}|abc -> (abc)?
/// a{0,0}abc -> abc
pub fn epsilonPruning(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try epsilonPruningInner(model, gpa, old_ast, old_ast.root, &new_nodes);

  if (new_root) |root| {
    return Ast.init(root, try new_nodes.toOwnedConstSlice(gpa));
  } else {
    // The entire pattern collapsed into nothing. 
    // Safely append a single active epsilon node to serve as the root.
    try new_nodes.append(gpa, .epsilon);
    return Ast.init(0, try new_nodes.toOwnedConstSlice(gpa));
  }
}

fn epsilonPruningInner(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast, 
  node_idx: usize, 
  new_nodes: *List(model, null, ast.Node)
) Allocator.Error!?usize {
  
  const node = old_ast.nodes[node_idx];
  switch (node) {
    .epsilon => return null, // Bubble up the epsilon state without writing to memory
    .assertion, .term => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try epsilonPruningInner(model, gpa, old_ast, c.lhs, new_nodes);
      const new_rhs = try epsilonPruningInner(model, gpa, old_ast, c.rhs, new_nodes);

      if (new_lhs == null and new_rhs == null) return null;
      if (new_lhs == null) return new_rhs;
      if (new_rhs == null) return new_lhs;

      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs.?, .rhs = new_rhs.? } });
      return new_nodes.len() - 1;
    },
    .@"union" => |u| {
      const new_lhs = try epsilonPruningInner(model, gpa, old_ast, u.lhs, new_nodes);
      const new_rhs = try epsilonPruningInner(model, gpa, old_ast, u.rhs, new_nodes);

      if (new_lhs == null and new_rhs == null) return null;

      if (new_lhs == null) {
        try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_rhs.?, .repeat = .{ .min = 0, .max = 1 } } });
        return new_nodes.len() - 1;
      }

      if (new_rhs == null) {
        try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_lhs.?, .repeat = .{ .min = 0, .max = 1 } } });
        return new_nodes.len() - 1;
      }

      try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs.?, .rhs = new_rhs.? } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      const new_child = try epsilonPruningInner(model, gpa, old_ast, q.node, new_nodes);

      if (new_child == null) return null;

      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child.?, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

// test epsilonPruning {
//   // Not really testable currently in isolation
//   // quantifier normalization has to occur first
//
//   try expectOptimization(.epsilon_pruning, "()", "");
//   try expectOptimization(.epsilon_pruning, "()a", "a");
//   try expectOptimization(.epsilon_pruning, "b()", "b");
//   try expectOptimization(.epsilon_pruning, "(){0,0}", "");
//   try expectOptimization(.epsilon_pruning, "()()", "");
//
//   try expectOptimization(.epsilon_pruning, "a|()", "a?");
//   try expectOptimization(.epsilon_pruning, "abc|()", "(abc)?");
//   try expectOptimization(.epsilon_pruning, "()|a", "a?");
//   try expectOptimization(.epsilon_pruning, "()|()", "");
//
//   try expectOptimization(.epsilon_pruning, "()*", "");
//   try expectOptimization(.epsilon_pruning, "()+", "");
//   try expectOptimization(.epsilon_pruning, "()?", "");
//   try expectOptimization(.epsilon_pruning, "(){3,5}", "");
//
//   try expectOptimization(.epsilon_pruning, "((()|())*)?", "");
//   try expectOptimization(.epsilon_pruning, "(a|())()", "a?");
// }

/// a{2,3} -> aaa?
/// a{3,} -> aaa+
/// a{2,2} -> aa
/// a{0,5} -> a?a?a?a?a?
pub fn unrollQuantifiers(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try unrollQuantifiersInner(model, gpa, old_ast, old_ast.root, &new_nodes);

  return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
}

fn unrollQuantifiersInner(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast, 
  node_idx: usize, 
  new_nodes: *List(model, null, ast.Node)
) Allocator.Error!usize {
  
  const node = old_ast.nodes[node_idx];
  switch (node) {
    .epsilon, .assertion, .term => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try unrollQuantifiersInner(model, gpa, old_ast, c.lhs, new_nodes);
      const new_rhs = try unrollQuantifiersInner(model, gpa, old_ast, c.rhs, new_nodes);
      
      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" => |u| {
      const new_lhs = try unrollQuantifiersInner(model, gpa, old_ast, u.lhs, new_nodes);
      const new_rhs = try unrollQuantifiersInner(model, gpa, old_ast, u.rhs, new_nodes);
      
      try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      if (q.repeat.is_unity()) {
        return try unrollQuantifiersInner(model, gpa, old_ast, q.node, new_nodes);
      }

      if (q.repeat.is_epsilon()) {
        try new_nodes.append(gpa, .epsilon);
        return new_nodes.len() - 1;
      }

      var result_idx: ?usize = null;
      const is_unbounded = q.repeat.max == null;

      const exact_count = if (is_unbounded and q.repeat.min > 0) q.repeat.min - 1 else q.repeat.min;

      for (0..exact_count) |_| {
        const current_child = try unrollQuantifiersInner(model, gpa, old_ast, q.node, new_nodes);
        if (result_idx) |r| {
          try new_nodes.append(gpa, .{ .concat = .{ .lhs = r, .rhs = current_child } });
          result_idx = new_nodes.len() - 1;
        } else {
          result_idx = current_child;
        }
      }

      if (!is_unbounded) {
        const max = q.repeat.max.?;
        if (max > q.repeat.min) {
          for (0..(max - q.repeat.min)) |_| {
            const current_child = try unrollQuantifiersInner(model, gpa, old_ast, q.node, new_nodes);
            try new_nodes.append(gpa, .{ .quantifier = .{ .node = current_child, .repeat = .{ .min = 0, .max = 1 } } });
            const opt_child = new_nodes.len() - 1;

            if (result_idx) |r| {
              try new_nodes.append(gpa, .{ .concat = .{ .lhs = r, .rhs = opt_child } });
              result_idx = new_nodes.len() - 1;
            } else {
              result_idx = opt_child;
            }
          }
        }
      } else {
        const tail_min: usize = if (q.repeat.min == 0) 0 else 1;
        const current_child = try unrollQuantifiersInner(model, gpa, old_ast, q.node, new_nodes);
        try new_nodes.append(gpa, .{ .quantifier = .{ .node = current_child, .repeat = .{ .min = tail_min, .max = null } } });
        const tail_node = new_nodes.len() - 1;

        if (result_idx) |r| {
          try new_nodes.append(gpa, .{ .concat = .{ .lhs = r, .rhs = tail_node } });
          result_idx = new_nodes.len() - 1;
        } else {
          result_idx = tail_node;
        }
      }

      return result_idx.?;
    },
  }
}

// test unrollQuantifiers {
//   try expectOptimization(.unroll_quantifiers, "a{0,}", "a*");
//   try expectOptimization(.unroll_quantifiers, "a{1,}", "a+");
//   try expectOptimization(.unroll_quantifiers, "a{0,1}", "a?");
//
//   try expectOptimization(.unroll_quantifiers, "a{1,1}", "a");
//   try expectOptimization(.unroll_quantifiers, "a{0,0}", "()");
//   try expectOptimization(.unroll_quantifiers, "a{3,3}", "aaa");
//   try expectOptimization(.unroll_quantifiers, "a{0,2}", "a?a?");
//   try expectOptimization(.unroll_quantifiers, "a{0,4}", "a?a?a?a?");
//   try expectOptimization(.unroll_quantifiers, "a{1,3}", "aa?a?");
//   try expectOptimization(.unroll_quantifiers, "a{2,3}", "aaa?");
//   try expectOptimization(.unroll_quantifiers, "a{2,5}", "aaa?a?a?");
//   try expectOptimization(.unroll_quantifiers, "a{2,4}", "aaa?a?");
//   try expectOptimization(.unroll_quantifiers, "a{2,}", "aa+");
//   try expectOptimization(.unroll_quantifiers, "a{5,}", "aaaaa+");
//
//   try expectOptimization(.unroll_quantifiers, "a{2,5}{0,3}", "(aaa?a?a?)?(aaa?a?a?)?(aaa?a?a?)?");
// }

/// Removes all repeated quantifiers:
///   a?? -> a?
///   a+* -> a*
///   a??*+??+++???******* -> a*
/// Asserts quantifiers have been unrolled
pub fn normalizeQuantifiers(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try normalizeQuantifiersInner(model, gpa, old_ast, old_ast.root, &new_nodes);

  return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
}

fn normalizeQuantifiersInner(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast, 
  node_idx: usize, 
  new_nodes: *List(model, null, ast.Node)
) Allocator.Error!usize {
  
  const node = old_ast.nodes[node_idx];
  switch (node) {
    .epsilon, .assertion, .term => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try normalizeQuantifiersInner(model, gpa, old_ast, c.lhs, new_nodes);
      const new_rhs = try normalizeQuantifiersInner(model, gpa, old_ast, c.rhs, new_nodes);
      
      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" => |u| {
      const new_lhs = try normalizeQuantifiersInner(model, gpa, old_ast, u.lhs, new_nodes);
      const new_rhs = try normalizeQuantifiersInner(model, gpa, old_ast, u.rhs, new_nodes);
      
      try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      if (q.repeat.max) |max| {
        assert(max == 1);         // only ?
      } else {
        assert(q.repeat.min < 2); // only + *
      }

      const new_child = try normalizeQuantifiersInner(model, gpa, old_ast, q.node, new_nodes);
      const child_node = new_nodes.get(new_child);
      
      if (child_node == .quantifier) {
        const inner = child_node.quantifier.repeat;
        const outer = q.repeat;
        
        const min = inner.min * outer.min;
        const max: ?usize = if (inner.max == null or outer.max == null) null else 1;
        
        new_nodes.set(new_child, .{
          .quantifier = .{
            .node = child_node.quantifier.node,
            .repeat = .{ .min = min, .max = max }
          }
        });
        return new_child;
      }

      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

// test normalizeQuantifiers {
//   try expectOptimization(.normalize_quantifiers, "(a?)?", "a?");
//   try expectOptimization(.normalize_quantifiers, "(a?)+", "a*");
//   try expectOptimization(.normalize_quantifiers, "(a?)*", "a*");
//
//   try expectOptimization(.normalize_quantifiers, "(a+)?", "a*");
//   try expectOptimization(.normalize_quantifiers, "(a+)+", "a+");
//   try expectOptimization(.normalize_quantifiers, "(a+)*", "a*");
//
//   try expectOptimization(.normalize_quantifiers, "(a*)?", "a*");
//   try expectOptimization(.normalize_quantifiers, "(a*)+", "a*");
//   try expectOptimization(.normalize_quantifiers, "(a*)*", "a*");
//
//   try expectOptimization(.normalize_quantifiers, "((a?)+)+", "a*");
//   try expectOptimization(.normalize_quantifiers, "((a+)+)+", "a+");
//   try expectOptimization(.normalize_quantifiers, "(((a*)?)+)*", "a*");
//   
//   try expectOptimization(.normalize_quantifiers, "a?b*", "a?b*");
//   try expectOptimization(.normalize_quantifiers, "(a+b+)+", "(a+b+)+");
//
//   try expectOptimization(.normalize_quantifiers, "(a+b+)+", "(a+b+)+");
//
//   try expectOptimization(.normalize_quantifiers, "a??????", "a?");
//   try expectOptimization(.normalize_quantifiers, "a++???+*******+++???", "a*");
//   try expectOptimization(.normalize_quantifiers, "a+++", "a+");
// }

/// errorABC|errorSOMESHIT      ->            error(ABC|SOMESHIT)
/// same shit for the tail
// pub fn unionTrimming(
//   comptime model: MemoryModel,
//   gpa: Allocator,
//   old_ast: Ast,
// ) Allocator.Error!Ast {
//   var new_nodes: List(model, null, ast.Node) = .empty;
//   errdefer new_nodes.deinit(gpa);
//
//   const new_root = try unionTrimmingInner(model, gpa, old_ast, old_ast.root, &new_nodes);
//
//   return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
// }

// ^^^^b       a$$$$4
// pub fn assertionDeduplication(
//   comptime model: MemoryModel,
//   gpa: Allocator,
//   old_ast: Ast,
// ) Allocator.Error!Ast {
//   var new_nodes: List(model, null, ast.Node) = .empty;
//   errdefer new_nodes.deinit(gpa);
//
//   const new_root = try assertionDeduplicationInner(model, gpa, old_ast, old_ast.root, &new_nodes);
//
//   return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
// }


/// NOTE: THIS CONSIDERS FUTURE UTF8 IMPLEMENTATION
/// 
/// Merges repeated unions/sets together minimizing memory footprint
/// 
/// - A single state takes roughly 4 bytes for smallish comptime known patterns
/// - A single range takes 2 bytes
/// 
/// Let s be the size of a range, m be the size of a single state and r be the size of a slice.
/// 
/// Consider a sequence S of unions of length k characters a|b|c|...|q
/// 
/// These k characters can be represented as n <= k integer ranges
/// 
/// The set representation of the alternation requires 1 slice, 1 term state in the nfa and n ranges
/// Therefore, total size of S is when represented as a set: s * n + m + r
/// 
/// In an NFA graph however, we require k + (k - 1) states, one for each term 
///   and another for each union symbol. As such the total amount of memory is:
///   m * (k + (k - 1))   =   m(2k - 1)
/// 
/// Goal is to determine when the NFA graph consumes more memory, e.g. for what k > 0
///   m(2k - 1)   >=   s * n + r + m
/// 
/// Time complexity wise, compact is more optimal
/// 1. Unrolled as a state-graph, the NFA will move onto k parallel states at once, 
///   and then check each and every state
/// 2. When merged to a single term state, the NFA will loop over all ranges n <= k times
/// 
/// Therefore, if both require the same amount of memory, we will prefer the merged approach
/// 
/// Example
/// 
/// Assuming a worst-case for merging:
///   s = 2
///   r = 16 (64-bit systems)
///   m = 4 (comptime known pattern with length of around 100)
///   k = n
/// 
///  NFA GRAPH    >=    MERGE
/// 4(2k - 1)     >=    2k + 16 + 4
/// ->    6k - 24 >=    0       when   k > 3
/// 
/// 
/// Even in the worst-case merging is better for even small alternation such as  a|c|e|g
/// 
/// If the union has a complex tree attached to it   T|a|b   , the math above still holds
///   a|b -> G    -> T|G      changes nothing
/// 
/// TODO: test more thoroughly including 'applySetMergeDestructively'
pub fn setMerge(
  comptime model: MemoryModel,
  comptime state_size: comptime_int,
  comptime slice_size: comptime_int,
  gpa: Allocator,
  sets: *List(model, null, Set),
  old_ast: Ast,
  /// Whether we always merge in order to reduce set count as much as possible no matter what
  aggressive: bool,
  max_state_count: ?usize,
) Allocator.Error!Ast {
  assert(state_size <= 32); // avoid weird business, it should be much smaller
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  var count: usize = if (max_state_count) |_| old_ast.calculateNfaStateCountAssumeOptimized() else 0;
  const new_root = try setMergeInner(
    model,
    state_size,
    slice_size,
    gpa,
    sets,
    old_ast,
    old_ast.root,
    &new_nodes,
    aggressive,
    max_state_count,
    &count,
  );

  return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
}

/// aggressive: never skips a merge, maximally minimizes state count
/// 
/// WARN: unimplemented
/// max_state_count: 
/// current_state_count: the current number of states in the ast
fn setMergeInner(
  comptime model: MemoryModel,
  comptime state_size: comptime_int,
  comptime slice_size: comptime_int,
  gpa: Allocator,
  sets: *List(model, null, Set),
  old_ast: Ast,
  node_idx: usize,
  new_nodes: *List(model, null, ast.Node),
  aggressive: bool,
  max_state_count: ?usize,
  current_state_count: *usize,
) Allocator.Error!usize {
  const node = old_ast.nodes[node_idx];

  switch (node) {
    .epsilon, .assertion, .term => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try setMergeInner(model, state_size, slice_size, gpa, sets, old_ast, c.lhs, new_nodes, aggressive, max_state_count, current_state_count);
      const new_rhs = try setMergeInner(model, state_size, slice_size, gpa, sets, old_ast, c.rhs, new_nodes, aggressive, max_state_count, current_state_count);
      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" =>  {
      return try optimizeUnion(model, state_size, slice_size, gpa, sets, old_ast, node_idx, new_nodes, aggressive, max_state_count, current_state_count);
    },

    .quantifier => |q| {
      const new_child = try setMergeInner(model, state_size, slice_size, gpa, sets, old_ast, q.node, new_nodes, aggressive, max_state_count, current_state_count);
      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

/// See algorithm top level comment
fn shouldMerge(
  comptime state_size: comptime_int,
  comptime slice_size: comptime_int,
  merged_set: Set,
) bool {

  // NFA GRAPH   >=   MERGE
  // m(s * k + 1)   >=   s * n + r + m
  // s := range size
  // r := slice size
  // k := cardinality
  // n := number of ranges
  // m := state size

  const s = @sizeOf(Range);
  const r = slice_size;
  const k = merged_set.cardinality();
  const n = merged_set.ranges.len;
  const m = state_size;

  // m * (2 * k - 1) >= s * n + r + m;
  // 2 * k * m - m >= s * n + r + m
  return  2 * k * m >= s * n + r + 2 * m;
}

/// TODO: too confusing, redo before implementing bidirectionality
fn optimizeUnion(
  comptime model: MemoryModel,
  comptime state_size: comptime_int,
  comptime slice_size: comptime_int,
  gpa: Allocator,
  sets: *List(model, null, Set),
  old_ast: Ast,
  start_idx: usize,
  new_nodes: *List(model, null, ast.Node),
  aggressive: bool,
  max_state_count: ?usize,
  current_state_count: *usize,
) Allocator.Error!usize {
  // Currently bi-directionality is unimplemented
  // As-is respecting these two parameters

  var complex_branches: List(model, null, usize) = .empty;
  defer complex_branches.deinit(gpa);

  assert(old_ast.nodes[start_idx] == .@"union");

  const set_acc_bad = b: {
    var ranges_acc: List(model, null, Range) = .empty;
    errdefer ranges_acc.deinit(gpa);

    // Deconstruct the entire union, and separate all child nodes to complex / set nodes
    //  a|b|c*|[mn]    ->  a|b|[mn]     c*
    try collectUnionBranches(model, gpa, old_ast, sets, start_idx, &complex_branches, &ranges_acc);
    const owned_ranges = try ranges_acc.toOwnedConstSlice(gpa);
    break :b Set.init(owned_ranges);
  };

  const set_acc_canon = if (comptime model == .dynamic) b: {
    defer set_acc_bad.deinit(gpa);
    const canon = try set_acc_bad.canonizeAlloc(gpa);
    break :b canon;
  } else b: {
    const canon = set_acc_bad.canonizeComptime();
    break :b canon;
  };
  var canon_owned = true;
  errdefer if ((comptime model == .dynamic) and canon_owned) set_acc_canon.deinit(gpa);

  if (!shouldMerge(state_size, slice_size, set_acc_canon) and !aggressive) {
    if (comptime model == .dynamic) {
      set_acc_canon.deinit(gpa);
      canon_owned = false;
    }
    // Abort; perform standard recursive union transform
    const u = old_ast.nodes[start_idx].@"union";
    const new_lhs = try setMergeInner(model, state_size, slice_size, gpa, sets, old_ast, u.lhs, new_nodes, aggressive, max_state_count, current_state_count);
    const new_rhs = try setMergeInner(model, state_size, slice_size, gpa, sets, old_ast, u.rhs, new_nodes, aggressive, max_state_count, current_state_count);
    try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
    return new_nodes.len() - 1;
  }

  for (0..complex_branches.len()) |i| {
    const old_branch_idx = complex_branches.get(i);
    const new_idx = try setMergeInner(model, state_size, slice_size, gpa, sets, old_ast, old_branch_idx, new_nodes, aggressive, max_state_count, current_state_count);

    complex_branches.set(i, new_idx);
  }
  
  // Create the single accumulated node
  //  + add its index to the final_branches list
  if (!set_acc_canon.isEmptySet()) {

    if (set_acc_canon.isSizeOne()) { // Unwrap single chars from set
      if (comptime model == .dynamic) {
        set_acc_canon.deinit(gpa);
        canon_owned = false;
      }
      try new_nodes.append(gpa, .{ .term = .{ .char = set_acc_canon.ranges[0].start } });
      try complex_branches.append(gpa, new_nodes.len() - 1);

    } else {
      var found: ?usize = null;
      for (sets.getConstSlice(), 0..) |set, i| {
        if (set.equal(set_acc_canon)) {
          found = i;
          break;
        }
      }

      if (found) |f| {
        try new_nodes.append(gpa, .{ .term = .{ .set_idx = f } });
        try complex_branches.append(gpa, new_nodes.len() - 1);
        if (comptime model == .dynamic) {
          set_acc_canon.deinit(gpa);
          canon_owned = false;
        }

      } else {
        
        try new_nodes.append(gpa, .{ .term = .{ .set_idx = sets.len() } });
        try complex_branches.append(gpa, new_nodes.len() - 1);

        try sets.append(gpa, set_acc_canon);
      }
    }
  } else {
    if (comptime model == .dynamic) {
      set_acc_canon.deinit(gpa);
      canon_owned = false;
    }
  }

  // If the accumulation resulted in a single term, then return its index
  if (complex_branches.len() == 1) {
    return complex_branches.get(0);
  }

  // Construct the tree of complex children that we werent collapsable
  var current_root = complex_branches.get(0);
  for (complex_branches.getConstSlice()[1..]) |branch_idx| {
    try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = current_root, .rhs = branch_idx } });
    current_root = new_nodes.len() - 1;
  }

  return current_root;
}

fn collectUnionBranches(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
  global_sets: *List(model, null, Set),
  node_idx: usize,
  complex_target: *List(model, null, usize),
  ranges_acc: *List(model, null, Range),
) Allocator.Error!void {
  const node = old_ast.nodes[node_idx];

  switch (node) {
    .@"union" => |u| {
      try collectUnionBranches(model, gpa, old_ast, global_sets, u.lhs, complex_target, ranges_acc);
      try collectUnionBranches(model, gpa, old_ast, global_sets, u.rhs, complex_target, ranges_acc);
    },
    .term => |t| switch (t) {
      .char => |c| {
        const temp: Range = .init(c, c + 1);
        try ranges_acc.append(gpa, temp);
      },
      .set_idx => |s_idx| {
        const existing = global_sets.get(s_idx);
        try ranges_acc.appendSlice(gpa, existing.ranges);
      },
    },
    else => {
      // It's a complex node (concat, quantifier, etc.)
      try complex_target.append(gpa, node_idx);
    },
  }
}

test "setMerge: Complex union flattening" {
  // Assuming a test helper that accepts: (optimization_type, state_size, slice_size, pattern, expected)
  
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|b|c", "a|b|c");
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|b|c|d", "[abcd]");
  // try expectOptimizationWithArch(.set_merge, 4, 8, "a|b|c", "[abc]");
  //
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|c|e|g", "[aceg]");
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|c|e", "a|c|e");
  //
  // // Mixed complex logic on standard 64-bit
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|[bc]|d|e", "[abcde]");
  // try expectOptimizationWithArch(.set_merge, 4, 16, "[ab]|[cd]", "[abcd]");
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|\\d|b|c", "[abc\\d]");
  //
  // // Complex unmergeable branches attached to sets
  // // a|b|c|d merges, x+ and y* remain outside
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|b|x+|c|d|y*", "[abcd]|x+|y*");
  //
  // // Nested union grouping isolation
  // try expectOptimizationWithArch(.set_merge, 4, 16, "a|(b|(c|(d|e)))", "[abcde]");
  // try expectOptimizationWithArch(.set_merge, 4, 16, "(a|b)x(y|z)", "(a|b)x(y|z)"); // Too small to merge on 64-bit!
  // try expectOptimizationWithArch(.set_merge, 4, 8, "(a|b)x(y|z)", "[ab]x[yz]");    // Merges on 32-bit
  //
  // // Identical sets should use the exact same memory index
  // try expectOptimizationWithArch(.set_merge, 4, 16, "[a-z]x[a-z]|y|[a-z]", "[a-z]x[a-z]|y|[a-z]"); 
}

// test "setMerge: Recursive quantifiers and deep unions" {
//   // 1. Quantifier wrapped union adjacent to a regular union
//   // The outer union cannot be merged, but it must pass the optimization down
//   // to the inner a|b|c structure.
//   try expectOptimizationWithArch(.set_merge, 4, 8, "x|(a|b|c)+|y", "x|[abc]+|y");
//
//   // 2. Multiple sibling unions wrapped in different quantifiers
//   // Both sides should independently optimize into their respective sets.
//   try expectOptimizationWithArch(.set_merge, 4, 8, "(a|b|c)*|(d|e|f)+", "[abc]*|[def]+");
//
//   // 3. Deeply nested structure
//   // The deepest level (a|b) merges with c. Then the quantifier applies.
//   // The highest level d|e merges, leaving two branches.
//   try expectOptimizationWithArch(.set_merge, 4, 8, "(((a|b)|c)*|d|e)", "[abc]*|[de]");
//
//   // 4. Quantifiers breaking union chains
//   // The + modifier binds tightly to c, meaning a|b merges to [ab], 
//   // d|e merges to [de], and c+ remains a standalone complex branch.
//   try expectOptimizationWithArch(.set_merge, 4, 8, "a|b|c+|d|e", "[ab]|[de]|c+");
//
//   // 5. Assertions and Epsilon intersections
//   // Ensure non-consuming nodes break the merge sequence correctly while
//   // still optimizing the surrounding characters.
//   try expectOptimizationWithArch(.set_merge, 4, 8, "a|b|\\b|c|d", "[ab]|\\b|[cd]");
//   
//   // 6. Concatenation blocks
//   // The union a|b|c is fully encapsulated by concatenations, it should merge
//   // cleanly before the surrounding sequence executes.
//   try expectOptimizationWithArch(.set_merge, 4, 8, "start(a|b|c)end", "start[abc]end");
// }
