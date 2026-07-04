const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const MetaData = pzre.compile.parse_node.MetaData;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const List = polymorphic_memory.presets.single_ended.Create;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

const lens = pzre.lens;
const debug = lens.debug;

const misc = pzre.misc;
const Repeat = misc.Repeat;
const Assertion = misc.Assertion;

const dispatch = pzre.compile.dispatch;
const compile = pzre.compile;
const Features = compile.Features;

pub const optimize = @import("optimize.zig");
pub const Optimization = optimize.Optimization;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("optimize.zig");
  }
}

pub const Node = union(enum) {
  /// A special, explicit epsilon node.
  /// When the empty string is parsed, the result is a tree with a single epsilon node.
  /// Repeats a{0,0} are not treated as epsilons by the tree.
  /// This is because the purpose of the AST is to represent the grammar perfectly.
  /// For algorithms that use the ast for semantic analysis, treat both as epsilon.
  epsilon,
  assertion: Assertion,
  term: Term,
  concat: struct { lhs: usize, rhs: usize },
  @"union": struct { lhs: usize, rhs: usize },
  quantifier: struct { node: usize, repeat: Repeat },
  // capture: struct { id: usize, node: usize },

  pub const Term = union(enum) {
    set_idx: usize,
    char: u8,
  };
};

