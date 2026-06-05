const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const ast = pzre.ast;
const Ast = ast.Ast;

const debug = pzre.lens.debug;
const strategy = pzre.compile.strategy;
const regex = pzre.regex;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const List = polymorphic_memory.presets.single_ended.Create;

const misc = pzre.misc;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const Range = ascii.Range;

const testing = std.testing;
const expectDeeplyEqual = pzre.lens.testing.expectDeeplyEqual;
const expect = std.testing.expect;
const expectEqual = std.testing.expectEqual;

const compile = pzre.compile;

/// Optimization helper
fn swapAst(comptime model: MemoryModel, gpa: Allocator, curr: *Ast, next: Ast) void {
  if (model == .dynamic) curr.deinit(gpa);
  curr.* = next;
}

pub const Optimization = enum {
  normalize_quantifiers,
  prune_start_anchors,
  prune_end_anchors,
  epsilon_pruning,
  set_merge,
  minimize_jumps,
};

  /// Performs semantic preserving optimizations on the ast
  /// 
  /// Due to the MemoryModel abstraction, both setes and the ast are misleadingly passed as mutable
  ///   however they are mutated, destroyed or returned as-is depending on what optimizations are active
  /// Accessing either (original sets or ast) post-call is illegal
  /// 
  /// The caller cannot cover this call with an ast-sets destruction errdefer
  /// This call will deinit both if it fails
  ///
  /// Returns new sets and ast
pub fn optimizeDestructively(
  comptime model: MemoryModel,
  gpa: Allocator,
  active_opts: std.EnumSet(Optimization),
  sets: []const Set,
  old_ast: Ast,
) Allocator.Error!struct {[]const Set, Ast} {
  if (active_opts.eql(.initEmpty())) return .{sets, old_ast};

  var curr = old_ast;
  errdefer if (model == .dynamic) curr.deinit(gpa);

  var active_sets: []const Set = sets;
  var setslist: List(model, null, Set) = undefined;
  var list_owns_sets = false;

  errdefer {
    if (list_owns_sets) {
      misc.destroySetslist(model, gpa, &setslist);
    } else {
      misc.destroySets(gpa, active_sets);
    }
  }

  // Phase 1: Pure topological transformations
  if (active_opts.contains(.normalize_quantifiers)) {
    swapAst(model, gpa, &curr, try unrollQuantifiers(model, gpa, curr));
    swapAst(model, gpa, &curr, try normalizeQuantifiers(model, gpa, curr));
  }

  if (active_opts.contains(.epsilon_pruning)) 
    swapAst(model, gpa, &curr, try epsilonPruning(model, gpa, curr));

  if (active_opts.contains(.minimize_jumps)) {
    swapAst(model, gpa, &curr, try minimizeJumps(model, gpa, curr));
  }

  // Phase 2: Set integration and merging
  if (active_opts.contains(.set_merge)) {
    setslist = .initUsing(active_sets);
    list_owns_sets = true;

    swapAst(model, gpa, &curr, try setMerge(model, gpa, &setslist, curr));

    active_sets = if (model == .dynamic) try setslist.toOwnedConstSlice(gpa)
      else setslist.toOwnedConstSlice(undefined) catch unreachable;
    list_owns_sets = false;
  }

  // Phase 3: Final compaction
  const compact_sets, const final_ast = try curr.compactSets(model, gpa, active_sets);
  misc.destroySets(gpa, active_sets);

  return .{ compact_sets, final_ast };
}

/// Routing already contains anchoring information
/// Removes anchors from the AST as long as all paths go through such anchors
pub fn routingLowering(
  old_ast: Ast,
  comptime model: MemoryModel,
  gpa: Allocator,
  active_opts: std.EnumSet(Optimization),
  routing: ast.Routing,
) Allocator.Error!Ast {
  var curr = old_ast;
  errdefer if (model == .dynamic) curr.deinit(gpa);

  const prune_start = switch (routing) {
    .exact_match, .prefix_match => true,
    else => false,
  } and active_opts.contains(.prune_start_anchors);

  const prune_end = switch (routing) {
    .exact_match, .suffix_match => true,
    else => false
  } and active_opts.contains(.prune_end_anchors);
 
  if (prune_start) swapAst(model, gpa, &curr, try pruneStartAnchors(model, gpa, curr));
  if (prune_end) swapAst(model, gpa, &curr, try pruneEndAnchors(model, gpa, curr));

  // If anything was pruned, sweep the resulting epsilons
  if (prune_start or prune_end) {
    swapAst(model, gpa, &curr, try epsilonPruning(model, gpa, curr));
  }

  return curr;
}

