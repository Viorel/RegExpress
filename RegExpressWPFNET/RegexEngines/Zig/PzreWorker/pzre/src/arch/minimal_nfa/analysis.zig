const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const pzre = @import("../../root.zig");
const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const Range = ascii.Range;

const arch = pzre.arch;
const RelativeBreakpoint = arch.RelativeBreakpoint;
const nfa = arch.minimal_nfa;
const state = nfa.state;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;
const polymorphicSetUnionInplace = pzre.pse.polymorphicSetUnionInplace;

const List = polymorphic_memory.presets.single_ended.Create;

const lens = pzre.lens;
const debug = lens.debug;

pub fn determineMaxConcurrency(states_len: usize) usize {
  // TODO: find lower bound
  return states_len;
}

pub fn isNullable(
  comptime rbp: arch.RelativeBreakpoint,
  comptime sets_bp: arch.AbsoluteBreakpoint,
  comptime context_bp: arch.AbsoluteBreakpoint,
  states: []const state.State(rbp, sets_bp),
) bool {
  return isNullableInner(rbp, sets_bp, context_bp, states, 0);
}

/// Returns false if the branch is not nullable
fn isNullableInner(
  comptime rbp: arch.RelativeBreakpoint,
  comptime sets_bp: arch.AbsoluteBreakpoint,
  comptime context_bp: arch.AbsoluteBreakpoint,
  states: []const state.State(rbp, sets_bp),
  idx: usize,
) bool {

  const castAltPath = arch.integer_utils(
    context_bp.Index(),
    rbp.Offset()
  ).castAltPath;
 
  if (idx == states.len) return true; // accept reached
  const s = states[idx];
  return switch (s.tag) {
    .term_set, .term_set_alt_jump, .term_char, .term_char_alt_jump => false,
    .split => {
      if (isNullableInner(rbp, sets_bp, context_bp, states, idx + 1)) return true;
      if (s.alt_jump > 0) {
        const jump = castAltPath(@intCast(idx), s.alt_jump);
        return isNullableInner(rbp, sets_bp, context_bp, states, jump);
      }
      return false;
    },
    .jump => {
      const jump = castAltPath(@intCast(idx), s.alt_jump);
      return isNullableInner(rbp, sets_bp, context_bp, states, jump);
    },
    .line_start, .line_end, .not_word_boundary, .text_end, .text_start, .word_boundary => {
      return isNullableInner(rbp, sets_bp, context_bp, states, idx + 1);
    },
    .line_start_alt_jump, .line_end_alt_jump, .not_word_boundary_alt_jump, .text_end_alt_jump, .text_start_alt_jump, .word_boundary_alt_jump => {
      if (s.alt_jump > 0) {
        const jump = castAltPath(@intCast(idx), s.alt_jump);
        return isNullableInner(rbp, sets_bp, context_bp, states, jump);
      }
      return false;
    },
  };
}
