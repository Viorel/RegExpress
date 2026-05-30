//! pragmatic zig regex (pzre)
//! Evaluation implemented using the thompson nfa method (no bad cases)
const std = @import("std");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;
const ArrayList = std.ArrayList;
const Io = std.Io;
const PoolError = context.PoolError;

const pzre = @import("../root.zig");
const lens = pzre.lens;
const debug = lens.debug;

const ascii = pzre.encoding.ascii;
const Set = ascii.IntegerSet;
const Range = pzre.structures.range.Range;
const IdxRange = Range(usize);
const meta = pzre.meta;

const ComptimeArrayList = pzre.structures.comptime_arraylist.ComptimeArrayList;

const Sets = pzre.language.Sets;
const Limits = pzre.Limits;

const ParseError = pzre.ParseError;

const polymorphic_memory = pzre.structures.polymorphic_memory;
const MemoryModel = pzre.structures.polymorphic_memory.MemoryModel;

const _ast = pzre.ast;
const Ast = _ast.Ast;

const misc = pzre.misc;

pub const resolution = @import("resolution.zig");
pub const context = @import("context.zig");
pub const lists = @import("lists.zig");
pub const machine = @import("machine.zig");
pub const state = @import("state.zig");
pub const fragment = @import("fragment.zig");
pub const search_problem = @import("search_problem.zig");
pub const analysis = @import("analysis.zig");
pub const SearchProblem = search_problem.SearchProblem;
pub const MachineSpan = search_problem.MachineSpan;

comptime {
  if (@import("builtin").is_test) {
    _ = @import("resolution.zig");
    _ = @import("context.zig");
    _ = @import("fragment.zig");
    _ = @import("lists.zig");
    _ = @import("machine.zig");
    _ = @import("state.zig");
    _ = @import("search_problem.zig");
    _ = @import("analysis.zig");
  }
}

pub const Match = struct {
  /// Most likely aliases with input but not guaranteed (static memory)
  str: []const u8,
  loc: IdxRange,
};

pub const ManyReplacements = struct {
  /// The range encompassing all replacements, mapped to the original input string's indices.
  span: Range(usize),
  /// The number of replacements that were performed
  count: usize,
  /// Newly allocated string
  new: []const u8,

  const Self = @This();

  pub fn deinit(r: Self, gpa: Allocator) void {
    gpa.free(r.new);
  }
};

pub const Replacement = struct {
  /// The range encompassing all replacements, mapped to the original input string's indices.
  span: Range(usize),
  /// Newly allocated string
  new: []const u8,

  const Self = @This();

  pub fn deinit(r: Self, gpa: Allocator) void {
    gpa.free(r.new);
  }
};