pub const Ast = struct {
  /// Owned array of nodes
  nodes: []const Node,
  root: usize,

  const Self = @This();

  pub fn init(root: usize, nodes: []const Node) Self {
    assert(root < nodes.len);
    return Self{.nodes = nodes, .root = root};
  }

  /// Generates a new AST representing the reversed regular language.
  /// The returned Ast must be deinited by the caller.
  pub fn reverse(self: Self, comptime model: MemoryModel, gpa: Allocator) Allocator.Error!Ast {

    var reversed_nodes: List(model, null, Node) = .empty;
    try reversed_nodes.ensureTotalCapacityPrecise(gpa, self.nodes.len);

    for (self.nodes) |node| {
      const new: Node = switch (node) {
        .epsilon => .{ .epsilon = {} },
        .term => |t| .{ .term = t },
        .assertion => |ass| .{ .assertion = invertAssertion(ass) },
        .concat => |data| .{ .concat = .{ .lhs = data.rhs, .rhs = data.lhs } },
        .@"union" => |data| .{ .@"union" = .{ .lhs = data.lhs, .rhs = data.rhs } },
        .quantifier => |data| .{ .quantifier = .{ .node = data.node, .repeat = data.repeat } },
      };
      reversed_nodes.append(gpa, new) catch unreachable;
    }


    const reversed =  Ast.init(self.root, try reversed_nodes.toOwnedConstSlice(gpa));
    assert(reversed.nodes.len == self.nodes.len);
    return reversed;
  }

  /// Calculates the most restrictive required character set for the branch.
  pub fn narrowestRequiredSet(self: Self, gpa: Allocator, node_idx: usize, sets: []const Set) Allocator.Error!Set {
    switch (self.nodes[node_idx]) {
      .epsilon, .assertion => return Set.universe.dupe(gpa),
      .term => |t| {
        return switch (t) {
          .char => |c| b: {
            const range = ascii.Range{ .start = c, .end = c + 1 };
            break :b Set.initDuped(gpa, &.{range});
          },
          .set_idx => |idx| sets[idx].dupe(gpa),
        };
      },
      .concat => |c| {
        var lhs = try self.narrowestRequiredSet(gpa, c.lhs, sets);
        errdefer lhs.deinit(gpa);
        const rhs = try self.narrowestRequiredSet(gpa, c.rhs, sets);
        
        if (lhs.cardinality() <= rhs.cardinality()) {
          rhs.deinit(gpa);
          return lhs;
        } else {
          lhs.deinit(gpa);
          return rhs;
        }
      },
      .@"union" => |u| {
        var lhs = try self.narrowestRequiredSet(gpa, u.lhs, sets);
        errdefer lhs.deinit(gpa);
        var rhs = try self.narrowestRequiredSet(gpa, u.rhs, sets);
        defer rhs.deinit(gpa);

        return Set.unionAlloc(gpa, lhs, rhs);
      },
      .quantifier => |q| {
        if (q.repeat.min == 0) return Set.universe.dupe(gpa);
        
        return self.narrowestRequiredSet(gpa, q.node, sets);
      },
    }
  }

  pub fn invertAssertion(ass: Assertion) Assertion {
    return switch (ass) {
      .line_start => .line_end,
      .line_end => .line_start,
      .text_start => .text_end,
      .text_end => .text_start,
      .word_boundary => .word_boundary,
      .not_word_boundary => .not_word_boundary,
    };
  }

  /// Deduces the mathematical boundary of the pattern for execution routing.
  /// The AST handles multilining logic internally. You can trust the text assertions
  pub fn determineRouting(self: Self) Routing {
    const start_assertions: []const Assertion = &.{ .text_start };

    const end_assertions: []const Assertion = &.{ .text_end };

    const is_start_anchored = self.isAssertionGuaranteed(start_assertions);
    const is_end_anchored = self.isAssertionGuaranteed(end_assertions);

    if (is_start_anchored and is_end_anchored) return .exact_match;
    if (is_start_anchored) return .prefix_match;
    if (is_end_anchored) return .suffix_match;
    
    return .unanchored_search;
  }

  /// Checks whether all paths through root to accept always encounter one of the given assertions
  pub fn isAssertionGuaranteed(self: Self, assertions: []const Assertion) bool {
    return isAssertionGuaranteedInner(self.nodes, self.root, assertions);
  }

  fn isAssertionGuaranteedInner(nodes: []const Node, node_idx: usize, assertions: []const Assertion) bool {
    const node = nodes[node_idx];
    
    return switch (node) {
      .epsilon, .term => false,
      .assertion => |ass| {
        for (assertions) |target| {
          if (target == ass) return true;
        } else return false;
      },
      .concat => |c| isAssertionGuaranteedInner(nodes, c.lhs, assertions) or isAssertionGuaranteedInner(nodes, c.rhs, assertions),
      .@"union" => |u| isAssertionGuaranteedInner(nodes, u.lhs, assertions) and isAssertionGuaranteedInner(nodes, u.rhs, assertions),
      .quantifier => |q| q.repeat.min > 0 and isAssertionGuaranteedInner(nodes, q.node, assertions),
    };
  }

  /// Creates a new compact sets from old_sets by removing all redundant entries
  /// new_sets.len <= old_sets.len
  pub fn compactSets(
    self: *Ast,
    comptime model: MemoryModel,
    gpa: Allocator,
    old_sets: []const Set,
  ) Allocator.Error!struct {[]const Set, Ast} {
    if (comptime model == .comptime_dynamic) {
      return self.compactSetsComptime(old_sets);
    } else {
      const new_sets = try self.compactSetsRuntime(gpa, old_sets);
      return .{ new_sets, self.* };
    }
  }

  fn compactSetsRuntime(
    self: *Ast,
    gpa: Allocator,
    old_sets: []const Set,
  ) Allocator.Error![]const Set {
    var newsetlist: List(.dynamic, null, Set) = .empty;
    errdefer if (!@inComptime()){
      for (newsetlist.getSlice()) |*set| {
        set.deinit(gpa);
      }
      newsetlist.deinit(gpa);
    };
    
    const mapping = try gpa.alloc(?usize, old_sets.len);
    defer gpa.free(mapping);
    @memset(mapping, null);

    const mutable_nodes = @constCast(self.nodes);
    try self.compactSetsRuntimeInner(mutable_nodes, self.root, old_sets, &newsetlist, mapping, gpa);
    
    const new_sets = try newsetlist.toOwnedConstSlice(gpa);
    assert(new_sets.len <= old_sets.len);

    return new_sets;
  }

  fn compactSetsRuntimeInner(
    self: *Ast,
    nodes: []Node,
    node_idx: usize,
    old_sets: []const Set,
    new_sets: *List(.dynamic, null, Set),
    mapping: []?usize,
    gpa: Allocator,
  ) Allocator.Error!void {
    switch (nodes[node_idx]) {
      .epsilon, .assertion => return,
      .term => |*t| switch (t.*) {
        .char => return,
        .set_idx => |old_idx| {
          if (mapping[old_idx]) |new_idx| {
            t.set_idx = new_idx;
            return;
          }

          const target_set = old_sets[old_idx];
          const duped = try target_set.dupe(gpa);
          errdefer duped.deinit(gpa);
          const new_idx = new_sets.len();
          
          try new_sets.append(gpa, duped);
          mapping[old_idx] = new_idx;
          t.set_idx = new_idx;
        },
      },
      .concat => |c| {
        try self.compactSetsRuntimeInner(nodes, c.lhs, old_sets, new_sets, mapping, gpa);
        try self.compactSetsRuntimeInner(nodes, c.rhs, old_sets, new_sets, mapping, gpa);
      },
      .@"union" => |u| {
        try self.compactSetsRuntimeInner(nodes, u.lhs, old_sets, new_sets, mapping, gpa);
        try self.compactSetsRuntimeInner(nodes, u.rhs, old_sets, new_sets, mapping, gpa);
      },
      .quantifier => |q| {
        try self.compactSetsRuntimeInner(nodes, q.node, old_sets, new_sets, mapping, gpa);
      },
    }
  }

  // ==========================================================================
  // Comptime (Allocates New Tree)
  // ==========================================================================

  fn compactSetsComptime(
    self: *Ast,
    old_sets: []const Set,
  ) struct {[]const Set, Ast} {
    var newsetlist: List(.comptime_dynamic, null, Set) = .empty;
    var new_nodes: List(.comptime_dynamic, null, Node) = .empty;
    
    var mapping_buf = [_]?usize{null} ** old_sets.len;
    const mapping = mapping_buf[0..];

    const new_root = self.compactSetsComptimeInner(self.root, old_sets, &newsetlist, mapping, &new_nodes);

    const new_sets = newsetlist.toOwnedConstSlice(undefined) catch unreachable;
    const final_ast = Ast.init(new_root, new_nodes.toOwnedConstSlice(undefined) catch unreachable);

    assert(new_sets.len <= old_sets.len);
    return .{ new_sets, final_ast };
  }

  fn compactSetsComptimeInner(
    self: *Ast,
    node_idx: usize,
    old_sets: []const Set,
    new_sets: *List(.comptime_dynamic, null, Set),
    mapping: []?usize,
    new_nodes: *List(.comptime_dynamic, null, Node),
  ) usize {
    const node = self.nodes[node_idx];
    switch (node) {
      .epsilon, .assertion => {
        new_nodes.append(undefined, node) catch unreachable;
        return new_nodes.len() - 1;
      },
      .term => |t| switch (t) {
        .char => {
          new_nodes.append(undefined, node) catch unreachable;
          return new_nodes.len() - 1;
        },
        .set_idx => |old_idx| {
          var new_idx: usize = undefined;
          
          if (mapping[old_idx]) |existing_idx| {
            new_idx = existing_idx;
          } else {
            const target_set = old_sets[old_idx];
            new_idx = new_sets.len();
            new_sets.append(undefined, target_set) catch unreachable;
            mapping[old_idx] = new_idx;
          }
          
          new_nodes.append(undefined, .{ .term = .{ .set_idx = new_idx } }) catch unreachable;
          return new_nodes.len() - 1;
        },
      },
      .concat => |c| {
        const new_lhs = self.compactSetsComptimeInner(c.lhs, old_sets, new_sets, mapping, new_nodes);
        const new_rhs = self.compactSetsComptimeInner(c.rhs, old_sets, new_sets, mapping, new_nodes);
        new_nodes.append(undefined, .{ .concat = .{ .lhs = new_lhs, .rhs = new_rhs } }) catch unreachable;
        return new_nodes.len() - 1;
      },
      .@"union" => |u| {
        const new_lhs = self.compactSetsComptimeInner(u.lhs, old_sets, new_sets, mapping, new_nodes);
        const new_rhs = self.compactSetsComptimeInner(u.rhs, old_sets, new_sets, mapping, new_nodes);
        new_nodes.append(undefined, .{ .@"union" = .{ .lhs = new_lhs, .rhs = new_rhs } }) catch unreachable;
        return new_nodes.len() - 1;
      },
      .quantifier => |q| {
        const new_child = self.compactSetsComptimeInner(q.node, old_sets, new_sets, mapping, new_nodes);
        new_nodes.append(undefined, .{ .quantifier = .{ .node = new_child, .repeat = q.repeat } }) catch unreachable;
        return new_nodes.len() - 1;
      },
    }
  }

  /// Asserts that simple optimizations have been performed
  /// These are the optimizations that do not depend on knowing what the final state size is
  /// These are mandatory, and always performed
  /// 1. quantifier unrolling
  /// 2. epsilon elimination
  ///
  /// Does not check for orphaned nodes
  pub fn assertPhaseOneOptimized(self: Self) void {
    // if (!@inComptime()) debug.prettyPrint(.{self});
    if (self.nodes.len == 1 and self.nodes[0] == .epsilon) return;

    for (self.nodes) |node| {
      switch (node) {
        .epsilon => unreachable,
        .quantifier => |q| {
          const r = q.repeat;
          assert(!r.is_epsilon());
          assert(r.is_optional() or r.is_plus() or r.is_star());
        },
        else => {},
      }
    }
  }

  /// Calculates the exact number of NFA states this AST will generate.
  /// Assumes the AST is fully compacted (no orphaned nodes), and that first-tier optimizations have been
  /// performed, that is, epsilons are not present, and quantifiers have been unrolled
  ///
  /// Currently, optimizations use COPY ON WRITE model that never leave unorphaned nodes.
  ///
  /// NOTE: This was incorrectly not accounting for .jump states. Currently unused. Maybe fixable?
  pub fn calculateNfaStateCountAssumeOptimized(self: Self) usize {
    self.assertPhaseOneOptimized();
    var state_count: usize = 0; // the topology does not include an accept state

    for (self.nodes) |node| {
      switch (node) {
        .concat, .epsilon => {},
        // If the tree is empty, then if has a single epsilon
        // As asserted
        .term, .assertion, .@"union", .quantifier => {
          state_count += 1;
        },
      }
    }
    return state_count;
  }

  //NOTE: This state calculation logic is horrible implementation
  // It has to be perfectly synced with parse_node.zig -> two sources of identical truth
  // also this is implementation specific (assumes minimal_nfa linker)
  // arch directories should own their AST state-calculation calculation logic
  // and they should instantiate a parse_node for the calculation, similar to how the AST instantiates a 
  // fragment for compilation
  // and the parse node, should simply call the architecture defined state counting logic

  /// Calculates the exact number of NFA states this AST will generate,
  /// Given optimization status, decies whether to use optimized fast-pass or slower calculation without 
  ///   assumptions
  pub fn calculateNfaStateCount(self: Self, optimization_status: std.EnumSet(Optimization)) error{Overflow}!usize {
    // if (optimization_status.contains(.epsilon_pruning) 
    //   and optimization_status.contains(.normalize_quantifiers)) {
    //
    //   return self.calculateNfaStateCountAssumeOptimized();
    // } else {
    //   const result = try self.calculateNfaStateCountInner(self.root);
    //   return if (result.is_epsilon) 0 else result.states_count; // the topology does not include accept
    // }
   
    _ = optimization_status;
    const result = try self.calculateNfaStateCountInner(self.root);
    return if (result.is_epsilon) 0 else result.states_count;
  }

  fn calculateNfaStateCountInner(self: Self, node_idx: usize) error{Overflow}!MetaData {
    const node = self.nodes[node_idx];
    switch (node) {
      .epsilon => return MetaData.initEpsilon(),
      .assertion, .term => return MetaData.initTerm(),
      .concat => |c| {
        const lhs = try self.calculateNfaStateCountInner(c.lhs);
        const rhs = try self.calculateNfaStateCountInner(c.rhs);
        return try lhs.concat(rhs);
      },
      .@"union" => |u| {
        const lhs = try self.calculateNfaStateCountInner(u.lhs);
        const rhs = try self.calculateNfaStateCountInner(u.rhs);
        return try lhs.@"union"(rhs);
      },
      .quantifier => |q| {
        const child = try self.calculateNfaStateCountInner(q.node);
        return try child.quantify(q.repeat);
      },
    }
  }

  /// Immutably builds the manifest
  /// Only fields as required by 'interests' are populated
  pub fn buildManifest(
    self: Self,
    comptime config: compile.Config,
    comptime interests: std.EnumSet(ManifestField),
    comptime model: MemoryModel,
    gpa: Allocator,
    routing: Routing,
    artifacts: *compile.Artifacts,
  ) error{Overflow, OutOfMemory}!Manifest {
    const restrictive_literal: []const u8 = "";
    if (interests.contains(.restrictive_literal)) {
      // restrictive literal logic TODO
    }

    const capture_group_segmentable: ?CaptureGroupSegmentation = null;
    if (interests.contains(.capture_group_segmentation)) {
      // capture group segmentation TODO
    }
   
    const nfa_states_count: ?usize = if (interests.contains(.nfa_states_count)) 
      try self.calculateNfaStateCount(config.ast_optimizations) else null;

    artifacts.reverse_ast = if (interests.contains(.rnfa_states_count)) try self.reverse(model, gpa) else null;

    const rnfa_states_count: ?usize = if (artifacts.reverse_ast) |rev|
      try rev.calculateNfaStateCount(config.ast_optimizations) else null;

    return .{
      .features = .{
        // TODO: implement capture group detection logic
        .capture_groups = if (config.features.capture_groups) false else false,
      },
      .nfa_states_count = nfa_states_count,
      .rnfa_states_count = rnfa_states_count,
      .routing = routing,
      .restrictive_literal = restrictive_literal,
      .capture_group_segmentation = capture_group_segmentable,
    };
  }

  pub fn dupe(self: Self, gpa: Allocator) Allocator.Error!Self {
    if (@inComptime()) {
      const nodes = self.nodes ++ &[_]Node{};
      return Self{.nodes = nodes, .root = self.root};
    } else {
      const nodes = try gpa.dupe(Node, self.nodes);
      return Self{.nodes = nodes, .root = self.root};
    }
  }

  pub fn deinit(self: *Ast, gpa: Allocator) void {
    if (@inComptime()) return;
    gpa.free(self.nodes);
  }
};

