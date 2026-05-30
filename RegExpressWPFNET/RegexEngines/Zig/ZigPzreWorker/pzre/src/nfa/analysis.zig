const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const pzre = @import("../root.zig");
const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const Range = ascii.Range;
const nfa = pzre.nfa;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;
const polymorphicSetUnionInplace = pzre.pse.polymorphicSetUnionInplace;

const List = polymorphic_memory.presets.single_ended.Create;

const state = pzre.nfa.state;

const lens = pzre.lens;
const debug = lens.debug;

pub fn determineMaxConcurrency(states_len: usize) usize {
  // TODO: find lower bound
  return states_len;
}

/// Constructs a set of characters that match the start state
pub fn generateStartSet(
  comptime model: MemoryModel,
  comptime breakpoint: nfa.state.Breakpoint,
  gpa: Allocator,
  states: []const state.State(breakpoint),
  sets: []const Set,
) Allocator.Error!Set {
  var acc: List(model, null, Range) = .empty;
  errdefer acc.deinit(gpa);

  try generateStartSetInner(model, breakpoint, gpa, states, sets, 0, &acc);
  const set = Set{.ranges = try acc.toOwnedConstSlice(gpa)};
  return set;
}

/// Accumulates the set of characters that would move the automata one step forwards
/// If 'accept' is encountered immediately through splits, the returned set is the universe
pub fn generateStartSetInner(
  comptime model: MemoryModel,
  comptime breakpoint: nfa.state.Breakpoint,
  gpa: Allocator,
  states: []const state.State(breakpoint),
  sets: []const Set,
  idx: usize,
  acc: *List(model, null, Range),
) Allocator.Error!void {
  const State = state.State(breakpoint);
  const s = states[idx];
  switch (s.tag) {
    .term_set, .term_set_alt_jump => {
      const set = sets[s.term.set_idx];
      try polymorphicSetUnionInplace(model, gpa, acc, set);
    },
    .term_char, .term_char_alt_jump => {
      const c = s.term.char.value;
      const range = Range{.start = c, .end = c + 1};
      const set = Set.init(&.{range});
      try polymorphicSetUnionInplace(model, gpa, acc, set);
    },
    .split => {
      try generateStartSetInner(model, breakpoint, gpa, states, sets, idx + 1, acc);
      if (s.alt_jump > 0) {
        const jump = State.castAltPath(@intCast(idx), s.alt_jump);
        try generateStartSetInner(model, breakpoint, gpa, states, sets, jump, acc);
      }
    },
    .jump => {
      const jump = State.castAltPath(@intCast(idx), s.alt_jump);
      try generateStartSetInner(model, breakpoint, gpa, states, sets, jump, acc);
    },
    .accept => {
      switch (model) {
        .dynamic => {
          const universe = Set.universe.ranges;
          acc.clearRetainingCapacity();
          try acc.ensureCapacityPrecise(gpa, universe.len);
          var slice = try acc.addManyAsSlice(gpa, universe.len);
          @memcpy(slice[0..], universe[0..]);
        },
        .comptime_dynamic => {
          acc.data.items = Set.universe.ranges;
        },
      }

    },
    .line_start, .line_end, .not_word_boundary, .text_end, .text_start, .word_boundary => {
      try generateStartSetInner(model, breakpoint, gpa, states, sets, idx + 1, acc);
    },
    .line_start_alt_jump, .line_end_alt_jump, .not_word_boundary_alt_jump, .text_end_alt_jump, .text_start_alt_jump, .word_boundary_alt_jump => {
      if (s.alt_jump > 0) {
        const jump = State.castAltPath(@intCast(idx), s.alt_jump);
        try generateStartSetInner(model, breakpoint, gpa, states, sets, jump, acc);
      }
    },
  }
}
