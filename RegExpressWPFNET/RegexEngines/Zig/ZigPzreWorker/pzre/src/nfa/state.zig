//! The primary object found in the state machine
//! 
//! Extremely optimized for the fallback search problem: bi_directional_pass
//! In many cases, matching should not instantiate the real forward NFA at all, but instead passed to SIMD powered search algorithms through problem formulations. The NFA using bi_directional_pass is the absolute last line of defence against the worst patterns with no practical DFA construction
//! 
const std = @import("std");
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const builtin = @import("builtin");
const misc = pzre.misc;
const Assertion = misc.Assertion;

pub const Tag = enum(u8) {
  // NOTE: The system expects alternate jumping tags to end with "_alt_jump" (duck typing)

  /// The final state
  accept = 0,
  /// Term where next state is at next index
  term_set,
  /// Term but next state follows .alt_jump
  term_set_alt_jump,
  term_char,
  term_char_alt_jump,
  /// Split where next state is at next index
  split,

  /// A simple node that immediately jumps to .alt_jump
  /// This is required when an island ends in a node that jumps backwards 
  ///   while also requiring it to jump forwards. Examples:
  ///   - a+|b+
  ///   - (a+)*
  ///   - a*|a*
  jump,

  /// All assertions
  line_start,
  line_start_alt_jump,
  line_end,
  line_end_alt_jump,
  text_start,
  text_start_alt_jump,
  text_end,
  text_end_alt_jump,
  word_boundary,
  word_boundary_alt_jump,
  not_word_boundary,
  not_word_boundary_alt_jump,

  pub fn assertAssertion(ass: Tag) void {
    if (comptime builtin.mode == .Debug) {
      switch (ass) {
        .line_start, .line_start_alt_jump,
        .line_end, .line_end_alt_jump,
        .text_start, .text_start_alt_jump,
        .text_end, .text_end_alt_jump,
        .word_boundary, .word_boundary_alt_jump,
        .not_word_boundary, .not_word_boundary_alt_jump => {},
        else => unreachable,
      }
    }
  }

  pub fn fromAssertion(ass: Assertion) Tag {
    return switch (ass) {
      inline else => |a| @field(Tag, @tagName(a)),
    };
  }
};

/// The integer size breakpoint that defines the size of the State structure
pub const Breakpoint = enum {
  i8,
  i16,
  i32,

  pub fn Offset(comptime breakpoint: Breakpoint) type {
    return switch (breakpoint) {
      .i8 => i8,
      .i16 => i16,
      .i32 => i32,
    };
  }

  pub fn stateSize(pt: Breakpoint) usize {
    const S = State(pt);
    return @sizeOf(S);
  }
};

pub fn getBreakpoint(comptime states_len: usize) Breakpoint {
  if (states_len <= std.math.maxInt(i8)) return .i8;
  if (states_len <= std.math.maxInt(i16)) return .i16;
  if (states_len <= std.math.maxInt(i32)) return .i32;
  @compileError("Defined max_states is too large. Upperbound is std.math.maxInt(i32)");
}

pub fn DefineOffset(comptime breakpoint: Breakpoint) type {
  return switch (breakpoint) {
    .i8 => i8,
    .i16 => i16,
    .i32 => i32,
  };
}

pub fn DefineIdx(comptime breakpoint: Breakpoint) type {
  const Offset = DefineOffset(breakpoint);
  return @Int(.unsigned, @bitSizeOf(Offset));
}

pub fn FromLength(comptime states_len: usize) type {
  return State(getBreakpoint(states_len));
}

/// TODO: decouple offset and index breakpoints. the topology design is supposed to collapse branches as quick as possible, therefore offsets are supposed to jump close-by very often. Its possible to use i8 offsets even for patterns that are thousands of states long, by analyzing the maximum jump length at comptime
/// WARN: Unused
pub const Breakpoints = struct {
  offset: Breakpoint,
  set_idx: Breakpoint,
  /// For contexts
  idx: Breakpoint,
};

