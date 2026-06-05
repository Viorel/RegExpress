const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;

const pzre = @import("../../root.zig");
const MemoryModel = pzre.MemoryModel;

const Repeat = misc.Repeat;
const Assertion = misc.Assertion;

const polymorphic_memory = pzre.structures.polymorphic_memory;

const misc = pzre.misc;

const arch = pzre.arch;
const nfa = arch.minimal_nfa;
const RelativeBreakpoint = arch.RelativeBreakpoint;

/// A single incomplete machine fragment represented as a sequence of states
/// The machine is 'incomplete' because there is no end state (.out.len > 0)
/// The start state is always idx 0 of .automata
/// 
/// NOTE: 'accept' is not included in the topology anymore, now we detect for end_of_states as being accept
/// The removal of accept increases the modularity of this topology drastically, as engines can be much more easily embedded into eachother and simply sliced from existing compilations
/// 
/// -- Theory --
/// The goal is to keep the states that are connected as close together as possible to maximize cache locality. This is achieved in the following way:
/// 1. The states are stored in a contiguous memory location
/// 2. The common transition case: the next state is the immediate next index (i) -> (i + 1). Uncommonly, the machine jumps to some other index using an offset
/// 3. The jumps via offset should still be relatively close. On a split, we form two state "islands" that converge as fast as possible:
/// 
/// M(aa|bb)c
///  
/// Here 'M' is a sequence of states, which is then followed by '(aa|bb)c'. 'aa' and 'bb' form separate state sequences. These two sequences are accessible from 'M', and converge onto 'c'
/// 
/// Consider a pattern S = (aaa|bbb)?
/// In order to explain the theory we use the following notation:
///
///     ? | a a a b b b         1. terminal symbols
///     ? 4     ?     ?         2. jump offset
/// S:  0 1 2 3 4 5 6 7         3. state indices
///     . .                     4. is split flag
/// 
/// - S is a machine fragment for the string (aaa|bbb)?, represented as a sequence of states encoding it
/// - The middle row contains the absolute indices of the fragment
/// - The top row contains jump offsets. Empty signifies an offset of 1 (common case; left out), a question mark signifies that the state is dangling, e.g. it is waiting for another state to be added, in which case all of the dangling states are connected to it
/// - The bottom row contains boolean flags on whether the state is a split. Splits can always move either to the next index, or jump using the indicated offset.
/// 
/// Lets complete the fragment S by appending an accept state to it. Lets use % as accept. A fragment that ends in % is a complete NFA
/// 
///      ? | a a a b b b %
///      8 4     4        
/// S%:  0 1 2 3 4 5 6 7 8
///      . .              
/// 
/// The last state always dangles if it is not the accept state. This occurs if the last state is a split, and it has a negative offset jump, the forward +1 path dangles
/// 
/// At state i:
///   0, the machine is on a split created by the ? operator. It can choose to continue onto the next state 1, or to jump to index i + 8
///   1, move to state 2 or 5
///   2, move to state 3
///   4, a terminal state that can only move to i + 4
/// and so on
/// 
/// The machine graph of S
/// 
///        2   3   4
///        o - o - o - ?
///  0   1/
///  o - o
///   \   \5   6   7
///    \   o - o - o - ?
///     ?
/// 
/// The numbers are indices of the underlying array. States 0, 4 and 7 dangle
/// 
/// The concatenation of S with a char 'c' (aaa|bbb)?c, would produce an automata with the following graph
/// 
///         2   3   4
///         o - o - o
///  0   1/          \    8
///  o - o            - - o - - ?
///   \   \5   6   7 /   /
///    \   o - o - o    /
///     \              /
///      -------------/
/// 
/// Next we will go over how the fragments are built. The idea is that we have an arbitrary automata fragment S with no accept state, and an operation is performed on it. This produces a new automata S' with a new state either prepended or appended to S. S is arbitrary, however we show concrete examples using the syntax showcased above for the same S:
/// 
///     ? | a a a b b b         1. terminal symbols
///     ? 4     ?     ?         2. jump offset
/// S:  0 1 2 3 4 5 6 7         3. state indices
///     . .                     4. is split flag
/// 
/// Optional: S?
///   - createSplit s
///   - prepend S s
///      
///     ? ? | a a a b b b
///     ? ? 4     ?     ?
/// S?: s 0 1 2 3 4 5 6 7
///     . . .
/// 
/// Star: S*
///   - createSplit s
///   - prepend S s
///   - connect S s
/// 
///     * ? | a a a b b b
///     ?-1 4    -5    -8
/// S*: s 0 1 2 3 4 5 6 7
///     . . .            
/// 
/// Plus: S+
///   - createSplit s
///   - append S s
///   - connect S s
/// 
///     ? | a a a b b b +
///     8 4     4      -8
/// S+: 0 1 2 3 4 5 6 7 s
///     . .             .
/// 
/// -- Concatenation --
/// 
/// For a single character 'c':
/// 
///     ? | a a a b b b c
///     8 4     4       ?
/// Sc: 0 1 2 3 4 5 6 7 s
///     . .              
/// 
/// More generally, we concatenate another fragment M to S, represented by the pattern SM
/// 
/// We define an additional machine M = ((a|b)+)?
/// 
///     ? | a b +
///     ? 2 2  -3
/// M:  0 1 2 3 4 
///     . .     .
/// 
/// Concatenation: SM
///   - append S M
///   - connect S M.start
/// 
///      ? | a a a b b b ? |  a  b  +
///      8 4     4       ? 2  2    -3
/// SM:  0 1 2 3 4 5 6 7 8 9 10 11 12
///      . .             . .        .
/// 
/// Union: S|M
///   - createSplit s
///   - prepend S s
///   - append S M
///   - connect s M.start
/// 
///      | ? | a a a b b b ? |  a  b  +
///      9 ? 4     ?     ? ? 2  2    -3
/// S|M: s 0 1 2 3 4 5 6 7 8 9 10 11 12
///      . . .             . .        .
/// 
/// IF S ends in a split, then a special .jump state 'j' is added to the topology
/// -> Sj|M
/// This is because a split only has a single alt_jump
/// 
/// Accept is equivalent to character concatenation, but instead of dangling, the machine accepts
/// 
/// The more exact repetition curly brace syntax is unrolled
/// S{m,n} = S^mS?^(n-m)
/// S{m} = S^m
/// S{m,} = S^mS*
/// 
/// 
pub fn Fragment(
  comptime model: MemoryModel,
  comptime rbp: RelativeBreakpoint,
  comptime sets_bp: arch.AbsoluteBreakpoint,
) type {
  const State = nfa.state.State(rbp, sets_bp);

  const StateList = polymorphic_memory.presets.double_ended.Create(model, null, State);
  const OutList = polymorphic_memory.presets.single_ended.Create(model, null, usize);

  return struct {
    /// The actual machine as a sequence of states
    states: StateList = .empty,

    /// The dangling state indices
    out: OutList = .empty,
    
    /// This solely exists to fix a topological problem relating to the + operator
    /// In the topology, the + operator is the only addition that appends a split to the states list
    /// This means that it is the only node that needs to be able to:
    /// 1. jump to its destined location
    /// 2. move onto the next index
    /// 3. jump past the island following it, similar to how terms jump:
    ///   (a|b+)c     ->   after b matches, it increments its index to go to c (2.)
    ///   (a+|b)c     ->   a has to jump b 'island' directly to c (3.)
    ///
    /// The state object is extremely compact and has no ability to jump to two separate locations
    /// In the situation (3.), the + operator adds two separate states
    /// The usual + .split, followed by a .jump state that unconditionally jumps over the island 
    ///   after the split transitions via a basic increment
    /// 
    /// Importantly, JUMPS should not be blindly appended when not needed. We should assert that they always 
    /// jump more than 1 states. For example this should not trigger a jump:
    ///   aabb+aa
    ///
    /// That is what this flag does, it keeps track on whether a plus has previously been appended,
    ///   and whether a jump needs to be added before continuing
    unpatchable_fallthrough: bool = false,

    const Self = @This();

    pub const empty: Self = .{};

    pub const Error = StateList.Error;

    pub fn reset(self: *Self) void {
      self.out.clearRetainingCapacity();
      self.states.clearRetainingCapacity();
      self.unpatchable_fallthrough = false;
    }

    pub fn create(gpa: Allocator, state: State) Error!Self {
      var self: Self = .empty;
      errdefer self.destroy(gpa);
      try self.states.append(gpa, state);
      try self.out.append(gpa, 0);
      return self;
    }

    const no_alt_path = 0;

    fn resolveFallthrough(self: *Self, gpa: Allocator) Error!void {
      if (self.unpatchable_fallthrough) {
        try self.states.append(gpa, .{ .tag = .jump, .alt_jump = 0, .term = .no_term });
        try self.out.append(gpa, self.states.len() - 1);
        self.unpatchable_fallthrough = false;
      }
    }

    pub fn optional(self: *Self, gpa: Allocator) Error!void {
      try self.prependSplit(gpa, no_alt_path);
      self.correctDanglingStates(0, 1);
      try self.out.append(gpa, 0);
    }

    pub fn star(self: *Self, gpa: Allocator) Error!void {
      try self.resolveFallthrough(gpa);
      try self.prependSplit(gpa, no_alt_path);
      self.connect(0, 1);
      try self.out.append(gpa, 0);
    }

    pub fn plus(self: *Self, gpa: Allocator) Error!void {
      const casted_len: isize = @intCast(self.states.len());
      try self.appendSplit(gpa, -casted_len);
      self.connect(self.len() - 1, 0);
      self.unpatchable_fallthrough = true;
    }

    pub fn @"union"(self: *Self, gpa: Allocator, other: Self) Error!void {
      try self.resolveFallthrough(gpa);
      const other_start = self.states.len();
      try self.prependSplit(gpa, @intCast(other_start + 1));
      self.correctDanglingStates(0, 1);
      try self.appendOther(gpa, other);
      self.unpatchable_fallthrough = other.unpatchable_fallthrough;
    }

    pub fn concat(self: *Self, gpa: Allocator, other: Self) Error!void {
      const other_start = self.states.len();
      self.connect(other_start, 0);
      try self.appendOther(gpa, other);
      self.unpatchable_fallthrough = other.unpatchable_fallthrough;
    }

    fn concatNoConsume(self: *Self, gpa: Allocator, other: Self) Error!void {
      const other_start = self.states.len();
      self.connect(other_start, 0);
      try self.appendOther(gpa, other);
      self.unpatchable_fallthrough = other.unpatchable_fallthrough;
    }

    fn concatSelf(self: *Self, gpa: Allocator) Error!void {
      self.connect(self.states.len(), 0);
      try self.states.dupeInplace(gpa);
    }

    /// Returns the number of states in the fragment
    pub fn len(self: Self) usize {
      return self.states.len();
    }

    /// Finalizes the machine
    /// 'self' is deinited
    /// The accept state is a non-existent ghost node that lives at states.len
    /// Edges to accept are pointing to states.len
    pub fn accept(self: *Self, gpa: Allocator) Error![]const State {

      // There is no reason to resolve the fallthrough when accepting. 
      // The jumps are for jumping over islands, but the next index is simply accept

      self.connect(self.states.len(), 0);
      const states = try self.states.toOwnedConstSlice(gpa);
      self.out.deinit(gpa);
      return states;
    }

    pub fn repeatExact(self: *Self, gpa: Allocator, reps: Repeat) Error!void {
      if (reps.max) |known_max| { // a{3,5} = aaaa?a?
        const concrete_count = reps.min;
        var optional_count = known_max - reps.min;
        if (reps.min == 0 and optional_count > 0) optional_count -= 1;

        var cloned: ?Self = if (optional_count > 0) b: {
          var cloned = try self.clone(gpa);
          errdefer cloned.destroy(gpa);

          try cloned.optional(gpa);
          break :b cloned;
        } else null; 
        defer if (cloned) |*c| c.destroy(gpa);

        if (reps.min == 0) {
          try self.optional(gpa);
        }

        if (concrete_count > 0) try self.concatInplaceNTimes(gpa, concrete_count);

        if (cloned) |c| {
          for (0..optional_count) |_| try self.concatNoConsume(gpa, c);
        }

      } else if (reps.min == 0) { // a*
        try self.star(gpa);
      } else {                    // a{3,} = aaa+
        if (reps.min == 1) {
          try self.plus(gpa);
        } else {
          // Create the looping tail (a+)
          var tail = try self.clone(gpa);
          defer tail.destroy(gpa);
          try tail.plus(gpa);

          // Build the concrete base (a^{m-1})
          try self.concatInplaceNTimes(gpa, reps.min - 1);

          // Attach the tail (a^{m-1} a+)
          try self.concatNoConsume(gpa, tail);
        }
      }
    }

    /// concatenates self repeatedly to final length
    /// a -> aaaaa for final_len = 5
    /// or ab -> ababababab
    fn concatInplaceNTimes(self: *Self, gpa: Allocator, amount: usize) Error!void {
    assert(amount != 0);
    if (amount == 1) return;

    const expected_finishing_len = self.states.len() * amount;
    
    // Clone the base unit exactly once BEFORE connecting its dangling states.
    var base = try self.clone(gpa);
    defer base.destroy(gpa);
    try self.states.ensureUnusedCapacity(gpa, expected_finishing_len);

    // This sucks ass due to the dynamic comptime datastructure. This shit needs to be fixed
    for (1..amount) |_| {
      try self.concatNoConsume(gpa, base);
    }

    assert(self.states.len() == expected_finishing_len);
  }

    /// Appends another fragment onto self
    fn appendOther(self: *Self, gpa: Allocator, other: Self) Error!void {
      const incr_amount = self.states.len();

      var mutable_other = other;
      try self.states.appendOther(gpa, &mutable_other.states);
      const correct_start_idx = self.out.len();
      try self.out.appendOther(gpa, &mutable_other.out);
      self.correctDanglingStates(correct_start_idx, incr_amount);
    }

    fn clone(self: *Self, gpa: Allocator) Error!Self {
      var states = try self.states.clone(gpa);
      errdefer states.deinit(gpa);
      return Self{
        .states = states,
        .out = try self.out.clone(gpa),
        .unpatchable_fallthrough = self.unpatchable_fallthrough,
      };
    }

    /// alt_path of 0 signifies no alt path
    fn appendSplit(self: *Self, gpa: Allocator, alt_path: isize) Error!void {
      assert(alt_path <= std.math.maxInt(State.Offset));
      assert(alt_path >= std.math.minInt(State.Offset));
      try self.states.append(gpa, .{ .tag = .split, .alt_jump = @intCast(alt_path) });
    }

    /// alt_path of 0 signifies no alt path
    /// assumes self.out is empty (the indices would be left out of date)
    fn prependSplit(self: *Self, gpa: Allocator, alt_path: isize) Error!void {
      assert(alt_path <= std.math.maxInt(State.Offset));
      assert(alt_path >= std.math.minInt(State.Offset));
      try self.states.pushFront(gpa, .{ .tag = .split, .alt_jump = @intCast(alt_path) });
    }

    /// Connects all dangling states to a state
    /// 'target_idx' is absolute 
    /// 'correction' is an offset added to each entry in self.out
    fn connect(self: *Self, target_idx: usize, correction: usize) void {
      for (self.out.getConstSlice()) |dangling_entry_abs_idx| {

        const dangle_idx = dangling_entry_abs_idx + correction;
        const relative_idx: isize = @bitCast(target_idx -% dangle_idx);
        assert(relative_idx <= std.math.maxInt(State.Offset));
        assert(relative_idx >= std.math.minInt(State.Offset));
        const casted: State.Offset = @intCast(relative_idx);

        const state = self.states.getConstPtr(dangle_idx);
        switch (state.tag) {
          .split, .jump => {
            // jumps/splits should NEVER target the next state; otherwise they are redundant
            assert(casted != 1);
            self.states.set(dangle_idx, State{
              .tag = state.tag,
              .alt_jump = casted,
              .term = .no_term,
            });
          },
          inline else => |tag| {
            const tn = @tagName(tag);
            // alt jumps are already connected: runtime assertion
            assert(!std.mem.endsWith(u8, tn, "alt_jump"));

            const alt_name = tn ++ "_alt_jump";
            if (@hasField(nfa.state.Tag, alt_name)) {
              if (relative_idx != 1) {
                const jump_tag = @field(nfa.state.Tag, alt_name);
                self.states.set(dangle_idx, State{
                  .tag = jump_tag,
                  .alt_jump = casted,
                  .term = state.term,
                });
              }
            } else unreachable; // non jumping tag passed
          },
        }
      }
      self.out.clearRetainingCapacity();
    }

    /// Fixes the out of date absolute indices
    fn correctDanglingStates(self: *Self, start_idx: usize, idx_change: usize) void {
      if (!@inComptime()) {
        const slice = self.out.getSlice()[start_idx..];
        for (0 .. slice.len) |i| {
          slice[i] += idx_change;
        }
      } else {
        var idxs: [self.out.len()]usize = undefined;
        const items = self.out.getConstSlice();
       
        @memcpy(idxs[0..], items);
        for (start_idx..idxs.len) |i| idxs[i] += idx_change;
        const frozen_idxs = idxs[0..] ++ &[0]usize{};
        self.out.data = .initUsing(frozen_idxs);
      }
    }

    pub fn destroy(self: *Self, gpa: Allocator) void {
      self.states.deinit(gpa);
      self.out.deinit(gpa);
    }
  };
}