/// Compiles both strings to NFAs and verifies their state topologies are identical.
pub fn expectOptimization(
  comptime opt: Optimization,
  pattern: []const u8,
  expected: []const u8,
  test_strings: []const []const u8,
) !void {
  const testing_gpa = std.testing.allocator;

  const dummy = struct {
    fn f(gpa: Allocator, pat: []const u8, exp: []const u8, ts: []const []const u8) !void {

      // We need to pick a strategy that properly allows the optimizations to happen
      // - We need to guarantee that both are compiled with the same strategy (cant let be null)
      // - Strategies encode execution information. anchor assertions cannot be deleted if that information 
      //    cannot be encoded within the strategy
      const strat: compile.strategy.Name = switch (opt) {
        .prune_start_anchors => .start_anchor_pass,
        .prune_end_anchors => .end_anchor_reverse_pass,
        else => .start_set_pass,
      };
      
      // We need to fix the architecture and the solution strategy
      // Compile Expected (Unoptimized)
      const expected_config = comptime compile.Config{
        .ast_optimizations = .initEmpty(),
        .strategy = strat,
      };
      const arch: pzre.Arch = .{ .minimal_nfa = .{.context = .{ .dynamic = .u16 }, .offset_bp = .i16} };
     
      var expected_regex = try regex.compile(arch, expected_config, gpa, exp);
      defer expected_regex.deinit(gpa);

      // Compile Pattern (Optimized)

      const actual_config = comptime compile.Config{
        .ast_optimizations = std.EnumSet(Optimization).initOne(opt),
        .strategy = strat,
      };
     
      var actual_regex = try regex.compile(arch, actual_config, gpa, pat);
      defer actual_regex.deinit(gpa);

      // debug.prettyPrint(.{
      //   .optimize_expected = expected_regex,
      //   .optimize_actual = actual_regex,
      // });

      // Equal sets, same state count
      try expect(misc.eqlSets(expected_regex.internals.sets, actual_regex.internals.sets));
      try expectEqual(expected_regex.internals.states.len, actual_regex.internals.states.len);

      // Some simple semantic tests
      var ctx = try expected_regex.initContext(gpa);
      defer ctx.deinit(gpa);
      for (ts) |str| {
        const expected_match = expected_regex.match(&ctx, str);
        const pattern_match = actual_regex.match(&ctx, str);
        try expectDeeplyEqual(expected_match, pattern_match);
      }
    }
  };

  try std.testing.checkAllAllocationFailures(testing_gpa, dummy.f, .{ pattern, expected, test_strings });
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

test "Optimization: epsilonPruning" {
  // Not really testable currently in isolation
  // quantifier normalization has to occur first

  try expectOptimization(.epsilon_pruning, "()", "", &.{ "", "a" });
  try expectOptimization(.epsilon_pruning, "()a", "a", &.{ "a", "", "b", "aa" });
  try expectOptimization(.epsilon_pruning, "b()", "b", &.{ "b", "", "a", "bb" });
  try expectOptimization(.epsilon_pruning, "(){0,0}", "", &.{ "", "a" });
  try expectOptimization(.epsilon_pruning, "()()", "", &.{ "", "a" });

  try expectOptimization(.epsilon_pruning, "a|()", "a?", &.{ "a", "", "b" });
  try expectOptimization(.epsilon_pruning, "abc|()", "(abc)?", &.{ "abc", "", "a", "ab" });
  try expectOptimization(.epsilon_pruning, "()|a", "a?", &.{ "a", "", "b" });
  try expectOptimization(.epsilon_pruning, "()|()", "", &.{ "", "a" });

  try expectOptimization(.epsilon_pruning, "()*", "", &.{ "", "a" });
  try expectOptimization(.epsilon_pruning, "()+", "", &.{ "", "a" });
  try expectOptimization(.epsilon_pruning, "()?", "", &.{ "", "a" });
  try expectOptimization(.epsilon_pruning, "(){3,5}", "", &.{ "", "a" });

  try expectOptimization(.epsilon_pruning, "((()|())*)?", "", &.{ "", "a" });
  try expectOptimization(.epsilon_pruning, "(a|())()", "a?", &.{ "a", "", "b" });
}

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

test "Optimization: unrollQuantifiers" {
  try expectOptimization(.normalize_quantifiers, "a{0,}", "a*", &.{ "", "a", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{1,}", "a+", &.{ "a", "aaaa", "", "b" });
  try expectOptimization(.normalize_quantifiers, "a{0,1}", "a?", &.{ "", "a", "aa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{1,1}", "a", &.{ "a", "", "aa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{0,0}", "()", &.{ "", "a", "b" });
  try expectOptimization(.normalize_quantifiers, "a{3,3}", "aaa", &.{ "aaa", "aa", "aaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{0,2}", "a?a?", &.{ "", "a", "aa", "aaa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{0,4}", "a?a?a?a?", &.{ "", "a", "aa", "aaaa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{1,3}", "aa?a?", &.{ "a", "aa", "aaa", "", "aaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "a{2,3}", "aaa?", &.{ "aa", "aaa", "a", "aaaa", "" });
  try expectOptimization(.normalize_quantifiers, "a{2,5}", "aaa?a?a?", &.{ "aa", "aaaa", "aaaaa", "a", "aaaaaa" });
  try expectOptimization(.normalize_quantifiers, "a{2,4}", "aaa?a?", &.{ "aa", "aaaa", "a", "aaaaa" });
  try expectOptimization(.normalize_quantifiers, "a{2,}", "aa+", &.{ "aa", "aaaa", "a", "" });
  try expectOptimization(.normalize_quantifiers, "a{5,}", "aaaaa+", &.{ "aaaaa", "aaaaaa", "aaaa", "" });

  try expectOptimization(
    .normalize_quantifiers, 
    "a{2,5}{0,3}", 
    "(aaa?a?a?)?(aaa?a?a?)?(aaa?a?a?)?", 
    &.{ "", "aa", "aaaaa", "aaaaaaaaaaaaaaa", "a", "aaaaaaaaaaaaaaaa" }
  );
}

/// Removes all repeated quantifiers:
///   a?? -> a?
///   a+* -> a*
///   a??*+??+++???******* -> a*
/// 
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

test "Optimization: normalizeQuantifiers" {
  try expectOptimization(.normalize_quantifiers, "(a?)?", "a?", &.{ "", "a", "aa", "b" });
  try expectOptimization(.normalize_quantifiers, "(a?)+", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "(a?)*", "a*", &.{ "", "a", "aa", "aaaaa", "b" });

  try expectOptimization(.normalize_quantifiers, "(a+)?", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "(a+)+", "a+", &.{ "a", "aa", "aaaaa", "", "b" });
  try expectOptimization(.normalize_quantifiers, "(a+)*", "a*", &.{ "", "a", "aa", "aaaaa", "b" });

  try expectOptimization(.normalize_quantifiers, "(a*)?", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "(a*)+", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "(a*)*", "a*", &.{ "", "a", "aa", "aaaaa", "b" });

  try expectOptimization(.normalize_quantifiers, "((a?)+)+", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "((a+)+)+", "a+", &.{ "a", "aa", "aaaaa", "", "b" });
  try expectOptimization(.normalize_quantifiers, "(((a*)?)+)*", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  
  try expectOptimization(.normalize_quantifiers, "a?b*", "a?b*", &.{ "", "a", "b", "ab", "abbb", "aa" });
  try expectOptimization(.normalize_quantifiers, "(a+b+)+", "(a+b+)+", &.{ "ab", "abab", "aabb", "a", "b", "" });

  try expectOptimization(.normalize_quantifiers, "a??????", "a?", &.{ "", "a", "aa", "b" });
  try expectOptimization(.normalize_quantifiers, "a++???+*******+++???", "a*", &.{ "", "a", "aa", "aaaaa", "b" });
  try expectOptimization(.normalize_quantifiers, "a+++", "a+", &.{ "a", "aa", "aaaaa", "", "b" });
}

/// NOTE: THIS CONSIDERS FUTURE UTF8 IMPLEMENTATION
/// 
/// Merges repeated unions/sets together minimizing memory footprint
/// 
/// 
/// TODO: test more thoroughly including 'applySetMergeDestructively'
pub fn setMerge(
  comptime model: MemoryModel,
  gpa: Allocator,
  sets: *List(model, null, Set),
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try setMergeInner(
    model,
    gpa,
    sets,
    old_ast,
    old_ast.root,
    &new_nodes,
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
  gpa: Allocator,
  sets: *List(model, null, Set),
  old_ast: Ast,
  node_idx: usize,
  new_nodes: *List(model, null, ast.Node),
) Allocator.Error!usize {
  const node = old_ast.nodes[node_idx];
  switch (node) {
    .epsilon, .assertion, .term => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try setMergeInner(model, gpa, sets, old_ast, c.lhs, new_nodes);
      const new_rhs = try setMergeInner(model, gpa, sets, old_ast, c.rhs, new_nodes);
      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" => {
      return try optimizeUnion(model, gpa, sets, old_ast, node_idx, new_nodes);
    },
    .quantifier => |q| {
      const new_child = try setMergeInner(model, gpa, sets, old_ast, q.node, new_nodes);
      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

/// Simpler merge not requiring any knowledge over architecture
fn shouldMerge(merged_set: Set) bool {
  const k = merged_set.cardinality();
  const n = merged_set.ranges.len;

  // 1. If merging naturally collapsed adjacent characters into a smaller number of ranges 
  //    (e.g., a|b|c -> [a-c], so k=3, n=1), it is always worth it.
  if (n < k) return true;

  // 2. If the alternation is wide enough, the execution overhead of NFA branching 
  //    outweighs the memory overhead of the Set slice. 
  //    3 is a standard industry threshold.
  if (k >= 3) return true;

  return false;
}

fn optimizeUnion(
  comptime model: MemoryModel,
  gpa: Allocator,
  sets: *List(model, null, Set),
  old_ast: Ast,
  start_idx: usize,
  new_nodes: *List(model, null, ast.Node),
) Allocator.Error!usize {
  var complex_branches: List(model, null, usize) = .empty;
  defer complex_branches.deinit(gpa);

  assert(old_ast.nodes[start_idx] == .@"union");

  const set_acc_bad = b: {
    var ranges_acc: List(model, null, Range) = .empty;
    errdefer ranges_acc.deinit(gpa);

    try collectUnionBranches(model, gpa, old_ast, sets, start_idx, &complex_branches, &ranges_acc);
    
    const owned_ranges = try ranges_acc.toOwnedConstSlice(gpa);
    break :b Set.init(owned_ranges);
  };

  const set_acc_canon = if (comptime model == .dynamic) b: {
    defer set_acc_bad.deinit(gpa);
    break :b try set_acc_bad.canonizeAlloc(gpa);
  } else b: {
    break :b set_acc_bad.canonizeComptime();
  };
  
  var canon_owned = true;
  errdefer if ((comptime model == .dynamic) and canon_owned) set_acc_canon.deinit(gpa);
  
  if (!shouldMerge(set_acc_canon)) {
    if (comptime model == .dynamic) {
      set_acc_canon.deinit(gpa);
      canon_owned = false;
    }
    
    // Abort; perform standard recursive union transform
    const u = old_ast.nodes[start_idx].@"union";
    const new_lhs = try setMergeInner(model, gpa, sets, old_ast, u.lhs, new_nodes);
    const new_rhs = try setMergeInner(model, gpa, sets, old_ast, u.rhs, new_nodes);
    try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
    return new_nodes.len() - 1;
  }

  for (0..complex_branches.len()) |i| {
    const old_branch_idx = complex_branches.get(i);
    const new_idx = try setMergeInner(model, gpa, sets, old_ast, old_branch_idx, new_nodes);
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
  try expectOptimization(.set_merge, "a|b|c", "[abc]", &.{ "a", "b", "c", "d" });
  try expectOptimization(.set_merge, "a|b|c|d", "[abcd]", &.{ "a", "d", "x" });
  try expectOptimization(.set_merge, "a|[bc]|d|e", "[abcde]", &.{ "a", "c", "e", "f" });
  try expectOptimization(.set_merge, "[ab]|[cd]", "[abcd]", &.{ "a", "d", "x" });
  try expectOptimization(.set_merge, "a|\\d|b|c", "[abc\\d]", &.{ "a", "c", "5", "z" });
  
  // Complex unmergeable branches attached to sets
  try expectOptimization(
    .set_merge,
    "a|b|x+|c|d|y*",
    "[abcd]|x+|y*",
    &.{ "a", "d", "xx", "", "yyyy", "z" }
  );

  // Nested union grouping isolation
  try expectOptimization(.set_merge, "a|(b|(c|(d|e)))", "[abcde]", &.{ "a", "c", "e", "x" });

  // Adjacent character merging
  try expectOptimization(.set_merge, "(a|b)x(y|z)", "[ab]x[yz]", &.{ "axy", "bxz", "cxw" });

  // Identical sets should cleanly collapse without memory bloat
  try expectOptimization(
    .set_merge,
    "[a-z]x[a-z]|y|[a-z]",
    "[a-z]x[a-z]|[a-z]",
    &.{ "axa", "y", "z", "A", "xx" }
  );
}

test "setMerge: Recursive quantifiers and deep unions" {
  // Quantifier wrapped union adjacent to a regular union
  try expectOptimization(.set_merge, "x|(a|b|c)+|y", "[abc]+|[xy]", &.{ "x", "y", "abc", "z" });

  // Multiple sibling unions wrapped in different quantifiers
  try expectOptimization(
    .set_merge, 
    "(a|b|c)*|(d|e|f)+", 
    "[abc]*|[def]+", 
    &.{ "", "abc", "def", "x" }
  );

  // Deeply nested structure
  try expectOptimization(.set_merge, "(((a|b)|c)*|d|e)", "[abc]*|[de]", &.{ "", "abc", "d", "e", "x" });

  // Quantifiers breaking union chains
  try expectOptimization(.set_merge, "a|b|c+|d|e", "c+|[abde]", &.{ "a", "ccc", "e", "x" });

  // Assertions and Epsilon intersections
  try expectOptimization(.set_merge, "a|b|\\b|c|d", "[abcd]|\\b", &.{ "a", "b", "c" });
  
  // Concatenation blocks
  try expectOptimization(.set_merge, "start(a|b|c)end", "start[abc]end", &.{ "startaend", "startxend" });
}

pub fn pruneStartAnchors(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try pruneStartAnchorsInner(model, gpa, old_ast, old_ast.root, &new_nodes);
  return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
}

pub fn pruneEndAnchors(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try pruneEndAnchorsInner(model, gpa, old_ast, old_ast.root, &new_nodes);
  return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
}

/// Whether the node always consumes strictly max=0 characters
fn isZeroWidth(old_ast: Ast, node_idx: usize) bool {
  switch (old_ast.nodes[node_idx]) {
    .term => return false,
    .epsilon, .assertion => return true,
    .concat => |c| return isZeroWidth(old_ast, c.lhs) and isZeroWidth(old_ast, c.rhs),
    .@"union" => |u| return isZeroWidth(old_ast, u.lhs) and isZeroWidth(old_ast, u.rhs),
    .quantifier => |q| return isZeroWidth(old_ast, q.node) or (q.repeat.max != null and q.repeat.max.? == 0),
  }
}

/// Prunes any start anchors that are always encountered before any input could have been consumed
/// Running this only makes sense if the search problem strategy has already been derived from the AST
/// Even if we determine that an anchor is always encountered and pick the .start_anchor_pass strategy,
///   we cannot delete all start anchor assertions. It would make a semantically invalid transformation:
///   \Aab\Ac -> abc
///   Instead, we can only delete redundant assertions 
/// 
/// Similar doc comments apply to pruneEndAnchors
/// 
pub fn pruneStartAnchorsInner(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
  node_idx: usize,
  new_nodes: *List(model, null, ast.Node),
) Allocator.Error!usize {
  const node = old_ast.nodes[node_idx];
  switch (node) {
    .term, .epsilon => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .assertion => |ass| {
      if (ass == .text_start) {
        try new_nodes.append(gpa, .epsilon);
      } else {
        try new_nodes.append(gpa, node);
      }
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try pruneStartAnchorsInner(model, gpa, old_ast, c.lhs, new_nodes);
      
      // Only traverse into RHS if LHS strictly consumes 0 characters
      const new_rhs = if (isZeroWidth(old_ast, c.lhs))
        try pruneStartAnchorsInner(model, gpa, old_ast, c.rhs, new_nodes)
      else
        try copyNode(model, gpa, old_ast, c.rhs, new_nodes); // Simple deep copy of untouched branch

      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" => |u| {
      // Both branches start at the same string index, so traverse both
      const new_lhs = try pruneStartAnchorsInner(model, gpa, old_ast, u.lhs, new_nodes);
      const new_rhs = try pruneStartAnchorsInner(model, gpa, old_ast, u.rhs, new_nodes);
      try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      // Only enter the quantifier if it doesn't loop, or if the loop body consumes nothing
      const safe_to_enter = (q.repeat.max != null and q.repeat.max.? <= 1) or isZeroWidth(old_ast, q.node);
      
      const new_child = if (safe_to_enter)
        try pruneStartAnchorsInner(model, gpa, old_ast, q.node, new_nodes)
      else
        try copyNode(model, gpa, old_ast, q.node, new_nodes);
        
      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

pub fn pruneEndAnchorsInner(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
  node_idx: usize,
  new_nodes: *List(model, null, ast.Node),
) Allocator.Error!usize {
  // We traverse the AST until text_end is encountered, we mark 

  const node = old_ast.nodes[node_idx];
  switch (node) {
    .term, .epsilon => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .assertion => |ass| {
      if (ass == .text_end) {
        try new_nodes.append(gpa, .epsilon);
      } else {
        try new_nodes.append(gpa, node);
      }
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      // consider c := a(\zb)
      // lhs := a            copied
      // rhs := \zb          recurse
      // 
      // consider c := \zb
      // lhs := \z           copied
      // rhs := b            recurse->copied
      //
      // End result is that the broken pattern is preserved

      const rhs_is_zero = isZeroWidth(old_ast, c.rhs);
      
      const new_lhs = if (rhs_is_zero)
        try pruneEndAnchorsInner(model, gpa, old_ast, c.lhs, new_nodes)
      else
        try copyNode(model, gpa, old_ast, c.lhs, new_nodes);

      const new_rhs = try pruneEndAnchorsInner(model, gpa, old_ast, c.rhs, new_nodes);

      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" => |u| {
      const new_lhs = try pruneEndAnchorsInner(model, gpa, old_ast, u.lhs, new_nodes);
      const new_rhs = try pruneEndAnchorsInner(model, gpa, old_ast, u.rhs, new_nodes);
      try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      const safe_to_enter = (q.repeat.max != null and q.repeat.max.? <= 1) or isZeroWidth(old_ast, q.node);
      
      const new_child = if (safe_to_enter)
        try pruneEndAnchorsInner(model, gpa, old_ast, q.node, new_nodes)
      else
        try copyNode(model, gpa, old_ast, q.node, new_nodes);
        
      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

fn copyNode(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
  node_idx: usize,
  new_nodes: *List(model, null, ast.Node),
) Allocator.Error!usize {
  const node = old_ast.nodes[node_idx];
  switch (node) {
    .term, .epsilon, .assertion => {
      try new_nodes.append(gpa, node);
      return new_nodes.len() - 1;
    },
    .concat => |c| {
      const new_lhs = try copyNode(model, gpa, old_ast, c.lhs, new_nodes);
      const new_rhs = try copyNode(model, gpa, old_ast, c.rhs, new_nodes);
      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .@"union" => |u| {
      const new_lhs = try copyNode(model, gpa, old_ast, u.lhs, new_nodes);
      const new_rhs = try copyNode(model, gpa, old_ast, u.rhs, new_nodes);
      try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      const new_child = try copyNode(model, gpa, old_ast, q.node, new_nodes);
      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
  }
}

test "Optimization: Anchor Pruning" {
  // --- Start Anchor Pruning ---
  // Perfectly safe, mathematically redundant start anchors
  try expectOptimization(.prune_start_anchors, "^a", "a", &.{ "a", "ba", "" });
  try expectOptimization(.prune_start_anchors, "^^a", "a", &.{ "a", "ba", "" });
  try expectOptimization(.prune_start_anchors, "(^a)", "a", &.{ "a", "ba", "" });
  try expectOptimization(.prune_start_anchors, "^a|^b", "a|b", &.{ "a", "b", "c" });
  try expectOptimization(.prune_start_anchors, "^(a|b)", "a|b", &.{ "a", "b", "c" });

  // Trapped start anchors (Impossible patterns, must NOT be pruned)
  try expectOptimization(.prune_start_anchors, "a^b", "a^b", &.{ "a", "ab", "a^b", "" });
  try expectOptimization(.prune_start_anchors, "a*^b", "a*^b", &.{ "a", "b", "ab", "" });
  try expectOptimization(.prune_start_anchors, "(a|)^b", "(a|)^b", &.{ "a", "b", "ab" });
  
  // Looping start anchors
  try expectOptimization(.prune_start_anchors, "(^)+", "()+", &.{ "a", "" }); // Safe
  try expectOptimization(.prune_start_anchors, "(^a)+", "(^a)+", &.{ "a", "aa", "" }); // Unsafe

  // --- End Anchor Pruning ---
  // Perfectly safe, mathematically redundant end anchors
  try expectOptimization(.prune_end_anchors, "a$", "a", &.{ "a", "ab", "" });
  try expectOptimization(.prune_end_anchors, "a$$", "a", &.{ "a", "ab", "" });
  try expectOptimization(.prune_end_anchors, "a$|b$", "a|b", &.{ "a", "b", "c" });
  try expectOptimization(.prune_end_anchors, "(a|b)$", "a|b", &.{ "a", "b", "c" });

  // Trapped end anchors (Impossible patterns, must NOT be pruned)
  try expectOptimization(.prune_end_anchors, "a$b", "a$b", &.{ "a", "b", "ab", "" });
  try expectOptimization(.prune_end_anchors, "a$b*", "a$b*", &.{ "a", "ab", "abb", "" });
  try expectOptimization(.prune_end_anchors, "a$(|b)", "a$(|b)", &.{ "a", "b", "ab" });
  
  // Looping end anchors
  try expectOptimization(.prune_end_anchors, "($)+", "()+", &.{ "a", "" }); // Safe
  try expectOptimization(.prune_end_anchors, "(a$)+", "(a$)+", &.{ "a", "aa", "" }); // Unsafe
}

/// Moves `+` quantified leaves in a union chain to the rightmost position 
/// to minimize topological jumping in the compiled NFA.
/// See fragment.zig unpatchable_fallthrough
/// A|B+|C -> A|C|B+
pub fn minimizeJumps(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast,
) Allocator.Error!Ast {
  var new_nodes: List(model, null, ast.Node) = .empty;
  errdefer new_nodes.deinit(gpa);

  const new_root = try minimizeJumpsInner(model, gpa, old_ast, old_ast.root, &new_nodes);

  return Ast.init(new_root, try new_nodes.toOwnedConstSlice(gpa));
}

fn minimizeJumpsInner(
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
      const new_lhs = try minimizeJumpsInner(model, gpa, old_ast, c.lhs, new_nodes);
      const new_rhs = try minimizeJumpsInner(model, gpa, old_ast, c.rhs, new_nodes);
      try new_nodes.append(gpa, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } });
      return new_nodes.len() - 1;
    },
    .quantifier => |q| {
      const new_child = try minimizeJumpsInner(model, gpa, old_ast, q.node, new_nodes);
      try new_nodes.append(gpa, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } });
      return new_nodes.len() - 1;
    },
    .@"union" => {
      // Collect all leaves of the union chain
      var leaves: List(model, null, usize) = .empty;
      defer leaves.deinit(gpa);
      try collectUnionLeaves(model, gpa, old_ast, node_idx, &leaves);

      // Optimize each leaf and collect into an append only list
      var opt_leaves: List(model, null, usize) = .empty;
      defer opt_leaves.deinit(gpa);
      
      const leaf_slice = leaves.getConstSlice();
      for (leaf_slice) |old_leaf| {
        const opt_leaf = try minimizeJumpsInner(model, gpa, old_ast, old_leaf, new_nodes);
        try opt_leaves.append(gpa, opt_leaf);
      }

      const opt_slice = opt_leaves.getConstSlice();

      // Find the first plus quantifier 
      var plus_idx: ?usize = null;
      for (opt_slice, 0..) |leaf, i| {
        const n = new_nodes.get(leaf);
        if (n == .quantifier) {
          const rep = n.quantifier.repeat;
          if (rep.min > 0 and rep.max == null) {
            plus_idx = i;
            break;
          }
        }
      }

      // Rebuild the union chain right associatively
      const base_idx = plus_idx orelse (opt_slice.len - 1);
      var current = opt_slice[base_idx];

      var i: usize = opt_slice.len;
      while (i > 0) {
        i -= 1;
        if (i == base_idx) continue;
        
        try new_nodes.append(gpa, .{ .@"union" = .{ .lhs = opt_slice[i], .rhs = current } });
        current = new_nodes.len() - 1;
      }

      return current;
    },
  }
}

fn collectUnionLeaves(
  comptime model: MemoryModel,
  gpa: Allocator,
  old_ast: Ast, 
  node_idx: usize, 
  leaves: *List(model, null, usize)
) Allocator.Error!void {
  if (old_ast.nodes[node_idx] == .@"union") {
    const u = old_ast.nodes[node_idx].@"union";
    try collectUnionLeaves(model, gpa, old_ast, u.lhs, leaves);
    try collectUnionLeaves(model, gpa, old_ast, u.rhs, leaves);
  } else {
    try leaves.append(gpa, node_idx);
  }
}

test "Optimization: minimizeJumps" {
  // --- 1. Basic Flat Chains ---
  // Standard single + leaf
  try expectOptimization(.minimize_jumps, "a|b+|c", "a|c|b+", &.{ "a", "b", "bb", "c" });
  
  // Moves the FIRST + leaf it finds (a+ moves to the end)
  try expectOptimization(.minimize_jumps, "a+|b+|c", "b+|c|a+", &.{ "a", "b", "c", "aa", "bb" });

  // Moves the + leaf even if it's already adjacent to the end
  try expectOptimization(.minimize_jumps, "a|b|c+", "a|b|c+", &.{ "a", "b", "c", "cc" });


  // --- 2. Deeply Nested within Concatenations ---
  // Wrapped in prefix and suffix
  try expectOptimization(.minimize_jumps, "start(a|b+|c)end", "start(a|c|b+)end", &.{ "startaend", "startbbend", "startcend" });
  
  // Multiple unions inside a concatenation sequence
  try expectOptimization(
    .minimize_jumps, 
    "(a|b+)(c+|d)", 
    "(a|b+)(d|c+)", 
    &.{ "ac", "ad", "bbc", "bbcc" }
  );


  // --- 3. Deeply Nested within Quantifiers ---
  // Plus leaf inside a star loop
  try expectOptimization(.minimize_jumps, "(a|b+|c)*", "(a|c|b+)*", &.{ "", "a", "bb", "c", "abbc" });
  
  // Plus leaf inside a plus loop
  try expectOptimization(.minimize_jumps, "(x|y+|z)+", "(x|z|y+)+", &.{ "x", "y", "z", "xxyyz" });
  
  // Plus leaf inside an exact repeater
  try expectOptimization(.minimize_jumps, "(a|b+|c){3,3}", "(a|c|b+){3,3}", &.{ "aaa", "bbb", "abc" });


  // --- 4. Complex Branches and Right-Associativity ---
  // Multi-character complex branches
  try expectOptimization(.minimize_jumps, "(ab)|(cd)+|(ef)", "(ab)|(ef)|(cd)+", &.{ "ab", "cdcd", "ef" });

  // Naturally right-associative AST flattening
  // The AST for a|(b|(c+|d)) parses deeply. The flattener must extract all leaves: a, b, c+, d
  try expectOptimization(.minimize_jumps, "a|(b|(c+|d))", "a|b|d|c+", &.{ "a", "b", "cc", "d" });


  // --- 5. Negative/Control Cases (Should NOT move) ---
  // Asterisks (*) are not targeted by this pass because they resolve fallthrough naturally
  try expectOptimization(.minimize_jumps, "a|b*|c", "a|b*|c", &.{ "a", "", "bb", "c" });

  // Optional (?) branches are not targeted
  try expectOptimization(.minimize_jumps, "a|b?|c", "a|b?|c", &.{ "a", "", "b", "c" });

  // Plus wrapped inside an Optional (the branch root is ?, not +)
  try expectOptimization(.minimize_jumps, "a|(b+)?|c", "a|(b+)?|c", &.{ "a", "", "bb", "c" });
}
