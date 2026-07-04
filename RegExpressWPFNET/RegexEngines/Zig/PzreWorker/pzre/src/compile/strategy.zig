//! The modeled mathematical search problem

const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const meta = pzre.meta;

const AbsoluteBreakpoint = pzre.arch.AbsoluteBreakpoint;

const ast = pzre.ast;
const Ast = ast.Ast;
const Set = pzre.Set;
const Match = pzre.Match;

pub const Submachine = enum {unfa, nfa, rnfa};

pub const Name = enum {
  start_anchor_pass,
  end_anchor_reverse_pass,
  start_anchor_full_pass,
  bi_directional_pass,
  start_set_pass,
  /// This is just to silence the zig compiler
  /// Sometimes I want to leave else branches even if all cases are covered
  unimplemented_problem,
 
  const Self = @This();

  pub fn routing(self: Self) ast.Routing {
    return switch (self) {
      .start_anchor_full_pass => .exact_match,
      .start_anchor_pass => .prefix_match,
      .end_anchor_reverse_pass => .suffix_match,
      .unimplemented_problem, .bi_directional_pass, .start_set_pass => .unanchored_search,
    };
  }
 
  pub fn requires(self: Self, submachine: Submachine) bool {
    return switch (submachine) {
      .nfa => switch (self) {
        .bi_directional_pass, .start_anchor_pass, .start_set_pass, .start_anchor_full_pass => true,
        .end_anchor_reverse_pass, .unimplemented_problem => false,
      },
      .rnfa => switch (self) {
        .bi_directional_pass, .end_anchor_reverse_pass, => true,
        .unimplemented_problem, .start_anchor_pass, .start_set_pass, .start_anchor_full_pass => false,
      },
      .unfa => switch (self) {
        .bi_directional_pass, => true,
        .start_anchor_pass, .unimplemented_problem, .start_set_pass, .start_anchor_full_pass, .end_anchor_reverse_pass => false,
      },
    };
  }
 
};

