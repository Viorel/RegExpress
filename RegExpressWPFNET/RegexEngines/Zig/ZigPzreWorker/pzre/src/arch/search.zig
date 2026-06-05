//! The universal machine orchestrator
//! Responsible for managing the machines and figuring out where within the input the machines should be called

const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const meta = pzre.meta;

const arch = pzre.arch;
const AbsoluteBreakpoint = arch.AbsoluteBreakpoint;
const strategy = pzre.compile.strategy;
const Formulation = strategy.Formulation;

const Ast = pzre.ast.Ast;
const Set = pzre.Set;
const Match = pzre.Match;
const debug = pzre.lens.debug;

/// Finds the first match that starts within range [start_idx, max_base]   end inclusive
/// Start index is allowed to be str.len for matching boundaries
pub fn find(
  comptime indexing_breakpoint: AbsoluteBreakpoint,
  problem: Formulation(indexing_breakpoint),
  comptime Machine: type,
  ctx: *Machine.Context,
  states: []const Machine.State,
  sets: []const Set,
  word_set: Set,
  str: []const u8,
  start_idx: usize,
  max_base: usize,
  /// Currently unused
  captures: ?[]usize,
) ?Match {
  assert(start_idx <= str.len);
  assert(start_idx <= max_base);

  // -- 1. Infer correct approach given what is available -- //

  // Case when search problem does not exist
  // All blocks assert that no search problem exist
  // Blocks are only allowed to perform 1 call to the machine
  if (max_base == start_idx) {
    @branchHint(.cold);
    switch (problem) {
      inline else => |f| {
        if (comptime meta.isStruct(@TypeOf(f))) {
          if (comptime @hasField(@TypeOf(f), "nfa")) {
            const span = @field(f, "nfa");
            var m = Machine.init(ctx, states[span.start .. span.end], sets, word_set);

            if (m.matches(.{}, str, start_idx, str.len, captures)) |end| {
              return Match{.str = str[start_idx .. end], .loc = .init(start_idx, end)};
            }

            return null;
          } else if (comptime @hasField(@TypeOf(f), "rnfa")) {
            const span = @field(f, "rnfa");
            var m = Machine.init(ctx, states[span.start .. span.end], sets, word_set);

            // consider the input "...abc"
            // the word abc starts at start_idx of the input string. the machine was compiled for the pattern "abc" in reverse, so the machine matches "cba"
            // 
            // we need to iterate the input in reverse starting from some idx > start_idx bounded at str.len. This implies a search problem. by assertion of being in this block: no search problem exists, then it is implied that the reverse iteration start index is either start_idx or str.len. The machine only matches start_idx if it matches the empty string. Otherwise it is implied that reverse iteration start index is str.len

            // Begin by assuming the common case, e.g. the pattern is right-anchored "abc$"
            if (m.matches(.{ .iterate_reverse = true, .reversed_machine = true }, str, str.len, start_idx, captures)) |start| {
              @branchHint(.likely);
              // debug.prettyPrint(.{
              //   .reverse_right_anchor_found_match_start = start,
              // });
              if (start == start_idx) {
                return Match{ .str = str[start_idx..str.len], .loc = .init(start_idx, str.len) };
              }
            }

            // TODO: constrict str, so no pointless iteration occurs

            if (start_idx < str.len) {
              if (m.matches(.{ .iterate_reverse = true, .reversed_machine = true, .non_greedy = true }, str, start_idx, 0, captures)) |start| {
                if (start == start_idx) {
                  return Match{ .str = str[start_idx..start_idx], .loc = .init(start_idx, start_idx) };
                }
              }
            }

            return null;
          }
        }
      }
    }
  }

  // -- 2. Solve using machine's intended search problem formulation -- //
  switch (problem) {
    .start_set_pass => |f| {
      var m = Machine.init(ctx, states[f.nfa.start .. f.nfa.end], sets, word_set);
      var base = start_idx;

      while (base <= str.len) {
        const focus = str[base..];

        if (focus.len == 0) { // handle the very end of the string for the case when the pattern is nullable
          @branchHint(.cold);
          if (m.matches(.{}, str, str.len, str.len, captures)) |_| {
            return Match{.str = "", .loc = .init(str.len, str.len)};
          } return null;
        }
        
        const next_offset = if (f.start_set.isUniverse()) b: {
          // This algorithm is never picked dynamically as it is not immune to ReDoS
          // We assume that the user picked this manually for non-pathological patterns
          @branchHint(.cold);
          break :b 0;
        } else f.start_set.find(u8, focus) orelse {
          @branchHint(.unlikely);
          // No start set found. Nullable patterns have the universe as the start set
          // Therefore there is no match UNLESS the pattern is nullable and focus is the end of string
          //  -> however this was handled above: if (focus.len == 0) {...}
          return null;
        };

        const next = base + next_offset;

        if (m.matches(.{}, str, next, str.len, captures)) |end| {
          return Match{.str = str[next .. end], .loc = .init(next, end)};
        }

        base = next + 1;
      }
      return null;
    },
    .start_anchor_full_pass => |f| {
      if (start_idx > 0) {
        @branchHint(.cold);
        return null;
      }
      
      var m = Machine.init(ctx, states[f.nfa.start .. f.nfa.end], sets, word_set);
      if (m.matches(.{}, str, 0, str.len, captures)) |end| {
        if (end == str.len) {
          return Match{ .str = str[0..end], .loc = .init(0, end) };
        }
      }
      return null;
    },

    .start_anchor_pass => |f| {
      if (start_idx > 0) {
        @branchHint(.cold);
        return null;
      }
      const base_idx = 0;

      var m = Machine.init(ctx, states[f.nfa.start .. f.nfa.end], sets, word_set);
      if (m.matches(.{}, str, base_idx, str.len, captures)) |end| {
        return Match{.str = str[base_idx .. end], .loc = .init(base_idx, end)};
      }

      return null;
    },

    .end_anchor_reverse_pass => |f| {
      var m = Machine.init(ctx, states[f.rnfa.start .. f.rnfa.end], sets, word_set);
      if (m.matches(.{ .iterate_reverse = true, .reversed_machine = true }, str, str.len, 0, captures)) |start| {

        if (start >= start_idx and start <= max_base) {
          @branchHint(.likely);
          const r = Match{.str = str[start .. str.len], .loc = .init(start, str.len)};
          // debug.prettyPrint(.{.matched = r});
          return r;
        }
      }

      return null;
    },

    .bi_directional_pass => |f| {
      if (start_idx > str.len) {
        @branchHint(.cold);
        return null;
      }

      // -- SIMD Pre-Filter (fast-fail) --
      // Unimplemented

      // -- Finding valid_end (f) non-greedily --
      var unfa = Machine.init(ctx, states[f.unfa.start .. f.unfa.end], sets, word_set);
      const result = unfa.matches(.{ .non_greedy = true }, str, start_idx, str.len, null);

      // "non greedily" is retarded. The automata will stop instantly
      // not true, just because the beginning matches doesnt mean the whole automata does

      // debug.prettyPrint(.{
      //   .first_pass = start_idx,
      //   .result = result,
      // });

      if (result == null) {
        @branchHint(.unlikely);
        return null;
      }
      const exclusive_end = result.?;

      // -- Finding valid_start (s) backwards greedily --
      var rnfa = Machine.init(ctx, states[f.rnfa.start .. f.rnfa.end], sets, word_set);

      // The RNFA matches greedily backwards. Going past start_idx means can mean a couple of things:
      //  1. The RNFA could have stopped earlier, but it greedily went past
      //  2. The RNFA HAD to go past start_idx in order to match
      //
      // As such, we cant clamp the inclusive_start to start_idx, we have to ensure that a real match
      // occured within the bounds. Slicing the inputs to the automata does not work due to assertions
      //  The only way is to introduce a max_iteration bound. When the NFA reaches this, it treats it
      //  as the final iteration

      const rev_result = rnfa.matches(.{ .iterate_reverse = true, .reversed_machine = true }, str, exclusive_end, start_idx, null);
      const inclusive_start = rev_result.?;

      // -- Greedy resolution (t) --
      var nfa = Machine.init(ctx, states[f.nfa.start .. f.nfa.end], sets, word_set);
      const greed_result = nfa.matches(.{}, str, inclusive_start, str.len, captures);
      const greedier_end = greed_result orelse unreachable;
      assert(exclusive_end <= greedier_end);

      const r = Match{
        .str = str[inclusive_start..greedier_end],
        .loc = .init(inclusive_start, greedier_end),
      };
      // debug.prettyPrint(.{.matched = r});
      return r;
    },
    else => unreachable,
  }
}
