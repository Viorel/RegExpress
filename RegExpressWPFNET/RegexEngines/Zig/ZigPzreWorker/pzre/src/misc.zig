const std = @import("std");
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;

const pzre = @import("root.zig");

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

const lens = pzre.lens;
const debug = lens.debug;
const state = pzre.nfa.state;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = polymorphic_memory.MemoryModel;
const List = polymorphic_memory.presets.single_ended.Create;
const Model = pzre.MemoryModel;

const nfa = pzre.nfa;

pub fn destroySetslist(
  comptime model: Model,
  gpa: Allocator,
  sets: *List(model, null, Set),
) void {
  for (sets.getConstSlice()) |*set| set.deinit(gpa);
  sets.deinit(gpa);
}

pub fn appendUniverse(
  comptime model: Model,
  gpa: Allocator,
  sets: []const Set,
) Allocator.Error![]const Set {

  const universe = ascii.Set.ALL;

  for (sets) |set| {
    if (set.equal(universe)) {
      return sets;
    }
  }

  const owned_set = if (!@inComptime()) try universe.dupe(gpa)
    else universe;
  errdefer if (!@inComptime()) owned_set.deinit(gpa);

  var setslist: List(model, null, Set) = .initUsing(sets);

  _ = try setslist.ensureCapacityPrecise(gpa, sets.len + 1);
  setslist.append(gpa, owned_set) catch unreachable;
  return setslist.getConstSlice();
}

pub fn destroySets(gpa: Allocator, sets: []const Set) void {
  if (@inComptime()) return;
  for (0 .. sets.len) |i| sets[i].deinit(gpa);
  gpa.free(sets);
}

pub fn dupeSets(gpa: Allocator, sets: []const Set) Allocator.Error![]const Set {
  const new_sets = try gpa.alloc(Set, sets.len);
  errdefer gpa.free(new_sets);

  var count: usize = 0;
  errdefer for (0..count) |i| new_sets[i].deinit(gpa);

  for (sets) |set| {
    new_sets[count] = try set.dupe(gpa);
    count += 1;
  }

  return new_sets;
}

/// Depicts the number of repetitions for a quantifier
/// ? + * {n,m}
pub const Repeat = struct {
  min: usize,
  /// null: any number of repetitions
  max: ?usize,

  const Self = @This();

  pub fn is_arbitrary_repeat(self: Self) bool {
    return if (self.max) |max| max > 1 else false;
  }

  pub fn is_star(self: Self) bool {
    return self.min == 0 and self.max == null;
  }

  pub fn is_plus(self: Self) bool {
    return self.min == 1 and self.max == null;
  }

  pub fn is_optional(self: Self) bool {
    return self.min == 0 and self.max == 1;
  }

  pub fn is_epsilon(self: Self) bool {
    return self.min == 0 and self.max == 0;
  }

  pub fn is_unity(self: Self) bool {
    return self.min == 1 and self.max == 1;
  }

  pub fn is_fixed(self: Self) bool {
    return self.max != null and self.min == self.max.?;
  }

  pub fn is_unbounded(self: Self) bool {
    return self.max == null;
  }
};

/// An assertion is a condition. 
/// When the nfa reaches such a node, it checks a condition to see whether its allowed to proceed.
pub const Assertion = enum {
  line_start,           // ^ in multiline-mode
  line_end,             // $ in multiline-mode
  text_start,           // \A or ^ in non multiline-mode
  text_end,             // \z or $ in non multiline-mode
  word_boundary,        // \b
  not_word_boundary,    // \B
};

/// A simplified term that the Parser uses
pub const Atom = union (enum) {
  set_idx: usize,
  char: u8,
  assertion: Assertion,
};
