const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const pzre = @import("../../root.zig");
const debug = pzre.lens.debug;
const Ast = pzre.ast.Ast;
const Set = pzre.encoding.ascii.IntegerSet;
const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = pzre.MemoryModel;

const compile = pzre.compile;
const strategy = compile.strategy;

const ConfigResolved = nfa.ConfigResolved;

const arch = pzre.arch;
const nfa = arch.minimal_nfa;
const state = nfa.state;
const Limits = pzre.compile.Limits;
const destroySets = pzre.misc.destroySets;

pub const Requirement = enum {nfa, rnfa, unfa, start_set};
pub fn getRequirements(name: strategy.Name) error{NoAvailableArchitecture}!std.EnumSet(Requirement) {
  const reqs: []const Requirement = switch (name) {
    .start_anchor_full_pass, .start_anchor_pass => &.{.nfa},
    .end_anchor_reverse_pass => &.{.rnfa},
    .start_set_pass => &.{.nfa, .start_set},
    .bi_directional_pass => &.{.unfa, .nfa, .rnfa},

    else => return error.NoAvailableArchitecture,
  };
  return std.EnumSet(Requirement).initMany(reqs);
}

/// The duck-typed linker interface called by Regex
pub fn linker(
  comptime config: ConfigResolved,
  comptime limits: Limits,
  comptime model: MemoryModel,
  comptime problem_bp: arch.AbsoluteBreakpoint,
  comptime sets_bp: arch.AbsoluteBreakpoint,
) type {
  const State = nfa.state.State(config.offset_bp, sets_bp);
  const Formulation = strategy.Formulation(problem_bp);
  const MachineSpan = Formulation.MachineSpan;
  const Internals = nfa.Internals(config, problem_bp, sets_bp);
 
  const AstNode = pzre.ast.Node;
  const ObjectNode = compile.parse_node.ParseNode(limits, .initOne(.nfa), model, config.offset_bp, sets_bp);
  const NodeList = polymorphic_memory.presets.single_ended.Create(model, null, AstNode);
  const StateList = polymorphic_memory.presets.single_ended.Create(model, null, State);
  const castAltPath = arch.integer_utils(
    config.context.breakpoint().Index(),
    config.offset_bp.Offset()
  ).castAltPath;
 
  const RangeList = polymorphic_memory.presets.single_ended.Create(model, null, pzre.Range);
 
  return struct {
    pub fn build(
      gpa: Allocator,
      // The returned Internals references .sets
      // Mutates sets in-place
      // Does not clean up any artifacts field
      artifacts: *compile.Artifacts,
      word_set: Set,
      strat: strategy.Name,
    ) compile.Error!Internals {
      const ast = artifacts.ast;
      const reqs = try getRequirements(strat);

      var states: StateList = .empty;
      errdefer states.deinit(gpa);

      var unfa_span: MachineSpan = .{ .start = 0, .end = 0 };
      var nfa_span: MachineSpan = .{ .start = 0, .end = 0 };
      var rnfa_span: MachineSpan = .{ .start = 0, .end = 0 };
      var start_set: ?Set = null;

      if (reqs.contains(.unfa)) {

        var contains_universe: bool = false;
        const universe = pzre.ascii.Set.ALL;
        for (artifacts.sets) |set| {
          if (set.equal(universe)) {
            contains_universe = true;
            break;
          }
        }

        if (!contains_universe) {
          artifacts.sets = try pzre.misc.appendUniverse(model, gpa, artifacts.sets);
        }

        // Fix the end of the prefix
        const static_prefx = comptime getUnanchoredPrefix();
        assert(static_prefx.len == 2);
        var prefix: [2]State = undefined;
        @memcpy(prefix[0..], static_prefx);
        prefix[1].term.set_idx = @truncate(artifacts.sets.len - 1);

        unfa_span.start = @truncate(states.len());

        var slice: []const State = prefix[0..];
        if (@inComptime()) slice = slice ++ [_]State{};
        try states.appendSlice(gpa, slice);
      }

      // Forward NFA
      if (reqs.contains(.nfa) or reqs.contains(.unfa)) {
        const compiled_nfa = try compileStatesFromAst(gpa, ast);
        defer if (!@inComptime()) gpa.free(compiled_nfa);
        
        nfa_span.start = @truncate(states.len());
        try config.isValidLength(limits, compiled_nfa.len);
        try states.appendSlice(gpa, compiled_nfa);
        nfa_span.end = @truncate(states.len());

        if (reqs.contains(.unfa)) {
          try config.isValidLength(limits, unfa_span.len());
          unfa_span.end = nfa_span.end;
        }
      }

      // Reverse NFA
      if (reqs.contains(.rnfa)) {
        var rev_ast = if (artifacts.reverse_ast) |rev| rev else try ast.reverse(model, gpa);
        defer if (artifacts.reverse_ast == null) rev_ast.deinit(gpa);
        
        const compiled_rnfa = try compileStatesFromAst(gpa, rev_ast);
        defer if (!@inComptime()) gpa.free(compiled_rnfa);
        
        rnfa_span.start = @truncate(states.len());
        try states.appendSlice(gpa, compiled_rnfa);
        try config.isValidLength(limits, compiled_rnfa.len);
        rnfa_span.end = @truncate(states.len());
        if (reqs.contains(.rnfa) and reqs.contains(.nfa)) {
          // Fundamentally, this cannot be asserted
          // Due to the .jump state being inserted as needed (see fragment.zig unpatchable_fallthrough) 
          // A tree such as 'c(a|b+)' does not require a dedicated jump state
          // reversed: '(b+|a)c'      does require one
          // 
          // assert(rnfa_span.len() == nfa_span.len());
        }
      }

      // Start Set
      if (reqs.contains(.start_set)) {
        start_set = try generateStartSet(
          gpa,
          states.getConstSlice()[nfa_span.start..nfa_span.end],
          artifacts.sets
        );
      }
      errdefer if (start_set) |ss| ss.deinit(gpa);

      const form: Formulation = switch (strat) {
        inline else => |comptime_strat| blk: {
          const tn = @tagName(comptime_strat);
          var form = @unionInit(Formulation, tn, undefined);
          var field_ptr = &@field(form, tn);
          const FormValue = @TypeOf(field_ptr.*);

          if (@hasField(FormValue, "nfa")) { 
            assert(reqs.contains(.nfa));
            field_ptr.nfa = nfa_span;
          }
          if (@hasField(FormValue, "rnfa")) { 
            assert(reqs.contains(.rnfa));
            field_ptr.rnfa = rnfa_span;
          }
          if (@hasField(FormValue, "unfa")) { 
            assert(reqs.contains(.unfa));
            field_ptr.unfa = unfa_span;
          }
          if (@hasField(FormValue, "start_set")) { 
            assert(reqs.contains(.start_set));
            field_ptr.start_set  = start_set.?;
          }

          break :blk form;
        },
      };

      if (limits.max_states < states.len()) return error.TooManyStates;
      const owned_states = try states.toOwnedConstSlice(gpa);

      return Internals{
        .states = owned_states,
        .sets = artifacts.sets,
        .word_set = word_set,
        .formulation = form,
      };
    }

    fn compileStatesFromAst(
      gpa: Allocator,
      ast: Ast,
    ) compile.Error![]const State {
      var node = try compileStatesFromAstInner(gpa, ast, ast.root);
      const states = b: {
        errdefer node.destroy(gpa);
        break :b try node.nfa.accept(gpa);
      };

      // Assert that the AST did not lie
      if (comptime builtin.mode == .Debug) {
        // We do not know optimization status
        const expected_len = try ast.calculateNfaStateCount(.initEmpty());
        // debug.prettyPrint(.{
        //   .ast = ast,
        //   .expected_len = expected_len,
        //   .states = states,
        // });
        assert(expected_len == states.len);
      }

      return states;
    }

    fn compileStatesFromAstInner(
      gpa: Allocator,
      ast: Ast,
      node_idx: usize,
    ) compile.Error!ObjectNode {
     
      var dummy_list: NodeList = .empty;
      defer dummy_list.deinit(gpa);
     
      switch (ast.nodes[node_idx]) {
        .epsilon => {
          return try ObjectNode.createEpsilon(gpa, &dummy_list);
        },
        .assertion => |ass| {
          return try ObjectNode.create(gpa, &dummy_list, .{ .assertion = ass });
        },
        .term => |n| {
          const s = switch (n) {
            .set_idx => |i| State{
              .tag = .term_set,
              .term = .{ .set_idx = @intCast(i) },
            },
            .char => |c| State{
              .tag = .term_char,
              .term = .{ .char = .{ .value = c } },
            },
          };
          return try ObjectNode.createFromState(gpa, s);
        },
        .concat => |n| {
          var lhs = try compileStatesFromAstInner(gpa, ast, n.lhs);
          var rhs = b: {
            errdefer lhs.destroy(gpa);
            break :b try compileStatesFromAstInner(gpa, ast, n.rhs);
          };
     
          return try lhs.concat(gpa, &rhs, &dummy_list);
        },
        .@"union" => |n| {
          var lhs = try compileStatesFromAstInner(gpa, ast, n.lhs);
          var rhs = b: {
            errdefer lhs.destroy(gpa);
            break :b try compileStatesFromAstInner(gpa, ast, n.rhs);
          };
     
          return lhs.@"union"(gpa, &rhs, &dummy_list) catch error.OutOfMemory;
        },
        .quantifier => |n| {
          var term = try compileStatesFromAstInner(gpa, ast, n.node);
          term.repeatExact(gpa, n.repeat, &dummy_list) catch return error.OutOfMemory;
     
          return term;
        },
      }
    }
   
    fn getUnanchoredPrefix() []const State {
      comptime {
        const exact_states = 2;
        var parser: pzre.compile.parse.Parser(.{}, .{}, .{}, .{}, .initMany(&.{.nfa}), .comptime_dynamic, config.offset_bp, sets_bp) = .new;
        const result = parser.parseComptime(".*") catch unreachable;
        assert(result.nfa_states.len == exact_states);
        return result.nfa_states;
      }
    }
   
    /// Constructs a set of characters that match the start state
    pub fn generateStartSet(
      gpa: Allocator,
      states: []const State,
      sets: []const Set,
    ) Allocator.Error!Set {
      if (nfa.analysis.isNullable(config.offset_bp, sets_bp, config.context.breakpoint(), states)) {
        return switch (model) {
          .dynamic => try Set.universe.dupe(gpa),
          .comptime_dynamic => Set.universe,
        };
      } else {
        var acc: RangeList = .empty;
        errdefer acc.deinit(gpa);

        try generateStartSetInner(gpa, states, sets, 0, &acc);
        assert(acc.len() > 0); // not nullable
        const set = Set{.ranges = try acc.toOwnedConstSlice(gpa)};
        return set;
      }
    }

    /// Accumulates the set of characters that would move the automata one step forwards
    /// If 'accept' is encountered immediately through splits (end of states), 'acc' is empty
    pub fn generateStartSetInner(
      gpa: Allocator,
      states: []const State,
      sets: []const Set,
      idx: usize,
      acc: *RangeList,
    ) Allocator.Error!void {
      if (idx == states.len) return; // accept reached
      const s = states[idx];
      switch (s.tag) {
        .term_set, .term_set_alt_jump => {
          const set = sets[s.term.set_idx];
          try pzre.pse.polymorphicSetUnionInplace(model, gpa, acc, set);
        },
        .term_char, .term_char_alt_jump => {
          const c = s.term.char.value;
          const range = pzre.Range{.start = c, .end = c + 1};
          const set = Set.init(&.{range});
          try pzre.pse.polymorphicSetUnionInplace(model, gpa, acc, set);
        },
        .split => {
          try generateStartSetInner(gpa, states, sets, idx + 1, acc);
          if (s.alt_jump > 0) {
            const jump = castAltPath(@intCast(idx), s.alt_jump);
            try generateStartSetInner(gpa, states, sets, jump, acc);
          }
        },
        .jump => {
          const jump = castAltPath(@intCast(idx), s.alt_jump);
          try generateStartSetInner(gpa, states, sets, jump, acc);
        },
        .line_start, .line_end, .not_word_boundary, .text_end, .text_start, .word_boundary => {
          try generateStartSetInner(gpa, states, sets, idx + 1, acc);
        },
        .line_start_alt_jump, .line_end_alt_jump, .not_word_boundary_alt_jump, .text_end_alt_jump, .text_start_alt_jump, .word_boundary_alt_jump => {
          if (s.alt_jump > 0) {
            const jump = castAltPath(@intCast(idx), s.alt_jump);
            try generateStartSetInner(gpa, states, sets, jump, acc);
          }
        },
      }
    }
  };
}
