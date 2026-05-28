//! Draft of subproblems, current aim for 0.1.0 is to only implement linear_three_pass 
//!   for variable length patterns

const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const pzre = @import("../root.zig");
const state = pzre.nfa.state;
const State = state.State;

const Ast = pzre.ast.Ast;
const Set = pzre.Set;

pub const MachineSpan = pzre.structures.range.Range(usize);

pub const default: Name = .bi_directional_pass;
pub const default_low_latency: Name = .start_set_pass;

pub const Requirement = enum {nfa, rnfa, prefix, start_set};

pub const Requirements = struct {
  nfa: bool = false,
  /// reverse nfa
  rnfa: bool = false,
  /// .*  for unanchored nfa (unfa)
  prefix: bool = false,
  /// characters that match the beginning of the machine
  start_set: bool = false,
};

pub const Name = enum {
  strict_start_anchor,
  strict_end_anchor,
  exact_match,
  bi_directional_pass,
  start_set_pass,

  pub fn hasRequirement(self: Name, req: Requirement) bool {
    const reqs = self.getRequirements();
    return switch (req) {
      .nfa => reqs.nfa,
      .rnfa => reqs.rnfa,
      .prefix => reqs.prefix,
      .start_set => reqs.start_set,
    };
  }

  pub fn getUniqueSubmachineRequirement(self: Name) ?Requirement {
    const reqs = self.getRequirements();
    var count: usize = 0;
    var unique: ?Requirement = null;

    if (reqs.nfa) {
      count += 1;
      unique = .nfa;
    }
    if (reqs.rnfa) {
      count += 1;
      unique = .rnfa;
    }
    if (reqs.prefix) {
      count += 1;
      unique = .prefix;
    }

    if (count == 1) return unique;
    return null;
  }

  pub fn getRequirements(self: Name) Requirements {
    return switch (self) {
      .exact_match, .strict_start_anchor => .{ .nfa = true },
      .strict_end_anchor => .{ .rnfa = true },
      .start_set_pass => .{ .nfa = true, .start_set = true },
      .bi_directional_pass => .{ .prefix = true, .nfa = true, .rnfa = true },
    };
  }
};

/// A collection of approaches to the subproblem of advancing the state machine non-catastrophically
/// 
/// WIP, these are only ideas. currently doing research on what are common approaches
///
pub const SearchProblem = union (Name) {
  /// NOTE: some of these are unimplemented

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
  strict_start_anchor: struct {
    nfa: MachineSpan
  },

  /// abc$  abc\z
  ///   Input is reversed; Machine is reverse-compiled; Start index is always 0
  strict_end_anchor: struct {
    rnfa: MachineSpan
  },

  /// ^abc$
  ///   NFA is simply ran once
  exact_match: struct {
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

  pub fn deinit(self: *SearchProblem, gpa: Allocator) void {
    _ = switch (self.*) {
      .start_set_pass => |f| f.start_set.deinit(gpa),
      .bi_directional_pass, .strict_end_anchor, .strict_start_anchor, .exact_match => .{},
    };
  }
};

/// A precompiled prefix type used 
pub const unanchored_prefix_types: [3]type = .{i8, i16, i32};

/// A precompiled prefix used 
pub const unanchored_prefixes = struct {
  i8: []const state.State(.i8),
  i16: []const state.State(.i16),
  i32: []const state.State(.i32),
}{
  .i8 = compileUnanchoredPrefix(.i8),
  .i16 = compileUnanchoredPrefix(.i16),
  .i32 = compileUnanchoredPrefix(.i32),
};

fn compileUnanchoredPrefix(comptime breakpoint: state.Breakpoint) []const state.State(breakpoint) {
  comptime {
    const exact_states = 3;
    var parser: pzre.parse.Parser(.{}, .{}, .{}, true, .make_nfa, .comptime_dynamic, breakpoint) = .new;
    const result = parser.parseUnbounded(undefined, ".*") catch unreachable;
    assert(result.nfa_states.len == exact_states);
    return result.nfa_states[0..2]; // goodbye accept
  }
}

pub inline fn getUnanchoredPrefix(comptime breakpoint: state.Breakpoint) []const state.State(breakpoint) {
  comptime {
    return switch (breakpoint) {
      .i8 => unanchored_prefixes.i8,
      .i16 => unanchored_prefixes.i16,
      .i32 => unanchored_prefixes.i32,
    };
  }
}

pub fn inferProblem(ast: Ast) Name {
  const has_start = ast.isAssertionGuaranteed(&.{.text_start});
  const has_end = ast.isAssertionGuaranteed(&.{.text_end});

  if (has_start and has_end) return .exact_match;
  if (has_start) return .strict_start_anchor;
  if (has_end) return .strict_end_anchor;

  return default;
}

pub inline fn calculateFinalStatesCount(nfa_len: usize, problem: Name) usize {
  return switch (problem) {
    .bi_directional_pass => nfa_len * 2 + 2,
    .start_set_pass, .exact_match, .strict_end_anchor, .strict_start_anchor => nfa_len,
  };
}

pub inline fn calculateMaximumSubmachineSize(nfa_len: usize, problem: Name) usize {
  return switch (problem) {
    .bi_directional_pass => nfa_len + 2,
    .start_set_pass, .exact_match, .strict_end_anchor, .strict_start_anchor => nfa_len,
  };
}