/// The purely mathematical formulation of the search problem. It describes how the problem of searching the 
/// input is solved.
/// It does not concern itself with architectures. For a problem involving automatas, it assumes the abstract 
/// machine, and only stores the indices where machine solving the problem is located in.
/// 
/// It describes how the state machine is advanced non-catastrophically
/// 
/// It contains non-architecture defined metadata
/// Ideally, the State slice would be stored here, however it is not possible due to being arch-defined
///
pub fn Formulation(comptime indexing_breakpoint: AbsoluteBreakpoint) type {
  return union (Name) {
    /// NOTE: some of these are unimplemented (and bad)

    /// error\d{3}
    ///   Length is constant N, there is a single anchor (highly restrictive) byte x with index i
    ///   x should be the rarest byte we can find in practical inputs
    ///
    /// 1. Discard state-machine: compile NFA into an array of bitmasks
    /// 2. Determine a single rarest byte in the pattern e.g. 'r'
    /// 3. Skip forwards until you find 'r'
    /// 4. Apply bitmask at location with offset -i, repeat if no match
    ///

    // restrictive_anchor_byte: struct {anchor: u8, length: usize},

    /// [abc][qwd]
    ///   Length is constant N, with no byte anchors
    ///   Find most-restrictive set (by default smallest non-whitespace set)
    ///   
    /// 1. Do restrictive_fixed_byte but by finding the set instead of the byte
    ///
    /// value: match length
    // restrictive_anchor_set: struct {anchor_set_idx: u8, length: usize},

    /// \d{2}\w{3}[a-zA-Z]
    ///   Length is constant N, with no byte anchors, and sets too large
    /// 
    /// 1. Discard state-machine: compile pattern into a 256-entry array of N-bit masks (one mask per possible byte value)
    /// 2. Process input sequentially (or in SIMD chunks) without attempting to skip forward
    /// 3. Update a running N-bit state acc = ((acc << 1) | 1) & bitmask[current_byte]
    /// 4. If the N-th bit of the acc becomes 1, a full match has just completed at index i
    /// 5. start_pos = i - N + 1
    ///
    /// value: match length

    // dense_simd_masking: usize,

    /// [a-zA-Z123\t]{3}[123]a?
    ///   Least-wide set ([123]) has a fixed prefix count (min == max == 3)
    ///   Start position is determined by:
    ///     1. find first encounter of x \in least_wide_set at pos i
    ///     2. start_pos = i - fixed_prefix
    ///     3. if no match, resume SIMD search starting at i + 1
    ///
    /// value: prefix length

    // fixed_offset_extraction: usize,

    /// ^abc  \Aabc
    ///   Start index is always 0
    start_anchor_pass: struct {
      nfa: MachineSpan
    },

    /// abc$  abc\z
    ///   Input is reversed; Machine is reverse-compiled; Start index is always 0
    end_anchor_reverse_pass: struct {
      rnfa: MachineSpan
    },

    /// ^abc$
    ///   NFA is simply ran once
    start_anchor_full_pass: struct {
      nfa: MachineSpan
    },

    /// Literal alternations     some|dictionary|entries
    /// 
    /// Aho-Corasick
    ///

    // multi_literal_alternation,

    /// [a-z]+(critical_failure)[0-9]+
    ///   Pattern contains a highly restrictive, multi-byte literal sequence 
    ///   surrounded by variable-length bounds.
    ///   
    ///   1. SIMD memmem for the exact substring.
    ///   2. Execute reverse NFA backwards from the substring start to resolve prefix.
    ///   3. Execute forward NFA from the substring end to resolve suffix.
    ///
    /// This approach requires partial NFA compilation:
    ///   - [a-z]+
    ///   - (critical_failure)
    ///   - [0-9]+
    /// 
    /// The rNFA consists of [a-z]+
    ///
    /// value: restrictive substring length

    // inner_substring_extraction: usize,

    /// General purpose, no-bad case matching for variable length patterns
    /// fallback method
    /// 
    /// NOTE: pre-filter unimplemented
    /// 
    ///   1. SIMD Pre-Filter (fast-fail):
    ///     - only if the pattern has a literal sequence e.g. "fatal error" in ".*fatal error.*"
    ///     - scan quickly using SIMD for "fatal error", proceed only if such exists
    ///   2. Finding valid_end:
    ///     - NFA is compiled to pNFA with an unbounded wildcard "[0-9]*b" -> ".*[0-9]*b"
    ///     - This makes the NFA execute all starting positions in parallel
    ///     - pNFA determines the first valid end pos f non-greedily
    ///   3. Finding valid_start:
    ///     - NFA is also reverse compiled to rNFA
    ///     - rNFA is executed backwards greedily starting from f
    ///     - rNFA will always finish with a valid start pos s
    ///   4. Greedy resolution:
    ///     - Given s, f. Original NFA is executed from s onwards
    ///     - NFA is guaranteed to match, but the true match will be at idx t >= f
    ///     - This exists because the region [s, f] is a non-greedy match.
    /// 
    bi_directional_pass: struct {
      /// unfa and nfa overlap, 
      /// unfa := .*nfa
      /// Unanchored nfa
      unfa: MachineSpan,
      /// Forward nfa
      nfa: MachineSpan,
      /// Reversed nfa
      rnfa: MachineSpan,
    },

    /// A simple formulation with O(n^2) complexity
    /// The user is responsible for avoiding ReDoS
    /// 
    /// 1. The algorithm searches the input for a character within 'start_set' at index i
    /// 2. The find algorithm is executed anchored at i
    /// 3. if no match, goto 1. starting at idx i + 1
    /// 
    /// Performs well when the start set is rarely hit, with even better performance than bi_directional_pass
    /// However, any pattern starting with '.*' will catastrophically degrade performance
    /// 
    /// This algorithm is required when AST generation is specifically avoided. However this is the case due to the current lazy implementation. TODO: create an inversion of the Fragment object in order for the parser to build the reverse and forward nfa on the same pass. This allows the usage of the bi-directional pass algorithm in low latency parsing.
    /// 
    start_set_pass: struct {
      nfa: MachineSpan,
      /// The set of characters the nfa matches
      start_set: Set,
    },

    unimplemented_problem: struct {},

    pub const MachineSpan = pzre.structures.range.Range(indexing_breakpoint.Index());
    const Self = @This();

    pub fn requiredContextLen(self: Self) usize {
      var max: usize = 0;
      switch (self) {
        inline else => |f| {
          inline for (std.meta.fields(@TypeOf(f))) |span_field| {
            if (comptime span_field.type == MachineSpan) {
              const span = @field(f, span_field.name);
              const submachine_len = span.len();
              max = @max(max, submachine_len);
            }
          }
        }
      }
      return max;
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      _ = switch (self.*) {
        .start_set_pass => |f| f.start_set.deinit(gpa),
        .unimplemented_problem, .bi_directional_pass, .end_anchor_reverse_pass, .start_anchor_pass, .start_anchor_full_pass => .{},
      };
    }
  };
}