/// Recursively checks if two sub-trees are structurally identical
pub fn areNodesEqual(nodes: []const Node, a: usize, b: usize) bool {
  if (a == b) return true;
  
  const n_a = nodes[a];
  const n_b = nodes[b];
  const Tag = std.meta.Tag(Node);
  
  if (@as(Tag, n_a) != @as(Tag, n_b)) return false;

  return switch (n_a) {
    .epsilon => true,
    .assertion => |ass| ass == n_b.assertion,
    .term => |t| switch (t) {
      .char => |c| n_b.term == .char and c == n_b.term.char,
      .set_idx => |idx| n_b.term == .set_idx and idx == n_b.term.set_idx,
    },
    .concat => |c| areNodesEqual(nodes, c.lhs, n_b.concat.lhs) and areNodesEqual(nodes, c.rhs, n_b.concat.rhs),
    .@"union" => |u| 
      (areNodesEqual(nodes, u.lhs, n_b.@"union".lhs) and areNodesEqual(nodes, u.rhs, n_b.@"union".rhs)) or
      (areNodesEqual(nodes, u.lhs, n_b.@"union".rhs) and areNodesEqual(nodes, u.rhs, n_b.@"union".lhs)),
    .quantifier => |q| 
      q.repeat.min == n_b.quantifier.repeat.min and
      q.repeat.max == n_b.quantifier.repeat.max and
      areNodesEqual(nodes, q.node, n_b.quantifier.node),
  };
}