/// List size defines the integers used. If max_states are bounded to maxInt(u8),
/// then a single state is only 4 bytes. If null, the size is 4*@sizeOf(usize)
pub fn State(comptime breakpoint: Breakpoint) type {
  return packed struct {
    /// Alternate next index
    /// This is only followed on *_alt_jump tags, and splits
    ///
    /// The nfa generation is optimized so that alt_jumps are exceedingly rare, and in the cases when they occur, the jump is small. We can branchHint to the compiler that the most common state transition is i <- i + 1. This should be taken into consideration during AST optimization: Reduce jumps as much as possible in order to minimize mispredictions
    ///
    /// Additionally, the State structure has no absolute indices pointing within itself,
    ///   Movement is always strictly relational
    ///   This means that machines can be built modularly, and concatenated with eachother:
    ///   
    ///   a.*(error|name)      <-  this could be represented using two different machines 
    ///                            with different solvers
    ///   a.*    and    error|name
    /// 
    /// This could allow for some kind of modularity to the engine. Currently, it is only used for
    ///   concatenating forward nfa's with unanchored prefixes. For example complex automatas could be broken to three separate machines, where the middle one is a fixed-length machine for a restrictive search problem.
    /// 
    /// The machines can be shuffled efficiently aswell as long as the shuffled parts are valid fragments
    /// a*(ab)+q?  -> (ab)+q?a*
    /// 
    alt_jump: Offset = 0,            // @sizeOf(Idx)
    term: Term = .no_term,           // @sizeOf(Idx)
    tag: Tag,                        // 1 byte
    /// There is a conflict on whether to use packed structs or extern structs for the state structure. Ideally, we would let the user choose what to use.
    /// 
    /// Packed structs are collapsed to a single backing integer that represents the entire bitwidth.
    /// This means that the size of State has to be brought up to pow2 with padding in order to 
    ///   avoid non-standard integer sizes.
    /// 
    /// Extern structs have no backing integer behavior, and instead are aligned according to the field with the largest alignment. Ironically, this makes extern structs actually smaller than packed structs.
    /// 
    /// Extern states produce a smaller state machine: smaller memory footprint, better cache locality. 
    /// 
    /// Packed states produce more optimal jumps, due to states random access being a simple bit shift (due to pow2 alignment). 
    /// 
    /// All of this means that longer, and less complicated patterns (with less + * | ? operators) perform better on an extern machine. On the other hand, if the machine is complicated with lots of random jumps, the throughput benefit of a packed machine should outperform it.
    /// 
    /// For example, if index size is 1 bytes, then pre_padded_size is 3 bytes. Packed state size is 3, extern state size is 4 with padding.
    ///
    /// NOTE: there is 1byte of extra space for something
    /// A tag could cause a jump to a branch that then assigns a value to this
    /// The only constraint is that the machine can never read from the padding as it would introduce
    /// additional branching confusion
    _: StatePadding = 0,

    /// TODO: add breakpoint
    pub const SetIdx = u16;
    pub const Offset = DefineOffset(breakpoint);
    pub const Idx = DefineIdx(breakpoint);

    const pre_padded_size = @sizeOf(Offset) + @sizeOf(Term) + @sizeOf(Tag);
    const padded_size = std.math.ceilPowerOfTwo(usize, pre_padded_size) catch unreachable;
    const remaining_bytes = padded_size - pre_padded_size;
    const StatePadding = @Int(.unsigned, remaining_bytes * 8);

    const Self = @This();

    comptime {
      assert(@sizeOf(Tag) == 1);
      assert(@sizeOf(Term) == @sizeOf(SetIdx));
      assert(@bitSizeOf(Idx) % 8 == 0);
      assert(@bitSizeOf(Offset) % 8 == 0);
      assert(@bitSizeOf(Term) % 8 == 0);
      assert(@sizeOf(Offset) > 0);
      assert(@bitSizeOf(Self) % 8 == 0);
      assert(std.math.isPowerOfTwo(@alignOf(Self)));
      assert(std.math.isPowerOfTwo(@sizeOf(Self)));
    }

    /// The base atomic unit of a regex that matches something, e.g. chars or char sets 
    pub const Term = packed union {
      set_idx: SetIdx,
      char: Char,

      const Char = packed struct {
        const padding = @sizeOf(SetIdx) - 1;
        const padding_bits = padding * 8;
        const TermPaddingInt = @Int(.unsigned, padding_bits);

        value: u8,
        _: TermPaddingInt = 0,
      };

      pub const no_term: Term = .{ .char = .{ .value = 0 } };
    };

    pub inline fn castAltPath(base: Idx, offset: Offset) Idx {
      // eqv to
      // return @intCast(@as(Offset, @intCast(base)) + offset);
      return base +% @as(Idx, @bitCast(offset));
    }

    /// comparison for two states
    pub fn eql(a: Self, b: Self) bool {
      if (a.tag != b.tag) return false;

      return switch (a.tag) {
        .term_char => a.term.char.value == b.term.char.value,
        .term_char_alt_jump => a.term.char.value == b.term.char.value and a.alt_jump == b.alt_jump,
        
        .term_set => a.term.set_idx == b.term.set_idx,
        .term_set_alt_jump => a.term.set_idx == b.term.set_idx and a.alt_jump == b.alt_jump,

        .split => a.alt_jump == b.alt_jump,
        .accept => true,
        .jump => a.alt_jump == b.alt_jump,

        .word_boundary,
        .text_start,
        .text_end,
        .not_word_boundary,
        .line_end,
        .line_start => true,

        .word_boundary_alt_jump,
        .text_start_alt_jump,
        .text_end_alt_jump,
        .not_word_boundary_alt_jump,
        .line_end_alt_jump,
        .line_start_alt_jump => a.alt_jump == b.alt_jump,
      };
    }
  };
}