/// A threadsafe, immutable Nfa structure that performs zero heap allocations
/// 
/// Regarding the modes:
///   .fixed is exact, comptime derived. If both are .fixed, then they are equal
///   .dynamic is comptime defined upper-bound
pub fn Nfa(
  comptime limits: Limits,
  /// The set the parser used for defining \b \B assertions
  comptime builtin_sets: Sets,
  /// Memory model used during compilation
  comptime model: MemoryModel,
  /// Defines the maximum length a single submachine in the states array can be
  comptime breakpoint: state.Breakpoint,
  /// Defines the integers of the context
  comptime context_breakpoint: state.Breakpoint,
  /// Defines the type of context
  comptime context_mode: context.Mode,
) type {

  return struct {
    /// The compiled machine
    states: []const State,
    /// The sets (integer ranges) of the original pattern
    sets: []const Set,
    /// The problem this NFA is compiled to solve
    formulation: SearchProblem,

    const Self = @This();

    pub const State = state.State(breakpoint);
    const StateList = pzre.structures.polymorphic_memory.presets.single_ended.Create(model, null, State);

    pub const Machine = machine.Machine(builtin_sets.word_set, context_breakpoint, context_mode, breakpoint);
    pub const Context = context.Context(context_mode, context_breakpoint);

    /// Returns the size of the compiled machine without context or sets
    pub fn machineSize(self: Self) usize {
      return @sizeOf(Self) + self.states.len * @sizeOf(State);
    }

    /// Returns the size of the compiled machine without context
    pub fn setsSize(self: Self) usize {
      var total_size: usize = self.start_set.heapSize();

      for (self.sets) |set| total_size += set.heapSize();
      total_size += self.sets.len * @sizeOf(Set);

      return total_size;
    }

    /// Finalizes the NFA machine given the available parse objects.
    /// Takes ownership over sets and any machine fragments
    /// Objects is const in order to not have a trillion constcasts in compile.zig
    pub fn solveWith(gpa: Allocator, sets: []const Set, objects: *const PrecursorObjects, problem: search_problem.Name) ParseError!Self {
      var self = b: {
        errdefer misc.destroySets(gpa, sets);
        break :b try createStateTopology(gpa, sets, objects, problem);
      };

      var r = b: {
        errdefer if (!@inComptime()) {
          gpa.free(self.states);
          self.formulation.deinit(gpa);
        };
        break :b try self.performPostGenModifications(gpa, sets, problem);
      };

      errdefer r.deinit(gpa);
      try r.validateLimits();
      initSanityCheck(r.formulation, r.states);
      return r;
    }

    /// A collection of compilation artifacts for the final compilation stage.
    ///
    /// 1. This object is initiated with objects generated from the parser.
    /// 2. Then, this object will fulfill itself to match the exact requirements of the problem 
    ///     given what is available
    /// 3. The objects will then be concatenated and wrapped in an Nfa object
    ///
    pub const PrecursorObjects  = struct {
      ast: ?Ast = null,
      nfa: ?[]const State = null,
      rnfa: ?[]const State = null,
      prefix: ?[]const State = null,
      start_set: ?Set = null,

      /// Generates any missing objects as required and returns a new populated struct.
      /// Caller is responsible for deinited the returned object.
      /// Touching the object post return is illegal ()
      pub fn fulfill(
        self: PrecursorObjects,
        gpa: Allocator,
        sets: []const Set,
        reqs: search_problem.Requirements,
      ) !PrecursorObjects {
        // Self cannot be modified mutably (comptime memory model), 
        //  additionally its being errdeferd from above

        var new_nfa: ?[]const State = null;
        var new_rnfa: ?[]const State = null;
        errdefer if (!@inComptime()) {
          if (new_nfa) |n| gpa.free(n);
          if (new_rnfa) |r| gpa.free(r);
        };

        if (reqs.nfa and self.nfa == null) {
          if (self.ast == null) return error.InvalidPrecursor;
          new_nfa = try self.ast.?.compileStates(limits, model, breakpoint, gpa);
        }

        if (reqs.rnfa and self.rnfa == null) {
          if (self.ast == null) return error.InvalidPrecursor;
          var rev_ast = try self.ast.?.reverse(model, gpa);
          defer rev_ast.deinit(gpa);
          new_rnfa = try rev_ast.compileStates(limits, model, breakpoint, gpa);
        }

        var new_prefix: ?[]const State = null;
        if (reqs.prefix and self.prefix == null) {
          new_prefix = comptime search_problem.getUnanchoredPrefix(breakpoint);
        }

        var new_start_set: ?Set = null;
        if (reqs.start_set and self.start_set == null) {
          const active_nfa = new_nfa orelse self.nfa.?;
          new_start_set = try analysis.generateStartSet(model, breakpoint, gpa, active_nfa, sets);
        }

        var result = self;
        if (new_nfa) |n| result.nfa = n;
        if (new_rnfa) |r| result.rnfa = r;
        if (new_prefix) |p| result.prefix = p;
        if (new_start_set) |s| result.start_set = s;

        // Strip unused memory and NULL the pointers to prevent double-freeing
        if (!reqs.nfa and result.nfa != null) {
          if (!@inComptime()) gpa.free(result.nfa.?);
          result.nfa = null;
        }
        if (!reqs.rnfa and result.rnfa != null) {
          if (!@inComptime()) gpa.free(result.rnfa.?);
          result.rnfa = null;
        }
        if (!reqs.start_set and result.start_set != null) {
          if (!@inComptime()) result.start_set.?.deinit(gpa);
          result.start_set = null;
        }

        return result;
      }

      /// Returns the total length of the contained machine fragments
      pub fn len(self: PrecursorObjects) usize {
        var total_len: usize = 0;
        if (self.prefix) |p| total_len += p.len;
        if (self.nfa) |p| total_len += p.len;
        if (self.rnfa) |p| total_len += p.len;
        return total_len;
      }

      pub fn deinit(self: PrecursorObjects, gpa: Allocator) void {
        if (!@inComptime()) {
          self.deinitFragments(gpa);
          if (self.start_set) |r| r.deinit(gpa);
        }
      }

      pub fn deinitFragments(self: PrecursorObjects, gpa: Allocator) void {
        if (!@inComptime()) {
          if (self.nfa) |n| gpa.free(n);
          if (self.rnfa) |r| gpa.free(r);
        }
      }
    };


    /// Constructs the final state given what is available in 'precursors'
    /// Precursors is consumed
    fn createStateTopology(gpa: Allocator, sets: []const Set, original_precursors: *const PrecursorObjects, problem: search_problem.Name) ParseError!Self {
      const reqs = problem.getRequirements();

      var precursors = b: {
        defer @constCast(original_precursors).deinit(gpa);
        break :b try @constCast(original_precursors).fulfill(gpa, sets, reqs);
      };
      
      // Deinits anything not set to null
      defer precursors.deinit(gpa);

      if (problem.getUniqueSubmachineRequirement()) |uniq| {
        const final_states = switch (uniq) {
          .nfa => b: {
            const s = precursors.nfa.?;
            precursors.nfa = null;
            break :b s;
          },
          .rnfa => b: {
            const s = precursors.rnfa.?;
            precursors.rnfa = null;
            break :b s;
          },
          .prefix => b: {
            const s = precursors.prefix.?;
            precursors.prefix = null;
            break :b s;
          },
          else => unreachable,
        };

        const span: MachineSpan = .{ .start = 0, .end = final_states.len };

        const formulation: SearchProblem = switch (problem) {
          .exact_match => .{ .exact_match = .{ .nfa = span } },
          .strict_start_anchor => .{ .strict_start_anchor = .{ .nfa = span } },
          .strict_end_anchor => .{ .strict_end_anchor = .{ .rnfa = span } },
          .start_set_pass => b: {
            const sset = precursors.start_set.?;
            precursors.start_set = null;
            break :b .{ .start_set_pass = .{ .nfa = span, .start_set = sset } };
          },
          else => unreachable,
        };

        return Self.init(formulation, final_states, sets);
      }

      var states: StateList = .empty;
      errdefer states.deinit(gpa);

      try states.ensureCapacityPrecise(gpa, precursors.len());

      var current_offset: usize = 0;
      var prefix_span: MachineSpan = .{ .start = 0, .end = 0 };
      var nfa_span: MachineSpan = .{ .start = 0, .end = 0 };
      var rnfa_span: MachineSpan = .{ .start = 0, .end = 0 };

      if (reqs.prefix) {
        states.appendSlice(gpa, precursors.prefix.?) catch unreachable;
        prefix_span = .{ .start = current_offset, .end = current_offset + precursors.prefix.?.len };
        current_offset += precursors.prefix.?.len;
      }

      if (reqs.nfa) {
        states.appendSlice(gpa, precursors.nfa.?) catch unreachable;
        nfa_span = .{ .start = current_offset, .end = current_offset + precursors.nfa.?.len };
        current_offset += precursors.nfa.?.len;
      }

      if (reqs.rnfa) {
        states.appendSlice(gpa, precursors.rnfa.?) catch unreachable;
        rnfa_span = .{ .start = current_offset, .end = current_offset + precursors.rnfa.?.len };
        current_offset += precursors.rnfa.?.len;
      }

      const formulation: SearchProblem = switch (problem) {
        .bi_directional_pass => .{ .bi_directional_pass = .{
          .unfa = .{ .start = prefix_span.start, .end = nfa_span.end },
          .nfa = nfa_span,
          .rnfa = rnfa_span,
        }},
        else => unreachable, 
      };

      return Self.init(formulation, try states.toOwnedConstSlice(gpa), sets);
    }

    /// Perform final fixes once all structures have been generated
    /// Sets is consumed
    fn performPostGenModifications(self: Self, gpa: Allocator, sets: []const Set, problem: search_problem.Name) Allocator.Error!Self {

      // Append dotset if its missing
      const new_sets = if (problem.hasRequirement(.prefix)) b: {
        errdefer misc.destroySets(gpa, sets);
        break :b try misc.appendUniverse(model, gpa, sets);
      } else sets;
      errdefer misc.destroySets(gpa, new_sets);

      // Modify unfa dotset set index
      if (problem.hasRequirement(.prefix)) {
        switch (self.formulation) {
          inline else => |f| {
            if (comptime meta.isStruct(@TypeOf(f))) {
              if (@hasField(@TypeOf(f), "unfa")) {
                const span: MachineSpan = @field(f, "unfa");
                const unfa_states = self.states[span.start .. span.end];
                assert(unfa_states.len >= 3);

                assert(unfa_states[0].tag == .split);
                assert(unfa_states[1].alt_jump == -1);
                assert(unfa_states[1].tag == .term_set_alt_jump);

                const head = self.states[0..span.start];
                const unfa_prefix = self.states[span.start .. span.start + 2];
                const unfa_rest = self.states[span.start + 2 .. span.end];
                const tail = self.states[span.end .. ];

                const new_states = if (@inComptime()) b: {
                  comptime var new = unfa_prefix[1];
                  new.term.set_idx = @truncate(new_sets.len - 1);
                  const new_states: []const State = head ++ &[_]State{unfa_prefix[0], new} ++ unfa_rest ++ tail;
                  break :b new_states;
                } else b: {
                  var new = unfa_states[1];
                  new.term.set_idx = @truncate(new_sets.len - 1);
                  @constCast(unfa_states)[1] = new;
                  break :b self.states;
                };

                assert(new_states.len == self.states.len);
                return Self.init(self.formulation, new_states, new_sets);
              }
            }
          }
        }
      }

      return Self.init(self.formulation, self.states, new_sets);
    }

    /// Limits are also validated during parsing, and when parsing finishes
    /// This is the final validation once optimizations and machine concatenations have finished
    pub fn validateLimits(self: Self) ParseError!void {
      if (self.states.len > limits.max_states) return error.TooManyStates;

      switch (self.formulation) {
        inline else => |f| {
          inline for (std.meta.fields(@TypeOf(f))) |field| {
            if (comptime field.type == MachineSpan) {
              const span = @field(f, field.name);
              const submachine = self.states[span.start .. span.end];
              const max_submachine_len = std.math.maxInt(limits.max_submachine_states.Offset());
              if (submachine.len > max_submachine_len) return error.TooManyStates;
            }
          }
        },
      }
    }

    /// Checks whether the compiled machine is legal given the constraints
    ///
    pub fn initSanityCheck(formulation: SearchProblem, states: []const State) void {
      if (states.len > limits.max_states) {
        const fmt = "Invalid machine initiation. Maximum total states len is {d}, but the machine has size {d}";
        const args = .{limits.max_states, states.len};
        if (@inComptime()) {
          lens.debug.compileError(fmt, args);
        } else lens.debug.panic(fmt, args);
      }

      switch (formulation) {
        inline else => |f| {
          inline for (std.meta.fields(@TypeOf(f))) |field| {
            if (comptime field.type == MachineSpan) {
              const span = @field(f, field.name);
              const submachine = states[span.start .. span.end];
              const fmt = "Invalid submachine initiation for problem {s}. Maximum {s} len is {d}, but the {s} submachine has size {d}";

              const max_submachine_len = std.math.maxInt(limits.max_submachine_states.Offset());
              if (submachine.len > max_submachine_len) {
                const args = .{@tagName(formulation), "submachine", max_submachine_len, field.name, submachine.len};
                if (@inComptime()) {
                  lens.debug.compileError(fmt, args);
                } else lens.debug.panic(fmt, args);
              }

              if (comptime context_mode == .fixed) {
                const max = context_mode.fixed;
                if (submachine.len > max) {
                  const args = .{@tagName(formulation), "fixed context", max, field.name, submachine.len};
                  if (@inComptime()) {
                    lens.debug.compileError(fmt, args);
                  } else lens.debug.panic(fmt, args);
                }
              }
            }
          }
        },
      }
    }

    pub fn init(formulation: SearchProblem, states: []const State, sets: []const Set) Self {
      // @compileLog(states, formulation);

      const s = Self{
        .formulation = formulation,
        .states = states,
        .sets = sets,
      };

      // debug.prettyPrint(.{s});
      // debug.inspectMemory(&.{s});
      return s;
    }

    pub fn requiredContextLen(self: Self) usize {
      var max: usize = 0;
      switch (self.formulation) {
        inline else => |f| {
          inline for (std.meta.fields(@TypeOf(f))) |span_field| {
            if (span_field.type == MachineSpan) {
              const span = @field(f, span_field.name);
              const submachine_len = span.len();
              max = @max(max, submachine_len);
            }
          }
        }
      }
      return max;
    }

    /// Creates a single context for this nfa. See context.Mode
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn initContext(self: Self, gpa: Allocator) Allocator.Error!Context {
      const len = self.requiredContextLen();
      return Context.init(gpa, len);
    }

    /// Creates a single context for this nfa. See context.Mode
    ///
    /// Contexts do not require manual reset
    /// 
    /// Asserts (at comptime) the context is fixed
    /// 
    pub fn initContextFixed(self: Self) Context {
      comptime assert(context_mode == .fixed or context_mode == .compact_fixed);
      const len = self.requiredContextLen();
      return Context.init(undefined, len) catch unreachable;
    }

    /// Creates a single context for a system of nfas, including this one
    /// See context.Mode
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn initContextIncluding(self: Self, gpa: Allocator, including: []const Self) Allocator.Error!Context {
      return Context.initForMany(Self, gpa, self, including);
    }

    /// Creates a single context for this nfa. See context.Mode
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn createContext(self: Self, gpa: Allocator) Allocator.Error!*Context {
      const ctx = try gpa.create(Context);
      errdefer gpa.deinit(ctx);
      ctx.* = try self.initContext(gpa);
      return ctx;
    }

    /// Creates a single context for a system of nfas, including this one
    /// See context.Mode
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn createContextIncluding(self: Self, gpa: Allocator, including: []const Self) Allocator.Error!*Context {
      const ctx = try gpa.create(Context);
      errdefer gpa.deinit(ctx);
      ctx.* = try self.initContextIncluding(gpa, including);
      return ctx;
    }

    /// Creates an optimized context pool for multithreaded systems.
    /// See context.Mode
    /// 
    /// The pool is optimized for this + including nfas
    /// 
    /// Each thread should acquire a context, perform the match, and release it.
    ///
    /// Contexts do not require manual reset
    /// 
    pub fn initContextPool(self: Self, comptime pool_conf: context.PoolConfig, gpa: Allocator, io: Io, n_workers: usize, including: []const Self) PoolError!context.Pool(context_mode, breakpoint, pool_conf) {
      return context.Pool(context_mode, breakpoint, pool_conf).init(
        Self,
        gpa,
        io,
        n_workers,
        self,
        including,
      );
    }

    /// Returns true if the entire string matches
    pub fn matchesExact(self: Self, ctx: *Context, str: []const u8) bool {
      const is_match = self.matchStart(ctx, str);
      if (is_match) |m| {
        if (m.len == str.len) return true;
      }
      return false;
    }

    /// Attempts to match the string
    /// Returns the head that matched; str[0..end <= str.len]
    pub inline fn matchStart(self: Self, ctx: *Context, str: []const u8) ?[]const u8 {
      if (self.find(ctx, str, 0, 0)) |result| {
        return result.str;
      } else return null;
    }

    /// True if any substring matches
    pub inline fn matches(self: Self, ctx: *Context, str: []const u8) bool {
      return self.match(ctx, str) != null;
    }

    /// Finds the first match
    pub inline fn match(self: Self, ctx: *Context, str: []const u8) ?Match {
      return self.find(ctx, str, 0, str.len);
    }

    /// Finds the first match that starts within range [start_idx, max_base]   end inclusive
    /// Start index is allowed to be str.len for matching boundaries
    pub fn find(
      self: Self,
      ctx: *Context,
      str: []const u8,
      start_idx: usize,
      max_base: usize,
    ) ?Match {
      assert(start_idx <= str.len);
      assert(start_idx <= max_base);

      // debug.prettyPrint(.{.find_target = str, .start_idx = start_idx, .max_base = max_base, .formula = self.formulation});

      // -- 1. Infer correct approach given what is available -- //

      // Case when search problem does not exist
      // All blocks assert that no search problem exist
      // Blocks are only allowed to perform 1 call to the machine
      if (max_base == start_idx) {
        switch (self.formulation) {
          inline else => |f| {
            if (comptime meta.isStruct(@TypeOf(f))) {
              if (comptime @hasField(@TypeOf(f), "nfa")) {
                var m = self.makeMachine(ctx, @field(f, "nfa"));

                if (m.matches(.{}, self.sets, str, start_idx, str.len)) |end| {
                  return Match{.str = str[start_idx .. end], .loc = .init(start_idx, end)};
                }

                return null;
              } else if (comptime @hasField(@TypeOf(f), "rnfa")) {
                var m = self.makeMachine(ctx, @field(f, "rnfa"));

                // consider the input "...abc"
                // the word abc starts at start_idx of the input string. the machine was compiled for the pattern "abc" in reverse, so the machine matches "cba"
                // 
                // we need to iterate the input in reverse starting from some idx > start_idx bounded at str.len. This implies a search problem. by assertion of being in this block: no search problem exists, then it is implied that the reverse iteration start index is either start_idx or str.len. The machine only matches start_idx if it matches the empty string. Otherwise it is implied that reverse iteration start index is str.len

                // Begin by assuming the common case, e.g. the pattern is right-anchored "abc$"
                if (m.matches(.{ .iterate_reverse = true, .reversed_machine = true }, self.sets, str, str.len, start_idx)) |start| {
                  // debug.prettyPrint(.{
                  //   .reverse_right_anchor_found_match_start = start,
                  // });
                  if (start == start_idx) {
                    return Match{ .str = str[start_idx..str.len], .loc = .init(start_idx, str.len) };
                  }
                }

                // TODO: constrict str, so no pointless iteration occurs

                if (start_idx < str.len) {
                  if (m.matches(.{ .iterate_reverse = true, .reversed_machine = true, .non_greedy = true }, self.sets, str, start_idx, 0)) |start| {
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
      switch (self.formulation) {
        .start_set_pass => |f| {
          var m = self.makeMachine(ctx, f.nfa);
          var base = start_idx;

          while (base <= str.len) {
            const start = f.start_set.find(u8, str[base..]) orelse {
              if (m.matches(.{}, self.sets, str, str.len, str.len)) |_| {
                return Match{.str = "", .loc = .init(str.len, str.len)};
              } return null;
            };

            if (self.find(ctx, str, start, start)) |succ| return succ else {
              base += 1;
              continue;
            }
          }
          return null;
        },
        .exact_match => |f| {
          if (start_idx > 0) return null;
          
          var m = self.makeMachine(ctx, f.nfa);
          if (m.matches(.{}, self.sets, str, 0, str.len)) |end| {
            if (end == str.len) {
              return Match{ .str = str[0..end], .loc = .init(0, end) };
            }
          }
          return null;
        },

        .strict_start_anchor => |f| {
          if (start_idx > 0) return null;
          const base_idx = 0;

          var m = self.makeMachine(ctx, f.nfa);
          if (m.matches(.{}, self.sets, str, base_idx, str.len)) |end| {
            return Match{.str = str[base_idx .. end], .loc = .init(base_idx, end)};
          }

          return null;
        },

        .strict_end_anchor => |f| {
          var m = self.makeMachine(ctx, f.rnfa);
          if (m.matches(.{ .iterate_reverse = true, .reversed_machine = true }, self.sets, str, str.len, 0)) |start| {

            if (start >= start_idx and start <= max_base) {
              const r = Match{.str = str[start .. str.len], .loc = .init(start, str.len)};
              // debug.prettyPrint(.{.matched = r});
              return r;
            }
          }

          return null;
        },

        .bi_directional_pass => |f| {
          if (start_idx > str.len) return null;

          // -- SIMD Pre-Filter (fast-fail) --
          // Unimplemented

          // -- Finding valid_end (f) non-greedily --
          var unfa = self.makeMachine(ctx, f.unfa);
          const result = unfa.matches(.{ .non_greedy = true }, self.sets, str, start_idx, str.len);

          // "non greedily" is retarded. The automata will stop instantly
          // not true, just because the beginning matches doesnt mean the whole automata does

          // debug.prettyPrint(.{
          //   .first_pass = start_idx,
          //   .result = result,
          // });

          if (result == null) return null;
          const exclusive_end = result.?;

          // -- Finding valid_start (s) backwards greedily --
          var rnfa = self.makeMachine(ctx, f.rnfa);

          // The RNFA matches greedily backwards. Going past start_idx means can mean a couple of things:
          //  1. The RNFA could have stopped earlier, but it greedily went past
          //  2. The RNFA HAD to go past start_idx in order to match
          //
          // As such, we cant clamp the inclusive_start to start_idx, we have to ensure that a real match
          // occured within the bounds. Slicing the inputs to the automata does not work due to assertions
          //  The only way is to introduce a max_iteration bound. When the NFA reaches this, it treats it
          //  as the final iteration

          const rev_result = rnfa.matches(.{ .iterate_reverse = true, .reversed_machine = true }, self.sets, str, exclusive_end, start_idx);
          const inclusive_start = rev_result.?;

          // -- Greedy resolution (t) --
          var nfa = self.makeMachine(ctx, f.nfa);
          const greed_result = nfa.matches(.{}, self.sets, str, inclusive_start, str.len);
          const greedier_end = greed_result orelse unreachable;
          assert(exclusive_end <= greedier_end);

          const r = Match{
            .str = str[inclusive_start..greedier_end],
            .loc = .init(inclusive_start, greedier_end),
          };
          // debug.prettyPrint(.{.matched = r});
          return r;
        },
      }
    }

    /// Finds all matches and stores them in a slice
    pub fn findAllAlloc(self: Self, ctx: *Context, gpa: Allocator, str: []const u8) Allocator.Error![]Match {
      var r: ArrayList(Match) = .empty;
      var it = self.matchIter(ctx, str);

      while (it.next()) |m| {
        try r.append(gpa, m);
      }

      return r.toOwnedSlice(gpa);
    }

    pub fn findAllComptime(comptime self: Self, comptime str: []const u8) []const Match {
      comptime {
        var ctx = self.initContext(undefined) catch unreachable;
        var r: ComptimeArrayList(Match) = .empty;
        var it = self.matchIter(&ctx, str);

        while (it.next()) |m| {
          r.append(m);
        }

        return r.items;
      }
    }

    pub const MatchIterator = struct {
      idx: usize = 0,
      nfa: Self,
      str: []const u8,
      ctx: *Context,

      const It = @This();

      pub fn init(self: Self, ctx: *Context, str: []const u8) It {
        return .{
          .nfa = self,
          .str = str,
          .ctx = ctx,
        };
      }

      pub fn next(it: *It) ?Match {
        // debug.prettyPrint(.{
        //   .it_idx = it.idx,
        // });

        if (it.idx > it.str.len) return null;
        if (it.nfa.find(it.ctx, it.str, it.idx, it.str.len)) |m| {
          // debug.prettyPrint(.{.next_match = m });
          const start = m.loc.start;
          const end = m.loc.end;
          assert(start >= it.idx);
          it.idx = if (start == end) end + 1 else end;

          // debug.prettyPrint(.{
          //   .next_it = m,
          // });

          return m;
        } else {
          // debug.prettyPrint(.{.next_match = null });
          it.idx = it.str.len + 1;
          return null;
        }
      }

      pub fn reset(it: *It) void {
        it.idx = 0;
      }
    };

    /// Returns an iterator for all matches
    /// Allocation is only done once for nfa construction
    pub fn matchIter(self: Self, ctx: *Context, str: []const u8) MatchIterator {
      return .init(self, ctx, str);
    }

    /// Finds all matches and replaces them with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    ///
    /// Returns the region where replacements occured and the number of replacements
    ///
    pub fn replaceAll(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
    ) Allocator.Error!?ManyReplacements {
      return self.replaceAllWithin(ctx, gpa, str, replacement, 0, str.len);
    }

    /// Finds all matches starting within range [start_idx, max_base]   end inclusive
    ///   and replaces them with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    ///
    /// Returns the region where replacements occured and the number of replacements
    ///
    pub fn replaceAllWithin(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
      start_idx: usize,
      max_base: usize,
    ) Allocator.Error!?ManyReplacements {
      assert(start_idx <= str.len);
      assert(start_idx <= max_base);

      var span: Range(usize) = .init(0, 0);
      var count: usize = 0;

      var it = self.matchIter(ctx, str);
      it.idx = start_idx;
      var r: std.ArrayList(u8) = .empty;
      errdefer r.deinit(gpa);

      var previous_match_end: usize = 0;

      if (it.next()) |first| {
        count += 1;
        span = first.loc;
        previous_match_end = first.loc.end;

        const head = str[0 .. first.loc.start];
        const mem = try r.addManyAsSlice(gpa, head.len + replacement.len);
        @memcpy(mem[0 .. head.len], head);
        @memcpy(mem[head.len .. head.len + replacement.len], replacement);
      } else return null;

      while (it.idx <= max_base) {
        if (it.next()) |result| {
          count += 1;
          span.end = result.loc.end;

          const head = str[previous_match_end .. result.loc.start];
          const mem = try r.addManyAsSlice(gpa, head.len + replacement.len);
          @memcpy(mem[0 .. head.len], head);
          @memcpy(mem[head.len .. head.len + replacement.len], replacement);

          previous_match_end = result.loc.end;
        } else break;
      }

      try r.appendSlice(gpa, str[previous_match_end..]);

      const new = try r.toOwnedSlice(gpa);

      return ManyReplacements{.new = new, .count = count, .span = span};
    }

    /// Finds the first match and replaces it with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    pub fn replaceFirst(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
    ) Allocator.Error!?Replacement {
      return self.replaceFirst(ctx, gpa, str, replacement, 0, str.len);
    }

    /// Finds the first match that starts within range [start_idx, max_base]   end inclusive
    ///   and replaces it with replacement
    ///
    /// Returns a newly allocated string in return_val.new
    pub fn replaceFirstWithin(
      self: Self,
      ctx: *Context,
      gpa: Allocator,
      str: []const u8,
      replacement: []const u8,
      start_idx: usize,
      max_base: usize,
    ) Allocator.Error!?Replacement {
      assert(start_idx <= str.len);
      assert(start_idx <= max_base);

      if (self.find(ctx, str, start_idx, max_base)) |result| {
        const head = str[0 .. result.loc.start];
        const tail = str[result.loc.end ..];
        const r = try gpa.alloc(u8, head.len + replacement.len + tail.len);
        @memcpy(r[0 .. head.len], head);
        @memcpy(r[head.len .. head.len + replacement.len], replacement);
        @memcpy(r[head.len + replacement.len .. ], tail);
        return Replacement{.new = r, .span = result.loc};
      }
      return null;
    }

    /// Checks whether the context is supported for this particular nfa
    fn assertValidCtx(ctx: *Context, states: []const State) void {
      const max_conc = analysis.determineMaxConcurrency(states.len);
      if (context_mode == .fixed) {
        // debug.prettyPrint(.{ctx, states.len});
        assert(ctx.data.last_list_idxs.len >= states.len);
        assert(ctx.data.lists[0].buffer.len >= max_conc);
      } else {
        assert(ctx.data.last_list_idxs.items.len >= states.len);
        assert(ctx.data.lists[0].capacity >= max_conc);
      }
    }

    /// Puts a machine together swiftly
    pub inline fn makeMachine(self: Self, ctx: *Context, span: MachineSpan) Machine {
      // if (!@inComptime()) debug.prettyPrint(.{.full_machine = self});

      const states = self.states[span.start .. span.end];
      // if (!@inComptime()) debug.prettyPrint(.{.machine = states});
      assertValidCtx(ctx, states);
      return Machine.init(ctx, states);
    }

    pub fn deinit(self: *Self, gpa: Allocator) void {
      if (!@inComptime()) {
        gpa.free(self.states);
        pzre.misc.destroySets(gpa, self.sets);
      }
      self.formulation.deinit(gpa);
    }
  };
}