pub const ManifestField = enum { restrictive_literal, capture_group_segmentation, nfa_states_count, rnfa_states_count };

/// The exact mathematical, topological, and structural requirements extracted from the AST.
pub const Manifest = struct {
  /// This is not the original user defined features in configuration, instead 
  /// its the true features implied by the pattern
  features: Features,
  routing: Routing,
  nfa_states_count: ?usize = null,
  rnfa_states_count: ?usize = null,
  /// A heuristic analysis function determined that the pattern contains a fixed-length anchor that is
  /// good enough to power anchored search algorithms.
  ///
  /// Value is the fixed length slice that contains the restrictive literal
  ///   or empty for none found
  restrictive_literal: []const u8 = "",
  /// The pattern can be segmentized according to its capture groups, example:
  /// ^(\d+)\.(\d+)\.(\d+)(-([0-9A-Za-z.-]+))?(\+([0-9A-Za-z.-]+))?$
  /// Segmentizes into 9 machines
  /// 1. ^\d+
  /// 2. \.
  /// 3. \d+
  /// 4. \.
  /// 5. \d+
  /// 6. -
  /// 7. ([0-9A-Za-z.-]+)?
  /// 8. \+
  /// 9. ([0-9A-Za-z.-]+)?$
  ///
  /// capture groups can be extracted on a linear pass:
  /// 1, 3, 5, (6+7), 7, (8+9), 9
  ///
  /// Notice that segmentation can work when capture groups nest
  ///
  /// Null for no segmentation being available
  capture_group_segmentation: ?CaptureGroupSegmentation = null,
};

pub const CaptureGroupSegmentation = struct {
  /// The segmentation, e.g. 1-9
  /// Notice that the sum of these do not always match the pattern
  fragments: []const []const u8,
  /// The indices that form captures
  /// in the example above: 1, 3, 5, (6+7), 7, (8+9), 9
  capture_indices: []const []const usize,
};

/// How a pattern is anchored to the input.
pub const Routing = enum {
  /// ^abc$
  exact_match,
  /// ^abc
  prefix_match,
  /// abc$
  suffix_match,
  /// abc (found within the input)
  unanchored_search,
};
