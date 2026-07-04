const std = @import("std");
const pzre = @import("../root.zig");
const assert = std.debug.assert;

const strategy = pzre.compile.strategy;
const arch = pzre.arch;
const ArchResolved = pzre.arch.ArchResolved;
const compile = pzre.compile;

const Manifest = pzre.ast.Manifest;

// --- Dispatch ---
// Its sole purpose is to choose the best architecture for the pattern being
// compiled.
//
// The manifest (the structural fact-sheet describing the pattern) arrives already
// built from the analysis stage. From there:
//
// 1. Bidding. Each architecture is fed the manifest. An architecture returns a
// Bid if it can formulate a search problem given the manifest, or null if it has
// no solution. The bid contains how efficiently it solves the problem AND the
// strategy it would use to do so. Each architecture has to return a Bid in O(1).
//
// 2. Choice narrowing. The universe of architectures is narrowed to as few choices
// as possible according to the best bids. A Choice object is generated. The Choice
// either contains a single best choice that was determined before building, or a
// post-poned decision that can only be made during building. As an example, we
// could be left choosing between an NFA and a DFA architecture.
pub const Bid = struct {
  tier: Speed,
  /// Not counting resources that can be shared, e.g. sets or context
  /// NFA returns: self_size + states_count * state_size
  ///
  /// Null if the architecture cannot predict its layout size without subset construction/linking
  /// It is expect that the arch fills this field in O(1)
  total_memory_footprint: ?usize,
  /// How the total memory usage scales as the number of states increase
  /// NFA returns: state_size
  memory_scaling_profile: usize,
  /// The strategy this architecture would use to solve the problem. Co-produced
  /// with the footprint because footprint depends on the chosen strategy.
  strategy: strategy.Name,

  /// Returns true if self is better than other
  /// Prioritizes speed over anything
  ///
  /// Asserts that if both self and other are in the same speed bracket, they both have either have a known
  ///   total memory footprint, or neither do.
  pub fn betterThan(self: Bid, other: Bid) bool {
    const self_tier = @intFromEnum(self.tier);
    const other_tier = @intFromEnum(other.tier);

    if (self_tier < other_tier) return true;
    if (self_tier > other_tier) return false;

    if (self.total_memory_footprint != null) {
      const self_fp = self.total_memory_footprint.?;
      const other_fp = other.total_memory_footprint.?;

      if (self_fp < other_fp) return true;
      if (self_fp > other_fp) return false;
    } else {
      assert(other.total_memory_footprint == null);
    }

    return self.memory_scaling_profile < other.memory_scaling_profile;
  }
};

/// Ranks the algorithmic complexity and execution overhead of a backend.
/// Lower integer values represent faster/preferable execution models.
pub const Speed = enum(u8) {
  /// Pure SIMD O(N/M) execution
  /// State machines are never compiled
  simd = 0,
  /// close to O(N/M) execution, automata execution search location is simd accelerated
  /// e.g. variable-length patterns with good anchor points
  simd_accelerated,
  /// O(N) execution, memory-heavy setup but optimal traversal (e.g., DFA)
  deterministic,
  /// O(N * M) execution, lightweight setup, no catastrophic failure (e.g., Thompson NFA)
  /// In most cases very close to O(N)
  no_redos_nfa,
};

pub const DispatchError = error{
  /// No provided architecture is capable of safely compiling the pattern's manifest.
  /// This also fires if a Regex defined for a single architecture cannot solve the implied problem
  NoAvailableArchitecture,
};

pub const Decision = struct {
  idx: usize,
  bid: Bid,
};

pub const Choice = union(enum) {
  single: Decision,
  /// Decision is impossible before the building stage
  deterministic_with_fallback: struct {
    dfa: Decision,
    fallback_nfa: Decision,
  },
};

/// Picks the best choice out of 'archs'
/// Returns architecture index as well as metadata
pub fn choose(
  comptime archs: []const ArchResolved,
  comptime sets_bp: arch.AbsoluteBreakpoint,
  manifest: Manifest,
  user_strat: ?strategy.Name,
) DispatchError!Choice {
  comptime std.debug.assert(archs.len > 0);
  if (archs.len == 1) return Choice{ .single = .{
    .idx = 0,
    .bid = archs[0].bid(sets_bp, manifest, user_strat) orelse return error.NoAvailableArchitecture,
  } };

  var best_idx: ?usize = null;
  var best_bid: ?Bid = null;

  var best_nfa_idx: ?usize = null;
  var best_nfa_bid: ?Bid = null;

  inline for (archs, 0..) |a, i| {
    if (a.bid(sets_bp, manifest, user_strat)) |bid| {

      // Track the best NFA specifically for fallback checking
      if (bid.tier == .no_redos_nfa) {
        if (best_nfa_bid == null or bid.betterThan(best_nfa_bid.?)) {
          best_nfa_idx = i;
          best_nfa_bid = bid;
        }
      }

      // Ranking & Tie-breaking for the overall best architecture
      if (best_bid == null or bid.betterThan(best_bid.?)) {
        best_idx = i;
        best_bid = bid;
      }
    }
  }

  if (best_idx) |idx| {
    if (best_bid.?.tier == .deterministic) {
      if (best_nfa_idx) |nfa_idx| {
        return Choice{ .deterministic_with_fallback = .{
          .dfa = .{ .idx = idx, .bid = best_bid.? },
          .fallback_nfa = .{ .idx = nfa_idx, .bid = best_nfa_bid.? },
        } };
      }
    }

    return Choice{ .single = .{ .idx = idx, .bid = best_bid.? } };
  }

  return error.NoAvailableArchitecture;
}

/// Resolves the dispatcher's choice into a final architecture index and strategy.
pub fn resolveChoice(choice: Choice, manifest: Manifest) DispatchError!Decision {
  _ = manifest;

  switch (choice) {
    .single => |decision| return decision,
    .deterministic_with_fallback => |decision| {
      // Template logic: Default to the DFA choice
      // Future logic: build a partial DFA, and if states > threshold, return decision.fallback_nfa
      return decision.fallback_nfa;
    },
  }
}
