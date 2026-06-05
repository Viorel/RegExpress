//! This is a very old stub file, to be implemented
const std = @import("std");
const Allocator = std.mem.Allocator;
const AddrMap = std.AutoHashMapUnmanaged(*usize, *usize);
const ArrayList = std.ArrayList;
const pzre = @import("../root.zig");
const ComptimeArrayList = pzre.structures.comptime_arraylist.ComptimeArrayList;
const BoundedArray = pzre.structures.bounded_array.BoundedArray;
const state = pzre.nfa.state;
const context = pzre.nfa.context;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;

const Sets = pzre.Sets;
const Limits = pzre.compile.Limits;

const ParseError = pzre.ParseError;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;
pub const search.Formulation = pzre.nfa.search.Formulation;
pub const search.Formulation = search.Formulation.search.Formulation;

pub fn NamedPattern(comptime Name: type) type {
  return struct {
    name: Name,
    pattern: []const u8,
  };
}

pub fn Pack(
  comptime Name: type,
  comptime limits: Limits,
  /// The set the parser used for defining \b \B assertions
  comptime builtin_sets: Sets,
  /// Memory model used during compilation
  comptime model: MemoryModel,
  /// Defines the maximum length a single submachine in the states array can be
  comptime rbp: arch.RelativeBreakpoint,
  /// Defines the integers of the context
  comptime context_breakpoint: arch.RelativeBreakpoint,
  /// Defines the type of context
  comptime context_mode: context.Mode,
) type {

  pzre.meta.assertIndexEnum(Name);

  return struct {
    /// All of the compiled machines packed into a single array
    /// The machines potentially overlap in this array, as its the result of the
    /// shortest substring problem
    states: []const State,
    sets: []const Set,
    formulations: []const search.Formulation,

    pub const State = state.State(rbp, sets_bp);

    const Self = @This();

    pub fn getFormulation(self: Self, name: Name) search.Formulation {
      return self.formulations[@intFromEnum(name)];
    }

    pub fn match(comptime key: Name, input: []const u8) bool {
      _ = key;
      _ = input;
      return true; 
    }

    fn compile() !void {}

    // fn index(name: Name) {}
  };
}

pub fn main() !void {
  const Parsers = enum {
    http,
    assign,
    date,
  };

  const family = Pack(Parsers, &.{
    .{ .name = .http, .pattern = "^[A-Z]+ /[^ ]* HTTP/[0-9.]+$" },
    .{ .name = .assign, .pattern = "^[a-zA-Z_]+\\s*=\\s*[0-9]+$" },
    .{ .name = .date, .pattern = "^[0-9]{4}-[0-9]{2}-[0-9]{2}$" },
  });

  family.match(.http, "GET");

  const is_match = family.match(.http, "GET / HTTP/1.1");
  _ = is_match;

  std.debug.print("{any}\n", .{comptime pzre.meta.isTuple(@TypeOf(struct{}))});
}
